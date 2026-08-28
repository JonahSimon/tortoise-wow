#include "Config/Config.h"

#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/PerformanceMonitor.h"
#include "strategy/values/LastMovementValue.h"
#include "strategy/actions/MovementActions.h"
#include "AccountMgr.h"
#include "ObjectMgr.h"
#include "Database/DatabaseEnv.h"
#include "PlayerbotAI.h"
#include "BotDiagnostics.h"  // for SC_LOG
#include "Objects/Player.h"
#include "playerbot/AiFactory.h"
#include "PlayerbotCommandServer.h"
#include "MemoryMonitor.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "FleeManager.h"
#include "playerbot/ServerFacade.h"

#include "Battlegrounds/BattleGround.h"
#include "Battlegrounds/BattleGroundMgr.h"
#include "Chat/ChannelMgr.h"
#include "Guild/GuildMgr.h"
#include "World/WorldState.h"
#include "PlayerbotLoginMgr.h"
#include "Transports/Transport.h"
#include "Maps/PathFinder.h"
#include "Group/Group.h"
#include "Movement/spline/MoveSpline.h"
#include "playerbot/strategy/Engine.h"
#include "playerbot/TravelDestinations.h"
#include "Movement/MotionMaster.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <cmath>

#ifndef MANGOSBOT_ZERO
#ifdef CMANGOS
#include "Arena/ArenaTeam.h"
#endif
#ifdef MANGOS
#include "ArenaTeam.h"
#endif
#endif

#include "playerbot/TravelMgr.h"
#include <iomanip>
#include <float.h>

#if PLATFORM == PLATFORM_WINDOWS
#include "windows.h"
#include "psapi.h"
#endif

using namespace ai;
using namespace MaNGOS;

INSTANTIATE_SINGLETON_1(RandomPlayerbotMgr);

#ifdef CMANGOS
#include <boost/thread/thread.hpp>
#endif

#ifdef MANGOS
class PrintStatsThread: public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.PrintStats(); return 0; }
};
#endif
#ifdef CMANGOS
void PrintStatsThread(uint32 requesterGuid)
{
    sRandomPlayerbotMgr.PrintStats(requesterGuid);
}
#endif

void activatePrintStatsThread(uint32 requesterGuid)
{
#ifdef MANGOS
    PrintStatsThread *thread = new PrintStatsThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(PrintStatsThread, requesterGuid);
    t.detach();
#endif
}

#ifdef MANGOS
class CheckBgQueueThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckBgQueue(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckBgQueueThread()
{
    sRandomPlayerbotMgr.CheckBgQueue();
}
#endif

void activateCheckBgQueueThread()
{
#ifdef MANGOS
    CheckBgQueueThread *thread = new CheckBgQueueThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(CheckBgQueueThread);
    t.detach();
#endif
}

#ifdef MANGOS
class CheckLfgQueueThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckLfgQueue(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckLfgQueueThread()
{
    sRandomPlayerbotMgr.CheckLfgQueue();
}
#endif

void activateCheckLfgQueueThread()
{
#ifdef MANGOS
    CheckLfgQueueThread *thread = new CheckLfgQueueThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(CheckLfgQueueThread);
    t.detach();
#endif
}

#ifdef MANGOS
class CheckPlayersThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckPlayers(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckPlayersThread()
{
    sRandomPlayerbotMgr.CheckPlayers();
}
#endif

void activateCheckPlayersThread()
{
#ifdef MANGOS
    CheckPlayersThread *thread = new CheckPlayersThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(CheckPlayersThread);
    t.detach();
#endif
}

class botPIDImpl
{
public:
    botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd);
    ~botPIDImpl();
    double calculate(double setpoint, double pv);
    void adjust(double Kp, double Ki, double Kd) { _Kp = Kp; _Ki = Ki; _Kd = Kd; }
    void reset() { _integral = 0; }

private:
    double _dt;
    double _max;
    double _min;
    double _Kp;
    double _Ki;
    double _Kd;
    double _pre_error;
    double _integral;
};


botPID::botPID(double dt, double max, double min, double Kp, double Ki, double Kd)
{
    pimpl = new botPIDImpl(dt, max, min, Kp, Ki, Kd);
}
void botPID::adjust(double Kp, double Ki, double Kd)
{
    pimpl->adjust(Kp, Ki, Kd);
}
void botPID::reset()
{
    pimpl->reset();
}
double botPID::calculate(double setpoint, double pv)
{
    return pimpl->calculate(setpoint, pv);
}
botPID::~botPID()
{
    delete pimpl;
}


/**
 * Implementation
 */
botPIDImpl::botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd) :
    _dt(dt),
    _max(max),
    _min(min),
    _Kp(Kp),
    _Ki(Ki),
    _Kd(Kd),
    _pre_error(0),
    _integral(0)
{
}

double botPIDImpl::calculate(double setpoint, double pv)
{

    // Calculate error
    double error = setpoint - pv;

    // Proportional term
    double Pout = _Kp * error;

    // Integral term
    _integral += error * _dt;

    double Iout = _Ki * _integral;

    // Derivative term
    double derivative = (error - _pre_error) / _dt;
    double Dout = _Kd * derivative;

    // Calculate total output
    double output = Pout + Iout + Dout;

    // Restrict to max/min
    if (output > _max)
    {
        output = _max;
        _integral -= error * _dt; //Stop integral buildup at max
    }
    else if (output < _min)
    {
        output = _min;
        _integral -= error * _dt; //Stop integral buildup at min
    }

    // Save error to previous error
    _pre_error = error;

    return output;
}

botPIDImpl::~botPIDImpl()
{
}

RandomPlayerbotMgr::RandomPlayerbotMgr() 
: PlayerbotHolder()
, processTicks(0)
, loginProgressBar(NULL)
{
    if (sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.randomBotAutologin)
    {
        sPlayerbotCommandServer.Start();
        PrepareTeleportCache();

        for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
        {
            for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
            {
                BgPlayers[j][i][0] = 0;
                BgPlayers[j][i][1] = 0;
                BgBots[j][i][0] = 0;
                BgBots[j][i][1] = 0;
                ArenaBots[j][i][0][0] = 0;
                ArenaBots[j][i][0][1] = 0;
                ArenaBots[j][i][1][0] = 0;
                ArenaBots[j][i][1][1] = 0;
                NeedBots[j][i][0] = false;
                NeedBots[j][i][1] = false;
            }
        }

        //1) Proportional: Amount activity is adjusted based on diff being above or below wanted diff. (100 wanted diff & 0.1 p = 150 diff = -5% activity)
        //2) Integral: Same as proportional but builds up each tick. (100 wanted diff & 0.01 i = 150 diff = -0.5% activity each tick)
        //3) Derative: Based on speed of diff. (+5 diff last tick & 0.05 d = -0.25% activity)
        pid.adjust(0.05,0.001,0.05);
        BgCheckTimer = 0;
        LfgCheckTimer = 0;
        PlayersCheckTimer = 0;
        EventTimeSyncTimer = 0;
        OfflineGroupBotsTimer = 0;
        guildsDeleted = false;
        arenaTeamsDeleted = false;

        std::list<uint32> availableBots = GetBots();

        for (auto& bot : availableBots)
        {
            if(GetEventValue(bot,"login"))
                SetEventValue(bot, "login", 0, 0);
        }

#ifndef MANGOSBOT_ZERO
        // load random bot team members
        auto results = CharacterDatabase.PQuery("SELECT guid FROM arena_team_member");
        if (results)
        {
            sLog.outString("Loading arena team bot members...");
            do
            {
                Field* fields = results->Fetch();
                uint32 lowguid = fields[0].GetUInt32();
                arenaTeamMembers.push_back(lowguid);
            } while (results->NextRow());
        }
#endif
        // sync event timers
        SyncEventTimers();

        for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
        {
            if (!sMapStore.LookupEntry(i))
                continue;

            uint32 mapId = sMapStore.LookupEntry(i)->MapID;
            facingFix[mapId] = {};
        }

        showLoginWarning = true;
    }
}

RandomPlayerbotMgr::~RandomPlayerbotMgr()
{
}

int RandomPlayerbotMgr::GetMaxAllowedBotCount()
{
    return GetEventValue(0, "bot_count");
}

inline void print_line(Unit* bot, const std::vector<std::pair<int, int>> line, bool is_sqDist_greater_200)
{
    std::ostringstream out;
    out << bot->GetName() << ",";
    out << std::fixed << std::setprecision(1);
    out << "\"LINESTRING(";
    for (auto& p : line)
    {
        out << p.first << " " << p.second << (&p == &line.back() ? "" : ",");
    }    
    out << ")\",";
    out << bot->GetOrientation() << ",";
    out << std::to_string(bot->getRace()) << ",";
    out << std::to_string(bot->getClass()) << ",";
    out << (is_sqDist_greater_200 ? "1" : "0");
    sPlayerbotAIConfig.log("player_paths.csv", out.str().c_str());
}

inline void print_path(Unit* bot, std::vector<std::pair<int, int>>& log)
{
    std::vector<std::pair<int, int>> line;

    std::pair<int, int> lastP = {0, 0};

    for (auto& p : log)
    {
        if (lastP.first && lastP.second && pow(lastP.first - p.first, 2) + pow(lastP.second - p.second, 2) > 200 * 200)
        {
            if (line.size()>1)
                print_line(bot, line, false);      //Print previous path.
            print_line(bot, {lastP, p}, true); //Print jump.
            line.clear();
        }
        line.push_back(p);
        lastP = p;
    }
    if (line.size() > 1)
        print_line(bot, line, false); //Print remaining path.
}

void RandomPlayerbotMgr::LogPlayerLocation()
{
    botCount = 0;
    activeBots = 0;
    if (sPlayerbotAIConfig.randomBotAutologin)
    {
        ForEachPlayerbot([&](Player* bot) {
            if (GetBotAI(bot))
            {

                botCount++;
                if (GetBotAI(bot)->AllowActivity(ALL_ACTIVITY))
                {
                    activeBots++;
                }
            }
        });
    }

    for (auto i : GetPlayers())
    {
        Player* bot = i.second;
        if (!bot)
            continue;
        if (GetBotAI(bot))
        {
            botCount++;
            if (GetBotAI(bot)->AllowActivity(ALL_ACTIVITY))
                activeBots++;
        }
    }

    if (sPlayerbotAIConfig.hasLog("player_location.csv"))
    {
        try
        {
            sPlayerbotAIConfig.openLog("player_location.csv", "w");

            if (sPlayerbotAIConfig.hasLog("player_route.csv"))
                sPlayerbotAIConfig.openLog("player_route.csv", "w");

            if (sPlayerbotAIConfig.randomBotAutologin)
            {
                ForEachPlayerbot([&](Player* bot) {
                    std::ostringstream out;
                    out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                    out << "RND" << ",";
                    out << bot->GetName() << ",";
                    out << std::fixed << std::setprecision(2);
                    WorldPosition(bot).printWKT(out);
                    out << bot->GetOrientation() << ",";
                    out << std::to_string(bot->getRace()) << ",";
                    out << std::to_string(bot->getClass()) << ",";
                    out << bot->GetMapId() << ",";
                    out << bot->GetLevel() << ",";
                    out << bot->GetHealth() << ",";
                    out << bot->GetPowerPercent() << ",";
                    out << bot->GetMoney() << ",";

                    if (GetBotAI(bot))
                    {
                        out << std::to_string(uint8(GetBotAI(bot)->GetGrouperType())) << ",";
                        out << std::to_string(uint8(GetBotAI(bot)->GetGuilderType())) << ",";
                        out << (GetBotAI(bot)->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                        out << (GetBotAI(bot)->IsActive() ? "active" : "delay") << ",";
                        out << GetBotAI(bot)->HandleRemoteCommand("state") << ",";
                        PlayerbotAI* ai = GetBotAI(bot);
                        AiObjectContext* context = ai->GetAiObjectContext();

                        out << (AI_VALUE(bool, "should get money") ? "should get money" : "has enough money") << ",";

                        if (sPlayerbotAIConfig.hasLog("player_route.csv") && WorldPosition(bot))
                        {
                            LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");

                            std::vector<PathNodePoint> fullPath = lastMove.lastPath.getPath();

                            if (!fullPath.empty())
                            {
                                std::vector<std::pair<std::vector<WorldPosition>, bool>> splitPath;

                                bool currentWalkable = fullPath[0].isWalkable();
                                std::vector<WorldPosition> currentSegment;
                                currentSegment.push_back(fullPath[0].point);

                                for (size_t i = 1; i < fullPath.size(); i++)
                                {
                                    bool walkable = fullPath[i].isWalkable();

                                    if (walkable != currentWalkable)
                                    {
                                        // End current segment, start new one beginning with the last point
                                        splitPath.push_back({currentSegment, currentWalkable});
                                        currentSegment.clear();
                                        currentSegment.push_back(fullPath[i - 1].point); // shared junction point
                                        currentWalkable = walkable;
                                    }

                                    currentSegment.push_back(fullPath[i].point);
                                }

                                splitPath.push_back({currentSegment, currentWalkable});

                                uint32 segmentNr = 0;

                                for (auto& [segement, walkable] : splitPath)
                                {
                                    segmentNr++;
                                    std::ostringstream out;
                                    out << bot->GetName() << ",";
                                    out << std::fixed << std::setprecision(1);

                                    out << segmentNr << ",";

                                    WorldPosition().printWKT(segement, out, 1, false);

                                    out << bot->GetOrientation() << ",";
                                    out << std::to_string(bot->getRace()) << ",";
                                    out << std::to_string(bot->getClass()) << ",";
                                    out << (walkable ? "1" : "0") << ",";
                                    out << lastMove.moveEvent.getSource();
                                    sPlayerbotAIConfig.log("player_route.csv", out.str().c_str());
                                }
                            }
                        }
                    }
                    else
                    {
                        out << 0 << "," << 0 << ",err,err,err,err,";
                    }

                    out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                    out << (bot->IsDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive") << ",";

                    if (bot->GetGroup())
                        WorldPosition(bot).printWKT({bot, sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid())}, out, 1);

                    sPlayerbotAIConfig.log("player_location.csv", out.str().c_str());

                    if (sPlayerbotAIConfig.hasLog("player_paths.csv") && WorldPosition(bot))
                    {
                        auto& botMoveLog = playerBotMoveLog[bot->GetObjectGuid().GetCounter()];

                        std::pair<int32, int32> curDisplayPos = std::make_pair(WorldPosition(bot).getDisplayX(), WorldPosition(bot).getDisplayY());

                        botMoveLog.push_back(curDisplayPos);

                        if (botMoveLog.size() > 100)
                        {
                            print_path(bot, botMoveLog);
                            botMoveLog.clear();
                            botMoveLog.push_back(curDisplayPos); //Start next path at current position.
                        }
                    }
                });
            }

            for (auto i : GetPlayers())
            {
                Player* bot = i.second;
                if (!bot)
                    continue;

                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                out << "PLR" << ",";
                out << bot->GetName() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(bot).printWKT(out);
                out << bot->GetOrientation() << ",";
                out << std::to_string(bot->getRace()) << ",";
                out << std::to_string(bot->getClass()) << ",";
                out << bot->GetMapId() << ",";
                out << bot->GetLevel() << ",";
                out << bot->GetHealth() << ",";
                out << bot->GetPowerPercent() << ",";
                out << bot->GetMoney() << ",";
                if (GetBotAI(bot))
                {
                    out << std::to_string(uint8(GetBotAI(bot)->GetGrouperType())) << ",";
                    out << std::to_string(uint8(GetBotAI(bot)->GetGuilderType())) << ",";
                    out << (GetBotAI(bot)->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                    out << (GetBotAI(bot)->IsActive() ? "active" : "delay") << ",";
                    out << GetBotAI(bot)->HandleRemoteCommand("state") << ",";
                    PlayerbotAI* ai = GetBotAI(bot);
                    AiObjectContext* context = ai->GetAiObjectContext();

                    out << (AI_VALUE(bool, "should get money") ? "should get money" : "has enough money") << ",";
                }
                else
                {
                    out << 0 << "," << 0 << ",player,player,player,player,";
                }

                out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                out << (bot->IsDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive") << ",";

                if (bot->GetGroup())
                    WorldPosition(bot).printWKT({bot, sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid())}, out, 1);

                sPlayerbotAIConfig.log("player_location.csv", out.str().c_str());

                if (sPlayerbotAIConfig.hasLog("player_paths.csv") && WorldPosition(bot))
                {
                    auto& botMoveLog = playerBotMoveLog[bot->GetObjectGuid().GetCounter()];

                    std::pair<int32, int32> curDisplayPos = std::make_pair(WorldPosition(bot).getDisplayX(), WorldPosition(bot).getDisplayY());

                    botMoveLog.push_back(curDisplayPos);

                    if (botMoveLog.size() > 100)
                    {
                        print_path(bot, botMoveLog);
                        botMoveLog.clear();
                        botMoveLog.push_back(curDisplayPos); //Start next path at current position.
                    }
                }
            }
        }
        catch (...)
        {
            return;
            //This is to prevent some thread-unsafeness. Crashes would happen if bots get added or removed.
            //We really don't care here. Just skip a log. Making this thread-safe is not worth the effort.
        }
    }
    if (sPlayerbotAIConfig.hasLog("transport.csv"))
    {
        sPlayerbotAIConfig.openLog("transport.csv", "w");
        for (auto& [mapId, map] : sMapMgr.Maps())
        {
            for (auto& transport : WorldPosition(map->GetId(), 1, 1).getTransports())
            {
                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                if (transport->GetName() == nullptr || transport->GetName()[0] == '\0')
                {
                    GameObjectInfo const* data = sGOStorage.LookupEntry<GameObjectInfo>(transport->GetEntry());
                    out << data->name << ",";
                }
                else
                    out << transport->GetName() << ",";

                out << transport->GetEntry() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(transport).printWKT(out);
                out << transport->GetOrientation();

                sPlayerbotAIConfig.log("transport.csv", out.str().c_str());
            }
        }
    }
}

void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)
{
#ifdef MEMORY_MONITOR
    sMemoryMonitor.Print();
    sMemoryMonitor.LogCount(sConfig.GetStringDefault("LogsDir") + "/" + "memory.csv");
#endif

    // tick random bots' sessions so
    // teleport ACKs (HandleTeleportAck) and queued packets get processed.
    // See PlayerbotMgr::UpdateAIInternal for the rationale — same call,
    // same purpose, applied to the random-bot pool.
    UpdateSessions(elapsed);

    if (!sPlayerbotAIConfig.randomBotAutologin || !sPlayerbotAIConfig.enabled)
        return;

    // Populate-Around-Players (F1): refresh the real-player zone demand map every 30s.
    if (sPlayerbotAIConfig.populateAroundPlayers)
    {
        uint32 now = (uint32)time(nullptr);
        if (now - _playerZoneRefreshTime >= 30)
        {
            _playerZoneRefreshTime = now;
            RefreshPlayerZones();
        }
    }

    // Overland travel parties (F2): drive the active party; auto-fire one in test mode.
    if (sPlayerbotAIConfig.travelParties)
    {
        UpdateTravelParties();
        // Parties used to auto-form ONLY in test mode, so a live server with TravelParties=1 and
        // test mode off never formed a single one - the sole other caller is the GM command
        // `.rndbot travelparty`. That made the whole feature invisible in normal play, which is
        // the opposite of the point: these marches exist to make the world look inhabited.
        //
        // TravelPartySpawnInterval is the live cadence. Test mode keeps its fixed 60 s so every
        // recorded test recipe reproduces unchanged. The concurrency cap is what actually bounds
        // the population; this only sets how fast a freed slot is refilled.
        uint32 const spawnEvery = sPlayerbotAIConfig.travelPartyTestMode
                                      ? 60 : sPlayerbotAIConfig.travelPartySpawnInterval;
        if (spawnEvery)
        {
            uint32 now = (uint32)time(nullptr);
            if (now - _travelPartyTime >= spawnEvery)
            {
                _travelPartyTime = now;
                SpawnTravelParty();
            }
        }
    }

    if (!playersLevel)
        playersLevel = sPlayerbotAIConfig.syncLevelNoPlayer;

    ScaleBotActivity();
    if (sPlayerbotAIConfig.asyncBotLogin)
    {
        auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "AsyncBotLogin");
        sPlayerBotLoginMgr.Update(players);
        pmo.reset();
    }

    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");
    if (!maxAllowedBotCount || ((uint32)maxAllowedBotCount < sPlayerbotAIConfig.minRandomBots || (uint32)maxAllowedBotCount > sPlayerbotAIConfig.maxRandomBots))
    {
        maxAllowedBotCount = urand(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
        SetEventValue(0, "bot_count", maxAllowedBotCount,
            urand(sPlayerbotAIConfig.randomBotCountChangeMinInterval, sPlayerbotAIConfig.randomBotCountChangeMaxInterval));
    }

    std::list<uint32> availableBots = GetBots();    
    uint32 availableBotCount = availableBots.size();
    uint32 onlineBotCount = GetPlayerbotsAmount();
    
    SetAIInternalUpdateDelay(sPlayerbotAIConfig.randomBotUpdateInterval);

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT,
        onlineBotCount < maxAllowedBotCount ? "RandomPlayerbotMgr::Login" : "RandomPlayerbotMgr::UpdateAIInternal");

    if (time(nullptr) > (EventTimeSyncTimer + 30))
        SaveCurTime();

    if (availableBotCount < maxAllowedBotCount && !sWorld.IsShutdowning())
    {
        bool logInAllowed = true;
        if (sPlayerbotAIConfig.randomBotLoginWithPlayer)
        {
            logInAllowed = !players.empty();
        }

        if (logInAllowed)
        {
            AddRandomBots();
            EnsurePinnedBotsOnline();
        }
    }

    if (sPlayerbotAIConfig.syncLevelWithPlayers && players.size())
    {
        if (time(nullptr) > (PlayersCheckTimer + 60))
            CheckPlayers();
    }

    if (sPlayerbotAIConfig.randomBotJoinLfg && players.size())
    {
        if (time(nullptr) > (LfgCheckTimer + 30))
            CheckLfgQueue();
    }

    if (sPlayerbotAIConfig.randomBotJoinBG/* && players.size()*/)
    {
        if (time(nullptr) > (BgCheckTimer + 30))
            CheckBgQueue();
    }

    if (time(nullptr) > (OfflineGroupBotsTimer + 5) && players.size())
        AddOfflineGroupBots();

    uint32 updateBots = sPlayerbotAIConfig.randomBotsPerInterval == 0 ? UINT32_MAX : sPlayerbotAIConfig.randomBotsPerInterval;

    //Update bots
    for (auto bot : availableBots)
    {
        if (GetPlayerBot(bot))
        {
            if (ProcessBot(bot))
                updateBots--;

            if (!updateBots)
                break;
        }
    }

    uint32 maxLogins = sPlayerbotAIConfig.randomBotsMaxLoginsPerInterval;

    //Log in bots
    if (sRandomPlayerbotMgr.GetDatabaseDelay("CharacterDatabase") < 10 * IN_MILLISECONDS && !sPlayerbotAIConfig.asyncBotLogin && onlineBotCount < maxAllowedBotCount && maxLogins > 0)
    {
        for (auto bot : availableBots)
        {
            if (GetPlayerBot(bot))
                continue;   

            if (!eventCache[bot].empty() && GetEventValue(bot, "login"))
            {
                onlineBotCount++;
                continue;
            }

            if (GetEventValue(bot, "login"))
                onlineBotCount++;

            if (onlineBotCount >= maxAllowedBotCount)
                break;

            if (ProcessBot(bot)) {
                --maxLogins;
            }

            if (maxLogins == 0)
                break;
        }
    }

    LoginFreeBots();

    //sLog.outString("[char %d, bot %d]", CharacterDatabase.m_threadBody->m_sqlQueue.size(), CharacterDatabase.m_threadBody->m_sqlQueue.size());
   
    LogPlayerLocation();

    DelayedFacingFix();

    MirrorAh();

    for (auto& [mapId, map] : sMapMgr.Maps())
    {
        sPerformanceMonitor.Init(map->GetId(), map->GetInstanceId());
    }

    //Ping character database.
    CharacterDatabase.AsyncPQuery(&RandomPlayerbotMgr::DatabasePing, sWorld.GetCurrentMSTime(), std::string("CharacterDatabase"), "SELECT 1");

    PlayerbotHolder::UpdateAIInternal(elapsed, minimal);
}

void RandomPlayerbotMgr::ScaleBotActivity()
{
    float activityPercentage = getActivityPercentage();

    //if (activityPercentage >= 100.0f || activityPercentage <= 0.0f) pid.reset(); //Stop integer buildup during max/min activity

    //    % increase/decrease                   wanted diff                                         , avg diff
    float activityPercentageMod = pid.calculate(sRandomPlayerbotMgr.GetPlayers().empty() ? sPlayerbotAIConfig.diffEmpty : sPlayerbotAIConfig.diffWithPlayer, sWorld.GetAverageDiff());

    activityPercentage = activityPercentageMod + 50;

    //Cap the percentage between 0 and 100.
    activityPercentage = std::max(0.0f, std::min(100.0f, activityPercentage));

    setActivityPercentage(activityPercentage);

    if (sPlayerbotAIConfig.hasLog("activity_pid.csv"))
    {
        double virtualMemUsedByMe = 0;
#if PLATFORM == PLATFORM_WINDOWS
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
        virtualMemUsedByMe = pmc.PrivateUsage;
#endif

        std::ostringstream out;
        out << sWorld.GetCurrentMSTime() << ", ";

        out << sWorld.GetCurrentDiff() << ",";
        out << sWorld.GetAverageDiff() << ",";
        out << sWorld.GetMaxDiff() << ",";
        out << virtualMemUsedByMe << ",";
        out << activityPercentage << ",";
        out << activityPercentageMod << ",";
        out << activeBots << ",";
        out << GetPlayerbotsAmount() << ",";

        float totalLevel = 0, totalGold = 0, totalGearscore = 0;

        if (sPlayerbotAIConfig.randomBotAutologin)
        {
            ForEachPlayerbot([&](Player* bot)
            {
                if (GetBotAI(bot)->AllowActivity())
                {
                    std::string bracket = "level:" + std::to_string(bot->GetLevel() / 10);

                    float level = GetBotAI(bot)->GetLevelFloat();
                    totalLevel += level;
                    float gold = bot->GetMoney() / 10000;
                    totalGold += gold;
                    float gearscore = GetBotAI(bot)->GetEquipGearScore(bot, false, false);
                    totalGearscore += gearscore;

                    const uint32 botGuid = bot->GetObjectGuid().GetCounter();
                    PushMetric(botPerformanceMetrics[bracket], botGuid, level);
                    PushMetric(botPerformanceMetrics["gold"], botGuid, gold);
                    PushMetric(botPerformanceMetrics["gearscore"], botGuid, gearscore);
                }
            });
        }

        out << std::fixed << std::setprecision(4);
        out << totalLevel << ",";

        for (uint8 i = 0; i < (DEFAULT_MAX_LEVEL / 10) + 1; i++)
        {
            out << GetMetricDelta(botPerformanceMetrics["level:" + std::to_string(i)]) * 12 * 60 << ",";
        }

        out << totalGold << ",";
        out << GetMetricDelta(botPerformanceMetrics["gold"]) * 12 * 60 << ",";
        out << totalGearscore << ",";
        out << GetMetricDelta(botPerformanceMetrics["gearscore"]) * 12 * 60 << ",";
        //out << CharacterDatabase.m_threadBody->m_sqlQueue.size();

        sPlayerbotAIConfig.log("activity_pid.csv", out.str().c_str());
    }
}

void RandomPlayerbotMgr::LoginFreeBots()
{
    if (!sPlayerbotAIConfig.freeAltBots.empty() && sPlayerbotAIConfig.botAutologin != BotAutoLogin::LOGIN_ONLY_ALWAYS_ACTIVE)
    {
        std::vector<std::pair<uint32, uint32>> botsToRemove;

        for (auto [accountId, botGuid] : sPlayerbotAIConfig.freeAltBots)
        {
            ObjectGuid guid(ObjectGuid(HIGHGUID_PLAYER, botGuid));
            Player* bot = sObjectMgr.GetPlayer(guid, false);

            if (!bot)
            {
                sLog.outDetail("Add player %d", botGuid);
                AddPlayerBot(botGuid, accountId);
            }
            else if (!bot->IsBeingTeleported())
            {
                if (sRandomPlayerbotMgr.GetValue(botGuid, "create levelup"))
                {
                    PlayerbotFactory factory(bot, bot->GetLevel());
                    factory.Randomize(true, false);

                    sRandomPlayerbotMgr.SetValue(botGuid, "create levelup", 0);
                }

                Player* master = nullptr;

                if (sRandomPlayerbotMgr.GetValue(botGuid, "create group"))
                {
                    std::string groupWith = sRandomPlayerbotMgr.GetData(botGuid, "create group");

                    if (!groupWith.empty())
                    {
                        master = sObjectAccessor.FindPlayerByName(groupWith.c_str());

                        if (master)
                        {
                            GetBotAI(bot)->DoSpecificAction("join", Event("create group", "", master));
                        }
                    }

                    sRandomPlayerbotMgr.SetValue(botGuid, "create group", 0);
                }

                if (sRandomPlayerbotMgr.GetValue(botGuid, "create gear"))
                {
                    std::string gear = sRandomPlayerbotMgr.GetData(botGuid, "create gear");
                    if (gear == "empty")
                    {
                        for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
                        {
                            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                        }
                    }

                    else if (gear == "green" || gear == "uncommon")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_UNCOMMON);
                        factory.EquipGear();
                    }
                    else if (gear == "blue" || gear == "rare")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_RARE);
                        factory.EquipGear();
                    }
                    else if (gear == "purple" || gear == "epic")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_EPIC);
                        factory.EquipGear();
                    }
                    else if (gear == "upgrade")
                    {
                        PlayerbotFactory factory(bot, master ? master->GetLevel() : bot->GetLevel(), ITEM_QUALITY_NORMAL);
                        factory.UpgradeGear(false);
                    }
                    else if (gear == "sync")
                    {
                        PlayerbotFactory factory(bot, master ? master->GetLevel() : bot->GetLevel(), ITEM_QUALITY_NORMAL);
                        factory.UpgradeGear(true);
                    }
                    else if (gear == "best")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGearBest();
                    }
                    else if (gear == "partial")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGearPartialUpgrade();
                    }
                    else
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGear();
                    }
                }

                if (GetEventValue(botGuid, "test"))
                {
                    PlayerbotAI* ai = GetBotAI(bot);
                    AiObjectContext* context = ai->GetAiObjectContext();
                    std::string testName = GetEventData(botGuid, "test");
                    testName = std::regex_replace(testName, std::regex("\\'"), "'");
                    std::string strategyName = "test::" + testName;
                    ai->ChangeStrategy("+" + strategyName, BotState::BOT_STATE_NON_COMBAT);
                    SET_AI_VALUE2(bool, "manual bool", "is running test", true);

                    sRandomPlayerbotMgr.SetValue(botGuid, "test", 0);
                }

                if (!IsRandomBot(bot) && GetPlayerBot(guid)) //Place bot in player manager.
                {
                    for (auto& [mGuid, master] : players)
                    {
                        ObjectGuid masterGuid(ObjectGuid(HIGHGUID_PLAYER, mGuid));
                        if (accountId == sObjectMgr.GetPlayerAccountIdByGUID(masterGuid))
                        {
                            PlayerbotMgr* mgr = GetBotMgr(master);
                            if (mgr)
                            {
                                MovePlayerBot(guid, mgr);
                            }
                        }
                    }
                }

                if (master)
                    bot->TeleportTo(WorldPosition(master));

                BotAlwaysOnline always = BotAlwaysOnline(sRandomPlayerbotMgr.GetValue(botGuid, "always"));
                if (always != BotAlwaysOnline::ACTIVE)
                {
                    botsToRemove.push_back({accountId, botGuid});
                }
            }
        }

        sPlayerbotAIConfig.freeAltBots.remove_if([&](const std::pair<uint32, uint32>& entry) {
            return std::find(botsToRemove.begin(), botsToRemove.end(), entry) != botsToRemove.end();
        });
    }
}

void RandomPlayerbotMgr::DelayedFacingFix()
{
    if (!sPlayerbotAIConfig.turnInRpg)
        return;

    for (auto& fMap : facingFix) {
        for (auto& fInstance : fMap.second) {
            for (auto obj : fInstance.second) {
                if (time(0) - obj.second > 5)
                {
                    if (!obj.first.IsCreature())
                        continue;

                    GuidPosition guidP(obj.first, WorldPosition(fMap.first, 0, 0, 0));

                    Creature* unit = guidP.GetCreature(fInstance.first);

                    if (!unit)
                        continue;

                    CreatureData const* data = guidP.GetCreatureData();

                    if (!data)
                        continue;

                    if (unit->GetOrientation() == data->position.orientation)
                        continue;

                    unit->SetFacingTo(data->position.orientation);
                }
            }
        }
        facingFix[fMap.first].clear();
    }
}

void RandomPlayerbotMgr::DatabasePing(QueryResult* result, uint32 pingStart, std::string db)
{
    sRandomPlayerbotMgr.SetDatabaseDelay(db, sWorld.GetCurrentMSTime() - pingStart);
    delete result;
}

void RandomPlayerbotMgr::LoadNamedLocations()
{
    namedLocations.clear();

    auto result = WorldDatabase.Query("SELECT `name`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation` FROM `ai_playerbot_named_location` WHERE `name` NOT LIKE 'FISH_LOCATION%'");

    if (!result)
    {
        sLog.outString(">> Loaded 0 named locations - table is empty!");
        sLog.outString();
        return;
    }

    uint32 count = 0;
    do
    {
        ++count;

        Field* fields = result->Fetch();

        std::string name = fields[0].GetCppString();
        uint32 mapId = fields[1].GetUInt32();
        float positionX = fields[2].GetFloat();
        float positionY = fields[3].GetFloat();
        float positionZ = fields[4].GetFloat();
        float orientation = fields[5].GetFloat();

        AddNamedLocation(name, WorldLocation(mapId, positionX, positionY, positionZ, orientation));
    } while (result->NextRow());

    sLog.outString(">> Loaded %u named locations", count);
    sLog.outString();
}

bool RandomPlayerbotMgr::AddNamedLocation(std::string const& name, WorldLocation const& location)
{
    if (namedLocations.find(name) != namedLocations.end())
    {
        sLog.outError("RandomPlayerbotMgr::AddNamedLocation: Failed to add named location '%s' - already exists!", name.c_str());
        return false;
    }

    namedLocations[name] = location;

    return true;
}

bool RandomPlayerbotMgr::GetNamedLocation(std::string const& name, WorldLocation& location)
{
    auto itr = namedLocations.find(name);
    if (itr == namedLocations.end())
    {
        sLog.outError("RandomPlayerbotMgr::GetNamedLocation: Named location '%s' not found! Please ensure that your ai_playerbot_named_location table is up to date.", name.c_str());
        return false;
    }

    location = itr->second;

    return true;
}

uint32 RandomPlayerbotMgr::AddRandomBots()
{
    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");    
    uint32 currentAllowedBotCount = maxAllowedBotCount;

    uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    float currentAvgLevel = 0, wantedAvgLevel = 0, randomAvgLevel = 0;

    if(sPlayerbotAIConfig.asyncBotLogin)
        return 0;
  
    if (currentBots.size() < currentAllowedBotCount)
    {
        if (sPlayerbotAIConfig.syncLevelWithPlayers)
        {
            maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel + sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));

            wantedAvgLevel = maxLevel / 2;
            uint32 botsAmount = 0;
            ForEachPlayerbot([&](Player* bot)
            {
                currentAvgLevel += bot->GetLevel();
                botsAmount++;
            });
                

            if(currentAvgLevel)
            {
                currentAvgLevel = currentAvgLevel / botsAmount;
            }

            randomAvgLevel = (sPlayerbotAIConfig.randomBotMinLevel + std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel+ sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)))) / 2;
        }

        currentAllowedBotCount -= currentBots.size();

        int32 neededAddBots = currentAllowedBotCount;

    // BOTPROBE - temporary. Bots below level 20 are almost never online (2.4%)
    // while those at 30-49 are online 96% of the time, and measurement has ruled
    // out guid order, class, race, the logout event and any level filter. The
    // only gate left is the classRaceAllowed quota, which lives in memory only.
    // Counts every row this loop looks at, by level band and by the reason it
    // was skipped, so the answer comes from one startup instead of another guess.
    uint32 botProbe[7][7] = {};
#define BOTPROBE(b, r) do { botProbe[(b)][(r)]++; } while (0)

        currentAllowedBotCount = currentAllowedBotCount*2;      

        CharacterDatabase.AllowAsyncTransactions();
        CharacterDatabase.BeginTransaction();

        bool enoughBotsForCriteria = true;

        for (uint32 noCriteria = 0; noCriteria < 3; noCriteria++)
        {
            int32  classRaceAllowed[MAX_CLASSES][MAX_RACES] = { 0 };

            for (uint32 race = 1; race < MAX_RACES; ++race)
            {
                for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
                {
                    if (sPlayerbotAIConfig.useFixedClassRaceCounts)
                    {
                        classRaceAllowed[cls][race] = sPlayerbotAIConfig.fixedClassRaceCounts[{cls, race}];
                    }
                    else
                    {
                        if (sPlayerbotAIConfig.classRaceProbability[cls][race])
                            classRaceAllowed[cls][race] = ((sPlayerbotAIConfig.classRaceProbability[cls][race] * maxAllowedBotCount / sPlayerbotAIConfig.classRaceProbabilityTotal) + 1) * (noCriteria + 1);
                    }
                }
            }

            // The account list used to be walked in the same order on every pass
            // while classRaceAllowed is one global quota - so the first accounts
            // spent it and everything behind them was never reached at all.
            // Measured with the counters below before this fix: accounts 14-135
            // were online ~99% of the time, everything from 138 up sat at 4-6%,
            // and their levels followed, 37 against 11, because only the bots the
            // loop could reach ever got to play. That also starved the LFT queue
            // of low level companions. Shuffling per pass gives every account the
            // same chance of being served first.
            std::vector<uint32> accountOrder(sPlayerbotAIConfig.randomBotAccounts.begin(),
                                             sPlayerbotAIConfig.randomBotAccounts.end());
            std::shuffle(accountOrder.begin(), accountOrder.end(), *GetRandomGenerator());

            for (std::vector<uint32>::iterator i = accountOrder.begin(); i != accountOrder.end(); i++)
            {
                uint32 accountId = *i;

                std::unique_ptr<QueryResult> result;

                if (noCriteria == 2)
                {
                    result.reset(CharacterDatabase.PQuery("SELECT guid, level, totaltime, race, class FROM characters WHERE account = '%u'", accountId));
                }
                else
                {
                    bool needToIncrease = wantedAvgLevel && currentAvgLevel + 1 < wantedAvgLevel;
                    bool needToLower = wantedAvgLevel && currentAvgLevel > wantedAvgLevel + 1;
                    bool rndCanIncrease = !sPlayerbotAIConfig.disableRandomLevels && randomAvgLevel > currentAvgLevel;
                    bool rndCanLower = !sPlayerbotAIConfig.disableRandomLevels && randomAvgLevel < currentAvgLevel;

                    std::string query = "SELECT guid, level, totaltime, race, class FROM characters WHERE account = '%u' AND level <= %u";
                    std::string wasRand = sPlayerbotAIConfig.instantRandomize ? "totaltime" : "(level > 1)";

                    if (needToIncrease) //We need more higher level bots.
                    {
                        query += " AND (level > %u";
                        if (rndCanIncrease) //Log in higher level bots or bots that will be randomized.
                            query += " OR !" + wasRand;
                        query += ")";

                        result.reset(CharacterDatabase.PQuery(query.c_str(), accountId, maxLevel, (uint32)wantedAvgLevel));
                    }
                    else
                    {
                        if (needToLower && !rndCanLower) //Do not load unrandomized if it'll only increase level.
                            query += " AND " + wasRand;

                        result.reset(CharacterDatabase.PQuery(query.c_str(), accountId, maxLevel));
                    }
                }

                if (!result)
                    continue;

                do
                {
                    Field* fields = result->Fetch();
                    uint32 guid = fields[0].GetUInt32();
                    uint32 level = fields[1].GetUInt32();
                    uint32 totaltime = fields[2].GetUInt32();
                    uint32 race = fields[3].GetUInt32();
                    uint32 cls = fields[4].GetUInt32();

                    uint32 const band = std::min<uint32>(level / 10, 6);
                    BOTPROBE(band, 0);

                    if (GetEventValue(guid, "add"))
                    {
                        BOTPROBE(band, 1);
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (GetEventValue(guid, "logout"))
                    {
                        BOTPROBE(band, 2);
                        continue;
                    }

                    if (GetPlayerBot(guid))
                    {
                        BOTPROBE(band, 3);
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (std::find(currentBots.begin(), currentBots.end(), guid) != currentBots.end())
                    {
                        BOTPROBE(band, 4);
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (classRaceAllowed[cls][race] <= 0)
                    {
                        BOTPROBE(band, 5);
                        continue;
                    }

                    BOTPROBE(band, 6);
                    SetEventValue(guid, "add", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime));
                    SetEventValue(guid, "logout", 0, 0);
                    currentBots.push_back(guid);

                    if(!noCriteria)
                        classRaceAllowed[cls][race]--;

                    if (wantedAvgLevel)
                    {
                        if (sPlayerbotAIConfig.instantRandomize ? totaltime : level > 1)
                            currentAvgLevel += (float)level / currentBots.size();
                        else
                            currentAvgLevel += (float)level + randomAvgLevel; //Use predicted randomized level. This will be wrong but avarage out correct.
                    }

                    currentAllowedBotCount--;
                    neededAddBots--;

                    if (!currentAllowedBotCount)
                        break;

                } while (result->NextRow());

                if (!currentAllowedBotCount)
                    break;
            }

            if (!currentAllowedBotCount)
                break;

            {
        uint32 gesamt = 0;
        for (uint32 b = 0; b < 7; ++b)
            gesamt += botProbe[b][0];

        if (gesamt)
        {
            sLog.outBasic("BOTPROBE: band | seen | has-add | logout | in-world | in-list | quota | taken");
            for (uint32 b = 0; b < 7; ++b)
            {
                if (!botProbe[b][0])
                    continue;
                sLog.outBasic("BOTPROBE: %2u-%2u | %5u | %7u | %6u | %8u | %7u | %5u | %5u",
                    b * 10, b * 10 + 9, botProbe[b][0], botProbe[b][1], botProbe[b][2],
                    botProbe[b][3], botProbe[b][4], botProbe[b][5], botProbe[b][6]);
            }
        }
    }
#undef BOTPROBE

    if (showLoginWarning && neededAddBots > 0)
            {
                sLog.outError("Not enough accounts to meet selection criteria. A random selection of bots was activated to fill the server.");

                if (sPlayerbotAIConfig.syncLevelWithPlayers)
                    sLog.outError("Only bots between level %d and %d are selected to sync with player level", uint32((currentAvgLevel + 1 < wantedAvgLevel) ? wantedAvgLevel : 1), maxLevel);

                ChatHelper chat(nullptr);

                for (uint32 race = 1; race < MAX_RACES; ++race)
                {
                    for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
                    {

                            int32 moreWanted = classRaceAllowed[cls][race];
                            if (moreWanted > 0)
                            {
                                if (sPlayerbotAIConfig.useFixedClassRaceCounts)
                                {
                                    int32 totalWanted = sPlayerbotAIConfig.fixedClassRaceCounts[{cls, race}];
                                    sLog.outError("%d %s %ss needed but only %d found.", totalWanted, chat.formatRace(race).c_str(), chat.formatClass(cls).c_str(), totalWanted - moreWanted);
                                }
                                else
                                {
                                    int32 totalWanted = ((sPlayerbotAIConfig.classRaceProbability[cls][race] * maxAllowedBotCount / sPlayerbotAIConfig.classRaceProbabilityTotal) + 1);
                                    float percentage = float(sPlayerbotAIConfig.classRaceProbability[cls][race]) * 100.0f / sPlayerbotAIConfig.classRaceProbabilityTotal;
                                    sLog.outError("%d %s %ss needed to get %3.2f%% of total but only %d found.", totalWanted, chat.formatRace(race).c_str(), chat.formatClass(cls).c_str(), percentage, totalWanted - moreWanted);
                                }
                            }
                        
                    }
                }

                showLoginWarning = false;
            }
        }

        CharacterDatabase.CommitTransaction();

        if (currentAllowedBotCount)
            currentAllowedBotCount = std::max(int64(GetEventValue(0, "bot_count")) - int64(currentBots.size()), int64(0));

        if(currentAllowedBotCount && sPlayerbotAIConfig.randomBotAutoCreate && !sPlayerbotAIConfig.useFixedClassRaceCounts)
#ifdef MANGOSBOT_TWO
            sLog.outError("Not enough random bot accounts available. Need %d more!!", (uint32)ceil(currentAllowedBotCount / 10));
#else
            sLog.outError("Not enough random bot accounts available. Need %d more!!", (uint32)ceil(currentAllowedBotCount / 9));
#endif
      
    }

    return currentBots.size();
}

void RandomPlayerbotMgr::LoadBattleMastersCache()
{
    BattleMastersCache.clear();

    sLog.outString("---------------------------------------");
    sLog.outString("          Loading BattleMasters Cache  ");
    sLog.outString("---------------------------------------");
    sLog.outString();

    auto result = WorldDatabase.Query("SELECT `entry`,`bg_template` FROM `battlemaster_entry`");

    uint32 count = 0;

    if (!result)
    {
        sLog.outString(">> Loaded 0 battlemaster entries - table is empty!");
        sLog.outString();
        return;
    }

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        uint32 bgTypeId = fields[1].GetUInt32();

        CreatureInfo const* bmaster = sObjectMgr.GetCreatureTemplate(entry);
        if (!bmaster)
            continue;

#ifdef MANGOS
        FactionTemplateEntry const* bmFaction = sFactionTemplateStore.LookupEntry(bmaster->FactionAlliance);
#endif
#ifdef CMANGOS
        FactionTemplateEntry const* bmFaction = sFactionTemplateStore.LookupEntry(bmaster->Faction);
#endif
        uint32 bmFactionId = bmFaction->faction;
#ifdef MANGOS
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry(bmFactionId);
#endif
#ifdef CMANGOS
#ifdef MANGOSBOT_ONE
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry<FactionEntry>(bmFactionId);
#else
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry(bmFactionId);
#endif
#endif
        uint32 bmParentTeam = bmParentFaction->team;
        Team bmTeam = TEAM_BOTH_ALLOWED;
        if (bmParentTeam == 891)
            bmTeam = ALLIANCE;
        if (bmFactionId == 189)
            bmTeam = ALLIANCE;
        if (bmParentTeam == 892)
            bmTeam = HORDE;
        if (bmFactionId == 66)
            bmTeam = HORDE;

        BattleMastersCache[bmTeam][BattleGroundTypeId(bgTypeId)].insert(BattleMastersCache[bmTeam][BattleGroundTypeId(bgTypeId)].end(), entry);
        sLog.outDetail("Cached Battmemaster #%d for BG Type %d (%s)", entry, bgTypeId, bmTeam == ALLIANCE ? "Alliance" : bmTeam == HORDE ? "Horde" : "Neutral");

    } while (result->NextRow());

    sLog.outString(">> Loaded %u battlemaster entries", count);
    sLog.outString();
}

void RandomPlayerbotMgr::CheckBgQueue()
{
    if (!BgCheckTimer)
        BgCheckTimer = time(nullptr);

    if (time(nullptr) < (BgCheckTimer + 30))
    {
        return;
    }
    else
    {
        BgCheckTimer = time(nullptr);
    }

    sLog.outDetail("Checking BG Queue...");

    for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
    {
        for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
        {
            BgPlayers[j][i][0] = 0;
            BgPlayers[j][i][1] = 0;
            BgBots[j][i][0] = 0;
            BgBots[j][i][1] = 0;
            ArenaBots[j][i][0][0] = 0;
            ArenaBots[j][i][0][1] = 0;
            ArenaBots[j][i][1][0] = 0;
            ArenaBots[j][i][1][1] = 0;
            NeedBots[j][i][0] = false;
            NeedBots[j][i][1] = false;
        }
    }

    for (auto i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld())
            continue;

        if (!player->InBattleGroundQueue())
            continue;

        if (player->InBattleGround() && player->GetBattleGround()->GetStatus() == STATUS_WAIT_LEAVE)
            continue;

        for (int i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattleGroundQueueTypeId queueTypeId = player->GetBattleGroundQueueTypeId(i);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            uint32 TeamId = player->GetTeam() == ALLIANCE ? 0 : 1;

            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
#ifndef MANGOSBOT_TWO
            BattleGroundBracketId bracketId = sBattleGroundMgr.GetBattleGroundBracketIdFromLevel(bgTypeId, player->GetLevel());
#endif
#ifdef MANGOSBOT_TWO
            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            uint32 mapId = bg->GetMapId();
            PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, player->GetLevel());
            if (!pvpDiff)
                continue;

            BattleGroundBracketId bracketId = pvpDiff->GetBracketId();
#endif
#ifdef MANGOSBOT_TWO
            /* to fix
            if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
            {
                BattleGroundQueue& bgQueue = sServerFacade.bgQueue(queueTypeId);
                GroupQueueInfo ginfo;
                uint32 tempT = TeamId;

                if (bgQueue.GetPlayerGroupInfoData(player->GetObjectGuid(), &ginfo))
                {
                    if (ginfo.isRated)
                    {
                        for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
                        {
                            uint32 arena_team_id = player->GetArenaTeamId(arena_slot);
                            ArenaTeam* arenateam = sObjectMgr.GetArenaTeamById(arena_team_id);
                            if (!arenateam)
                                continue;
                            if (arenateam->GetType() != arenaType)
                                continue;

                            Rating[queueTypeId][bracketId][1] = arenateam->GetRating();
                        }
                    }
                    TeamId = ginfo.isRated ? 1 : 0;
                }
                if (player->InArena())
                {
                    if (player->GetBattleGround()->IsRated())
                        TeamId = 1;
                    else
                        TeamId = 0;
                }
                ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;
            }
         */
#endif
#ifdef MANGOSBOT_ONE
            if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
            {
                sWorld.GetBGQueue().GetMessager().AddMessage([queueTypeId, playerId = player->GetObjectGuid(), arenaType = arenaType, bracketId = bracketId, tempT = TeamId](BattleGroundQueue* bgQueue)
                    {
                        uint32 TeamId;
                        GroupQueueInfo ginfo;

                        BattleGroundQueueItem* queueItem = &bgQueue->GetBattleGroundQueue(queueTypeId);
                        Player *player = RandomPlayerbotMgr::instance().GetPlayer(playerId);

                        if (!player)
                            return;

                        if (queueItem->GetPlayerGroupInfoData(player->GetObjectGuid(), &ginfo))
                        {
                            if (ginfo.isRated)
                            {
                                for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
                                {
                                    uint32 arena_team_id = player->GetArenaTeamId(arena_slot);
                                    ArenaTeam* arenateam = sObjectMgr.GetArenaTeamById(arena_team_id);
                                    if (!arenateam)
                                        continue;
                                    if (arenateam->GetType() != arenaType)
                                        continue;

                                    sRandomPlayerbotMgr.Rating[queueTypeId][bracketId][1] = arenateam->GetRating();
                                }
                            }
                            TeamId = ginfo.isRated ? 1 : 0;
                        }
                        if (player->InArena())
                        {
                            if (player->GetBattleGround()->IsRated()/* && (ginfo.isRated && ginfo.arenaTeamId && ginfo.arenaTeamRating && ginfo.opponentsTeamRating)*/)
                                TeamId = 1;
                            else
                                TeamId = 0;
                        }
                        sRandomPlayerbotMgr.ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;

                    }
                );
            }
#endif
            if (GetBotAI(player))
                BgBots[queueTypeId][bracketId][TeamId]++;
            else
                BgPlayers[queueTypeId][bracketId][TeamId]++;

            if (!player->IsInvitedForBattleGroundQueueType(queueTypeId) && (!player->InBattleGround() || player->GetBattleGround()->GetTypeId() != sServerFacade.BgTemplateId(queueTypeId)))
            {
#ifndef MANGOSBOT_ZERO
                if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
                {
                    NeedBots[queueTypeId][bracketId][TeamId] = true;
                }
                else
                {
                    NeedBots[queueTypeId][bracketId][0] = true;
                    NeedBots[queueTypeId][bracketId][1] = true;
                }
#else
                NeedBots[queueTypeId][bracketId][0] = true;
                NeedBots[queueTypeId][bracketId][1] = true;
#endif
            }
        }
    }

    ForEachPlayerbot([&](Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return;

        if (!bot->InBattleGroundQueue())
            return;

        if (!IsFreeBot(bot))
            return;

        if (bot->InBattleGround() && bot->GetBattleGround() && bot->GetBattleGround()->GetStatus() == STATUS_WAIT_LEAVE)
            return;

        for (int i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattleGroundQueueTypeId queueTypeId = bot->GetBattleGroundQueueTypeId(i);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            uint32 TeamId = bot->GetTeam() == ALLIANCE ? 0 : 1;

            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);

#ifndef MANGOSBOT_TWO
            BattleGroundBracketId bracketId = sBattleGroundMgr.GetBattleGroundBracketIdFromLevel(bgTypeId, bot->GetLevel());;
#endif
#ifdef MANGOSBOT_TWO
            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            uint32 mapId = bg->GetMapId();
            PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
            if (!pvpDiff)
                continue;

            BattleGroundBracketId bracketId = pvpDiff->GetBracketId();
#endif
#ifdef MANGOSBOT_TWO
            /* to fix
            ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId);
            if (arenaType != ARENA_TYPE_NONE)
            {
                BattleGroundQueue& bgQueue = sServerFacade.bgQueue(queueTypeId);
                GroupQueueInfo ginfo;
                uint32 tempT = TeamId;
                if (bgQueue.GetPlayerGroupInfoData(bot->GetObjectGuid(), &ginfo))
                {
                    TeamId = ginfo.isRated ? 1 : 0;
                }
                if (bot->InArena())
                {
                    if (bot->GetBattleGround()->IsRated())
                        TeamId = 1;
                    else
                        TeamId = 0;
                }
                ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;
            }
        */
#endif
#ifdef MANGOSBOT_ONE
            ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId);
            if (arenaType != ARENA_TYPE_NONE)
            {
                sWorld.GetBGQueue().GetMessager().AddMessage([queueTypeId, botId = bot->GetObjectGuid(), arenaType = arenaType, bracketId = bracketId, tempT = TeamId](BattleGroundQueue* bgQueue)
                    {
                        uint32 TeamId;
                        GroupQueueInfo ginfo;

                        BattleGroundQueueItem* queueItem = &bgQueue->GetBattleGroundQueue(queueTypeId);
                        Player *bot = RandomPlayerbotMgr::instance().GetPlayer(botId);
                        if (!bot)
                            return;

                        if (queueItem->GetPlayerGroupInfoData(bot->GetObjectGuid(), &ginfo))
                        {
                            TeamId = ginfo.isRated ? 1 : 0;
                        }
                        if (bot->InArena())
                        {
                            if (bot->GetBattleGround()->IsRated()/* && (ginfo.isRated && ginfo.arenaTeamId && ginfo.arenaTeamRating && ginfo.opponentsTeamRating)*/)
                                TeamId = 1;
                            else
                                TeamId = 0;
                        }

                        

                        sRandomPlayerbotMgr.ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;

                    }
                );
            }
#endif
            BgBots[queueTypeId][bracketId][TeamId]++;
        }
    });

    for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
    {
        for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
        {
            BattleGroundQueueTypeId queueTypeId = BattleGroundQueueTypeId(j);

            if ((BgPlayers[j][i][0] + BgBots[j][i][0] + BgPlayers[j][i][1] + BgBots[j][i][1]) == 0)
                continue;

#ifndef MANGOSBOT_ZERO
            if (ArenaType type = sServerFacade.BgArenaType(queueTypeId))
            {
                sLog.outDetail("ARENA:%s %s: Plr (Skirmish:%d, Rated:%d) Bot (Skirmish:%d, Rated:%d) Total (Skirmish:%d Rated:%d)",
                    type == ARENA_TYPE_2v2 ? "2v2" : type == ARENA_TYPE_3v3 ? "3v3" : "5v5",
                    i == 0 ? "10-19" : i == 1 ? "20-29" : i == 2 ? "30-39" : i == 3 ? "40-49" : i == 4 ? "50-59" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 6) ? "60" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 7) ? "60-69" : i == 6 ? (i == 6 && MAX_BATTLEGROUND_BRACKETS == 16) ? "70-79" : "70" : "80",
                    BgPlayers[j][i][0],
                    BgPlayers[j][i][1],
                    BgBots[j][i][0],
                    BgBots[j][i][1],
                    BgPlayers[j][i][0] + BgBots[j][i][0],
                    BgPlayers[j][i][1] + BgBots[j][i][1]
                );
                continue;
            }
#endif
            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
            std::string _bgType;
            switch (bgTypeId)
            {
            case BATTLEGROUND_AV:
                _bgType = "AV";
                break;
            case BATTLEGROUND_WS:
                _bgType = "WSG";
                break;
            case BATTLEGROUND_AB:
                _bgType = "AB";
                break;
#ifndef MANGOSBOT_ZERO
            case BATTLEGROUND_EY:
                _bgType = "EotS";
                break;
#endif
#ifdef MANGOSBOT_TWO
            case BATTLEGROUND_RB:
                _bgType = "Random";
                break;
            case BATTLEGROUND_SA:
                _bgType = "SotA";
                break;
            case BATTLEGROUND_IC:
                _bgType = "IoC";
                break;
#endif
            default:
                _bgType = "Other";
                break;
            }
            sLog.outDetail("BG:%s %s: Plr (%d:%d) Bot (%d:%d) Total (A:%d H:%d)",
                _bgType.c_str(),
                i == 0 ? "10-19" : i == 1 ? "20-29" : i == 2 ? "30-39" : i == 3 ? "40-49" : i == 4 ? "50-59" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 6) ? "60" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 7) ? "60-69" : i == 6 ? (i == 6 && MAX_BATTLEGROUND_BRACKETS == 16) ? "70-79" : "70" : "80",
                BgPlayers[j][i][0],
                BgPlayers[j][i][1],
                BgBots[j][i][0],
                BgBots[j][i][1],
                BgPlayers[j][i][0] + BgBots[j][i][0],
                BgPlayers[j][i][1] + BgBots[j][i][1]
            );
        }
    }

    sLog.outDetail("BG Queue check finished");
    return;
}

void RandomPlayerbotMgr::CheckLfgQueue()
{
    if (!LfgCheckTimer || time(NULL) > (LfgCheckTimer + 30))
        LfgCheckTimer = time(NULL);

    if (sPlayerbotAIConfig.logRandomBotJoinLfg)
    {
        sLog.outDetail("Checking LFG Queue...");
    }

    // Clear LFG list
    LfgDungeons[HORDE].clear();
    LfgDungeons[ALLIANCE].clear();

    for (auto i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld())
            continue;

        bool isLFG = false;

#ifdef MANGOSBOT_ZERO
        WorldSafeLocsEntry const* ClosestGrave = player->GetMap()->GetGraveyardManager().GetClosestGraveYard(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), player->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = player->GetGroup();
        if (group)
        {
            if (sWorld.GetLFGQueue().IsGroupInQueue(group->GetId()))
            {
                isLFG = true;
                LFGGroupQueueInfo lfgInfo;
                sWorld.GetLFGQueue().GetGroupQueueInfo(&lfgInfo, group->GetId());
                uint32 lfgType = (zoneId << 16) | lfgInfo.areaId;
                LfgDungeons[player->GetTeam()].push_back(lfgType);
            }
        }
        else
        {
            if (sWorld.GetLFGQueue().IsPlayerInQueue(player->GetObjectGuid()))
            {
                isLFG = true;
                LFGPlayerQueueInfo lfgInfo;
                sWorld.GetLFGQueue().GetPlayerQueueInfo(&lfgInfo, player->GetObjectGuid());
                uint32 lfgType = (zoneId << 16) | lfgInfo.areaId;
                LfgDungeons[player->GetTeam()].push_back(lfgType);
            }
        }
#endif

#ifdef MANGOSBOT_ONE
        /* todo: Fix with new system
        WorldSafeLocsEntry const* ClosestGrave = player->GetMap()->GetGraveyardManager().GetClosestGraveYard(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), player->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = player->GetGroup();
        if (group && !group->IsFull())
        {
            if (group->IsLeader(player->GetObjectGuid()))
            {
                if (player->GetSession()->m_lfgInfo.queued && player->GetSession()->LookingForGroup_auto_add && player->m_lookingForGroup.more.isAuto())
                {
                    uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(player->m_lookingForGroup.more.entry));
                    LfgDungeons[player->GetTeam()].push_back(lfgType);
                    isLFG = true;
                }
            }
        }
        else if (!group)
        {
            for (int i = 0; i < MAX_LOOKING_FOR_GROUP_SLOT; ++i)
                if (!player->m_lookingForGroup.group[i].empty() && player->GetSession()->LookingForGroup_auto_join && player->m_lookingForGroup.group[i].isAuto())
                {
                    isLFG = true;
                    uint32 lfgType = (zoneId << 16) | ((0 << 8) | uint8(player->m_lookingForGroup.group[i].entry));
                    LfgDungeons[player->GetTeam()].push_back(lfgType);
                }

            if (!player->m_lookingForGroup.more.empty() && player->GetSession()->LookingForGroup_auto_add && player->m_lookingForGroup.more.isAuto())
            {
                uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(player->m_lookingForGroup.more.entry));
                LfgDungeons[player->GetTeam()].push_back(lfgType);
                isLFG = true;
            }
        }
        */
#endif

#ifdef MANGOSBOT_TWO
        Group* group = player->GetGroup();
        if (group)
        {
            if (group->IsLFGGroup())
            {
                isLFG = true;
                LFGQueueData& lfgData = sWorld.GetLFGQueue().GetQueueData(group->GetObjectGuid());
                if (lfgData.GetState() != LFG_STATE_NONE && lfgData.GetState() < LFG_STATE_DUNGEON)
                {
                    LfgDungeonSet dList = lfgData.GetDungeons();
                    for (auto dungeon : dList)
                    {
                        LfgDungeons[player->GetTeam()].push_back(dungeon);
                    }
                }
            }
        }
        else
        {
            if (player->GetLfgData().GetState() != LFG_STATE_NONE)
            {
                LFGQueueData& lfgData = sWorld.GetLFGQueue().GetQueueData(player->GetObjectGuid());
                isLFG = true;
                if (lfgData.GetState() < LFG_STATE_DUNGEON)
                {
                    LfgDungeonSet dList = lfgData.GetDungeons();
                    for (auto dungeon : dList)
                    {
                        LfgDungeons[player->GetTeam()].push_back(dungeon);
                    }
                }
            }
        }
#endif
    }

#ifdef MANGOSBOT_ONE
    /* todo: Fix with new system
    ForEachPlayerbot([&](Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return;

        if (LfgDungeons[bot->GetTeam()].empty())
            return;

        WorldSafeLocsEntry const* ClosestGrave = bot->GetMap()->GetGraveyardManager().GetClosestGraveYard(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(), bot->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = bot->GetGroup();
        if (group && !group->IsFull())
        {
            if (group->IsLeader(bot->GetObjectGuid()))
            {
                if (bot->GetSession()->m_lfgInfo.queued && bot->GetSession()->m_lfgInfo.autofill)
                {
                    uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(bot->m_lookingForGroup.more.entry));
                    LfgDungeons[bot->GetTeam()].push_back(lfgType);
                }
            }
        }
        else if (!group)
        {
            if (!bot->m_lookingForGroup.more.empty() && bot->GetSession()->LookingForGroup_auto_add && bot->m_lookingForGroup.more.isAuto())
            {
                uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(bot->m_lookingForGroup.more.entry));
                LfgDungeons[bot->GetTeam()].push_back(lfgType);
            }
        }
    });
    */
#endif

    if (sPlayerbotAIConfig.logRandomBotJoinLfg)
    {
       if (LfgDungeons[ALLIANCE].size() || LfgDungeons[HORDE].size())
            sLog.outDetail("LFG Queue check finished. There are real players in queue.");
       else
           sLog.outDetail("LFG Queue check finished. No real players in queue.");
    }
    return;
}

void RandomPlayerbotMgr::AddOfflineGroupBots()
{
    if (!OfflineGroupBotsTimer || time(NULL) > (OfflineGroupBotsTimer + 5))
        OfflineGroupBotsTimer = time(NULL);

    uint32 totalCounter = 0;
    for (const auto& i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld() || !player->GetGroup())
            continue;

        Group* group = player->GetGroup();
        if (group && group->IsLeader(player->GetObjectGuid()))
        {
            std::vector<uint32> botsToAdd;
            Group::MemberSlotList const& slots = group->GetMemberSlots();
            for (Group::MemberSlotList::const_iterator i = slots.begin(); i != slots.end(); ++i)
            {
                ObjectGuid member = i->guid;
                if (member == player->GetObjectGuid())
                    continue;

                if (!IsFreeBot(member.GetCounter()))
                    continue;

                if (sObjectMgr.GetPlayer(member))
                    continue;

                if (GetPlayerBot(member))
                    continue;

                botsToAdd.push_back(member.GetCounter());
            }

            if (botsToAdd.empty())
                return;

            uint32 maxToAdd = urand(1, 5);
            uint32 counter = 0;
            for (auto& guid : botsToAdd)
            {
                if (counter >= maxToAdd)
                    break;

                if (sPlayerbotAIConfig.IsFreeAltBot(guid))
                {
                    for (auto& bot : sPlayerbotAIConfig.freeAltBots)
                    {
                        if (bot.second == guid)
                        {
                            Player* player = GetPlayerBot(bot.second);
                            if (!player)
                            {
                                AddPlayerBot(bot.second, bot.first);
                            }
                        }
                    }
                }
                else
                    AddRandomBot(guid);

                counter++;
                totalCounter++;
            }
        }
    }

    if (totalCounter)
        sLog.outDetail("Added %u offline bots from groups", totalCounter);
}

Item* RandomPlayerbotMgr::CreateTempItem(uint32 item, uint32 count, Player const* player, uint32 randomPropertyId)
{
    if (count < 1)
        return nullptr;                                        // don't create item at zero count

    if (ItemPrototype const* pProto = sObjectMgr.GetItemPrototype(item))
    {
        if (count > pProto->GetMaxStackSize())
            count = pProto->GetMaxStackSize();

        MANGOS_ASSERT(count != 0 && "pProto->Stackable == 0 but checked at loading already");

        Item* pItem = NewItemOrBag(pProto);
        if (pItem->Create(0, item, player ? player->GetObjectGuid() : ObjectGuid()))
        {
            pItem->SetCount(count);
            if (int32 randId = randomPropertyId ? randomPropertyId : Item::GenerateItemRandomPropertyId(item))
                pItem->SetItemRandomProperties(randId);

            return pItem;
        }
        delete pItem;
    }
    return nullptr;
}

InventoryResult RandomPlayerbotMgr::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    dest = 0;
    Item* pItem = RandomPlayerbotMgr::CreateTempItem(item, 1, player);

    if (pItem)
    {
        InventoryResult result = player->CanEquipItem(slot, dest, pItem, true, false);

        pItem->RemoveFromUpdateQueueOf(player);

        if (!player->GetItemUpdateQueue().empty() && !player->GetItemUpdateQueue().back()) //Prevent queue overflow.
            player->GetItemUpdateQueue().pop_back();

        delete pItem;
        return result;
    }

    return EQUIP_ERR_ITEM_NOT_FOUND;
}

void RandomPlayerbotMgr::SaveCurTime()
{
    if (!EventTimeSyncTimer || time(NULL) > (EventTimeSyncTimer + 60))
        EventTimeSyncTimer = time(NULL);

    SetValue(uint32(0), "current_time", uint32(time(nullptr)));
}

void RandomPlayerbotMgr::SyncEventTimers()
{
    uint32 oldTime = GetValue(uint32(0), "current_time");
    if (oldTime)
    {
        uint32 curTime = time(nullptr);
        uint32 timeDiff = curTime - oldTime;
        CharacterDatabase.PExecute("UPDATE ai_playerbot_random_bots SET time = time + %u WHERE owner = 0 AND bot <> 0", timeDiff);
    }
}

void RandomPlayerbotMgr::CheckPlayers()
{
    if (!PlayersCheckTimer || time(NULL) > (PlayersCheckTimer + 60))
        PlayersCheckTimer = time(NULL);

    sLog.outDetail("Checking Players...");

    uint32 newPlayersLevel = 0;

    for (auto i : players)
    {
        Player* player = i.second;

        if (player->IsGameMaster())
            continue;

        //if (player->GetSession()->GetSecurity() > SEC_PLAYER)
        //    continue;

        if (player->GetLevel() > newPlayersLevel)
            newPlayersLevel = player->GetLevel();
    }

    if(playersLevel!= newPlayersLevel)
        sLog.outDetail("Max player level is %d, max bot level changed from %d to %d", newPlayersLevel, playersLevel, newPlayersLevel);
    else
        sLog.outDetail("Max player level is %d, max bot level set to %d", newPlayersLevel, newPlayersLevel);

    playersLevel = newPlayersLevel;

    return;
}

void RandomPlayerbotMgr::ScheduleRandomize(uint32 bot, uint32 time)
{
    SetEventValue(bot, "randomize", 1, time);
}

// Resolve the configured names once. Deferred rather than done in
// PlayerbotAIConfig::Initialize because that runs before the character database
// is usable, and a name is what a person can reasonably be asked to write in a
// config file.
void RandomPlayerbotMgr::ResolvePinnedBots()
{
    m_pinnedBotsResolved = true;

    for (const std::string& name : sPlayerbotAIConfig.pinnedBotNames)
    {
        std::string escaped = name;
        CharacterDatabase.escape_string(escaped);

        auto result = CharacterDatabase.PQuery("SELECT guid FROM characters WHERE name = '%s'", escaped.c_str());
        if (!result)
        {
            sLog.outError("PinnedBots: no character named '%s'", name.c_str());
            continue;
        }

        uint32 guid = result->Fetch()[0].GetUInt32();
        m_pinnedBots.insert(guid);
        sLog.outString("PinnedBots: '%s' (guid %u) will stay online and will not be relocated", name.c_str(), guid);
    }
}

bool RandomPlayerbotMgr::IsPinnedBot(uint32 guidLow)
{
    if (!m_pinnedBotsResolved)
        ResolvePinnedBots();

    return m_pinnedBots.find(guidLow) != m_pinnedBots.end();
}

// AddRandomBots only tops the population up to MaxRandomBots and stops there, so
// which characters get in is decided once and never revisited. A pinned bot that
// missed the cut would simply never appear, which is why this runs alongside it.
void RandomPlayerbotMgr::EnsurePinnedBotsOnline()
{
    if (!m_pinnedBotsResolved)
        ResolvePinnedBots();

    for (uint32 guid : m_pinnedBots)
    {
        if (!GetPlayerBot(guid))
            AddRandomBot(guid);
    }
}

void RandomPlayerbotMgr::ScheduleTeleport(uint32 bot, uint32 time)
{
    if (!time)
        time = 60 + urand(sPlayerbotAIConfig.randomBotTeleportMinInterval, sPlayerbotAIConfig.randomBotTeleportMaxInterval);
    SetEventValue(bot, "teleport", 1, time);
}

void RandomPlayerbotMgr::ScheduleChangeStrategy(uint32 bot, uint32 time)
{
    if (!time)
        time = urand(sPlayerbotAIConfig.minRandomBotChangeStrategyTime, sPlayerbotAIConfig.maxRandomBotChangeStrategyTime);
    SetEventValue(bot, "change_strategy", 1, time);
}

bool RandomPlayerbotMgr::AddRandomBot(uint32 bot)
{
    SC_LOG("AddRandomBot entry guid=%u", bot);
    Player* player = GetPlayerBot(bot);
    if (player)
    {
        SC_LOG("AddRandomBot guid=%u already online — returning true", bot);
        return true;
    }

    uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, bot));
    SC_LOG("AddRandomBot guid=%u accountId=%u — checking IsInRandomAccountList", bot, accountId);

    if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
    {
        SC_LOG("AddRandomBot guid=%u — NOT in random account list, FAIL", bot);
        sLog.outError("Bot #%d login fail: Not random bot!", bot);
        return false;
    }

    uint32 loginEv = GetEventValue(bot, "login");
    SC_LOG("AddRandomBot guid=%u — IsInRandomAccountList OK, login event=%u", bot, loginEv);

    // stale-event recovery: if login=1 is set but
    // we already proved (line ~2124, GetPlayerBot returned null) that no
    // actual session exists for this bot, the marker is stale — typically
    // because the previous mangosd crashed mid-tick before the bot's
    // logout cleanup could clear the event. Without this fix, the bot is
    // permanently un-summonable until someone manually `DELETE FROM
    // ai_playerbot_random_bots` and bounces mangosd to clear the in-memory
    // eventCache. We force-reset the event in both DB and cache, then
    // proceed with a fresh login.
    if (loginEv)
    {
        SC_LOG("AddRandomBot guid=%u — login=1 but no live session; treating as stale, force-resetting event", bot);
        SetEventValue(bot, "login", 0, 0);  // updates DB row + eventCache in one call
        loginEv = 0;
    }

    if (!loginEv)
    {
        SC_LOG("AddRandomBot guid=%u — calling AddPlayerBot", bot);
        AddPlayerBot(bot, 0);
        SC_LOG("AddRandomBot guid=%u — AddPlayerBot returned, setting event values", bot);
        SetEventValue(bot, "add", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime));
        SetEventValue(bot, "logout", 0, 0);
        SetEventValue(bot, "login", 1, -1);
        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime);
        SetEventValue(bot, "update", 1, randomTime);
        currentBots.push_back(bot);
        SC_LOG("AddRandomBot guid=%u — DONE, added to currentBots", bot);
        sLog.outDetail("Random bot added #%d", bot);
    }
    else
    {
        SC_LOG("AddRandomBot guid=%u — login event already set, SKIPPING actual login", bot);
    }

    return true;
}

void RandomPlayerbotMgr::MovePlayerBot(uint32 guid, PlayerbotHolder* newHolder)
{
    if (!sPlayerbotAIConfig.enabled)
        return;

    players[guid] = this->GetPlayerBot(guid);
    PlayerbotHolder::MovePlayerBot(guid, newHolder);
}

bool RandomPlayerbotMgr::ProcessBot(uint32 bot)
{
    Player* player = GetPlayerBot(bot);
    if (player && sPlayerbotAIConfig.IsFreeAltBot(player))
    {
        return false;
    }

    PlayerbotAI* ai = player ? GetBotAI(player) : NULL;

    bool botsAllowedInWorld = !sPlayerbotAIConfig.randomBotLoginWithPlayer || (!players.empty() && sWorld.GetActiveSessionCount() > 0);

    bool isValid = true;
   
    if (sPlayerbotAIConfig.randomBotTimedLogout && !GetEventValue(bot, "add") && !sPlayerbotAIConfig.asyncBotLogin) // RandomBotInWorldTime is expired.
        isValid = false;
    else if(!botsAllowedInWorld)                                               // Logout if all players logged out
        isValid = false;

    //Log out bot
    if (!isValid)
    {
        if (botsAllowedInWorld && player && player->GetGroup())
        {
            SetEventValue(bot, "add", 1, 120);                                 // Delay logout for 2 minutes while in group.
            return false;
        }

        if (!player || !player->IsInWorld())
            sLog.outDetail("Bot #%d: log out", bot);
        else
            sLog.outDetail("Bot #%d %s:%d <%s>: log out", bot, IsAlliance(player->getRace()) ? "A" : "H", player->GetLevel(), player->GetName());

        currentBots.remove(bot);
        SetEventValue(bot, "add", 0, 0);

        if (!player)
        {
            return false;
        }    

        LogoutPlayerBot(bot);

        if (sPlayerbotAIConfig.randomBotTimedOffline)
        {
            uint32 logout = GetEventValue(bot, "logout");

            if (!logout)
                SetEventValue(bot, "logout", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime));
        }

        return false;
    }

    //Log in bot (Added in AddRandomBots)
    if (!player)
    {
        if (!botsAllowedInWorld)
            return false;

        if (GetEventValue(bot, "login"))
            return true;

        AddPlayerBot(bot, 0);

        SetEventValue(bot, "login", 1, -1); // This will be reset to 0 on server startup. Check RandomPlayerbotMgr constructor

        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime);
        SetEventValue(bot, "update", 1, randomTime);

        return true;
    }

    if (!player->IsInWorld() || player->IsBeingTeleported() || player->GetSession()->isLogingOut()) //Skip bots that are in limbo.
        return false;

    if(GetEventValue(bot, "login"))
        SetEventValue(bot, "login", 0, 0); //Bot is no longer loggin in.

    uint32 update = GetEventValue(bot, "update");
    //Update the bot
    if (!update)
    {
        //Clean up expired values
        if (ai && !ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
            ai->GetAiObjectContext()->ClearExpiredValues();

        //Randomize/teleport bot
        // Note: ProcessBot() itself skips Randomize() (which reassigns level/spec) when
        // disableRandomLevels is set, but still runs ChangeStrategy()/RandomTeleportForLevel()
        // for idle bots - so this must not be skipped wholesale for disableRandomLevels=1.
        if (player->GetGroup() || player->IsTaxiFlying())
            return false;

        bool update = true;
        if (ai)
        {
            if (!sRandomPlayerbotMgr.IsRandomBot(player))
                update = false;

            if (player->GetGroup() && ai->GetGroupMaster() && (!GetBotAI(ai->GetGroupMaster()) || GetBotAI(ai->GetGroupMaster())->IsRealPlayer()))
                update = false;

            if (ai->HasPlayerNearby())
                update = false;
        }
        if (update)
            ProcessBot(player);

        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime * 5);
        SetEventValue(bot, "update", 1, randomTime);
        return true;
    }

    return false;
}

bool RandomPlayerbotMgr::ProcessBot(Player* player)
{
    if (!player || !player->IsInWorld() || player->IsBeingTeleported() || player->GetSession()->isLogingOut())
        return false;

    uint32 bot = player->GetGUIDLow();

    if (player->InBattleGround())
        return false;

    if (player->InBattleGroundQueue())
        return false;

    // only teleport idle bots
    bool idleBot = false;
    TravelTarget* target = GetBotAI(player)->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
    if (target)
    {
        if (target->GetTravelState() == TravelState::TRAVEL_STATE_IDLE)
            idleBot = true;
    }
    else
        idleBot = true;

    if (idleBot)
    {
        uint32 randomize = GetEventValue(bot, "randomize");
        // Randomize() reassigns level/spec - never call it when levels are pinned
        // (DisableRandomLevels=1). Falls through to the changeStrategy/teleport checks
        // below so idle bots still get RandomTeleportForLevel()'s zone relocation.
        if (!sPlayerbotAIConfig.disableRandomLevels && !randomize)
        {
            bool randomiser = true;
            if (player->GetGuildId())
            {
                Guild* guild = sGuildMgr.GetGuildById(player->GetGuildId());
                uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(guild->GetLeaderGuid());
                if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
                {
                    int32 rank = guild->GetRank(player->GetObjectGuid());
                    randomiser = rank < 4 ? false : true;
                }
            }

            if (randomiser)
            {
                Randomize(player);
                return true;
            }
        }

        // Both branches below teleport the bot - to an inn or to a grind spot -
        // which for a pinned bot means being pulled out of the quest chain it is
        // being watched through. Nothing else about it differs from any other
        // random bot.
        if (IsPinnedBot(bot))
            return false;

        uint32 changeStrategy = GetEventValue(bot, "change_strategy");
        if (!changeStrategy)
        {
            if (sPlayerbotAIConfig.enableRandomTeleports)
            {
                sLog.outDetail("Changing strategy for bot #%d %s:%d <%s>", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
                ChangeStrategy(player);
                ScheduleChangeStrategy(bot);
            }
            else
            {
                sLog.outDetail("Changing strategy for bot #%d %s:%d <%s> is supposed to happen, but enableRandomTeleports = false", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
            }
            return true;
        }

        uint32 teleport = GetEventValue(bot, "teleport");
        if (!teleport && players.size())
        {
            if (sPlayerbotAIConfig.enableRandomTeleports)
            {
                sLog.outDetail("Bot #%d %s:%d <%s>: sent to grind", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
                RandomTeleportForLevel(player, true);
                ScheduleTeleport(bot);
            }
            else
            {
                sLog.outDetail("Bot #%d %s:%d <%s>: supposed to be sent to grind, but enableRandomTeleports = false", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
            }
            return true;
        }
    }

    return false;
}

void RandomPlayerbotMgr::Revive(Player* player)
{
    uint32 bot = player->GetGUIDLow();

    //sLog.outString("Bot %d revived", bot);
    SetEventValue(bot, "dead", 0, 0);
    SetEventValue(bot, "revive", 0, 0);

    if (sServerFacade.GetDeathState(player) == CORPSE)
    {
        RandomTeleport(player);
    }
    else
    {
        RandomTeleportForLevel(player, false);
    }
}

// Zones RandomTeleport refuses outright, regardless of bot or level: Turtle's two custom
// player-only starting zones, which ship no MMAP data. F1 checks the same list so it never
// builds demand for a zone whose every candidate is guaranteed to be filtered out below.
static bool IsTeleportVetoedZone(uint32 zoneId)
{
    return zoneId == 5536 || zoneId == 5225;
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot, std::vector<WorldLocation> &locs, bool hearth, bool activeOnly)
{
    if (bot->IsBeingTeleported())
        return;

    if (bot->InBattleGround())
        return;

    if (bot->InBattleGroundQueue())
        return;

	if (bot->GetLevel() < 5)
		return;

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetObjectGuid()))
        return;

    // F2: never yank a bot that is marching in a travel party (the leader included - it IS
    // its group's leader, so the check above does not cover it).
    if (sPlayerbotAIConfig.travelParties && IsInTravelParty(bot))
        return;

    if (bot->IsTaxiFlying() && GetBotAI(bot)->HasPlayerNearby())
        return;

    if (locs.empty())
    {
        sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());
        return;
    }

    std::vector<WorldPosition> tlocs;

    for (auto& loc : locs)
    {
        tlocs.push_back(WorldPosition(loc));
    }

    //Do not teleport to maps disabled in config
    tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [](const WorldPosition& l) {std::vector<uint32>::iterator i = find(sPlayerbotAIConfig.randomBotMaps.begin(), sPlayerbotAIConfig.randomBotMaps.end(), l.getMapId()); return i == sPlayerbotAIConfig.randomBotMaps.end(); }), tlocs.end());

    // Do not drop bots into the enemy faction's home territory. The teleport
    // cache (ai_playerbot_tele_cache) only knows level, map and coordinates -
    // no faction - so an Alliance bot was as likely to land in Durotar as in
    // Elwynn. There is nothing for it to do there and the city guards kill it
    // on sight, over and over. Contested zones stay open.
    // Skipped if it would leave too little to choose from, so a level bracket
    // that only exists in enemy territory does not strand its bots.
    {
        std::vector<WorldPosition> friendly;
        friendly.reserve(tlocs.size());
        for (WorldPosition const& loc : tlocs)
            if (!loc.isEnemyHomeZoneFor(bot->GetTeam()))
                friendly.push_back(loc);

        if (friendly.size() >= tlocs.size() / 4)
            tlocs = friendly;
    }

    //Random shuffle based on distance. Closer distances are more likely (but not exclusively) to be at the begin of the list.
    tlocs = WorldPosition(bot).GetNextPoint(tlocs, 0);

    //5% + 0.1% per level chance node on different map in selection.
    //tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](WorldLocation const& l) {return l.position.mapid != bot->GetMapId() && urand(1, 100) > 0.5 * bot->GetLevel(); }), tlocs.end());

    //Continent is about 20.000 large
    //Bot will travel 0-5000 units + 75-150 units per level.
    //tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](WorldLocation const& l) {return l.position.mapid == bot->GetMapId() && sServerFacade.GetDistance2d(bot, l.coord_x, l.coord_y) > urand(0, 5000) + bot->GetLevel() * 15 * urand(5, 10); }), tlocs.end());

    // teleport to active areas only
    if (sPlayerbotAIConfig.randomBotTeleportNearPlayer && activeOnly)
    {
        tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [this](const WorldPosition& l)
        {
            uint32 mapId = l.getMapId();
            Map* tMap = sMapMgr.FindMap(mapId, 0);
            if (tMap && tMap->IsContinent() && tMap->HasActiveZones())
            {
                uint32 zoneId = sTerrainMgr.GetZoneId(mapId, l.coord_x, l.coord_y, l.coord_z);
                if (tMap->HasActiveZone(zoneId))
                {
                    if (sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmount > 0 && sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmountRadius > 0.0f)
                    {
                        uint32 botsNearTeleportPoint = 0;
                        ForEachPlayerbot([&](Player* otherBot)
                        {
                            // Only check the bots that are on the same zone
                            if (otherBot && !otherBot->IsBeingTeleported() && zoneId == otherBot->GetZoneId())
                            {
                                if (l.fDist(WorldPosition(otherBot)) <= sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmountRadius)
                                {
                                    botsNearTeleportPoint++;
                                }
                            }
                        });

                        return botsNearTeleportPoint >= sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmount;
                    }
                    else
                    {
                        return false;
                    }
                }
            }

            return true;
        }),
        tlocs.end());

        /*if (!tlocs.empty())
        {
            tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](const WorldPosition& l)
            {
                uint32 mapId = l.getMapId();
                Map* tMap = sMapMgr.FindMap(mapId, 0);
                if (!tMap || !tMap->IsContinent())
                        return true;

                if (!tMap->HasActiveAreas())
                    return true;

                AreaTableEntry const* area = l.getArea();
                if (area)
                {
                    if (!tMap->HasActiveZone(area->zone ? area->zone : area->ID))
                        return true;
                }
            }), tlocs.end());
        }*/
    }

    // filter starter zones
    tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](const WorldPosition& l)
    {
        uint32 mapId = l.getMapId();
        uint32 zoneId, areaId;
        sTerrainMgr.GetZoneAndAreaId(zoneId, areaId, mapId, l.coord_x, l.coord_y, l.coord_z);
        AreaTableEntry const* area = GetAreaEntryByAreaID(areaId);
        if (zoneId && zoneId != areaId)
        {
            AreaTableEntry const* zone = GetAreaEntryByAreaID(zoneId);
            if (!zone)
                return true;

            bool isEnemyZone = false;
            switch (zone->team)
            {
            case AREATEAM_ALLY:
                isEnemyZone = bot->GetTeam() != ALLIANCE;
                break;
            case AREATEAM_HORDE:
                isEnemyZone = bot->GetTeam() != HORDE;
                break;
            default:
                isEnemyZone = false;
                break;
            }
            if (isEnemyZone && (bot->GetLevel() < 21 || (zone->flags & AREA_FLAG_CAPITAL)))
                return true;

            // filter other races zones
            if (bot->GetLevel() < 30)
            {
                if ((zoneId == 12 || zoneId == 40) && bot->getRace() != RACE_HUMAN && bot->getRace() != RACE_HIGH_ELF)
                    return true;
                if ((zoneId == 1 || zoneId == 38) && bot->getRace() != RACE_DWARF && bot->getRace() != RACE_GNOME)
                    return true;
                if ((zoneId == 85 || zoneId == 130) && bot->getRace() != RACE_UNDEAD)
                    return true;
                if ((zoneId == 141 || zoneId == 148) && bot->getRace() != RACE_NIGHTELF)
                    return true;
                if ((zoneId == 14 || zoneId == 17) && !(bot->getRace() == RACE_ORC || bot->getRace() == RACE_TROLL || bot->getRace() == RACE_GOBLIN))
                    return true;
                if ((zoneId == 215) && bot->getRace() != RACE_TAUREN)
                    return true;
                // redridge / duskwood
                if ((zoneId == 44 || zoneId == 10) && bot->GetTeam() != ALLIANCE)
                    return true;
#ifndef MANGOSBOT_ZERO
                if ((zoneId == 3524 || zoneId == 3525) && bot->getRace() != RACE_DRAENEI)
                    return true;
                if ((zoneId == 3430 || zoneId == 3433) && bot->getRace() != RACE_BLOODELF)
                    return true;
#endif
            }
        }

        // Never send bots to custom player-only starting zones (no MMAP support)
        if (IsTeleportVetoedZone(zoneId))
            return true;

        if (!area)
            return true;

        bool isEnemyZone = false;
        switch (area->team)
        {
        case AREATEAM_ALLY:
            isEnemyZone = bot->GetTeam() != ALLIANCE;
            break;
        case AREATEAM_HORDE:
            isEnemyZone = bot->GetTeam() != HORDE;
            break;
        default:
            isEnemyZone = false;
            break;
        }
        return isEnemyZone && bot->GetLevel() < 21;

    }), tlocs.end());

    if (tlocs.empty())
    {
        if (activeOnly)
        {
            if (hearth)
                return RandomTeleportForRpg(bot, false);
            else
                return RandomTeleportForLevel(bot, false);
        }

        sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());

        return;
    }

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomTeleportByLocations");

    int index = 0;

    for (int i = 0; i < tlocs.size(); i++)
    {
        for (int attemtps = 0; attemtps < 3; ++attemtps)
        {
            WorldLocation loc = tlocs[i];

#ifdef MANGOSBOT_ONE
            // Teleport to Dark Portal area if event is in progress
            if (sWorldState.GetExpansion() == EXPANSION_NONE && bot->GetLevel() > 54 && urand(0, 100) > 20)
            {
                if (urand(0, 1))
                    loc = WorldLocation(uint32(0), -11772.43f, -3272.84f, -17.9f, 3.32447f);
                else
                    loc = WorldLocation(uint32(0), -11741.70f, -3130.3f, -11.7936f, 3.32447f);
            }
#endif

            float x = loc.coord_x + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.grindDistance) - sPlayerbotAIConfig.grindDistance / 2 : 0);
            float y = loc.coord_y + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.grindDistance) - sPlayerbotAIConfig.grindDistance / 2 : 0);
            float z = loc.coord_z;

            Map* map = sMapMgr.FindMap(loc.mapid, 0);
            if (!map)
                continue;

            uint32 areaId = sTerrainMgr.GetAreaId(loc.mapid, x, y, z);
            AreaTableEntry const* area = GetAreaEntryByAreaID(areaId);
            if (!area)
                continue;

#ifndef MANGOSBOT_ZERO
            // Do not teleport to outland before portal opening (allow new races zones)
            if (sWorldState.GetExpansion() == EXPANSION_NONE && (loc.mapid == 571 || (loc.mapid == 530 && area->team != 2 && area->team != 4)))
                continue;
#endif

#ifdef MANGOSBOT_TWO
            float ground = map->GetHeight(bot->GetPhaseMask(), x, y, z + 0.5f);
#else
            float ground = map->GetHeight(x, y, z + 0.5f);
#endif
            if (ground <= INVALID_HEIGHT)
                continue;

            z = 0.05f + ground;
            // area_name is a char* (aliased via a union with Name in Map.h), not a
            // locale array - area_name[0] was dereferencing it to a single char and
            // handing that raw value to %s as if it were a pointer, segfaulting the
            // instant the format machinery tried to read a string from it. It can
            // also be null (e.g. custom zones with incomplete DBC data), same
            // nullability WorldPosition::getAreaName() already guards against.
            sLog.outDetail("Random teleporting bot %s to %s %f,%f,%f (%u/%zu locations)",
                bot->GetName(), area->area_name ? area->area_name : "", x, y, z, attemtps, tlocs.size());

            if (bot->IsTaxiFlying())
                bot->GetMotionMaster()->MovementExpired();

            // A teleport into enemy territory is survivable - the bot leaves
            // again. Binding its home there is not: it hearths back for the
            // rest of its life and the city guards kill it every time. The
            // filter above may hand out an enemy location when too little else
            // is left, so refuse the bind separately from the teleport.
            //
            // Found 2026-08-10: 108 High Elves were bound to Undercity and
            // Orgrimmar this way and averaged 1179 deaths each, against 494
            // for correctly bound bots.
            bool const hostileHome = WorldPosition(loc).isEnemyHomeZoneFor(bot->GetTeam());

            if (hearth && !hostileHome)
                bot->SetHomebindToLocation(loc, area->ID);

            bot->GetMotionMaster()->Clear();
            bot->TeleportTo(loc.mapid, x, y, z, 0);
            bot->SendHeartBeat();
            GetBotAI(bot)->Reset(true);

            if (bot->GetGroup())
            {
                for (GroupReference* gref = bot->GetGroup()->GetFirstMember(); gref; gref = gref->next())
                {
                    Player* member = gref->getSource();
                    PlayerbotAI* ai = GetBotAI(bot);
                    if (ai && bot != member)
                    {
                        if (member->IsTaxiFlying())
                            member->GetMotionMaster()->MovementExpired();
                        if (hearth && !hostileHome)
                            member->SetHomebindToLocation(loc, area->ID);

                        member->GetMotionMaster()->Clear();
                        member->TeleportTo(loc.mapid, x, y, z, 0);
                        member->SendHeartBeat();
                        GetBotAI(member)->Reset(true);
                    }

                }
            }
            return;
        }
    }

    sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());
}

std::vector<std::pair<uint32, uint32>> RandomPlayerbotMgr::RpgLocationsNear(WorldLocation pos, const std::map<uint32, std::map<uint32, std::vector<std::string>>>& areaNames, uint32 radius)
{
    std::vector<std::pair<uint32, uint32>> results;
    float minDist = FLT_MAX;
    WorldPosition areaPos(pos);
    std::string hasZone = "-", wantZone = areaPos.getAreaName(true, true);

    for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
    {
        for (uint32 r = 1; r < MAX_RACES; r++)
        {
            uint32 i = 0;
            for (auto p : rpgLocsCacheLevel[r][level])
            {
                std::string currentZone = areaNames.at(level).at(r)[i];
                i++;

                if (currentZone != wantZone && hasZone == wantZone) //If we already have the right id but this location isn't in the right id. Skip it.
                    continue;

                if (currentZone == wantZone && hasZone != wantZone) //If this is the first spot with a good area id use this now.
                    minDist = FLT_MAX;

                float dist = WorldPosition(pos).fDist(p);

                if (dist > radius || dist > minDist)
                    continue;

                if (dist < minDist)
                    results.clear();

                results.push_back(std::make_pair(r, level));

                hasZone = currentZone;

                minDist = dist;
            }
        }
    }

    return results;
}

void RandomPlayerbotMgr::PrepareTeleportCache()
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    auto results = CharacterDatabase.PQuery("SELECT `map_id`, `x`, `y`, `z`, `level` FROM `ai_playerbot_tele_cache`");
    if (results)
    {
        sLog.outString("Loading random teleport caches for %d levels...", maxLevel);
        do
        {
            Field* fields = results->Fetch();
            uint16 mapId = fields[0].GetUInt16();
            float x = fields[1].GetFloat();
            float y = fields[2].GetFloat();
            float z = fields[3].GetFloat();
            uint16 level = fields[4].GetUInt16();
            WorldLocation loc(mapId, x, y, z, 0);
            locsPerLevelCache[level].push_back(loc);
        } while (results->NextRow());
    }
    else
    {
        sLog.outString("Preparing random teleport caches for %d levels...", maxLevel);
        BarGoLink bar(maxLevel);
        for (uint8 level = 1; level <= maxLevel; level++)
        {
            auto results = WorldDatabase.PQuery("SELECT `map`, `position_x`, `position_y`, `position_z` "
                "FROM (SELECT `map`, `position_x`, `position_y`, `position_z`, t.level_max, t.level_min, "
                "%u - (t.level_max + t.level_min) / 2 delta "
                "FROM creature c INNER JOIN creature_template t ON c.id = t.entry WHERE t.type != 8 AND t.npc_flags = 0 AND t.rank = 0 AND NOT (t.flags_extra & 1024 OR t.flags_extra & 65536 OR t.flags_extra & 64 OR t.unit_flags & 256 OR t.unit_flags & 512) AND t.loot_id != 0) q "
                "WHERE delta >= 0 AND delta <= %u AND map in (%s)",
                level,
                sPlayerbotAIConfig.randomBotTeleLevel,
                sPlayerbotAIConfig.randomBotMapsAsString.c_str()
            );
            if (results)
            {
                CharacterDatabase.BeginTransaction();
                do
                {
                    Field* fields = results->Fetch();
                    uint16 mapId = fields[0].GetUInt16();
                    float x = fields[1].GetFloat();
                    float y = fields[2].GetFloat();
                    float z = fields[3].GetFloat();
                    WorldLocation loc(mapId, x, y, z, 0);
                    locsPerLevelCache[level].push_back(loc);

                    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_tele_cache` (`level`, `map_id`, `x`, `y`, `z`) VALUES (%u, %u, %f, %f, %f)",
                        level, mapId, x, y, z);
                } while (results->NextRow());
                CharacterDatabase.CommitTransaction();
            }
            bar.step();
        }
    }

    sLog.outString("Preparing RPG teleport caches for %d factions...", sFactionTemplateStore.GetNumRows());

    results = WorldDatabase.PQuery("SELECT map, position_x, position_y, position_z, "
        "r.race, r.minl, r.maxl "
        "FROM creature c INNER JOIN ai_playerbot_rpg_races r ON c.id = r.entry "
        "WHERE r.race < 15");

    if (results)
    {
        do
        {
            for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
            {
                Field* fields = results->Fetch();
                uint16 mapId = fields[0].GetUInt16();
                float x = fields[1].GetFloat();
                float y = fields[2].GetFloat();
                float z = fields[3].GetFloat();
                //uint32 faction = fields[4].GetUInt32();
                //string name = fields[5].GetCppString();
                uint32 race = fields[4].GetUInt32();
                uint32 minl = fields[5].GetUInt32();
                uint32 maxl = fields[6].GetUInt32();

                if (level > maxl || level < minl) continue;

                WorldLocation loc(mapId, x, y, z, 0);
                for (uint32 r = 1; r < MAX_RACES; r++)
                {
                    if (race == r || race == 0) rpgLocsCacheLevel[r][level].push_back(loc);
                }
            }
            //bar.step();
        } while (results->NextRow());
    }

    sLog.outString("Enhancing RPG teleport cache");

    std::map<uint32, std::map<uint32, std::vector<std::string>>> areaNames;

    for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
    {
        for (uint32 r = 1; r < MAX_RACES; r++)
        {
            for (auto p : rpgLocsCacheLevel[r][level])
            {
                areaNames[level][r].push_back(WorldPosition(p).getAreaName(true, true));
            }
        }
    }

    std::vector<std::pair<std::pair<uint32, uint32>, WorldPosition>> newPoints;
    std::vector<std::pair<std::pair<uint32, uint32>, GuidPosition>> innPoints;

    //Static portals.
    for (auto& goData : WorldPosition().getGameObjectsNear(0, 0))
    {
        GuidPosition go(goData);

        auto data = sGOStorage.LookupEntry<GameObjectInfo>(go.GetEntry());

        if (!data)
            continue;

        if (data->type != GAMEOBJECT_TYPE_SPELLCASTER)
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(data->spellcaster.spellId);

        if (pSpellInfo->EffectTriggerSpell[0])
            pSpellInfo = sServerFacade.LookupSpellInfo(pSpellInfo->EffectTriggerSpell[0]);

        if (pSpellInfo->Effect[0] != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->Effect[1] != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->Effect[2] != SPELL_EFFECT_TELEPORT_UNITS)
            continue;

        SpellTargetPosition const* pos = sSpellMgr.GetSpellTargetPosition(pSpellInfo->Id);

        if (!pos)
            continue;

        std::vector<std::pair<uint32, uint32>> ranges = RpgLocationsNear(WorldPosition(pos), areaNames);

        for (auto& range : ranges)
            newPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), pos));
    }

    //Creatures.
    for (auto& creatureData : WorldPosition().getCreaturesNear(0, 0))
    {
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(creatureData->second.creature_id[0]);

        if (!cInfo)
            continue;

        if (cInfo->ExtraFlags & CREATURE_EXTRA_FLAG_INVISIBLE)
            continue;

        std::vector<uint32> allowedNpcFlags;

        allowedNpcFlags.push_back(UNIT_NPC_FLAG_BATTLEMASTER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_BANKER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_AUCTIONEER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_TRAINER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_VENDOR);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_REPAIR);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_INNKEEPER);

        for (auto flag : allowedNpcFlags)
        {          
            if ((cInfo->NpcFlags & flag) != 0)
            {
                std::vector<std::pair<uint32, uint32>> ranges = RpgLocationsNear(WorldPosition(creatureData), areaNames);

                if (cInfo->NpcFlags & UNIT_NPC_FLAG_INNKEEPER)
                {
                    for (auto& range : ranges)
                        innPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), creatureData));
                }
                else
                {
                    for (auto& range : ranges)
                        newPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), creatureData));
                }
                break;
            }
        }
    }

    for (auto newPoint : newPoints)
        rpgLocsCacheLevel[newPoint.first.first][newPoint.first.second].push_back(newPoint.second);
    
    for (auto innPoint : innPoints)
        innCacheLevel[innPoint.first.first][innPoint.first.second].push_back(std::make_pair(innPoint.second, innPoint.second));
}

void RandomPlayerbotMgr::PrintTeleportCache()
{
    sPlayerbotAIConfig.openLog("telecache.csv", "w");

    for (auto& l : sRandomPlayerbotMgr.locsPerLevelCache)
    {
        uint32 level = l.first;
        for (auto& p : l.second)
        {
            std::ostringstream out;
            out << level << ",";
            WorldPosition(p).printWKT(out);
            out << "LEVEL" << ",0," << WorldPosition(p).getAreaName(true, true);
            sPlayerbotAIConfig.log("telecache.csv", out.str().c_str());
        }
    }

    for (auto r : sRandomPlayerbotMgr.rpgLocsCacheLevel)
    {
        uint32 race =  r.first;
        for (auto& l : r.second)
        {
            uint32 level = l.first;
            for (auto& p : l.second)
            {
                std::ostringstream out;
                out << level << ",";
                WorldPosition(p).printWKT(out);
                out << "RPG" << "," << race << "," << WorldPosition(p).getAreaName(true, true);
                sPlayerbotAIConfig.log("telecache.csv", out.str().c_str());
            }
        }
    }
}

// ============================================================================
// Populate-Around-Players (F1)
// ============================================================================

// Telemetry to a plain file in the server's working directory, same instrument as F2Log.
// The demand map is invisible from outside the process - without this, a botless or
// player-present run looks identical from the DB, and "F1 did nothing" is indistinguishable
// from "F1 never ran".
// ponytail: debug instrument for the Phase 6 density validation; drop it once F1 is proven.
static void F1Log(std::string const& line)
{
    std::ofstream f("F1_populate.log", std::ios::app);
    if (f)
        f << "[" << (uint32)time(nullptr) << "] " << line << "\n";
}

void RandomPlayerbotMgr::RefreshPlayerZones()
{
    _playerZones.clear();

    // 1. Seed zones from online real players, straight off world sessions, filtered by
    // GetBotAI() == nullptr. (This core's `players` member is a MIXED map - real players plus
    // non-random bots, see OnPlayerLogin - so it can't be used as-is either way. The random
    // bots live in `GetAllBots()`, used further down.)
    for (auto const& itr : sWorld.GetAllSessions())
    {
        WorldSession* session = itr.second;
        if (!session)
            continue;

        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld() || GetBotAI(player))
            continue;

        uint32 zoneId = player->GetZoneId();

        // RandomTeleport vetoes these outright, so every biased candidate would be filtered
        // out downstream and the bot would not move at all - worse than stock behaviour.
        // Skipping the zone here means the roll falls through to a normal teleport instead.
        if (IsTeleportVetoedZone(zoneId))
        {
            F1Log("REFRESH: zone " + std::to_string(zoneId) +
                  " is teleport-vetoed (no MMAP data), skipping demand for it");
            continue;
        }

        ZoneDemand& d = _playerZones[zoneId];
        d.zoneId = zoneId;

        uint32 level = player->GetLevel();
        if (player->GetTeam() == ALLIANCE)
            d.playersAlliance++;
        else
            d.playersHorde++;

        d.minPlayerLevel = d.minPlayerLevel ? std::min(d.minPlayerLevel, level) : level;
        d.maxPlayerLevel = std::max(d.maxPlayerLevel, level);
    }

    if (_playerZones.empty())
    {
        F1Log("REFRESH: no real players in world (every F1 path falls back to stock)");
        return;
    }

    // 2. Density target + faction share per zone (territory-driven).
    for (auto& itr : _playerZones)
    {
        ZoneDemand& d = itr.second;

        auto ov = sPlayerbotAIConfig.populateDensityOverrides.find(d.zoneId);
        d.target = (ov != sPlayerbotAIConfig.populateDensityOverrides.end())
                       ? ov->second
                       : sPlayerbotAIConfig.populateDensityDefault;

        AreaTableEntry const* zone = GetAreaEntryByAreaID(d.zoneId);
        uint32 team = zone ? zone->team : 0;  // 2 = Alliance, 4 = Horde, else contested
        bool sanctuary = std::find(sPlayerbotAIConfig.populateSanctuaryZones.begin(),
                                   sPlayerbotAIConfig.populateSanctuaryZones.end(), d.zoneId) !=
                         sPlayerbotAIConfig.populateSanctuaryZones.end();
        bool starter = std::find(sPlayerbotAIConfig.populateStarterZones.begin(),
                                 sPlayerbotAIConfig.populateStarterZones.end(), d.zoneId) !=
                       sPlayerbotAIConfig.populateStarterZones.end();

        bool aPlayers = d.playersAlliance > 0;
        bool hPlayers = d.playersHorde > 0;
        float aShare;

        if (sanctuary || starter)
        {
            // Friendly-only by owning faction; never pull the enemy here.
            if (team == 2)
                aShare = 1.0f;
            else if (team == 4)
                aShare = 0.0f;
            else
                aShare = (aPlayers && !hPlayers) ? 1.0f : ((hPlayers && !aPlayers) ? 0.0f : 0.5f);
        }
        else
        {
            if (aPlayers && !hPlayers)
                aShare = sPlayerbotAIConfig.populateSoloFactionRatio;
            else if (hPlayers && !aPlayers)
                aShare = 1.0f - sPlayerbotAIConfig.populateSoloFactionRatio;
            else if (aPlayers && hPlayers)
                aShare = (float)d.playersAlliance / (float)(d.playersAlliance + d.playersHorde);
            else
                aShare = 0.5f;

            bool enemyPresent = aPlayers && hPlayers;
            if (!enemyPresent)
            {
                // Own-territory bias: mostly the owning faction + a small invader minority.
                if (team == 2)
                    aShare = std::max(aShare, 0.85f);
                else if (team == 4)
                    aShare = std::min(aShare, 0.15f);
            }
            else
            {
                // Opposing players present -> clamp toward the floor (emergent battles).
                float f = sPlayerbotAIConfig.populateMinorityFloor;
                aShare = std::max(f, std::min(1.0f - f, aShare));
            }
        }

        d.allianceShare = aShare;
        d.allowAlliance = aShare > 0.001f;
        d.allowHorde = aShare < 0.999f;
    }

    // 3. Count current bots per zone / faction / band.
    // `GetAllBots()` (PlayerbotHolder::playerBots), NOT `GetPlayers()`: this core's `players`
    // member only collects bots that are NOT random bots (see OnPlayerLogin, "Including non-random
    // bot player ... into random bot update"), so counting through it silently found zero bots in
    // every zone. Same spelling as AzerothCore's, contrary to the Phase 0 mapping note.
    for (auto const& itr : GetAllBots())
    {
        Player* bot = itr.second;
        if (!bot || !bot->IsInWorld())
            continue;

        auto zi = _playerZones.find(bot->GetZoneId());
        if (zi == _playerZones.end())
            continue;

        ZoneDemand& d = zi->second;
        if (bot->GetTeam() == ALLIANCE)
            d.curAlliance++;
        else
            d.curHorde++;

        if (IsPopulatePeer(d, bot->GetLevel()))
            d.curPeer++;
        else
            d.curTexture++;
    }

    for (auto const& itr : _playerZones)
    {
        ZoneDemand const& d = itr.second;
        std::ostringstream o;
        o << "REFRESH zone " << d.zoneId << ": players A" << d.playersAlliance << "/H"
          << d.playersHorde << " lvl " << d.minPlayerLevel << "-" << d.maxPlayerLevel
          << "; target " << d.target << " aShare " << d.allianceShare << " (allowA "
          << d.allowAlliance << " allowH " << d.allowHorde << "); bots A" << d.curAlliance
          << "/H" << d.curHorde << " peer " << d.curPeer << " tex " << d.curTexture;
        F1Log(o.str());
    }
}

bool RandomPlayerbotMgr::IsPopulatePeer(ZoneDemand const& d, uint32 level) const
{
    uint32 band = sPlayerbotAIConfig.populatePeerBand;
    uint32 lo = d.minPlayerLevel > band ? d.minPlayerLevel - band : 1;
    uint32 hi = d.maxPlayerLevel + band;
    return level >= lo && level <= hi;
}

std::vector<WorldLocation> RandomPlayerbotMgr::GetPlayerZoneTeleportLocations(Player* bot)
{
    std::vector<WorldLocation> out;
    if (!bot || _playerZones.empty())
        return out;

    // Filter the same level-bucketed location cache normal random teleport draws from,
    // rather than AzerothCore's sTravelMgr.GetTeleportLocations() (no equivalent here -
    // this core's TravelMgr is destination/purpose based, not a flat teleport-point list).
    std::vector<WorldLocation>& locs = locsPerLevelCache[bot->GetLevel()];
    if (locs.empty())
    {
        std::ostringstream o;
        o << "CANDIDATES " << bot->GetName() << " lvl " << (uint32)bot->GetLevel()
          << ": 0 - the level-" << (uint32)bot->GetLevel() << " location cache is empty";
        F1Log(o.str());
        return out;
    }

    // Reject reasons, so an empty result says WHY instead of just "0".
    uint32 rejZone = 0, rejFactionGate = 0, rejFactionFull = 0, rejBandFull = 0;

    Team team = bot->GetTeam();
    uint32 level = bot->GetLevel();

    // The peer band is only fillable where THIS bot's level bucket happens to hold points in the
    // demanded zone, and usually it does not: a level 1-8 bot has no cached point anywhere in
    // Stranglethorn, so a level 1 player parked there reads `peer 0` on every refresh and gets
    // only the 30% texture share, which is by construction the wrong level to play with. Measured
    // live 2026-08-22 in Stormwind and Stranglethorn: peer 0 in every single refresh of the run.
    //
    // PopulatePeerAnyLevelPoints lets a peer that found nothing in its own bucket draw from every
    // other bucket instead, still filtered to the demanded zone and still through every gate
    // below. The points are real cached teleport locations, so they are ground-snapped and
    // mesh-valid - the only thing being relaxed is which level bucket they were filed under.
    // Default off: it deliberately sends under-levelled bots into zones above their level.
    auto collect = [&](std::vector<WorldLocation>& src, uint32 cap) -> void
    {
    for (WorldLocation& loc : src)
    {
        if (cap && out.size() >= cap)
            return;
        uint32 zoneId = sTerrainMgr.GetZoneId(loc.mapId, loc.x, loc.y, loc.z);
        auto zi = _playerZones.find(zoneId);
        if (zi == _playerZones.end())
        {
            rejZone++;
            continue;
        }

        ZoneDemand const& d = zi->second;

        // Faction gate.
        if (team == ALLIANCE && !d.allowAlliance)
        {
            rejFactionGate++;
            continue;
        }
        if (team == HORDE && !d.allowHorde)
        {
            rejFactionGate++;
            continue;
        }

        float share = (team == ALLIANCE) ? d.allianceShare : (1.0f - d.allianceShare);
        uint32 factionTarget = (uint32)(d.target * share + 0.5f);
        uint32 factionCur = (team == ALLIANCE) ? d.curAlliance : d.curHorde;
        if (factionCur >= factionTarget)
        {
            rejFactionFull++;
            continue;
        }

        // Band gate (peer vs zone-texture).
        uint32 peerTarget = (uint32)(d.target * sPlayerbotAIConfig.populatePeerFraction + 0.5f);
        uint32 texTarget = d.target > peerTarget ? d.target - peerTarget : 0;
        bool peer = IsPopulatePeer(d, level);
        if (peer && d.curPeer >= peerTarget)
        {
            rejBandFull++;
            continue;
        }
        if (!peer && d.curTexture >= texTarget)
        {
            rejBandFull++;
            continue;
        }

        out.push_back(loc);
    }
    };

    collect(locs, 0);

    size_t const ownBucket = out.size();
    bool widened = false;

    if (out.empty() && sPlayerbotAIConfig.populatePeerAnyLevelPoints && rejZone && !rejFactionGate)
    {
        // Only widen for a bot that is a peer of some zone actually demanding bots, and only when
        // "wrong zone" is what emptied the list - a faction or density rejection is the system
        // working, and widening would just find more points it has to reject.
        bool anyPeerDemand = false;
        for (auto const& itr : _playerZones)
            if (IsPopulatePeer(itr.second, level))
                anyPeerDemand = true;

        if (anyPeerDemand)
        {
            widened = true;
            // Capped: the full cache is ~60 buckets of thousands of points each and this runs on
            // the bot update thread. 200 candidates is far more than a single random pick needs.
            for (auto& kv : locsPerLevelCache)
            {
                if (kv.first == bot->GetLevel())
                    continue;
                collect(kv.second, 200);
                if (out.size() >= 200)
                    break;
            }
        }
    }

    std::ostringstream o;
    o << "CANDIDATES " << bot->GetName() << " lvl " << level << " "
      << (team == ALLIANCE ? "A" : "H") << ": " << out.size() << " of " << locs.size()
      << " (rejected: wrong zone " << rejZone << ", faction gate " << rejFactionGate
      << ", faction full " << rejFactionFull << ", band full " << rejBandFull << ")";
    if (widened)
        o << " [peer widened: " << ownBucket << " in own bucket -> " << out.size()
          << " across all buckets]";
    F1Log(o.str());

    return out;
}

// ============================================================================
// Overland travel parties (F2)
// ============================================================================

// Telemetry to a plain file in the server's working directory, where the other logs land.
// ponytail: debug instrument for the walk validation; drop it once the route is proven.
// Raw MotionMaster::MovePoint does not survive on a bot: the leader was measured sitting on
// IDLE_MOTION_TYPE on the very next tick with its target still 847 yd away, so the march only
// advanced in the gap between our order and whatever cleared it (REPATH instrument, 2026-08-23).
// Every other bot in the module moves through MovementAction::MoveTo, which does the pathing and
// the AI's own movement bookkeeping - it is protected, so this exposes it.
namespace
{
    class F2Mover : public ai::MovementAction
    {
    public:
        explicit F2Mover(PlayerbotAI* ai) : MovementAction(ai, "f2 travel") {}
        using ai::MovementAction::MoveTo;
    };
}

// MoveTo returning true does NOT mean the bot moved: when TravelPath::makeShortCut clears the
// resolved path (its closest point farther than reactDistance), MoveTo takes the
// "Path collapsed - will rebuild next tick" branch and returns true having issued nothing
// (MovementActions.cpp:1150). Trusting the return value froze a leader at (907,-3907) in Durotar
// for five minutes, re-pathing to the same target every 2 s while raw MovePoint had walked that
// same spot fine (phase6d). So verify movement actually started, and keep raw MovePoint as the
// fallback for when it did not.
// Nor does IsMoving() right after the call mean it is going anywhere: TravelPath::ClipPath
// truncates the path at any hostile in aggro range and at remembered hazards, and cutTo() can
// leave a path barely longer than the bot itself. Measured at (1140,-4177) in Durotar: mv=T on
// every order, the leader moving each time, covering about one yard per order for two minutes
// until the stall detector disbanded the party.
//
// So the only honest test of a mover is ground covered, which the caller tracks across orders.
// Log codes: T = MoveTo took it, S = MoveTo claimed success but issued no movement, F = MoveTo
// declined, P = MoveTo demoted for not delivering, raw MovePoint driving.
// The "raw MovePoint" fallback below was never raw. `FORCED_MOVEMENT_RUN` is a cmangos symbol
// that this fork's compat shim defines as `1` (cmangos-compat-shim.h:647, comment: "bot only
// checks symbolic value"). That is true everywhere else, but here the value is handed straight to
// the *core's* MotionMaster::MovePoint(id, x, y, z, uint32 options, ...), whose `options` is a
// MoveOptions bitmask - and `1` is MOVE_PATHFINDING (MotionMaster.h:72). So every "raw" order
// silently ran a SECOND, independent navmesh query (MoveSplineInit.h:124) on top of the one F2
// had just done to choose the waypoint, and threw F2's path away. It also never set
// MOVE_RUN_MODE, so walk/run was left to the spline default.
//
// TravelPartyMoveMode picks the mover so the three can be compared without a rebuild:
//   0 = the old accidental MOVE_PATHFINDING, kept only as a control for A/B runs
//   1 = MovePoint with the flags actually intended: MOVE_PATHFINDING | MOVE_RUN_MODE  (DEFAULT)
//   2 = MovePath along the path F2 already computed - one query, nothing to disagree with
//
// ⚠️ This whole path is the FALLBACK. A healthy march never reaches it: the 2026-08-23 phase8
// baseline logged 'T' on 85 of 85 orders, so `mv=S`/`mv=P`/`mv=F` are the only ways in. The flag
// bug was therefore dormant, not the cause of the 2026-08-23 wedges - those were fixed by the
// 200 yd hop cap in c0b9025. It is fixed here so that the fallback is correct the day it does
// fire, and so the "demote to raw MovePoint" comment stops being a lie: mode 0 re-ran the very
// same navmesh query the module layer had just run, which is why demotion never rescued anything.
static char IssueMove(Player* leader, PlayerbotAI* lAI, uint32 mapId, float x, float y, float z,
                      bool forceRaw, Movement::PointsArray const& f2Path)
{
    uint32 const mode = sPlayerbotAIConfig.travelPartyMoveMode;

    auto issueRaw = [&]()
    {
        // Mode 2 needs at least a start and an end, and MoveSplineInit rewrites vertex 0 to the
        // live position, so a 2-point path is the minimum useful one.
        if (mode == 2 && f2Path.size() > 1)
        {
            leader->GetMotionMaster()->MovePath(f2Path, 0, false, false);
            return;
        }
        uint32 const opts = (mode >= 1) ? (MOVE_PATHFINDING | MOVE_RUN_MODE) : FORCED_MOVEMENT_RUN;
        leader->GetMotionMaster()->MovePoint(990001, x, y, z, opts);
    };

    // ignoreEnemyTargets: ClipPath otherwise truncates the path just short of any hostile in
    // aggro range, so the leader stops a few yards away, never pulls, and re-paths there forever
    // - the (826,-3869) deadlock. Walking in and taking the fight is the intended behaviour now
    // that the march yields to combat.
    if (!forceRaw && F2Mover(lAI).MoveTo(mapId, x, y, z, false, false, false, true))
    {
        if (leader->IsMoving())
            return 'T';
        issueRaw();
        return 'S';
    }

    issueRaw();
    return forceRaw ? 'P' : 'F';
}

// What actually reached the spline, read back from the unit right after the order was issued.
// Deliberately multi-valued and printed unconditionally: "the order produced no spline" and
// "this instrument never ran" must not look alike. spl=1 means movespline is already finalized,
// i.e. nothing is moving; splD is how far the issued move will actually travel.
static std::string F2SplineState(Player* leader)
{
    std::ostringstream o;
    bool const fin = leader->movespline->Finalized();
    o << " spl=" << (fin ? 1 : 0);
    if (fin)
        o << " splD=-1";
    else
    {
        Vector3 const d = leader->movespline->FinalDestination();
        o << " splD=" << (int)leader->GetDistance3dToCenter(d.x, d.y, d.z);
    }
    o << " ust=" << std::hex << (uint32)(leader->GetUnitState() &
        (UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_CONFUSED | UNIT_STAT_FLEEING |
         UNIT_STAT_FEIGN_DEATH | UNIT_STAT_DISTRACTED | UNIT_STAT_TAXI_FLIGHT |
         UNIT_STAT_IGNORE_PATHFINDING)) << std::dec;
    return o.str();
}

static void F2Log(std::string const& line)
{
    std::ofstream f("F2_travel.log", std::ios::app);
    if (f)
        f << "[" << (uint32)time(nullptr) << "] " << line << "\n";
}

// "map,x,y,z" -> WorldLocation. False (loc untouched) if the config value is malformed.
static bool ParseTravelPoint(std::string const& value, WorldLocation& loc)
{
    uint32 mapId = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    if (sscanf(value.c_str(), "%u,%f,%f,%f", &mapId, &x, &y, &z) != 4)
        return false;

    loc = WorldLocation(mapId, x, y, z);
    return true;
}

bool RandomPlayerbotMgr::IsInTravelParty(Player* bot)
{
    if (_travelParties.empty() || !bot)
        return false;

    ObjectGuid guid = bot->GetObjectGuid();
    for (TravelParty const& p : _travelParties)
        for (ObjectGuid const& m : p.memberGuids)
            if (m == guid)
                return true;

    return false;
}

// Muster points, one per (faction, continent). Four of these cover every overland instance in
// TravelDestinations.h, which is why G9 - "muster and destination must share a map" - never needed
// cross-map routing to be solved.
//
// Coordinates come from Turtle's own `game_tele` rows, same source as the original Orgrimmar
// muster, and every one is OUTDOORS. City interiors are avoided on purpose: `+follow` alone
// already leaves members stuck on city geometry, and the leader has to path out before the march
// even starts.
//
// ⚠️ Two decisions here that are not obvious:
//
// 1. **Alliance/Kalimdor is Auberdine, NOT Darnassus.** Darnassus is on Teldrassil, a separate
//    landmass reachable only by the Rut'theran boat, and F2 has no boat support - every Alliance
//    Kalimdor march from Darnassus would fail at the water. Auberdine is the northernmost Alliance
//    town on the Kalimdor mainland and is connected by road south to Ashenvale and the Barrens.
// 2. **`team` is stored here rather than read from the muster zone.** The single-config version
//    derived faction from `AreaTableEntry::team` of the muster's zone, which is fine for one
//    hand-set point but breaks the moment a muster sits in contested territory. Every coordinate
//    below is deliberately in its own faction's home zone (Durotar, Tirisfal, Darkshore, Elwynn),
//    so the two agree - but the table is the authority.
// This core's level cap. Registry rows above it are unreachable content (Grim Batol, 61).
static uint32 const MAX_TRAVEL_DEST_LEVEL = 60;

struct TravelMuster
{
    uint32 team;    // AreaTableEntry::team convention: 2 = Alliance, 4 = Horde
    uint32 map;
    float x, y, z;
    char const* name;
};

static TravelMuster const kMusters[] = {
    { 4, 1,  1493.35f, -4414.17f,  23.00f, "Orgrimmar gates" },      // Durotar
    { 4, 0,  1830.93f,   236.19f,  60.54f, "Ruins of Lordaeron" },   // Tirisfal, above Undercity
    { 2, 1,  6501.40f,   481.61f,   6.27f, "Auberdine" },            // Darkshore
    { 2, 0, -9448.55f,    68.24f,  56.32f, "Goldshire" },            // Elwynn
};

// R7: a march only counts if a real player might see it. Sample the straight muster->destination
// line and collect the zones it crosses.
//
// 400 yd between samples: vanilla zones are thousands of yards across, so this cannot step over
// one, and the longest route in the registry (13120 yd) costs 33 lookups. Sampling the straight
// line rather than the walked path is deliberate - the real path is not known until the party
// exists, and the gate has to answer BEFORE recruiting. The straight line is the corridor the
// party broadly follows, and being approximate here costs at most an occasional party that skirts
// a player's zone instead of entering it.
static float const TRAVEL_ZONE_SAMPLE_STEP = 400.0f;

// z is interpolated between the endpoints, which is crude on hilly ground. GetZoneId is driven
// almost entirely by x/y outdoors, and a bad z returns 0, which is skipped rather than guessed at.
static bool RouteCrossesZone(uint32 mapId, float ax, float ay, float az,
                             float bx, float by, float bz,
                             std::set<uint32> const& zones)
{
    float const dx = bx - ax, dy = by - ay, dz = bz - az;
    float const len = sqrt(dx * dx + dy * dy);
    uint32 const steps = std::max<uint32>(1, (uint32)(len / TRAVEL_ZONE_SAMPLE_STEP));
    for (uint32 i = 0; i <= steps; ++i)
    {
        float const t = (float)i / (float)steps;
        uint32 const zone = sTerrainMgr.GetZoneId(mapId, ax + dx * t, ay + dy * t, az + dz * t);
        if (zone && zones.count(zone))
            return true;
    }
    return false;
}

// Zones containing at least one REAL player, keyed by map.
//
// Sessions filtered by GetBotAI() == nullptr, the same path RefreshPlayerZones uses and for the
// same reason: this core's `players` member is a mixed map of real players and non-random bots,
// so it cannot answer "is a human here".
static void CollectPlayerZones(std::map<uint32, std::set<uint32>>& out)
{
    for (auto const& itr : sWorld.GetAllSessions())
    {
        WorldSession* session = itr.second;
        if (!session)
            continue;
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld() || GetBotAI(player))
            continue;
        out[player->GetMapId()].insert(player->GetZoneId());
    }
}

// Is this bot recruitable for a march at all, faction and state aside from level?
static bool IsTravelEligible(Player* bot)
{
    return bot && bot->IsInWorld() && bot->IsAlive() && !bot->GetGroup() && !bot->IsInCombat() &&
           !bot->InBattleGround() && !bot->InBattleGroundQueue() &&
           !bot->IsBeingTeleported() && !bot->IsTaxiFlying() && GetBotAI(bot);
}

std::string RandomPlayerbotMgr::SpawnTravelParty()
{
    if (!sPlayerbotAIConfig.travelParties)
        return "";

    // The cap this replaces was `ponytail: one party at a time. Lift the cap once a walk has been
    // watched end to end.` That condition is met - marches now complete reliably since G1's `loot`
    // fix (3cc6e1e), verified 2/2 to the cave rim and 2/2 to the real instance door.
    //
    // Lifting it is also what makes long registry routes harmless: a 9465 yd march is only a
    // problem while it owns the single slot for half an hour (G14).
    if (_travelParties.size() >= sPlayerbotAIConfig.travelPartyMaxConcurrent)
        return "";

    WorldLocation muster, dest;
    uint32 forcedTeam = 0;          // 0 = derive from the muster zone, as the config path always has
    uint32 bandMin = sPlayerbotAIConfig.travelPartyMinLevel;
    uint32 bandMax = sPlayerbotAIConfig.travelPartyMaxLevel;
    std::string destName;
    TravelDest const* pickedDest = nullptr;   // registry rows only; carries the inside door (G5b)

    if (sPlayerbotAIConfig.travelPartyUseRegistry)
    {
        // One-shot eligibility matrix. Selection filters are otherwise invisible until chance
        // happens to exercise them - G16 (enemy capitals) was found only because a Horde party was
        // caught mid-march to Stormwind Vault, and with a 3-party cap and 20 minute marches you
        // might wait an hour for the next one. This prints the whole muster x destination table on
        // the first spawn attempt so every filter can be checked deterministically.
        static bool dumped = false;
        if (!dumped)
        {
            dumped = true;
            for (TravelMuster const& m : kMusters)
            {
                uint32 ok = 0;
                std::string blocked;
                for (TravelDest const& d : kTravelDests)
                {
                    if (d.map != m.map)
                        continue;
                    if (d.reqLevel > MAX_TRAVEL_DEST_LEVEL)
                    {
                        blocked += std::string(d.name) + "(cap) ";
                        continue;
                    }
                    if (sPlayerbotAIConfig.travelPartyLowLevelMaxRoute > 0.f &&
                        sPlayerbotAIConfig.travelPartyLowLevelBand > 0 &&
                        d.maxLevel <= sPlayerbotAIConfig.travelPartyLowLevelBand)
                    {
                        float const rdx = d.x - m.x, rdy = d.y - m.y;
                        if (sqrt(rdx * rdx + rdy * rdy) > sPlayerbotAIConfig.travelPartyLowLevelMaxRoute)
                        {
                            blocked += std::string(d.name) + "(low band, far route) ";
                            continue;
                        }
                    }
                    AreaTableEntry const* a =
                        GetAreaEntryByAreaID(sTerrainMgr.GetZoneId(d.map, d.x, d.y, d.z));
                    if (a && (a->flags & AREA_FLAG_CAPITAL) && a->team && a->team != m.team)
                    {
                        blocked += std::string(d.name) + "(enemy capital) ";
                        continue;
                    }
                    ++ok;
                }
                F2Log("REGISTRY " + std::string(m.name) + " team=" + std::to_string(m.team) +
                      " map=" + std::to_string(m.map) + " eligible=" + std::to_string(ok) +
                      (blocked.empty() ? "" : " blocked: " + blocked));
            }
        }

        // Destination first, then recruit to fit it - the reverse of the config path, and the
        // reason a low-level party stops being sent across the Barrens: it gets an instance its
        // own level range can reach.
        //
        // Walk the musters in random order so one continent does not monopolise the (currently
        // single) party slot, and inside each, keep only the destinations that actually have
        // enough eligible bots in their own band right now.
        // R7: collect the zones real players are standing in, once, before any candidate work.
        // With the gate on and nobody logged in there is nothing to be seen by, so no party should
        // form at all - say so explicitly, because "zero parties" is exactly what a dead server
        // looks like and this project has already lost a session to that confusion.
        std::map<uint32, std::set<uint32>> playerZones;
        if (sPlayerbotAIConfig.travelPartyRequirePlayerZone)
        {
            CollectPlayerZones(playerZones);
            if (playerZones.empty())
            {
                F2Log("REQUIRE PLAYER ZONE is ON and no real player is in world - "
                      "no party will form, by design");
                return "";
            }
            std::string seen;
            for (auto const& mz : playerZones)
                for (uint32 z : mz.second)
                    seen += "map" + std::to_string(mz.first) + ":zone" + std::to_string(z) + " ";
            F2Log("REQUIRE PLAYER ZONE is ON, players in: " + seen);
        }

        size_t const musterCount = sizeof(kMusters) / sizeof(kMusters[0]);
        std::vector<size_t> order(musterCount);
        for (size_t i = 0; i < musterCount; ++i)
            order[i] = i;
        // Fisher-Yates on the core's own urand; this fork has no std-compatible RNG engine to
        // hand to std::shuffle.
        for (size_t i = musterCount; i > 1; --i)
            std::swap(order[i - 1], order[urand(0, (uint32)i - 1)]);

        struct Candidate { TravelMuster const* m; TravelDest const* d; };
        std::vector<Candidate> viable;
        // Counted so the "nothing to do" message can name the real reason. Reporting an R7 filter
        // as "not enough eligible bots" is precisely the wrong-cause confusion this gate is most
        // likely to cause, and the one that already cost this project a session.
        uint32 unseenRoutes = 0;

        for (size_t mi : order)
        {
            TravelMuster const& m = kMusters[mi];

            // One pass over the bot pool per muster, bucketed by level, instead of one pass per
            // (muster, destination) pair - 4 passes rather than ~150.
            uint32 perLevel[81] = {0};
            for (auto const& itr : GetAllBots())
            {
                Player* bot = itr.second;
                if (!IsTravelEligible(bot) || !IsRandomBot(bot))
                    continue;
                if ((m.team == 2 && bot->GetTeam() != ALLIANCE) ||
                    (m.team == 4 && bot->GetTeam() != HORDE))
                    continue;
                uint32 const lvl = bot->GetLevel();
                if (lvl <= 80)
                    ++perLevel[lvl];
            }

            for (TravelDest const& d : kTravelDests)
            {
                if (d.map != m.map)
                    continue;
                // reqLevel above the cap is content nobody on this core can enter (Grim Batol, 61).
                if (d.reqLevel > MAX_TRAVEL_DEST_LEVEL)
                    continue;

                // G16: do not march a party into the ENEMY'S CAPITAL. Stormwind Vault and
                // Stormwind Stockades sit inside Stormwind, Ragefire Chasm inside Orgrimmar, so a
                // naive same-map filter happily picked "Stormwind Vault from Ruins of Lordaeron"
                // - a Horde party walking into the Alliance capital, straight into the guards.
                // Observed live 2026-08-24 on the first multi-muster run.
                //
                // Keyed on AREA_FLAG_CAPITAL rather than the zone's team alone, because team
                // alone is far too blunt: Scarlet Monastery sits in Tirisfal (Horde territory) and
                // Alliance parties absolutely should still run it. Only capitals are lethal.
                if (AreaTableEntry const* destArea =
                        GetAreaEntryByAreaID(sTerrainMgr.GetZoneId(d.map, d.x, d.y, d.z)))
                {
                    if ((destArea->flags & AREA_FLAG_CAPITAL) && destArea->team &&
                        destArea->team != m.team)
                        continue;
                }
                // The route kills a low-level party, not the destination. Deadmines was picked
                // 5 times from Ruins of Lordaeron on the phase20 soak - 13119 yd, band 17-26 -
                // and lost its leader every single time, walking a level-20 crew through
                // Silverpine, Hillsbrad and Arathi. The same door entered cleanly from Goldshire
                // 1500 yd away. Bands topping out at 30 or below: 6 of 9 long routes lost the
                // leader, 0 of 8 short ones did.
                //
                // Filter the MUSTER, not the destination, so a near capital can still field the
                // party - this costs variety only where the pairing was doomed anyway.
                if (sPlayerbotAIConfig.travelPartyLowLevelMaxRoute > 0.f &&
                    sPlayerbotAIConfig.travelPartyLowLevelBand > 0 &&
                    d.maxLevel <= sPlayerbotAIConfig.travelPartyLowLevelBand)
                {
                    float const rdx = d.x - m.x, rdy = d.y - m.y;
                    if (sqrt(rdx * rdx + rdy * rdy) > sPlayerbotAIConfig.travelPartyLowLevelMaxRoute)
                        continue;
                }

                // R7: the march has to cross a zone somebody is standing in. Checked last of the
                // cheap filters because it is the only one that walks the route.
                if (sPlayerbotAIConfig.travelPartyRequirePlayerZone)
                {
                    auto const zit = playerZones.find(m.map);
                    if (zit == playerZones.end() ||
                        !RouteCrossesZone(m.map, m.x, m.y, m.z, d.x, d.y, d.z, zit->second))
                    {
                        ++unseenRoutes;
                        continue;
                    }
                }

                uint32 have = 0;
                for (uint32 l = d.minLevel; l <= d.maxLevel && l <= 80; ++l)
                    have += perLevel[l];
                if (have >= 2)
                    viable.push_back({ &m, &d });
            }

            if (!viable.empty())
                break;   // this muster can field a party; no need to look at the others
        }

        if (viable.empty())
        {
            if (unseenRoutes)
            {
                F2Log("no party: " + std::to_string(unseenRoutes) +
                      " route(s) had bots but crossed no zone a real player is in");
                sLog.outBasic("F2: %u candidate route(s) skipped - no real player would see them",
                              unseenRoutes);
            }
            else
                sLog.outError("F2: registry mode found no destination with enough eligible bots");
            return "";
        }

        Candidate const& pick = viable[urand(0, viable.size() - 1)];
        muster = WorldLocation(pick.m->map, pick.m->x, pick.m->y, pick.m->z);
        dest = WorldLocation(pick.d->map, pick.d->x, pick.d->y, pick.d->z);
        forcedTeam = pick.m->team;
        bandMin = pick.d->minLevel;
        bandMax = pick.d->maxLevel;
        destName = pick.d->name;
        pickedDest = pick.d;

        F2Log("SELECTED " + destName + " (trigger " + std::to_string(pick.d->trigger) + ", req " +
              std::to_string((uint32)pick.d->reqLevel) + ") from " + pick.m->name +
              " band " + std::to_string(bandMin) + "-" + std::to_string(bandMax));
    }
    else if (!ParseTravelPoint(sPlayerbotAIConfig.travelPartyMuster, muster) ||
             !ParseTravelPoint(sPlayerbotAIConfig.travelPartyDest, dest))
    {
        sLog.outError("F2: TravelPartyMuster/TravelPartyDest must be \"map,x,y,z\"");
        return "";
    }

    if (muster.mapId != dest.mapId)
    {
        sLog.outError("F2: muster and destination are on different maps (%u vs %u) - this is an "
                      "overland march, not a teleport chain", muster.mapId, dest.mapId);
        return "";
    }

    // Recruit from the faction that owns the muster point's zone (2 = Alliance, 4 = Horde in
    // AreaTable, anything else contested -> take either). Keeps the coordinates and the
    // faction filter from drifting apart when the config points somewhere else.
    //
    // Registry mode carries `team` on the muster row instead, because deriving it only works while
    // the muster sits in its own faction's home zone - true for all four today, but a silent trap
    // the moment one is moved somewhere contested.
    uint32 territory = forcedTeam;
    if (!territory)
    {
        uint32 musterZone = sTerrainMgr.GetZoneId(muster.mapId, muster.x, muster.y, muster.z);
        AreaTableEntry const* musterArea = GetAreaEntryByAreaID(musterZone);
        territory = musterArea ? musterArea->team : 0;
    }

    uint32 const want = std::min<uint32>(sPlayerbotAIConfig.travelPartyDungeonSize, 5);

    // Bots are teleported to the muster point wherever they were, so the party always starts
    // together at the city.
    std::vector<Player*> picked;
    for (auto const& itr : GetAllBots())
    {
        if (picked.size() >= want)
            break;

        Player* bot = itr.second;
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            continue;
        if (!IsRandomBot(bot))
            continue;
        if (territory == 2 && bot->GetTeam() != ALLIANCE)
            continue;
        if (territory == 4 && bot->GetTeam() != HORDE)
            continue;
        if (bot->GetLevel() < bandMin || bot->GetLevel() > bandMax)
            continue;
        if (!IsTravelEligible(bot))
            continue;

        picked.push_back(bot);
    }

    if (picked.size() < 2)
    {
        sLog.outError("F2: not enough eligible bots for a travel party (%u found)", (uint32)picked.size());
        return "";
    }

    // G13: the leader used to be picked.front(), an arbitrary bot in the band. Take the
    // highest-level member instead - it is the one most likely to survive the road, and it stops
    // a level 7 leading a party through zones that will kill it.
    std::sort(picked.begin(), picked.end(),
              [](Player* a, Player* b) { return a->GetLevel() > b->GetLevel(); });

    Player* leader = picked.front();
    PlayerbotAI* leaderAI = GetBotAI(leader);

    // Same pattern the module's own bot-party test uses (strategy/tests/CommandParty.cpp):
    // create the group directly and add members, no invite/accept dance - bots don't consent.
    // Group::Create()/_addMember() write straight to the groups/group_member tables.
    Group* group = new Group;
    if (!group->Create(leader->GetObjectGuid(), leader->GetName()))
    {
        delete group;
        return "";
    }
    sObjectMgr.AddGroup(group);

    leader->TeleportTo(muster.mapId, muster.x, muster.y, muster.z, 0.f);

    TravelParty party;
    party.leaderGuid = leader->GetObjectGuid();
    party.leaderName = leader->GetName();
    party.memberGuids.push_back(leader->GetObjectGuid());
    party.mapId = dest.mapId;
    party.destX = dest.x;
    party.destY = dest.y;
    party.destZ = dest.z;
    party.destName = destName;
    // Raids are marched to but never entered: the door is a legitimate destination, five bots in
    // Molten Core is not. Leaving insideMap at 0 is the whole gate - the arrival branch reads it.
    if (pickedDest && !pickedDest->isRaid)
    {
        party.insideMap = pickedDest->inMap;
        party.insideX = pickedDest->inX;
        party.insideY = pickedDest->inY;
        party.insideZ = pickedDest->inZ;
        party.insideO = pickedDest->inO;
    }

    for (size_t i = 1; i < picked.size(); ++i)
    {
        Player* member = picked[i];
        if (group->GetMembersCount() >= 5)
            break;
        if (!group->AddMember(member->GetObjectGuid(), member->GetName()))
            continue;

        PlayerbotAI* memberAI = GetBotAI(member);
        if (!memberAI)
            continue;

        member->TeleportTo(muster.mapId, muster.x + frand(-4.f, 4.f), muster.y + frand(-4.f, 4.f),
                           muster.z, 0.f);

        // March with the group instead of wandering off on their own errands.
        memberAI->SetMaster(leader);
        memberAI->ResetStrategies();
        memberAI->ChangeStrategy("+follow,-grind,-rpg,-travel", BotState::BOT_STATE_NON_COMBAT);

        party.memberGuids.push_back(member->GetObjectGuid());
    }

    // The leader is driven straight off the navmesh by MovePoint in the tick below.
    // ponytail: no TravelTarget/TravelDestination plumbing - the AzerothCore version needed a
    // forced travel target only to stop its own "travel" strategy from picking a different
    // destination, and the actual walking was MovePoint there too. Strip the autonomous
    // strategies instead; one less pair of owned pointers to leak on disband.
    leaderAI->ChangeStrategy(sPlayerbotAIConfig.travelPartyLeaderStrip, BotState::BOT_STATE_NON_COMBAT);

    // Distance-proportional hard timeout. The flat 900 s was already tight: the 3300 yd
    // Org->Wailing Caverns route took ~10 min *with a mount*. The stall detector below is the
    // real guard against a wedged march, so this only has to be generous enough not to cut off
    // a legitimate one. 5 min of slack + 2 s per yard (~0.5 yd/s, half of unmounted running).
    float const routeDist = std::hypot(dest.x - muster.x, dest.y - muster.y);
    party.deadline = (uint32)time(nullptr) + 300 + (uint32)(routeDist * 2.0f);
    party.bestProgressTime = (uint32)time(nullptr);  // start the no-progress clock now
    _travelParties.push_back(std::move(party));

    sLog.outString("F2: travel party formed (%u bots), marching to (%d,%d,%d)",
                   (uint32)_travelParties.back().memberGuids.size(),
                   (int)dest.x, (int)dest.y, (int)dest.z);
    {
        std::ostringstream o;
        o << "FORMED " << _travelParties.back().memberGuids.size() << " bots; leader "
          << leader->GetName() << " muster (" << (int)muster.x << "," << (int)muster.y << ","
          << (int)muster.z << ") -> dest (" << (int)dest.x << "," << (int)dest.y << ","
          << (int)dest.z << ") territory " << territory;
        F2Log(o.str());
    }

    return std::string(leader->GetName());
}

// Every per-party log line carries its leader's name. With one party at a time the plain lines
// were readable; with several marching at once an untagged F2_travel.log interleaves into
// nonsense - and that log is the instrument that found G1's cause. Tag before lifting the cap.
#define F2LogParty(p, line) F2Log("[" + (p).leaderName + "] " + (line))

void RandomPlayerbotMgr::UpdateTravelParties()
{
    if (_travelParties.empty())
        return;

    uint32 now = (uint32)time(nullptr);

    for (auto it = _travelParties.begin(); it != _travelParties.end();)
    {
        TravelParty& p = *it;
        Player* leader = sObjectMgr.GetPlayer(p.leaderGuid);

        bool done = false;
        // G5b: the teleports are already away; this party is not marching any more. A far
        // TeleportTo is asynchronous, so wait for every bot to land on the instance map before
        // disbanding - disband first and each bot walks through the door alone, binding to its own
        // copy of the dungeon instead of the party's. Bounded, because a bot that never lands must
        // not hold a party slot forever.
        if (p.enteringUntil)
        {
            uint32 inside = 0;
            for (ObjectGuid const& guid : p.memberGuids)
            {
                Player* member = sObjectMgr.GetPlayer(guid);
                if (member && member->IsInWorld() && !member->IsBeingTeleported() &&
                    member->GetMapId() == p.insideMap)
                    ++inside;
            }

            if (inside == (uint32)p.memberGuids.size() || now >= p.enteringUntil)
            {
                F2LogParty(p, "ENTERED " + p.destName + " map=" + std::to_string(p.insideMap) +
                      " (" + std::to_string(inside) + "/" +
                      std::to_string(p.memberGuids.size()) + " bots inside" +
                      (inside == (uint32)p.memberGuids.size() ? "" : ", gave up waiting") +
                      ") -> disband");
                DisbandTravelParty(p);
                it = _travelParties.erase(it);
                continue;
            }

            ++it;
            continue;
        }

        // G6: the leader dying used to end the march. It is a setback, not the end - revive it
        // where it fell and carry on, up to TravelPartyMaxDeaths (default 3, 0 restores the old
        // behaviour). Bounded on purpose: a party being farmed by something it cannot beat has to
        // stop and produce a verdict instead of reviving into the same mob forever.
        //
        // ResurrectPlayer + SpawnCorpseBones is the module's own revive, lifted verbatim from
        // RandomPlayerbotMgr::Refresh. A marching bot is still a random bot; it does not need a
        // second implementation. Refresh also calls ResetStrategies(), which is deliberately NOT
        // copied here - that would hand the leader its `travel`/`grind`/`loot` strategies back
        // mid-march, and re-stripping them is the thing G1 was.
        if (leader && leader->IsInWorld() && !leader->IsAlive() &&
            leader->GetMapId() == p.mapId && !leader->IsBeingTeleported() &&
            p.leaderDeaths < sPlayerbotAIConfig.travelPartyMaxDeaths)
        {
            ++p.leaderDeaths;
            leader->ResurrectPlayer(1.0f);
            leader->SpawnCorpseBones();

            F2LogParty(p, "LEADER DIED at " +
                  std::to_string((int)leader->GetDistance3dToCenter(p.destX, p.destY, p.destZ)) +
                  " yd out -> revived (" + std::to_string((int)p.leaderDeaths) + "/" +
                  std::to_string(sPlayerbotAIConfig.travelPartyMaxDeaths) + ")");

            // A corpse does not make progress, so the stall clock has been running against a bot
            // that could not move. Reset it, drop the stale waypoint, and let the mover earn the
            // wheel back rather than inheriting a demotion the death caused.
            p.bestProgressTime = now;
            p.curTgtSet = false;
            p.lastOrderSet = false;
            p.deadOrders = 0;
            p.forceRawMove = false;
            p.recoverUntil = now + 45;  // same eat-and-drink window a fight gets
            p.deadline += 60;           // the death and the hold do not eat the travel budget
        }
        else if (!leader || !leader->IsInWorld() || !leader->IsAlive() || leader->GetMapId() != p.mapId)
        {
            // Say WHY. This used to tear down in silence, so a party that died mid-march looked
            // exactly like one that never formed - two "FORMED" lines in a row with no verdict
            // between them. Watched on 2026-08-23 with a level 7 party in the Barrens.
            std::ostringstream o;
            o << "LEADER LOST (";
            if (!leader)                       o << "no player object";
            else if (!leader->IsInWorld())     o << "not in world";
            else if (!leader->IsAlive())       o << "dead at " << (int)leader->GetDistance3dToCenter(p.destX, p.destY, p.destZ)
                                                 << " yd out after " << (int)p.leaderDeaths << " revives";
            else                               o << "wrong map " << leader->GetMapId() << " != " << p.mapId;
            o << ") -> disband";
            F2LogParty(p, o.str());
            done = true;  // leader gone -> tear down
        }
        else if (now >= p.deadline)
        {
            F2LogParty(p, "TIMEOUT -> disband");
            sLog.outString("F2: travel party timed out; disbanding.");
            done = true;
        }
        // Arrival is judged in 2D with a generous vertical tolerance, not in 3D.
        //
        // Measured 2026-08-23 against the real Wailing Caverns door (trigger 228,
        // -753.60,-2212.78,21.54): two parties in a row walked the whole 3145 yd route and parked
        // at (-753,-2212,102) and (-753,-2212,105) - the SAME x and y, under one yard away
        // horizontally, ~80 yd straight up. They were standing on the cliff directly above the
        // cave mouth, because the ravine descent is not on map 1's navmesh, and PathFinder
        // correctly returned PATHFIND_NOPATH for the last 80 yd. A 3D check calls that a failure
        // at 81 yd; by any useful reading the party is at the door.
        //
        // Blackfathom Deeps proved 100 yd was not enough: that party finished at (4252,756,92)
        // against a door at (4252.37,756.97,-23.06) - 2D distance ZERO, 115 yd of pure Z - and was
        // still called a failure. The vertical slack is now TravelPartyArriveZ (default 150).
        else if (leader->GetDistance2dToCenter(p.destX, p.destY) <= 20.f &&
                 std::fabs(leader->GetPositionZ() - p.destZ) <= sPlayerbotAIConfig.travelPartyArriveZ)
        {
            // G5b: walking the last stretch is not an option for most doors - the descent is
            // off the navmesh, which is why arrival is judged in 2D at all. Crossing the trigger
            // is a teleport for a real player too, so do exactly what the trigger does: send the
            // party to areatrigger_teleport's own target. The leader goes first so the group's
            // instance binding is created by the leader, as it would be in a real run.
            if (sPlayerbotAIConfig.travelPartyEnterInstance && p.insideMap)
            {
                F2LogParty(p, "ARRIVED (2D<=20, dZ<=" +
                      std::to_string((int)sPlayerbotAIConfig.travelPartyArriveZ) +
                      ") -> entering " + p.destName);

                leader->TeleportTo(p.insideMap, p.insideX, p.insideY, p.insideZ, p.insideO);
                for (ObjectGuid const& guid : p.memberGuids)
                {
                    if (guid == p.leaderGuid)
                        continue;
                    Player* member = sObjectMgr.GetPlayer(guid);
                    if (!member || !member->IsInWorld())
                        continue;
                    if (!member->IsAlive())
                    {
                        member->ResurrectPlayer(1.0f);
                        member->SpawnCorpseBones();
                    }
                    member->TeleportTo(p.insideMap, p.insideX + frand(-3.f, 3.f),
                                       p.insideY + frand(-3.f, 3.f), p.insideZ, p.insideO);
                }

                p.enteringUntil = now + 60;   // ponytail: fixed wait, not a per-bot state machine
            }
            else
            {
                F2LogParty(p, "ARRIVED (2D<=20, dZ<=" +
                      std::to_string((int)sPlayerbotAIConfig.travelPartyArriveZ) + ") -> disband");
                sLog.outString("F2: travel party reached the destination; disbanding.");
                done = true;
            }
        }
        else if (PlayerbotAI* lAI = GetBotAI(leader))
        {
            if (!leader->IsBeingTeleported())
            {
                // Random-bot management re-adds the autonomous strategies, which would pick
                // their own destination and undo the march, so re-strip them every tick. The
                // guard reads the configured list rather than a hard-coded trio, otherwise
                // widening TravelPartyLeaderStrip would strip once at spawn and never again.
                {
                    bool needStrip = false;
                    std::string const& strip = sPlayerbotAIConfig.travelPartyLeaderStrip;
                    size_t i = 0;
                    while (i < strip.size() && !needStrip)
                    {
                        size_t const comma = strip.find(',', i);
                        std::string tok = strip.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
                        i = (comma == std::string::npos) ? strip.size() : comma + 1;
                        while (!tok.empty() && (tok.front() == ' ' || tok.front() == '-' || tok.front() == '+'))
                            tok.erase(tok.begin());
                        while (!tok.empty() && tok.back() == ' ')
                            tok.pop_back();
                        if (!tok.empty() && lAI->HasStrategy(tok, BotState::BOT_STATE_NON_COMBAT))
                            needStrip = true;
                    }
                    if (needStrip)
                        lAI->ChangeStrategy(strip, BotState::BOT_STATE_NON_COMBAT);
                }

                float const lx = leader->GetPositionX();
                float const ly = leader->GetPositionY();
                float const lz = leader->GetPositionZ();
                float const destDist = leader->GetDistance3dToCenter(p.destX, p.destY, p.destZ);

                // Fighting is part of travelling, not a failure of it. While any member is in
                // combat the march yields completely: no movement orders (they would fight the
                // combat engine for the leader), no straggler snapping (it would yank a bot out
                // of its fight), and the stall clock and deadline both stop, because a legitimate
                // pull-kill-eat cycle takes far longer than the 90 s no-progress limit.
                bool fighting = false;
                for (ObjectGuid const& guid : p.memberGuids)
                {
                    Player* member = sObjectMgr.GetPlayer(guid);
                    if (member && member->IsInWorld() && member->IsInCombat())
                    {
                        fighting = true;
                        break;
                    }
                }

                if (fighting && !p.inCombat)
                {
                    p.inCombat = true;
                    p.combatStart = now;
                    F2LogParty(p, "COMBAT start at " + std::to_string((int)destDist) + " yd out");
                }
                else if (!fighting && p.inCombat)
                {
                    uint32 const spent = now - p.combatStart;
                    p.inCombat = false;
                    p.deadline += spent;   // the fight does not eat the travel budget
                    p.curTgtSet = false;   // re-path fresh from wherever the fight ended
                    p.deadOrders = 0;
                    p.forceRawMove = false;
                    p.lastOrderSet = false;
                    p.bestProgressTime = now;
                    // The `food` strategy is in the default non-combat engine and the march never
                    // strips it, so the crew feeds itself - but only if nothing is ordering it to
                    // move. Give it a window before marching on, or the next pull lands on a party
                    // that never healed up.
                    p.recoverUntil = now + 45;
                    p.deadline += 45;  // grant the whole window up front; over-granting a safety
                                       // net costs nothing, cutting a march short costs the test
                    F2LogParty(p, "COMBAT end after " + std::to_string(spent) + "s, resuming march");
                }

                bool holding = p.inCombat;
                if (!holding && now < p.recoverUntil)
                {
                    // ponytail: health only, no mana check - a healer out of mana still walks.
                    // Add power if wipes downstream of an OOM healer show up in the telemetry.
                    for (ObjectGuid const& guid : p.memberGuids)
                    {
                        Player* member = sObjectMgr.GetPlayer(guid);
                        if (member && member->IsInWorld() && member->IsAlive() &&
                            lAI->GetHealthPercent(*member) < 80)
                        {
                            holding = true;
                            break;
                        }
                    }

                    if (!holding)
                        p.recoverUntil = 0;  // everyone topped up early, get moving
                }

                if (holding)
                {
                    p.bestProgressTime = now;  // freeze the stall clock while fighting or eating
                    // Heartbeat. This branch used to `continue` past the 15 s telemetry, so a long
                    // fight wrote NOTHING and the log looked identical to a hung server - which
                    // tripped a stall watchdog on 2026-08-23. A hold must never be silent.
                    if (now - p.lastLogTime >= 15)
                    {
                        p.lastLogTime = now;
                        F2LogParty(p, "HOLD (" + std::string(p.inCombat ? "combat" : "recovery") + ") at " +
                              std::to_string((int)destDist) + " yd out, " +
                              std::to_string(now - p.combatStart) + "s");
                    }
                    ++it;
                    continue;
                }

                if (destDist < p.bestDist - 2.0f)
                {
                    p.bestDist = destDist;
                    p.bestProgressTime = now;
                }

                // Close to the destination but no longer getting closer = the navmesh has no
                // route the rest of the way (AzerothCore hit exactly this at the Wailing
                // Caverns ravine: the leader circled the rim). Stop cleanly and say so in the
                // log instead of orbiting until the 15 min timeout.
                // ponytail: no ring-probe descent machinery - that was tuned against one
                // dungeon on the 3.3.5 mesh. Re-add only if a real route here needs it.
                bool const nearDest = leader->GetDistance2dToCenter(p.destX, p.destY) <= 120.f;
                // 45 s near the door, 90 s out on the road: a legitimate detour around a mountain
                // can spend a while not getting any closer in a straight line.
                if (now - p.bestProgressTime >= (nearDest ? 45u : 90u))
                {
                    std::ostringstream o;
                    o << "STALLED " << (nearDest ? "near destination" : "en route") << " (best "
                      << (int)p.bestDist << " yd, now " << (int)destDist << " yd) -> disband";
                    F2LogParty(p, o.str());
                    // Near the destination this is "close enough, the navmesh has no route the rest
                    // of the way". Anywhere else it means the march is wedged and would otherwise
                    // burn the whole 15 min deadline in silence, which is not a useful test result.
                    done = true;
                }

                // Keep the crew together: +follow alone leaves members stuck behind on city
                // geometry, so walk stragglers in and snap back anyone truly lost.
                float maxMemberDist = 0.f;
                for (ObjectGuid const& mg : p.memberGuids)
                {
                    if (mg == p.leaderGuid)
                        continue;

                    Player* member = sObjectMgr.GetPlayer(mg);
                    if (!member || !member->IsInWorld())
                        continue;

                    // G6: a dead follower is revived on the spot and costs the march nothing - the
                    // straggler snap below puts it back with the group either way. Only the
                    // LEADER's deaths count against TravelPartyMaxDeaths, because only the leader
                    // dying repeatedly means the route itself is unsurvivable.
                    if (!member->IsAlive() && !member->IsBeingTeleported() &&
                        sPlayerbotAIConfig.travelPartyMaxDeaths > 0)
                    {
                        member->ResurrectPlayer(1.0f);
                        member->SpawnCorpseBones();
                        F2LogParty(p, "MEMBER DIED " + std::string(member->GetName()) + " -> revived");
                    }

                    if (PlayerbotAI* mAI = GetBotAI(member))
                    {
                        if (mAI->HasStrategy("grind", BotState::BOT_STATE_NON_COMBAT) ||
                            mAI->HasStrategy("rpg", BotState::BOT_STATE_NON_COMBAT) ||
                            !mAI->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT))
                        {
                            mAI->SetMaster(leader);
                            mAI->ChangeStrategy("+follow,-grind,-rpg,-travel", BotState::BOT_STATE_NON_COMBAT);
                        }
                    }

                    float md = (member->GetMapId() == leader->GetMapId())
                                   ? member->GetDistance3dToCenter(lx, ly, lz)
                                   : 100000.f;
                    if (md > 55.f)
                    {
                        // The leader never stops to wait (stopping dismounts it, which desyncs
                        // everyone's speed), so snapping stragglers is what holds the group together.
                        member->NearTeleportTo(lx + frand(-4.f, 4.f), ly + frand(-4.f, 4.f), lz,
                                               member->GetOrientation());
                        md = 4.f;
                    }
                    else if (md > 12.f && !member->IsMoving() && !member->IsBeingTeleported())
                    {
                        member->GetMotionMaster()->MovePoint(990002, lx + frand(-5.f, 5.f),
                                                             ly + frand(-5.f, 5.f), lz,
                                                             FORCED_MOVEMENT_RUN);
                    }

                    maxMemberDist = std::max(maxMemberDist, md);
                }

                p.maxMemberDist = maxMemberDist;

                // Leader: one long navmesh spline at a time. Only re-path when it has arrived at
                // (or stopped short of) the current target - re-issuing MovePoint every tick
                // restarts the spline, which is what makes bots run/stop/run.
                float const toTgt = p.curTgtSet ? leader->GetDistance2dToCenter(p.curTgtX, p.curTgtY) : 1e9f;
                if (!done && now >= p.retryPathAfter &&
                    (!leader->IsMoving() || !p.curTgtSet || toTgt <= 12.f))
                {
                    // Instrument: a re-path every tick means the spline is being cancelled almost
                    // as fast as it is issued, and these three fields say by what. `why` is the
                    // condition that fired, `mm` is whatever movement generator is on top right
                    // now (a PointMovementGenerator here means ours survived; anything else means
                    // something took it), `mounted` catches the mount-attempt loop.
                    F2LogParty(p, "REPATH why=" + std::string(!leader->IsMoving() ? "notMoving"
                                                      : (!p.curTgtSet ? "noTgt" : "reachedTgt")) +
                          " toTgt=" + std::to_string((int)(toTgt > 1e8f ? -1.f : toTgt)) +
                          " mm=" + std::to_string((uint32)leader->GetMotionMaster()->GetCurrentMovementGeneratorType()) +
                          " mounted=" + std::to_string(leader->IsMounted() ? 1 : 0) +
                          " speed=" + std::to_string((int)leader->GetSpeed(MOVE_RUN)));
                    PathType ptype = PATHFIND_BLANK;
                    float endX = 0.f, endY = 0.f, endZ = 0.f;
                    char moveCode = '?';

                    // Did the previous order actually take us anywhere? Three dead orders in a
                    // row and MoveTo loses the wheel until it earns it back.
                    if (p.lastOrderSet)
                    {
                        float const covered = leader->GetDistance2dToCenter(p.lastOrderX, p.lastOrderY);
                        if (covered < 5.f)
                        {
                            if (p.deadOrders < 250)
                                ++p.deadOrders;
                            if (p.deadOrders >= 3)
                                p.forceRawMove = true;
                        }
                        else
                        {
                            p.deadOrders = 0;
                            p.forceRawMove = false;
                        }
                    }
                    p.lastOrderX = lx;
                    p.lastOrderY = ly;
                    p.lastOrderSet = true;

                    // Keep the navmesh path that chose the waypoint. Mode 2 walks this array
                    // directly instead of letting MovePoint run a second query for it, and the
                    // point count goes in the telemetry so a collapsed path is visible.
                    Movement::PointsArray chosenPath;

                    // Take the first REAL navmesh path (NORMAL or INCOMPLETE, and none of
                    // NOPATH/SHORTCUT/NOT_USING_PATH, which mean "straight line through terrain").
                    auto tryTarget = [&](float tgx, float tgy, float tgz) -> bool
                    {
                        PathFinder g(leader);
                        g.calculate(tgx, tgy, tgz);
                        ptype = g.getPathType();
                        if ((ptype & (PATHFIND_NORMAL | PATHFIND_INCOMPLETE)) == 0 ||
                            (ptype & (PATHFIND_NOPATH | PATHFIND_SHORTCUT | PATHFIND_NOT_USING_PATH)) != 0 ||
                            g.getPath().size() <= 1)
                            return false;

                        // Walk to a point we can actually reach this hop, not to the far end of
                        // the path. An endpoint 900 yd out is both unreachable in one spline and
                        // wildly unstable between ticks (it is whichever poly is closest-reachable
                        // right now), which is what made the leader ping-pong. Take the furthest
                        // path node within MAX_HOP instead; the next re-path continues from there
                        // with fresh information. ponytail: fixed 200 yd, not tuned per terrain.
                        float const MAX_HOP = 200.f;
                        Vector3 end = g.getPath().back();
                        for (auto const& node : g.getPath())
                        {
                            if (leader->GetDistance3dToCenter(node.x, node.y, node.z) <= MAX_HOP)
                                end = node;
                        }

                        // A real path that goes nowhere is worse than no path: PATHFIND_INCOMPLETE
                        // against a target thousands of yards out can come back ending a few yards
                        // ahead, and the leader then walks to it, re-paths, gets the same stub, and
                        // thrashes in place. (Watched exactly that in Durotar 2026-08-21: stuck at
                        // ~2540 yd out, re-pathing every 2-3 s.) Require the endpoint to be a real
                        // step forward, else fall through to the fan probes below, which aim at
                        // intermediate points and route around whatever is blocking us.
                        float const stepLen = leader->GetDistance3dToCenter(end.x, end.y, end.z);
                        float const endToDest = std::sqrt((end.x - p.destX) * (end.x - p.destX) +
                                                          (end.y - p.destY) * (end.y - p.destY) +
                                                          (end.z - p.destZ) * (end.z - p.destZ));
                        if (stepLen < 25.f || endToDest > destDist - 15.f)
                            return false;

                        endX = end.x;
                        endY = end.y;
                        endZ = end.z;
                        // Trim to the part we actually committed to, so mode 2 walks exactly the
                        // hop the waypoint describes rather than the whole 900 yd path.
                        chosenPath.clear();
                        for (auto const& node : g.getPath())
                        {
                            chosenPath.push_back(node);
                            if (node.x == end.x && node.y == end.y && node.z == end.z)
                                break;
                        }
                        return true;
                    };

                    bool usable = tryTarget(p.destX, p.destY, p.destZ);
                    if (!usable)
                    {
                        // Destination unreachable from here (wall, water, mesh gap): aim at
                        // intermediate points fanned out toward it instead.
                        float const baseAng = atan2(p.destY - ly, p.destX - lx);
                        float const dists[] = {250.f, 150.f, 90.f};
                        float const offs[] = {0.f, 0.4f, -0.4f, 0.8f, -0.8f};
                        for (float d : dists)
                        {
                            for (float a : offs)
                            {
                                if (tryTarget(lx + cos(baseAng + a) * d, ly + sin(baseAng + a) * d, lz))
                                {
                                    usable = true;
                                    break;
                                }
                            }
                            if (usable)
                                break;
                        }
                    }

                    // Commit to a waypoint instead of re-picking one every tick. An INCOMPLETE
                    // path against a destination thousands of yards out returns whichever poly is
                    // currently closest-reachable, and beside an obstacle that flips between two
                    // endpoints on either side of it. Measured at (740,-3804): toTgt swinging
                    // 456 -> 426 -> 499 -> 479 while the leader moved about five yards, i.e. the
                    // target was moving 70 yd a tick, not the bot. Both candidates clear the
                    // stepLen/endToDest guards above, so only hysteresis breaks the tie: keep the
                    // waypoint we are walking to unless the new one is a real improvement.
                    bool keptTgt = false;
                    // Commitment must not outlive progress: if the leader has already gone three
                    // orders without covering ground (the same signal that demotes the mover), the
                    // committed waypoint is the thing that is wrong, so let it pick a fresh one.
                    if (usable && p.curTgtSet && !p.forceRawMove)
                    {
                        float const newToDest = std::sqrt((endX - p.destX) * (endX - p.destX) +
                                                          (endY - p.destY) * (endY - p.destY));
                        float const curToDest = std::sqrt((p.curTgtX - p.destX) * (p.curTgtX - p.destX) +
                                                          (p.curTgtY - p.destY) * (p.curTgtY - p.destY));
                        if (newToDest > curToDest - 10.f &&
                            leader->GetDistance2dToCenter(p.curTgtX, p.curTgtY) > 12.f)
                        {
                            endX = p.curTgtX;
                            endY = p.curTgtY;
                            endZ = p.curTgtZ;
                            keptTgt = true;
                        }
                    }

                    if (usable)
                    {
                        p.curTgtX = endX;
                        p.curTgtY = endY;
                        p.curTgtZ = endZ;
                        p.curTgtSet = true;
                        p.retryPathAfter = 0;
                        moveCode = IssueMove(leader, lAI, p.mapId, endX, endY, endZ, p.forceRawMove, chosenPath);
                    }
                    else
                    {
                        // Transient off-mesh spot: 10 yd nudge toward the destination and retry.
                        p.curTgtSet = false;
                        p.retryPathAfter = now + 2;
                        float const dx = p.destX - lx, dy = p.destY - ly;
                        float const flat = sqrt(dx * dx + dy * dy);
                        float const step = std::min(10.0f, flat);
                        float const nx = (flat > 1.0f) ? lx + dx / flat * step : p.destX;
                        float const ny = (flat > 1.0f) ? ly + dy / flat * step : p.destY;
                        chosenPath.clear();  // the nudge has no path behind it; mode 2 falls back to MovePoint
                        moveCode = IssueMove(leader, lAI, p.mapId, nx, ny, lz, p.forceRawMove, chosenPath);
                    }

                    // p1 = points in the path F2 chose the waypoint from. p2 = points in a second
                    // PathFinder run to that same waypoint, which is exactly the query
                    // MoveSplineInit performs internally for a MOVE_PATHFINDING order. If p1 is
                    // healthy and p2 collapses to 0/1, the two queries disagree and the second one
                    // is what strands the leader - the whole point of this instrument.
                    //
                    // Only run it when the mover already reported trouble. A healthy march is
                    // 'T' on every order and would otherwise pay for a second navmesh query every
                    // couple of seconds per party, which stops being free once G2 lifts the
                    // one-party cap. p2=-2 means "not measured because nothing was wrong".
                    int p2 = -2;
                    if (usable && moveCode != 'T')
                    {
                        PathFinder v(leader);
                        v.calculate(endX, endY, endZ);
                        p2 = (int)v.getPath().size();
                    }

                    std::ostringstream o;
                    o << "PATH type=" << (uint32)ptype << " usable=" << usable
                      << " mv=" << moveCode << " keep=" << (keptTgt ? 1 : 0)
                      << " mode=" << sPlayerbotAIConfig.travelPartyMoveMode
                      << " p1=" << (int)chosenPath.size() << " p2=" << p2
                      << " toTgt=" << (usable ? (int)leader->GetDistance3dToCenter(endX, endY, endZ) : -1)
                      << F2SplineState(leader)
                      << " remaining=" << (int)leader->GetDistance2dToCenter(p.destX, p.destY)
                      << " mounted=" << leader->IsMounted() << " inWater=" << leader->IsInWater();
                    F2LogParty(p, o.str());
                }
            }

            if (now - p.lastLogTime >= 15)
            {
                p.lastLogTime = now;
                std::ostringstream o;
                o << "leader " << leader->GetName() << " zone " << leader->GetZoneId() << " pos ("
                  << (int)leader->GetPositionX() << "," << (int)leader->GetPositionY() << ","
                  << (int)leader->GetPositionZ() << ") dist "
                  << (int)leader->GetDistance3dToCenter(p.destX, p.destY, p.destZ)
                  << " best " << (int)p.bestDist
                  << " moving=" << leader->IsMoving()
                  << " active=" << lAI->AllowActivity(ALL_ACTIVITY, true)
                  << " groupSpread=" << (int)p.maxMemberDist
                  << " lvl=" << leader->GetLevel() << " cls=" << (uint32)leader->getClass()
                  // The last action the leader's OWN engine ran. This is the field that found
                  // G1's real cause: at every wedge it reads
                  //   |PUSH:move to loot - 7.000000 (trigger)|T:far from current loot|...
                  //   |A:move to loot - OK
                  // "OK" = executed. Keep it - two wrong theories (mount, then wander) both died
                  // the moment the queue was printed instead of reasoned about.
                  << " lastAct=" << (lAI->GetCurrentEngine() ? lAI->GetCurrentEngine()->GetLastAction() : "<no engine>");
                // Every non-combat strategy still live on the leader. The march only removes
                // TravelPartyLeaderStrip; everything else here runs its own actions every tick and
                // can cancel the march spline. Printed so the "who is stopping the leader"
                // bisection can be read off a log instead of guessed at.
                o << " strat=";
                for (auto const& sv : lAI->GetStrategies(BotState::BOT_STATE_NON_COMBAT))
                    o << sv << "|";
                F2LogParty(p, o.str());
            }
        }

        if (done)
        {
            DisbandTravelParty(p);
            it = _travelParties.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void RandomPlayerbotMgr::DisbandTravelParty(TravelParty& p)
{
    // Drop follow/travel and hand every bot back to normal random-bot behaviour.
    for (ObjectGuid const& guid : p.memberGuids)
    {
        Player* member = sObjectMgr.GetPlayer(guid);
        if (!member)
            continue;

        if (PlayerbotAI* memberAI = GetBotAI(member))
        {
            memberAI->SetMaster(nullptr);
            memberAI->ResetStrategies();
        }
    }

    Player* leader = sObjectMgr.GetPlayer(p.leaderGuid);
    if (leader && leader->GetGroup())
        leader->GetGroup()->Disband(true);
}

void RandomPlayerbotMgr::RandomTeleportForLevel(Player* bot, bool activeOnly)
{
    if (bot->InBattleGround())
        return;

    // Populate-Around-Players (F1): bias this teleport toward a real-player zone.
    if (sPlayerbotAIConfig.populateAroundPlayers &&
        urand(0, 100) < (uint32)(sPlayerbotAIConfig.populateProbTeleport * 100))
    {
        std::vector<WorldLocation> playerLocs = GetPlayerZoneTeleportLocations(bot);
        if (!playerLocs.empty())
        {
            uint32 const fromZone = bot->GetZoneId();
            float const fromX = bot->GetPositionX(), fromY = bot->GetPositionY();

            RandomTeleport(bot, playerLocs, false, activeOnly);

            // Log after the call, not before: RandomTeleport re-filters the list and can drop
            // every candidate, in which case the bot does not move at all. "Proposed" and
            // "arrived" are different events and the old line only ever reported the first.
            // A cross-map teleport is still pending here (GetZoneId lags), so read
            // IsBeingTeleported and the position, not the zone, to decide whether it took.
            bool const moved = bot->IsBeingTeleported() ||
                               bot->GetPositionX() != fromX || bot->GetPositionY() != fromY;

            std::ostringstream o;
            o << "BIASED TELEPORT " << bot->GetName() << " lvl " << (uint32)bot->GetLevel()
              << " from zone " << fromZone << " -> " << playerLocs.size()
              << " player-zone candidates -> ";
            if (!moved)
                o << "NO MOVE (every candidate vetoed downstream)";
            else if (bot->IsBeingTeleported())
                o << "teleport pending (cross-map)";
            else
                o << "zone " << bot->GetZoneId();
            F1Log(o.str());
            return;
        }

        F1Log(std::string("FALLBACK ") + bot->GetName() + " - no player-zone candidates, stock teleport");
    }

    sLog.outDetail("Preparing location to random teleporting bot %s for level %u", bot->GetName(), bot->GetLevel());
    RandomTeleport(bot, locsPerLevelCache[bot->GetLevel()], false, activeOnly);
    Refresh(bot);

    WorldPosition botPos(bot);

    ObjectGuid closestInn;
    float minDistance = -1.0f;
    for (auto& [innGuid, innPosition] : innCacheLevel[bot->getRace()][bot->GetLevel()])
    {
        float distance = botPos.sqDistance(innPosition);
        if (minDistance > 0 || distance >= minDistance)
            continue;

        minDistance = distance;
        closestInn = innGuid;
    }

    if (closestInn)
    {
        WorldPacket data(SMSG_TRAINER_BUY_SUCCEEDED, (8 + 4));
        data << closestInn;
        data << uint32(3286);                                   // Bind
        bot->GetSession()->SendPacket(data);
    }
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot)
{
    if (bot->InBattleGround())
        return;

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomTeleport");
    std::vector<WorldLocation> locs;

    std::list<Unit*> targets;
    float range = sPlayerbotAIConfig.randomBotTeleportDistance;
    MaNGOS::AnyUnitInObjectRangeCheck u_check(bot, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);

    if (!targets.empty())
    {
        for (std::list<Unit *>::iterator i = targets.begin(); i != targets.end(); ++i)
        {
            Unit* unit = *i;
            bot->SetPosition(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), 0);
            FleeManager manager(bot, sPlayerbotAIConfig.sightDistance, 0, true);
            float rx, ry, rz;
            if (manager.CalculateDestination(&rx, &ry, &rz))
            {
                WorldLocation loc(bot->GetMapId(), rx, ry, rz);
                locs.push_back(loc);
            }
        }
    }
    else
    {
        RandomTeleportForLevel(bot, true);
    }

    pmo.reset();

    Refresh(bot);
}

void RandomPlayerbotMgr::InstaRandomize(Player* bot)
{
    sRandomPlayerbotMgr.Randomize(bot);

    if(bot->GetLevel() > sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL))
        sRandomPlayerbotMgr.RandomTeleportForLevel(bot, false);
}

void RandomPlayerbotMgr::Randomize(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetSession()->isLogingOut())
        return;

    bool initialRandom = false;
    if (bot->GetLevel() <= sPlayerbotAIConfig.randombotStartingLevel)
        initialRandom = true;
#ifdef MANGOSBOT_TWO
    else if (bot->GetLevel() < 60 && bot->getClass() == CLASS_DEATH_KNIGHT)
        initialRandom = true;
#endif

    // give bot random level if is above or below level sync
    if (!initialRandom && players.size() && sPlayerbotAIConfig.syncLevelWithPlayers)
    {
        uint32 maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel + sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));
        if (bot->GetLevel() > maxLevel || (bot->GetLevel() + sPlayerbotAIConfig.syncLevelMaxAbove) < playersLevel)
            initialRandom = true;
    }

    if (initialRandom)
    {
        RandomizeFirst(bot);
        sLog.outDetail("Bot #%d %s:%d <%s>: gear/level randomised", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
    }
    else if (sPlayerbotAIConfig.randomGearUpgradeEnabled)
    {
        UpdateGearSpells(bot);
        sLog.outDetail("Bot #%d %s:%d <%s>: gear upgraded", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
    }
    else
    {
        // schedule randomise
        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
        SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);
    }

    //SetValue(bot, "version", MANGOSBOT_VERSION);
}

void RandomPlayerbotMgr::UpdateGearSpells(Player* bot)
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "UpgradeGear");

    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    uint32 lastLevel = GetValue(bot, "level");
    uint32 level = bot->GetLevel();
    PlayerbotFactory factory(bot, level);

    if (sPlayerbotAIConfig.disableRandomLevels)
    {
        // Randomize() exits early when DisableRandomLevels=1 (it would skip gear too).
        // Call UpgradeGear/InitGems directly so bots still get proper equipment.
        sLog.outBasic("Bot #%d <%s> lvl %d: UpdateGearSpells direct path (DisableRandomLevels=1)",
            bot->GetGUIDLow(), bot->GetName(), level);
        factory.UpgradeGearBest();
    }
    else
    {
        factory.Randomize(true, false);
    }

    if (lastLevel != level)
        SetValue(bot, "level", level);

    // schedule randomise
    uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
    SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);
}

void RandomPlayerbotMgr::RandomizeFirst(Player* bot)
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    // if lvl sync is enabled, max level is limited by online players lvl
    if (sPlayerbotAIConfig.syncLevelWithPlayers)
        maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel+ sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomizeFirst");
    uint32 level = urand(std::max(uint32(sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL)), sPlayerbotAIConfig.randomBotMinLevel), maxLevel);

#ifdef MANGOSBOT_TWO
    if (bot->getClass() == CLASS_DEATH_KNIGHT)
        level = urand(std::max(bot->GetLevel(), sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL)), std::max(sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL), maxLevel));
#endif

    if (urand(0, 100) < 100 * sPlayerbotAIConfig.randomBotMaxLevelChance && level < maxLevel)
        level = maxLevel;

#ifndef MANGOSBOT_ZERO
    if (sWorldState.GetExpansion() == EXPANSION_NONE && level > 60)
        level = 60;
#endif

#ifdef MANGOSBOT_TWO
    // do not allow level down death knights
    if (bot->getClass() == CLASS_DEATH_KNIGHT && level < sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL))
        return;

    // only randomise death knights to min lvl 60
    if (bot->getClass() == CLASS_DEATH_KNIGHT && level < 60)
        level = 60;
#endif

    if (level == sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL))
        return;

    SetValue(bot, "level", level);
    PlayerbotFactory factory(bot, level);
    factory.Randomize(false, false);

    // schedule randomise
    uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
    SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);

    bool hasPlayer = GetBotAI(bot)->HasRealPlayerMaster();
    GetBotAI(bot)->Reset(!hasPlayer);

    if (bot->GetGroup() && !hasPlayer)
        bot->RemoveFromGroup();
}

uint32 RandomPlayerbotMgr::GetZoneLevel(uint16 mapId, float teleX, float teleY, float teleZ)
{
	uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

	uint32 level;
    auto results = WorldDatabase.PQuery("SELECT AVG(t.level_min) minlevel, AVG(t.level_max) maxlevel FROM creature c "
            "INNER JOIN creature_template t ON c.id = t.entry "
            "WHERE map = '%u' AND t.level_min > 1 AND abs(position_x - '%f') < '%u' AND abs(position_y - '%f') < '%u'",
            mapId, teleX, sPlayerbotAIConfig.randomBotTeleportDistance / 2, teleY, sPlayerbotAIConfig.randomBotTeleportDistance / 2);

    if (results)
    {
        Field* fields = results->Fetch();
        uint8 minLevel = fields[0].GetUInt8();
        uint8 maxLevel = fields[1].GetUInt8();
        level = urand(minLevel, maxLevel);
        if (level > maxLevel)
            level = maxLevel;
    }
    else
    {
        level = urand(1, maxLevel);
    }

    return level;
}

void RandomPlayerbotMgr::Refresh(Player* bot)
{
    if (bot->IsBeingTeleportedFar() || !bot->IsInWorld())
        return;

    if (sServerFacade.UnitIsDead(bot))
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
        GetBotAI(bot)->ResetStrategies();
    }

    if (sPlayerbotAIConfig.disableRandomLevels)
        return;

    if (bot->InBattleGround())
        return;

    sLog.outDetail("Refreshing bot #%d <%s>", bot->GetGUIDLow(), bot->GetName());
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "Refresh");

    GetBotAI(bot)->Reset();

    bot->DurabilityRepairAll(false, 1.0f
#ifndef MANGOSBOT_ZERO
        , false
#endif
    );
	bot->SetHealthPercent(100);
	bot->SetPvP(true);

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();

    if (bot->GetMaxPower(POWER_MANA) > 0)
        bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));

    if (bot->GetMaxPower(POWER_ENERGY) > 0)
        bot->SetPower(POWER_ENERGY, bot->GetMaxPower(POWER_ENERGY));

    uint32 money = bot->GetMoney();
    bot->SetMoney(money + 500 * sqrt(urand(1, bot->GetLevel() * 5)));
}

bool RandomPlayerbotMgr::IsRandomBot(Player* bot)
{
    if (bot && GetBotAI(bot))
    {
        if (GetBotAI(bot)->IsRealPlayer())
            return false;
    }
    if (bot)
    {
        if (sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()))
            return true;

        return IsRandomBot(bot->GetGUIDLow());
    }

    return false;
}

bool RandomPlayerbotMgr::IsRandomBot(uint32 bot)
{
    ObjectGuid guid = ObjectGuid(HIGHGUID_PLAYER, bot);
    if (sPlayerbotAIConfig.IsInRandomAccountList(sObjectMgr.GetPlayerAccountIdByGUID(guid)))
        return true;

    return GetEventValue(bot, "add");
}

std::list<uint32> RandomPlayerbotMgr::GetBots()
{
    if (!currentBots.empty()) return currentBots;

    auto results = CharacterDatabase.Query(
            "SELECT bot FROM ai_playerbot_random_bots WHERE owner = 0 AND event = 'add'");

    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 bot = fields[0].GetUInt32();
            currentBots.push_back(bot);
        } while (results->NextRow());
    }

    return currentBots;
}

std::list<uint32> RandomPlayerbotMgr::GetBgBots(uint32 bracket)
{
    //if (!currentBgBots.empty()) return currentBgBots;

    auto results = CharacterDatabase.PQuery(
        "SELECT bot FROM ai_playerbot_random_bots WHERE event = 'bg' AND value = '%d'", bracket);
    std::list<uint32> BgBots;
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 bot = fields[0].GetUInt32();
            BgBots.push_back(bot);
        } while (results->NextRow());
    }

    return BgBots;
}

uint32 RandomPlayerbotMgr::GetEventValue(uint32 bot, std::string event)
{
    // load all events at once on first event load
    if (eventCache[bot].empty())
    {
        auto results = CharacterDatabase.PQuery("SELECT `event`, `value`, `time`, validIn, `data` FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", bot);
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                // defensive null-handling.
                // Penqle's Field::GetString() returns the bare pointer without null-check;
                // constructing std::string from nullptr is UB and segfaults. The bot's
                // ai_playerbot_random_bots table has `data` column nullable, and most rows
                // store NULL there. Guard against null so the cache load doesn't crash.
                const char* nameStr = fields[0].GetString();
                const char* dataStr = fields[4].GetString();
                std::string eventName = nameStr ? nameStr : "";
                if (eventName.empty())
                    continue; // skip malformed rows entirely
                CachedEvent e;
                e.value = fields[1].GetUInt32();
                e.lastChangeTime = fields[2].GetUInt32();
                e.validIn = fields[3].GetUInt32();
                e.data = dataStr ? std::string(dataStr) : std::string();
                eventCache[bot][eventName] = e;
            } while (results->NextRow());
        }
    }
    CachedEvent e = eventCache[bot][event];

    if ((time(0) - e.lastChangeTime) >= e.validIn && event != "specNo" && event != "specLink" && event != "init" && event != "current_time" && event != "always" && event != "selfbot")
        e.value = 0;

    return e.value;
}

int32 RandomPlayerbotMgr::GetValueValidTime(uint32 bot, std::string event)
{
    if (eventCache.find(bot) == eventCache.end())
        return 0;

    if (eventCache[bot].find(event) == eventCache[bot].end())
        return 0;

    CachedEvent e = eventCache[bot][event];

    return e.validIn-(time(0) - e.lastChangeTime);
}

std::string RandomPlayerbotMgr::GetEventData(uint32 bot, std::string event)
{
    std::string data = "";
    if (GetEventValue(bot, event))
    {
        CachedEvent e = eventCache[bot][event];
        data = e.data;
    }
    return data;
}

uint32 RandomPlayerbotMgr::SetEventValue(uint32 bot, std::string event, uint32 value, uint32 validIn, std::string data)
{
    CharacterDatabase.PExecute("DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u' AND event = '%s'",
            bot, event.c_str());
    if (value)
    {
        if (data != "")
        {
            CharacterDatabase.PExecute(
                "INSERT INTO ai_playerbot_random_bots (owner, bot, `time`, validIn, event, `value`, `data`) VALUES ('%u', '%u', '%u', '%u', '%s', '%u', '%s')",
                0, bot, (uint32)time(0), validIn, event.c_str(), value, data.c_str());
        }
        else
        {
            CharacterDatabase.PExecute(
                "INSERT INTO ai_playerbot_random_bots (owner, bot, `time`, validIn, event, `value`) VALUES ('%u', '%u', '%u', '%u', '%s', '%u')",
                0, bot, (uint32)time(0), validIn, event.c_str(), value);
        }
    }

    CachedEvent e(value, (uint32)time(0), validIn, data);
    eventCache[bot][event] = e;
    return value;
}

uint32 RandomPlayerbotMgr::GetValue(uint32 bot, std::string type)
{
    return GetEventValue(bot, type);
}

uint32 RandomPlayerbotMgr::GetValue(Player* bot, std::string type)
{
    return GetValue(bot->GetObjectGuid().GetCounter(), type);
}

std::string RandomPlayerbotMgr::GetData(uint32 bot, std::string type)
{
    return GetEventData(bot, type);
}

void RandomPlayerbotMgr::SetValue(uint32 bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    SetEventValue(bot, type, value, validIn == -1 ? 15*24*3600 : validIn, data);
}

void RandomPlayerbotMgr::SetValue(Player* bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    SetValue(bot->GetObjectGuid().GetCounter(), type, value, data, validIn);
}

bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.enabled)
    {
        sLog.outError("Playerbot system is currently disabled!");
        return false;
    }

    bool isRA = false;
    
    if (handler->GetSession()) //Client command
        isRA = true;
    else if (static_cast<CliHandler*>(handler) && static_cast<CliHandler*>(handler)->GetAccountId()) //RA call with account.
        isRA = true;

    if (!args || !*args)
    {
        sLog.outError("Usage: rndbot help/stats/update/reset/init/refresh/add/remove/more..");
        if (isRA)
            handler->SendSysMessage("Usage: rndbot help/stats/update/reset/init/refresh/add/remove/more..");

        std::list<std::string> messages = sRandomPlayerbotMgr.HandleHelp("");

        for (auto& msg : messages)
        {
            sLog.outString("%s", msg.c_str());
            if (isRA)
                handler->SendSysMessage(msg.c_str());
        }

        return true;
    }

    std::string cmd = args;

    // F2: travel parties, GM-gated here because the `.rndbot` chat command itself is
    // SEC_PLAYER in Chat.cpp - the generic handler tables below get no security context.
    if (cmd.find("travelparty") == 0)
    {
        // No session = console/RA, which is already privileged. ChatHandler::GetAccessLevel()
        // is protected, so read the security level off the session directly.
        if (handler->GetSession() && handler->GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
        {
            handler->SendSysMessage("You do not have permission to use this command.");
            return true;
        }

        if (!sPlayerbotAIConfig.travelParties)
        {
            handler->SendSysMessage("Travel parties are disabled (AiPlayerbot.TravelParties = 0).");
            return true;
        }

        std::string leaderName = sRandomPlayerbotMgr.SpawnTravelParty();
        if (leaderName.empty())
            handler->SendSysMessage("F2: could not form a travel party (no eligible bots, or one is already marching).");
        else
            handler->PSendSysMessage("F2: travel party marching. Leader is %s  ->  .appear %s",
                                     leaderName.c_str(), leaderName.c_str());
        return true;
    }

    std::map<std::string, ConsoleCommandHandler> handlers;
    handlers["help"] = &RandomPlayerbotMgr::HandleHelp;
    handlers["reset"] = &RandomPlayerbotMgr::HandleConsoleReset;
    handlers["stats"] = &RandomPlayerbotMgr::HandleConsoleStats;
    handlers["update"] = &RandomPlayerbotMgr::HandleConsoleUpdate;
    handlers["pid "] = &RandomPlayerbotMgr::HandleConsolePid;
    handlers["diff"] = &RandomPlayerbotMgr::HandleConsoleDiff;
    handlers["diff "] = &RandomPlayerbotMgr::HandleConsoleDiff;
    handlers["clean map"] = &RandomPlayerbotMgr::HandleConsoleCleanMap;
    handlers["login debug"] = &RandomPlayerbotMgr::HandleConsoleLoginDebug;

    for (auto& [prefix, consoleHandler] : handlers)
    {
        if (cmd.find(prefix) != 0)
            continue;

        size_t prefixLen = prefix.size();
        std::string param = cmd.size() > prefixLen + 1 ? cmd.substr(prefixLen + 1) : "";

        if (prefix == "stats")
            param = handler->GetSession() ? std::to_string(handler->GetSession()->GetPlayer()->GetObjectGuid()) : "";

        std::list<std::string> messages = (sRandomPlayerbotMgr.*consoleHandler)(param);
        for (auto& msg : messages)
        {
            sLog.outString("%s", msg.c_str());
            if(isRA)
                handler->SendSysMessage(msg.c_str());      
        }

        if (!messages.empty() && (prefix != "help" || param != "commands"))
            return true;
    }

    std::map<std::string, ConsolePlayerCommandHandler> playerHandlers;
    playerHandlers["init"] = &RandomPlayerbotMgr::HandleRandomizeFirst;
    playerHandlers["upgrade"] = &RandomPlayerbotMgr::HandleUpdateGearSpells;
    playerHandlers["refresh"] = &RandomPlayerbotMgr::HandleRefresh;
    playerHandlers["teleport"] = &RandomPlayerbotMgr::HandleRandomTeleportForLevel;
    playerHandlers["rpg"] = &RandomPlayerbotMgr::HandleRandomTeleportForRpg;
    playerHandlers["revive"] = &RandomPlayerbotMgr::HandleRevive;
    playerHandlers["grind"] = &RandomPlayerbotMgr::HandleRandomTeleport;
    playerHandlers["change_strategy"] = &RandomPlayerbotMgr::HandleChangeStrategy;
    playerHandlers["remove"] = &RandomPlayerbotMgr::HandleRemove;

    for (auto& [prefix, playerHandler] : playerHandlers)
    {
        if (cmd.find(prefix) != 0)
            continue;

        size_t prefixLen = prefix.size();
        std::string nameAndParams = cmd.size() > prefixLen + 1 ? cmd.substr(prefixLen + 1) : "";

        std::string name = "%";
        std::string params = "";

        if (!nameAndParams.empty())
        {
            size_t spacePos = nameAndParams.find(' ');
            if (spacePos != std::string::npos)
            {
                name = nameAndParams.substr(0, spacePos);
                params = nameAndParams.substr(spacePos + 1);
            }
            else
            {
                name = nameAndParams;
            }
        }

        sRandomPlayerbotMgr.consoleCmdParams = params;

        bool hasRandomBotCommand = false;

        ConsolePlayerCommandHandler handler_copy = playerHandler;

        sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* bot) {
            std::string botName = bot->GetName();
            if (botName.find(name) == 0)
            {

                std::list<std::string> messages = (sRandomPlayerbotMgr.*handler_copy)(bot);
                for (auto& msg : messages)
                {
                    sLog.outString("%s", msg.c_str());
                    if (isRA)
                        handler->SendSysMessage(msg.c_str());
                    hasRandomBotCommand = true;
                }
            }
        });

        if (hasRandomBotCommand)
            return true;
    }

    std::list<std::string> messages = sRandomPlayerbotMgr.HandlePlayerbotCommand(args, handler->GetSession() ? handler->GetSession()->GetPlayer():nullptr, static_cast<CliHandler*>(handler) ? static_cast<CliHandler*>(handler)->GetAccessLevel() : SEC_PLAYER);
    for (std::list<std::string>::iterator i = messages.begin(); i != messages.end(); ++i)
    {
        sLog.outString("%s", i->c_str());
        if (isRA)
            handler->SendSysMessage(i->c_str());
    }

    if (!messages.empty())
        return true;

    if (isRA)
        handler->SendSysMessage("usage: help/list/reload/more.. or add/init/remove/more.. PLAYERNAME");

    return true;
}

void RandomPlayerbotMgr::HandleCommand(uint32 type, const std::string& text, Player& fromPlayer, std::string channelName, Team team, uint32 lang, const std::string& to)
{
    ForEachPlayerbot([&](Player* bot)
    {
        if (type == CHAT_MSG_WHISPER && !to.empty() && bot->GetName() != to)
            return;

        if (type == CHAT_MSG_SAY)
        {
            if (bot->GetMapId() != fromPlayer.GetMapId() || sServerFacade.GetDistance2d(bot, &fromPlayer) > 25)
            {
                return;
            }
        }

        if (type == CHAT_MSG_YELL)
        {
            if (bot->GetMapId() != fromPlayer.GetMapId() || sServerFacade.GetDistance2d(bot, &fromPlayer) > 300)
            {
                return;
            }
        }

        if (type == CHAT_MSG_PARTY || type == CHAT_MSG_RAID)
        {
            if (!fromPlayer.IsInGroup(bot, true))
            {
                return;
            }
        }

        if (team != TEAM_BOTH_ALLOWED && bot->GetTeam() != team)
        {
            return;
        }

        if (type == CHAT_MSG_GUILD && bot->GetGuildId() != fromPlayer.GetGuildId())
        {
            return;
        }

        if (!channelName.empty())
        {
            if (ChannelMgr* cMgr = channelMgr(bot->GetTeam()))
            {
                Channel* chn = cMgr->GetChannel(channelName, bot);
                if (!chn)
                {
                    return;
                }
            }
        }

        GetBotAI(bot)->HandleCommand(type, text, fromPlayer, lang);
    });
}

void RandomPlayerbotMgr::OnPlayerLogout(Player* player)
{
    bool hadPlayerBot = GetPlayerBot(player->GetGUIDLow());

    DisablePlayerBot(player->GetGUIDLow());

    if (!hadPlayerBot && GetBotAI(player) && GetBotAI(player)->IsRealPlayer() && player->GetGroup() && sPlayerbotAIConfig.IsFreeAltBot(player))
        player->GetSession()->SetOffline(); //Prevent groupkick

    ForEachPlayerbot([&](Player* bot) {
        PlayerbotAI* ai = GetBotAI(bot);
        if (player == ai->GetMaster())
        {
            ai->SetMaster(NULL);
            if (!bot->InBattleGround())
            {
                ai->ResetStrategies();
            }
        }
    });

    players.erase(player->GetGUIDLow());
}

void RandomPlayerbotMgr::OnBotLoginInternal(Player * const bot)
{
    sLog.outDetail("%u/%d Bot %s logged in", GetPlayerbotsAmount(), sRandomPlayerbotMgr.GetMaxAllowedBotCount(), bot->GetName());
	//if (loginProgressBar && playerBots.size() < sRandomPlayerbotMgr.GetMaxAllowedBotCount()) { loginProgressBar->step(); }
	//if (loginProgressBar && playerBots.size() >= sRandomPlayerbotMgr.GetMaxAllowedBotCount() - 1) {
    //if (loginProgressBar && playerBots.size() + 1 >= sRandomPlayerbotMgr.GetMaxAllowedBotCount()) {
	//	sLog.outString("All bots logged in");
    //    delete loginProgressBar;
	//}
}

void RandomPlayerbotMgr::OnPlayerLogin(Player* player)
{
    if (!sPlayerbotAIConfig.enabled)
        return;

    ForEachPlayerbot([&](Player* bot)
    {
        if (player == bot)
            return;

        Group* group = bot->GetGroup();
        if (!group)
            return;

        for (GroupReference *gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->getSource();
            PlayerbotAI* ai = GetBotAI(bot);
            if (member == player && (!ai->GetMaster() || GetBotAI(ai->GetMaster())))
            {
                if (!bot->InBattleGround())
                {
                    ai->SetMaster(player);
                    ai->ResetStrategies();
                    ai->TellPlayer(ai->GetMaster(), BOT_TEXT("hello"));
                }
                break;
            }
        }
    });

    if (IsFreeBot(player))
    {
        uint32 guid = player->GetGUIDLow();
        if (!sPlayerbotAIConfig.IsFreeAltBot(player))
           SetEventValue(guid, "login", 0, 0);
    }
    else
    {
        players[player->GetGUIDLow()] = player;
        sLog.outDebug("Including non-random bot player %s into random bot update", player->GetName());
    }
}

void RandomPlayerbotMgr::OnPlayerLoginError(uint32 bot)
{
    SetEventValue(bot, "add", 0, 0);
    SetEventValue(bot, "login", 0, 0);
    currentBots.remove(bot);
}

Player* RandomPlayerbotMgr::GetRandomPlayer()
{
    if (players.empty())
        return NULL;

    uint32 index = urand(0, players.size() - 1);
    return players[index];
}

Player* RandomPlayerbotMgr::GetPlayer(uint32 playerGuid)
{
    PlayerBotMap::const_iterator it = players.find(playerGuid);
    return (it == players.end()) ? nullptr : it->second ? it->second : nullptr;
}

void RandomPlayerbotMgr::PrintStats(uint32 requesterGuid)
{
    Player* requester = GetPlayer(requesterGuid);
    std::stringstream ss; ss << GetPlayerbotsAmount() << " Random Bots online";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    std::map<uint32, int> alliance, horde;
    for (uint32 i = 0; i < 10; ++i)
    {
        alliance[i] = 0;
        horde[i] = 0;
    }

    std::map<uint8, int> perRace, perClass;
    for (uint8 race = RACE_HUMAN; race < MAX_RACES; ++race)
    {
        perRace[race] = 0;
    }
    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        perClass[cls] = 0;
    }

    uint32 dps = 0, heal = 0, tank = 0, active = 0, update = 0, randomize = 0, teleport = 0, changeStrategy = 0, dead = 0, combat = 0, revive = 0, taxi = 0, moving = 0, mounted = 0, afk = 0;
    int stateCount[(uint8)TravelState::MAX_TRAVEL_STATE + 1] = { 0 };
    std::vector<std::pair<Quest const*, int32>> questCount;

    ForEachPlayerbot([this, &dps, &heal, &tank, &active, &update, &randomize, &teleport, &changeStrategy, &dead, &combat, &revive, &taxi, &moving, &mounted, &afk, &alliance, &horde, &perRace, &perClass, &stateCount, &questCount](Player* bot)
    {
        if (IsAlliance(bot->getRace()))
            alliance[bot->GetLevel() / 10]++;
        else
            horde[bot->GetLevel() / 10]++;

        perRace[bot->getRace()]++;
        perClass[bot->getClass()]++;

        if (GetBotAI(bot)->AllowActivity())
            active++;

        if (GetBotAI(bot)->GetAiObjectContext()->GetValue<bool>("random bot update")->Get())
            update++;

        uint32 botId = bot->GetGUIDLow();
        if (!GetEventValue(botId, "randomize"))
            randomize++;

        if (!GetEventValue(botId, "teleport"))
            teleport++;

        if (!GetEventValue(botId, "change_strategy"))
            changeStrategy++;

        if (bot->IsTaxiFlying())
            taxi++;

        if (bot->IsMoving() && !bot->IsTaxiFlying() && !bot->IsFlying())
            moving++;

        if (bot->IsMounted() && !bot->IsTaxiFlying())
            mounted++;

        if (bot->IsInCombat())
            combat++;

        if (bot->isAFK())
            afk++;

        if (sServerFacade.UnitIsDead(bot))
        {
            dead++;
            //if (!GetEventValue(botId, "dead"))
            //    revive++;
        }

        int spec = AiFactory::GetPlayerSpecTab(bot);
        switch (bot->getClass())
        {
        case CLASS_DRUID:
            if (spec == 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_PALADIN:
            if (spec == 1)
                tank++;
            else if (spec == 0)
                heal++;
            else
                dps++;
            break;
        case CLASS_PRIEST:
            if (spec != 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_SHAMAN:
            if (spec == 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_WARRIOR:
            if (spec == 2)
                tank++;
            else
                dps++;
            break;
#ifdef MANGOSBOT_TWO
        case CLASS_DEATH_KNIGHT:
            if (spec == 0)
                tank++;
            else
                dps++;
            break;
#endif
        default:
            dps++;
            break;
        }

        TravelTarget* target = GetBotAI(bot)->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
        if (target)
        {
            TravelState state = target->GetTravelState();
            stateCount[(uint8)state]++;            
        }
    });

    ss.str(""); ss << "Bots level:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

	uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
	for (uint32 i = 0; i < 10; ++i)
    {
        if (!alliance[i] && !horde[i])
            continue;

        uint32 from = i*10;
        uint32 to = std::min(from + 9, maxLevel);
        if (!from) from = 1;

        ss.str(""); ss << "    " << from << ".." << to << ": " << alliance[i] << " alliance, " << horde[i] << " horde";
        sLog.outString("%s", ss.str().c_str());
        if (requester) { requester->SendMessageToPlayer(ss.str()); }
    }

    ss.str(""); ss << "Bots race:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    for (uint8 race = RACE_HUMAN; race < MAX_RACES; ++race)
    {
        if (perRace[race])
        {
            ss.str(""); ss << "    " << ChatHelper::formatRace(race) << ": " << perRace[race];
            sLog.outString("%s", ss.str().c_str());
            if (requester) { requester->SendMessageToPlayer(ss.str()); }
        }
    }

    ss.str(""); ss << "Bots class:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        if (perClass[cls])
        {
            ss.str(""); ss << "    " << ChatHelper::formatClass(cls) << ": " << perClass[cls];
            sLog.outString("%s", ss.str().c_str());
            if (requester) { requester->SendMessageToPlayer(ss.str()); }
        }
    }

    ss.str(""); ss << "Bots role:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    tank: " << tank << ", heal: " << heal << ", dps: " << dps;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "Bots status:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Active: " << active;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Moving: " << moving;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    //sLog.outString("Bots to:");
    //sLog.outString("    update: %d", update);
    //sLog.outString("    randomize: %d", randomize);
    //sLog.outString("    teleport: %d", teleport);
    //sLog.outString("    change_strategy: %d", changeStrategy);
    //sLog.outString("    revive: %d", revive);

    ss.str(""); ss << "    On taxi: " << taxi;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    On mount: " << mounted;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    In combat: " << combat;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Dead: " << dead;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    AFK: " << afk;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "Bots questing:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Picking quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_PICK_UP_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_PICK_UP_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Doing quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_DO_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_DO_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Completing quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_HAND_IN_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_HAND_IN_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Idling: " << stateCount[(uint8)TravelState::TRAVEL_STATE_IDLE];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }
}

double RandomPlayerbotMgr::GetBuyMultiplier(Player* bot)
{
    uint32 id = bot->GetGUIDLow();
    uint32 value = GetEventValue(id, "buymultiplier");
    if (!value)
    {
        value = urand(50, 120);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval, sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "buymultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

double RandomPlayerbotMgr::GetSellMultiplier(Player* bot)
{
    uint32 id = bot->GetGUIDLow();
    uint32 value = GetEventValue(id, "sellmultiplier");
    if (!value)
    {
        value = urand(80, 250);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval, sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "sellmultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

void RandomPlayerbotMgr::AddTradeDiscount(Player* bot, Player* master, int32 value)
{
    if (!master) return;
    uint32 discount = GetTradeDiscount(bot, master);
    int32 result = (int32)discount + value;
    discount = (result < 0 ? 0 : result);

    SetTradeDiscount(bot, master, discount);
}

void RandomPlayerbotMgr::SetTradeDiscount(Player* bot, Player* master, uint32 value)
{
    if (!master) return;
    uint32 botId =  bot->GetGUIDLow();
    uint32 masterId =  master->GetGUIDLow();
    std::ostringstream name; name << "trade_discount_" << masterId;
    SetEventValue(botId, name.str(), value, sPlayerbotAIConfig.maxRandomBotInWorldTime);
}

uint32 RandomPlayerbotMgr::GetTradeDiscount(Player* bot, Player* master)
{
    if (!master) return 0;
    uint32 botId =  bot->GetGUIDLow();
    uint32 masterId = master->GetGUIDLow();
    std::ostringstream name; name << "trade_discount_" << masterId;
    return GetEventValue(botId, name.str());
}

std::string RandomPlayerbotMgr::HandleRemoteCommand(std::string request)
{
    std::string::iterator pos = find(request.begin(), request.end(), ',');
    if (pos == request.end())
    {
        std::ostringstream out; out << "invalid request: " << request;
        return out.str();
    }

    std::string command = std::string(request.begin(), pos);
    uint32 guid = std::atoi(std::string(pos + 1, request.end()).c_str());
    Player* bot = GetPlayerBot(guid);
    if (!bot)
        return "invalid guid";

    PlayerbotAI *ai = GetBotAI(bot);
    if (!ai)
        return "invalid guid";

    return ai->HandleRemoteCommand(command);
}

void RandomPlayerbotMgr::ChangeStrategy(Player* player)
{
    uint32 bot = player->GetGUIDLow();

    if (urand(0, 100) > 100 * sPlayerbotAIConfig.randomBotRpgChance) // select grind / pvp
    {
        sLog.outDetail("Bot #%d %s:%d <%s>: sent to grind spot", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
        // teleport in different places only if players are online
        RandomTeleportForLevel(player, players.size());
        ScheduleTeleport(bot);
    }
    else
    {
        sLog.outDetail("Bot #%d %s:%d <%s>: sent to inn", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
        RandomTeleportForRpg(player, players.size());
        ScheduleTeleport(bot);
    }
}

void RandomPlayerbotMgr::RandomTeleportForRpg(Player* bot, bool activeOnly)
{
    uint32 race = bot->getRace();
    uint32 level = bot->GetLevel();

    // Some races (e.g. custom playable races) have no dedicated ai_playerbot_rpg_races
    // rows, or their low-level rows reference creature templates that were never
    // actually placed anywhere - leaving rpgLocsCacheLevel[race][level] empty and the
    // bot permanently stuck with no RPG/hearth destination. Fall back to the
    // race-neutral RPG cache, then to the general grind-spot cache, rather than failing.
    std::vector<WorldLocation>* locs = &rpgLocsCacheLevel[race][level];
    if (locs->empty())
        locs = &rpgLocsCacheLevel[0][level];
    if (locs->empty())
        locs = &locsPerLevelCache[level];

    sLog.outDetail("Random teleporting bot %s for RPG (%zu locations available)", bot->GetName(), locs->size());
    RandomTeleport(bot, *locs, true, activeOnly);
    Refresh(bot);

    //Travel cooldown for 10 minutes.
    if (GetBotAI(bot))
    {
        AiObjectContext* context = GetBotAI(bot)->GetAiObjectContext();
        TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

        sTravelMgr.SetNullTravelTarget(travelTarget);
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
        travelTarget->SetExpireIn(10 * MINUTE * IN_MILLISECONDS);
    }
}

void RandomPlayerbotMgr::Remove(Player* bot)
{
    SC_LOG("RandomPlayerbotMgr::Remove entry bot=%s",
           bot ? bot->GetName() : "(null)");
    uint32 owner = bot->GetGUIDLow();
    SC_LOG("RandomPlayerbotMgr::Remove guid=%u — deleting random_bots row", owner);
    CharacterDatabase.PExecute("DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%d'", owner);
    eventCache[owner].clear();
    SC_LOG("RandomPlayerbotMgr::Remove guid=%u — calling LogoutPlayerBot", owner);

    LogoutPlayerBot(owner);
    SC_LOG("RandomPlayerbotMgr::Remove guid=%u — DONE", owner);
}

const CreatureDataPair* RandomPlayerbotMgr::GetCreatureDataByEntry(uint32 entry)
{
    if (entry != 0 && sObjectMgr.GetCreatureTemplate(entry))
    {
        FindCreatureData worker(entry, NULL);
        sObjectMgr.DoCreatureData(worker);
        CreatureDataPair const* dataPair = worker.GetResult();
        return dataPair;
    }
    return NULL;
}

uint32 RandomPlayerbotMgr::GetCreatureGuidByEntry(uint32 entry)
{
    uint32 guid = 0;

    CreatureDataPair const* dataPair = sRandomPlayerbotMgr.GetCreatureDataByEntry(entry);
    guid = dataPair->first;

    return guid;
}

uint32 RandomPlayerbotMgr::GetBattleMasterEntry(Player* bot, BattleGroundTypeId bgTypeId, bool fake)
{
    Team team = bot->GetTeam();
    uint32 entry = 0;
    std::vector<uint32> Bms;

    for (auto i = begin(BattleMastersCache[team][bgTypeId]); i != end(BattleMastersCache[team][bgTypeId]); ++i)
    {
        Bms.insert(Bms.end(), *i);
    }

    for (auto i = begin(BattleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId]); i != end(BattleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId]); ++i)
    {
        Bms.insert(Bms.end(), *i);
    }

    if (Bms.empty())
        return entry;

    float dist1 = FLT_MAX;

    for (auto i = begin(Bms); i != end(Bms); ++i)
    {
        CreatureDataPair const* dataPair = sRandomPlayerbotMgr.GetCreatureDataByEntry(*i);
        if (!dataPair)
            continue;

        CreatureData const* data = &dataPair->second;

        Unit* Bm = sMapMgr.FindMap((uint32)data->position.mapid)->GetUnit(ObjectGuid(HIGHGUID_UNIT, *i, dataPair->first));
        if (!Bm)
            continue;

        if (bot->GetMapId() != Bm->GetMapId())
            continue;

        // return first available guid on map if queue from anywhere
        if (fake)
        {
            entry = *i;
            break;
        }

        AreaTableEntry const* area = GetAreaEntryByAreaID(sServerFacade.GetAreaId(Bm));
        if (!area)
            continue;

        if (area->team == 4 && bot->GetTeam() == ALLIANCE)
            continue;
        if (area->team == 2 && bot->GetTeam() == HORDE)
            continue;

        if (Bm->GetDeathState() == DEAD)
            continue;

        float dist2 = sServerFacade.GetDistance2d(bot, data->position.coord_x, data->position.coord_y);
        if (dist2 < dist1)
        {
            dist1 = dist2;
            entry = *i;
        }
    }

    return entry;
}

void RandomPlayerbotMgr::Hotfix(Player* bot, uint32 version)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    uint32 exp = bot->GetUInt32Value(PLAYER_XP);
    uint32 level = bot->GetLevel();
    uint32 id = bot->GetGUIDLow();

    for (int fix = version; fix <= MANGOSBOT_VERSION; fix++)
    {
        int count = 0;
        switch (fix)
        {
            case 1: // Apply class quests to previously made random bots

                if (level < 10)
                {
                    break;
                }

                for (std::list<uint32>::iterator i = factory.classQuestIds.begin(); i != factory.classQuestIds.end(); ++i)
                {
                    uint32 questId = *i;
                    Quest const *quest = sObjectMgr.GetQuestTemplate(questId);

                    if (!bot->SatisfyQuestClass(quest, false) ||
                        quest->GetMinLevel() > bot->GetLevel() ||
                        !bot->SatisfyQuestRace(quest, false) || bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
                        continue;

                    bot->SetQuestStatus(questId, QUEST_STATUS_COMPLETE);
                    bot->RewardQuest(quest, 0, bot, false);
                    bot->SetLevel(level);
                    bot->SetUInt32Value(PLAYER_XP, exp);
                    sLog.outDetail("Bot %d rewarded quest %d",
                        bot->GetGUIDLow(), questId);
                    count++;
                }

                if (count > 0)
                {
                    sLog.outDetail("Bot %d hotfix (Class Quests), %d quests rewarded",
                        bot->GetGUIDLow(), count);
                    count = 0;
                }
                break;
            case 2: // Init Riding skill fix

                if (level < 20)
                {
                    break;
                }
                factory.InitSkills();
                sLog.outDetail("Bot %d hotfix (Riding Skill) applied",
                    bot->GetGUIDLow());
                break;

            default:
                break;
        }
    }
    SetValue(bot, "version", MANGOSBOT_VERSION);
    sLog.outDetail("Bot %d hotfix v%d applied",
        bot->GetGUIDLow(), MANGOSBOT_VERSION);
}

void RandomPlayerbotMgr::MirrorAh()
{
    sRandomPlayerbotMgr.m_ahActionMutex.lock();

    ahMirror.clear();

    // Iterate all DBC auction house entries, deduplicating by object pointer
    // (cross-faction mode collapses all entries to one object).
    std::vector<AuctionHouseObject*> visited;
    for (uint32 i = 0; i < sAuctionHouseStore.GetNumRows(); ++i)
    {
        AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(i);
        if (!houseEntry)
            continue;

        AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(houseEntry);
        if (!auctionHouse)
            continue;
        if (std::find(visited.begin(), visited.end(), auctionHouse) != visited.end())
            continue;
        visited.push_back(auctionHouse);

        // Pure copy loop, no DB and no mail, so simply hold the auction lock
        // across it rather than snapshotting - otherwise the ahbot thread can
        // delete entries while we are copying them.
        AuctionHouseObject::Guard ahGuard(auctionHouse->GetLock());
        AuctionHouseObject::AuctionEntryMapBounds bounds = auctionHouse->GetAuctionsBounds_locked();

        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (!itr->second)
                continue;

            AuctionEntry auctionEntry = *itr->second;

            if (!auctionEntry.buyout)
                continue;

            if (!auctionEntry.itemCount)
                continue;

            ahMirror[auctionEntry.itemTemplate].push_back(auctionEntry);
        }
    }
    sRandomPlayerbotMgr.m_ahActionMutex.unlock();
}

typedef std::unordered_map <uint32, std::list<float>> botPerformanceMetric;
std::unordered_map<std::string, botPerformanceMetric> botPerformanceMetrics;

void RandomPlayerbotMgr::PushMetric(botPerformanceMetric& metric, const uint32 bot, const float value, uint32 maxNum) const
{
    metric[bot].push_back(value);

    if (metric[bot].size() > maxNum)
        metric[bot].pop_front();
}

float RandomPlayerbotMgr::GetMetricDelta(botPerformanceMetric& metric) const
{
    float deltaMetric = 0;
    for (auto& botMetric : metric)
    {
        std::list<float> values = botMetric.second;
        if (values.size() > 1)
            deltaMetric += (values.back() - values.front()) / values.size();
    }

    if (metric.empty())
        return 0;

    return deltaMetric / metric.size();
}

std::string RandomPlayerbotMgr::GetCommandTexts(const std::string& command)
{
    auto texts = GetCommandTexts();
    auto it = texts.find(command);
    if (it != texts.end())
        return it->second;
    return "";
}

std::unordered_map<std::string, std::string> RandomPlayerbotMgr::GetCommandTexts()
{
    return std::unordered_map<std::string, std::string>
    {
        {"init", "Randomize the first available bot.\nUsage: init"},
        {"upgrade", "Update gear and spells for all random bots.\nUsage: upgrade"},
        {"refresh", "Log out and log in all random bots to refresh their status.\nUsage: refresh"},
        {"teleport", "Teleport all random bots to a location suitable for their level.\nUsage: teleport"},
        {"rpg", "Teleport all random bots to a location for RPG activities.\nUsage: rpg"},
        {"revive", "Revive all dead random bots.\nUsage: revive"},
        {"grind", "Teleport all random bots to a grinding location.\nUsage: grind"},
        {"change_strategy", "Change the AI strategy for random bots.\nUsage: change_strategy <botname> <strategy>"},
        {"remove", "Remove a random bot from the server.\nUsage: remove <botname>"},
        {"reset", "Reset all random bots and clear event cache.\nUsage: reset"},
        {"diff", "Show server performance metrics.\nUsage: diff [player_diff] [empty_diff]"},
        {"stats", "Print bot statistics.\nUsage: stats"},
        {"update", "Trigger immediate bot AI update.\nUsage: update"},
        {"pid", "Adjust PID controller values.\nUsage: pid p i d"},
        {"clean map", "Unload and reload map files.\nUsage: clean map"},
        {"login debug", "Toggle login debug mode.\nUsage: login debug"},
        {"cmd", "Send command to a bot.\nUsage: cmd <botname> <command>"},
        {"help", "Show help for commands.\nUsage: help [command]"}
    };
}

std::list<std::string> RandomPlayerbotMgr::HandleHelp(std::string param)
{
    std::list<std::string> messages;
        
    if (param.empty())
    {
        messages.push_back("Type 'help commands for all available commands.");
        messages.push_back("Type 'help <command>' for more information on a specific command.");
        return messages;
    }

    if (param == "commands")
    {
        std::string commands = "Commands: ";
        for (auto& [command, help] : GetCommandTexts())
        {
            commands += command + ", ";
        }

        commands = commands.substr(0, commands.size() - 2);
        messages.push_back(commands);
        return messages;
    }
    
    
    std::string helpText = GetCommandTexts(param);
    if (!helpText.empty())
    {
        messages.push_back(helpText);
    }  
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomizeFirst(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomizeFirst(bot);
    messages.push_back("init applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleUpdateGearSpells(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    UpdateGearSpells(bot);
    messages.push_back("upgrade applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRefresh(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    Refresh(bot);
    messages.push_back("refresh applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleportForLevel(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomTeleportForLevel(bot);
    messages.push_back("teleport applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleportForRpg(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomTeleportForRpg(bot);
    messages.push_back("rpg applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRevive(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    Revive(bot);
    messages.push_back("revive applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleport(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomTeleport(bot);
    messages.push_back("grind applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleChangeStrategy(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    ChangeStrategy(bot);
    messages.push_back("change_strategy applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRemove(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    Remove(bot);
    messages.push_back("remove applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleReset(std::string param)
{
    std::list<std::string> messages;
    CharacterDatabase.PExecute("delete from ai_playerbot_random_bots where event not in ('temporary')");
    sRandomPlayerbotMgr.eventCache.clear();
    std::string msg = "Random bots were reset for all players. Please restart the Server.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleStats(std::string param)
{
    if (!Qualified::isValidNumberString(param))
    {
        return {"Stats: Error parsing " + param};
    }

    std::list<std::string> messages;
    std::string msg = "Stats requested.";
    messages.push_back(msg);

    ObjectGuid guid = ObjectGuid(uint64(std::stoull(param)));
    activatePrintStatsThread(guid);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleReload(std::string param)
{
    std::list<std::string> messages;
    sPlayerbotAIConfig.Initialize();
    std::string msg = "Playerbot config reloaded.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleUpdate(std::string param)
{
    std::list<std::string> messages;
    sRandomPlayerbotMgr.UpdateAIInternal(0);
    std::string msg = "Playerbot update triggered.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsolePid(std::string param)
{
    std::list<std::string> messages;
    std::string pids = param.substr(4);
    std::vector<std::string> pid = Qualified::getMultiQualifiers(pids, " ");

    if (pid.size() == 0)
        pid.push_back("0");
    if (pid.size() == 1)
        pid.push_back("0");
    if (pid.size() == 2)
        pid.push_back("0");
    sRandomPlayerbotMgr.pid.adjust(stof(pid[0]), stof(pid[1]), stof(pid[2]));

    std::string msg = "Pid set to p:" + std::to_string(stof(pid[0])) + " i:" + std::to_string(stof(pid[1])) + " d:" + std::to_string(stof(pid[2]));
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleDiff(std::string param)
{
    std::list<std::string> messages;
    if (param.empty())
    {
        std::stringstream ss;
        ss << "Avg diff: " << sWorld.GetAverageDiff() << "\n";
        ss << "Max diff: " << sWorld.GetMaxDiff() << "\n";
        ss << "char db ping: " << sRandomPlayerbotMgr.GetDatabaseDelay("CharacterDatabase") << "\n";
        ss << "Sessions online: " << sWorld.GetActiveSessionCount() << "\n";
        ss << "Bots online: " << sRandomPlayerbotMgr.botCount << " (active: " << sRandomPlayerbotMgr.activeBots << ")";

        messages.push_back(ss.str());
        return messages;
    }
    else if (param.find(" ") != std::string::npos)
    {
        std::vector<std::string> diff = Qualified::getMultiQualifiers(param, " ");
        if (diff.size() == 0)
            diff.push_back("100");
        if (diff.size() == 1)
            diff.push_back(diff[0]);
        sPlayerbotAIConfig.diffWithPlayer = stoi(diff[0]);
        sPlayerbotAIConfig.diffEmpty = stoi(diff[1]);

        std::string msg = "Diff set to " + std::to_string(stoi(diff[0])) + " (player), " + std::to_string(stoi(diff[1])) + " (empty)";
        messages.push_back(msg);
        return messages;
    }
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleCleanMap(std::string param)
{
    std::list<std::string> messages;
    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        if (!sMapStore.LookupEntry(i))
            continue;

        uint32 mapId = sMapStore.LookupEntry(i)->MapID;
        boost::thread t([mapId]() {WorldPosition::unloadMapAndVMaps(mapId); });
        t.detach();
    }

    std::string msg = "Map cleaning initiated.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleLoginDebug(std::string param)
{
    std::list<std::string> messages;
    sPlayerBotLoginMgr.ToggleDebug();
    std::string msg = "Login debug toggled.";
    messages.push_back(msg);
    return messages;
}

uint32 RandomPlayerbotMgr::GetOrCreateAccount(Player* master, std::string& error)
{
    uint32 maxCharsPerAccount = 9;
#ifdef MANGOSBOT_TWO
    maxCharsPerAccount = 10;
#endif

    auto accountNrQr = LoginDatabase.PQuery("SELECT max(replace(lower(username), lower('%s'), '') + 1 - 1) maxAccountNr FROM account WHERE replace(lower(username), lower('%s'), '') != 0", sPlayerbotAIConfig.randomBotAccountPrefix.c_str(), sPlayerbotAIConfig.randomBotAccountPrefix.c_str());

    if (!accountNrQr)
    {
        error = "Failed to find last " + sPlayerbotAIConfig.randomBotAccountPrefix + " account nr.";
        return 0;
    }

    Field* fields = accountNrQr->Fetch();
    // Solo-project fix: start at 0 (not randomBotAccountCount) so the lazy
    // creator reuses the same RNDBOT0..N pool that the startup loader
    // populates into randomBotAccounts. Upstream cmangos starts at
    // randomBotAccountCount, which puts manually-created bots at 200+
    // (outside the trusted-account list) and breaks `.rndbot add` for
    // non-admin users. The loop already skips occupied account numbers
    // (via `accountNumber++` at the bottom on full accounts), so starting
    // at 0 just walks forward to the first slot with room.
    uint32 accountNumber = 0;
    uint32 maxAccountNum = fields[0].GetUInt32();

    for (uint32 i = 0; i < 10000; i++)
    {
        std::ostringstream accountNameStr;
        accountNameStr << sPlayerbotAIConfig.randomBotAccountPrefix << accountNumber;
        std::string accountName = accountNameStr.str();

        uint32 accountId = sAccountMgr.GetId(accountName);

        if (!accountId)
        {
            std::string password;
            if (sPlayerbotAIConfig.randomBotRandomPassword)
            {
                for (int i = 0; i < 10; i++)
                    password += (char)urand('!', 'z');
            }
            else
                password = accountName;

            LoginDatabase.BeginTransaction();
#ifndef MANGOSBOT_ZERO
            uint8 max_expansion = MAX_EXPANSION;
            AccountOpResult result = sAccountMgr.CreateAccount(accountName, password, max_expansion);
#else
            AccountOpResult result = sAccountMgr.CreateAccount(accountName, password);
#endif
            LoginDatabase.CommitTransactionDirect();

            if (result == AOR_OK)
            {
                uint32 accountId = sAccountMgr.GetId(accountName);
                if (accountId)
                {
                    sPlayerbotAIConfig.randomBotAccounts.push_back(accountId);
                    return accountId;
                }
            }

            error = "Failed to create account";
            return 0;        
        }

        uint32 charCount = sAccountMgr.GetCharactersCount(accountId);

        if (charCount < maxCharsPerAccount)
        {
            if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
            {
                sPlayerbotAIConfig.randomBotAccounts.push_back(accountId);
            }
            return accountId;
        }

        accountNumber++;
    }

    error = "Failed to find a suitable account.";
    return 0;
}

void RandomPlayerbotMgr::OnBotDeleted(uint32 botGuid, uint32 accountId)
{
    if (accountId > 0 && sPlayerbotAIConfig.IsInRandomAccountList(accountId))
    {
        uint32 maxCharsPerAccount = 9;
    #ifdef MANGOSBOT_TWO
        maxCharsPerAccount = 10;
    #endif
    
        if (sAccountMgr.GetCharactersCount(accountId) == 0)
        {
            std::ostringstream prefix;
            prefix << sPlayerbotAIConfig.randomBotAccountPrefix;
            size_t prefixLen = prefix.str().length();
            
            auto result = LoginDatabase.PQuery("SELECT username FROM account WHERE id = '%u'", accountId);
            if (result)
            {
                std::string username = result->Fetch()[0].GetString();
                if (username.substr(0, prefixLen) == prefix.str())
                {
                    uint32 accountNum = std::stoul(username.substr(prefixLen));
                    if (accountNum >= sPlayerbotAIConfig.randomBotAccountCount)
                    {
                        sAccountMgr.DeleteAccount(accountId);
                        sLog.outString("Deleted empty random bot account: %s (id: %u)", username.c_str(), accountId);
                    }
                }
            }
        }
    }
}