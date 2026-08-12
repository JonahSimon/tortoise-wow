#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/TravelNode.h"

namespace ai
{
    class LastMovement
    {
    public:
        LastMovement()
        {
            clear();
        }

        LastMovement(LastMovement& other)
        {
            taxiNodes = other.taxiNodes;
            taxiMaster = other.taxiMaster;
            lastFollow = other.lastFollow;
            lastAreaTrigger = other.lastAreaTrigger;
            lastTransportEntry = other.lastTransportEntry;
            lastPath = other.lastPath;
            lastMoveShort = other.lastMoveShort;
            nextTeleport = other.nextTeleport;
            fleeCount = other.fleeCount;
            lastFleeAttempt = other.lastFleeAttempt;
            taxiFailEntry = other.taxiFailEntry;
            taxiFailCount = other.taxiFailCount;
            moveEvent = Event();
        }

        void clear()
        {
            lastPath.clear();
            lastFollow = NULL;
            lastAreaTrigger = 0;
            lastTransportEntry = 0;
            lastFlee = 0;
            fleeCount = 0;
            lastFleeAttempt = 0;
            lastMoveShort = WorldPosition();
            nextTeleport = 0;
            taxiFailEntry = 0;
            taxiFailCount = 0;
            moveEvent = Event();
        }

        void Set(Unit* lastFollow)
        {
            setPath(TravelPath());
            this->lastFollow = lastFollow;
        }

        void setPath(TravelPath path) { lastPath = path; }
    public:
        std::vector<uint32> taxiNodes;
        ObjectGuid taxiMaster;
        Unit* lastFollow;
        uint32 lastAreaTrigger;
        uint32 lastTransportEntry;
        time_t lastFlee;
        // Number of flee actions dispatched in quick succession (within returnDelay of each
        // other). Used to detect a "subsequent" flee loop so spellcasting can take priority.
        uint32 fleeCount;
        // Wall-clock of the last dispatched flee, used to decide whether the next flee is
        // "subsequent" (close in time) or a fresh flee (window lapsed -> count resets).
        time_t lastFleeAttempt;
        TravelPath lastPath;
        WorldPosition lastMoveShort;
        time_t nextTeleport;
        // Taxi path id that MinimalMove is currently failing to board, and how many attempts it
        // has made. Boarding can fail deterministically (unknown taxi node, no flightmaster to
        // learn it from), so the retries are capped and the leg is then skipped instead of the
        // bot wedging on it forever. Reset once the leg is boarded or abandoned.
        uint32 taxiFailEntry;
        uint32 taxiFailCount;
        Event moveEvent;
    };

    class LastMovementValue : public ManualSetValue<LastMovement&>
    {
    public:
        LastMovementValue(PlayerbotAI* ai) : ManualSetValue<LastMovement&>(ai, data) {}
    private:
        LastMovement data = LastMovement();
    };

    class StayTimeValue : public ManualSetValue<time_t>
    {
    public:
        StayTimeValue(PlayerbotAI* ai) : ManualSetValue<time_t>(ai, 0) {}
    };

    class LastLongMoveValue : public CalculatedValue<WorldPosition>
    {
    public:
        LastLongMoveValue(PlayerbotAI* ai) : CalculatedValue<WorldPosition>(ai, "last long move", 30) {}

        WorldPosition Calculate() override;
    };


    class HomeBindValue : public CalculatedValue<WorldPosition>
    {
    public:
        HomeBindValue(PlayerbotAI* ai) : CalculatedValue<WorldPosition>(ai, "home bind", 30) {}

        WorldPosition Calculate() override;

        virtual std::string Format() override;
    };
}
