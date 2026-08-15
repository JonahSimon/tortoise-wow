/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "Transport.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Path.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "DBCStores.h"
#include "World.h"
#include "GameObjectAI.h"
#include "MapReference.h"
#include "Player.h"
#include "Cell.h"
#include "CellImpl.h"
#include "Totem.h"
#include "GameObjectModel.h"
#include "ObjectAccessor.h"

Transport::Transport() : GenericTransport(),
    _transportInfo(nullptr), _isMoving(true), _pendingStop(false),
    _pathProgress(0)
{
    // the path progress is the only value that seem to matter
    m_updateFlag = UPDATEFLAG_TRANSPORT;
}

Transport::~Transport()
{
    sObjectAccessor.RemoveObject(this);
    ASSERT(_passengers.empty());
    ASSERT(m_maps.empty());
}

void Transport::AddToWorld()
{
    if (m_model)
        GetMap()->InsertGameObjectModel(*m_model);

    Object::AddToWorld();

    // After Object::AddToWorld so that for initial state the GO is added to the world (and hence handled correctly)
    UpdateCollisionState();

    if (!i_AI)
        AIM_Initialize();
}

void Transport::RemoveFromWorld()
{
    RemoveAllDynObjects();

    if (m_model && GetMap()->ContainsGameObjectModel(*m_model))
        GetMap()->RemoveGameObjectModel(*m_model);

    Object::RemoveFromWorld();
}

bool Transport::Create(uint32 guidlow, uint32 entry, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress)
{
    Relocate(x, y, z, ang);

    if (!IsPositionValid())
    {
        sLog.outError("Transport (GUID: %u) not created. Suggested coordinates isn't valid (X: %f Y: %f)",
                      guidlow, x, y);
        return false;
    }

    Object::_Create(guidlow, 0, HIGHGUID_MO_TRANSPORT);

    GameObjectInfo const* goinfo = sObjectMgr.GetGameObjectInfo(entry);

    if (!goinfo)
    {
        sLog.outErrorDb("Transport not created: entry in `gameobject_template` not found, guidlow: %u map: %u  (X: %f Y: %f Z: %f) ang: %f", guidlow, mapid, x, y, z, ang);
        return false;
    }

    m_goInfo = goinfo;

    TransportTemplate const* tInfo = sTransportMgr.GetTransportTemplate(entry);
    if (!tInfo)
    {
        sLog.outErrorDb("Transport %u (name: %s) will not be created, missing `transport_template` entry.", entry, goinfo->name.c_str());
        return false;
    }

    _transportInfo = tInfo;

    // initialize waypoints
    _nextFrame = tInfo->keyFrames.begin();
    _currentFrame = _nextFrame++;

    _pathProgress = time(nullptr) % (tInfo->pathTime / 1000);
    _pathProgress *= 1000;
    SetObjectScale(goinfo->size);
    SetUInt32Value(GAMEOBJECT_FACTION, goinfo->faction);
    SetUInt32Value(GAMEOBJECT_FLAGS, goinfo->flags);
    SetPeriod(tInfo->pathTime);
    SetEntry(goinfo->id);
    SetDisplayId(goinfo->displayId);
    SetGoState(GO_STATE_READY);
    SetGoType(GAMEOBJECT_TYPE_MO_TRANSPORT);
    SetGoAnimProgress(animprogress);
    UpdateRotationFields(0.0f, 1.0f);

    sObjectAccessor.AddObject(this);
    return true;
}

void Transport::Update(uint32 update_diff, uint32 /*time_diff*/)
{
    uint32 const positionUpdateDelay = 50;

    if (AI())
        AI()->UpdateAI(update_diff);
    else
        AIM_Initialize();

    if (GetKeyFrames().size() <= 1)
        return;

    if (IsMoving() || !_pendingStop)
        _pathProgress = (_pathProgress + update_diff) % GetPeriod();

    // Set current waypoint
    // Desired outcome: _currentFrame->DepartureTime < _pathProgress < _nextFrame->ArriveTime
    // ... arrive | ... delay ... | departure
    //      event /         event /
    for (;;)
    {
        if (_pathProgress >= _currentFrame->ArriveTime && _pathProgress < _currentFrame->DepartureTime)
        {
            SetMoving(false);
            break;  // its a stop frame and we are waiting
        }

        // not waiting anymore
        SetMoving(true);

        if (_pathProgress >= _currentFrame->DepartureTime && _pathProgress < _currentFrame->NextArriveTime)
            break;  // found current waypoint

        MoveToNextWaypoint();

        DEBUG_LOG("Transport %u (%s) moved to node %u %u %f %f %f", GetEntry(), GetName(), _currentFrame->Node->index, _currentFrame->Node->mapid, _currentFrame->Node->x, _currentFrame->Node->y, _currentFrame->Node->z);

        // Departure event
        if (_currentFrame->IsTeleportFrame())
        {
            if (TeleportTransport(_nextFrame->Node->mapid, _nextFrame->Node->x, _nextFrame->Node->y, _nextFrame->Node->z, _nextFrame->InitialOrientation))
                return; // Update more in new map thread
        }
        else if (_currentFrame->IsUpdateFrame())
        {
            SendOutOfRangeUpdateToMap();
            SendCreateUpdateToMap();
        }
    }

    // Set position
    _positionChangeTimer.Update(update_diff);
    if (_positionChangeTimer.Passed())
    {
        _positionChangeTimer.Reset(positionUpdateDelay);
        if (IsMoving() && _pathProgress)
        {
            float t = CalculateSegmentPos(float(_pathProgress) * 0.001f);
            G3D::Vector3 pos, dir;
            _currentFrame->Spline->evaluate_percent(_currentFrame->Index, t, pos);
            _currentFrame->Spline->evaluate_derivative(_currentFrame->Index, t, dir);
            UpdatePosition(pos.x, pos.y, pos.z, atan2(dir.y, dir.x) + M_PI);
        }
    }
}

void Transport::UpdatePosition(float x, float y, float z, float o)
{
    Relocate(x, y, z, o);

    Map* pOldMap = GetMap();
    for (auto const& pMap : m_maps)
    {
        SetMap(pMap);
        UpdateModelPosition();
    }
    SetMap(pOldMap);

    UpdatePassengerPositions(_passengers);
}

void Transport::MoveToNextWaypoint()
{
    // Set frames
    _currentFrame = _nextFrame++;
    if (_nextFrame == GetKeyFrames().end())
        _nextFrame = GetKeyFrames().begin();
    if (_currentFrame == GetKeyFrames().begin())
        _pathProgress = 0;
}

float Transport::CalculateSegmentPos(float now)
{
    KeyFrame const& frame = *_currentFrame;
    const float speed = float(m_goInfo->moTransport.moveSpeed);
    const float accel = float(m_goInfo->moTransport.accelRate);
    float timeSinceStop = frame.TimeFrom + (now - (1.0f / IN_MILLISECONDS) * frame.DepartureTime);
    float timeUntilStop = frame.TimeTo - (now - (1.0f / IN_MILLISECONDS) * frame.DepartureTime);
    float segmentPos, dist;
    float accelTime = _transportInfo->accelTime;
    float accelDist = _transportInfo->accelDist;
    // calculate from nearest stop, less confusing calculation...
    if (timeSinceStop < timeUntilStop)
    {
        if (timeSinceStop < accelTime)
            dist = 0.5f * accel * timeSinceStop * timeSinceStop;
        else
            dist = accelDist + (timeSinceStop - accelTime) * speed;
        segmentPos = dist - frame.DistSinceStop;
    }
    else
    {
        if (timeUntilStop < _transportInfo->accelTime)
            dist = 0.5f * accel * timeUntilStop * timeUntilStop;
        else
            dist = accelDist + (timeUntilStop - accelTime) * speed;
        segmentPos = frame.DistUntilStop - dist;
    }

    return segmentPos / frame.NextDistFromPrev;
}

bool Transport::TeleportTransport(uint32 newMapid, float x, float y, float z, float o)
{
    bool const differentMap = newMapid != GetMapId();
    Map const* oldMap = GetMap();
    
    if (differentMap)
    {
        std::unordered_set<Map*> mapsCopy = m_maps;
        for (auto const& pMap : mapsCopy)
            pMap->Remove<Transport>(this, false);
        MANGOS_ASSERT(m_maps.empty());
    }

    uint32 newInstanceId = sMapMgr.GetContinentInstanceId(newMapid, x, y);

    for (_passengerTeleportItr = _passengers.begin(); _passengerTeleportItr != _passengers.end();)
    {
        WorldObject* obj = (*_passengerTeleportItr++);

        float destX, destY, destZ, destO;
        destX = obj->GetTransOffsetX();
        destY = obj->GetTransOffsetY();
        destZ = obj->GetTransOffsetZ();
        destO = obj->GetTransOffsetO();
        CalculatePassengerPosition(destX, destY, destZ, &destO, x, y, z, o);

        switch (obj->GetTypeId())
        {
            case TYPEID_UNIT:
                // Units teleport on transport not implemented.
                RemovePassenger(obj);
                break;
            case TYPEID_GAMEOBJECT:
            {
                MANGOS_ASSERT(false && "clients before wotlk do not support boarding gameobject on transport");
                break;
            }
            case TYPEID_PLAYER:
            {
                // Remove some auras to prevent undermap
                Player* player = obj->ToPlayer();
                if (!player->IsInWorld())
                {
                    RemovePassenger(player);
                    break;
                }

                if (!player->IsAlive())
                    player->ResurrectPlayer(1.0f);

                if (player->IsHardcore())
                    player->SetHCImmunityTimer(20);

                player->RemoveSpellsCausingAura(SPELL_AURA_MOD_CONFUSE);
                player->RemoveSpellsCausingAura(SPELL_AURA_MOD_FEAR);
                player->CombatStopWithPets(true);

                // No need for teleport packet if no map change
                // The client still shows the correct loading screen when one is needed (Grom'Gol-Undercity)
                if (newMapid == player->GetMapId())
                {
                    player->m_movementInfo.SetAsServerSide();
                    player->TeleportPositionRelocation(destX, destY, destZ, destO);
                    if (newInstanceId != player->GetInstanceId())
                        sMapMgr.ScheduleInstanceSwitch(player, newInstanceId);
                }
                else
                    player->TeleportTo(newMapid, destX, destY, destZ, destO,
                        TELE_TO_NOT_LEAVE_TRANSPORT);

                break;
            }
            case TYPEID_DYNAMICOBJECT:
                obj->AddObjectToRemoveList();
                break;
            default:
                break;
        }
    }

    Relocate(x, y, z, o);

    if (differentMap)
    {
        sMapMgr.GetOrCreateContinentInstances(newMapid, this, m_maps);
        for (auto const& pMap : m_maps)
            pMap->Add<Transport>(this);
    }

    // set the instance at these coordinates as the main one
    SetLocationInstanceId(newInstanceId);
    Map* newMap = sMapMgr.CreateMap(newMapid, this);
    SetMap(newMap);
    MANGOS_ASSERT(m_maps.find(newMap) != m_maps.end());

    return newMap != oldMap;
}

void Transport::DoEventIfAny(KeyFrame const& node, bool departure)
{
}

void Transport::BuildUpdate(UpdateDataMapType& data_map)
{
    Map::PlayerList const& players = GetMap()->GetPlayers();
    if (players.isEmpty())
        return;

    for (const auto& player : players)
        BuildUpdateDataForPlayer(player.getSource(), data_map);

    ClearUpdateMask(true);
}


void Transport::SendOutOfRangeUpdateToMap()
{
    for (auto const& pMap : m_maps)
    {
        Map::PlayerList const& players = pMap->GetPlayers();
        if (!players.isEmpty())
        {
            UpdateData data;
            BuildOutOfRangeUpdateBlock(&data);
            WorldPacket packet;
            data.BuildPacket(&packet);
            for (const auto& player : players)
                if (player.getSource()->GetTransport() != this)
                    player.getSource()->SendDirectMessage(&packet);
        }
    }
}

void Transport::SendCreateUpdateToMap()
{
    for (auto const& pMap : m_maps)
    {
        Map::PlayerList const& players = pMap->GetPlayers();
        if (!players.isEmpty())
        {
            for (const auto& player : players)
            {
                if (player.getSource()->GetTransport() != this)
                {
                    UpdateData data;
                    BuildCreateUpdateBlockForPlayer(&data, player.getSource());
                    data.Send(player.getSource()->GetSession());
                }
            }
        }
    }
}
