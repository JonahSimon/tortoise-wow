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

#ifndef LOCAL_TRANSPORT_H
#define LOCAL_TRANSPORT_H

#include "GenericTransport.h"
#include "TransportMgr.h"

// GAMEOBJECT_TYPE_TRANSPORT (11): elevators, lifts and the Deeprun Tram cars.
//
// The 1.12 client animates these models locally on a fixed loop and reports its own
// transport-relative position back, so the server has historically ignored them entirely. That
// is why anything without a client - a bot - can never ride one: nothing tells the server where
// the car is, and nothing carries the passenger along with it.
//
// LocalTransport mirrors that client-side loop server-side, interpolating the object's position
// from the seeded keyframes in `transport_animation`.
//
// It deliberately never broadcasts its position: Relocate() does not touch GAMEOBJECT_POS_*, so
// no update packet is generated and real clients keep animating the model themselves, exactly as
// before. Only the server's internal notion of "where is the car" changes, which is all a bot
// needs.
class LocalTransport : public GenericTransport
{
    public:
        LocalTransport() : GenericTransport() {}

        bool Create(uint32 guidlow, uint32 name_id, Map* map, float x, float y, float z, float ang, float rotation0, float rotation1, float rotation2, float rotation3, uint32 animprogress, GOState go_state) override;

        void Update(uint32 update_diff, uint32 time_diff) override;

        using GenericTransport::AddPassenger;   // keep the (passenger, advised) overload visible
        void AddPassenger(WorldObject* passenger) override;

        uint32 GetPeriod() const { return _animation ? _animation->TotalTime : 0; }

        // Where the animation puts the object right now, whether or not it is being updated.
        // Used to place a passenger that boards while the lift is idle.
        void RefreshPosition();

    private:
        // Interpolate the animation offset at `msTime` into the loop and apply it to the
        // spawn (stationary) position.
        void ComputePositionAt(uint32 msTime, float& x, float& y, float& z) const;

        TransportAnimation const* _animation = nullptr;
        Position _stationary;   // spawn pose; the animation is relative to it
};

#endif
