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
// The 1.12 client animates these models itself on a fixed loop and reports its own
// transport-relative position back, so the server has historically ignored them entirely. That is
// why anything without a client - a bot - can never ride one: nothing tells the server where the
// car is, and nothing carries the passenger along with it.
//
// LocalTransport mirrors that client-side loop server-side, interpolating the offset from the
// seeded keyframes in `transport_animation`.
//
// The mirror is *only* a passenger frame. The gameobject itself never moves:
//
//   * it stays linked in the grid cell it was spawned in. This core has no GameObject grid
//     relocation, so relocating it would leave it registered in the wrong cell - grid searches
//     would keep finding a tram car back at its station, Map::Remove would unlink it from a cell
//     that never held it (or bail out before deleting it), and ObjectUpdater would stop visiting
//     it as soon as nobody stood near the spawn cell.
//   * its collision model stays put, so ground height and line of sight for real players standing
//     on a lift keep coming from where the client draws the floor, exactly as before this class
//     existed. Moving the model would only be right while the mirror is both calibrated and being
//     updated, and it would freeze wherever the last rider left it.
//   * GAMEOBJECT_POS_* is untouched, so no update packet is ever generated and real clients keep
//     animating the model themselves, unaffected.
//
// Passengers are carried through GetCarryPosition(): a passenger's t_pos offset is measured
// against the animated pose and re-applied to it, so boarding never jumps the passenger and the
// ride is the animation's own displacement. Whether the animated pose agrees with what a client
// draws is the phase question below.
//
// Phase: the client's loop is seeded by the uint32 the server writes into a type-11 create block
// (Object::BuildMovementUpdate, UPDATEFLAG_TRANSPORT), which is WorldTimer::getMSTime(). That is
// why this mirror is keyed off getMSTime() as well and not off wall-clock time the way
// Transport::Create is: getMSTime() is the clock both ends share, and it is re-seeded per client
// on every create block, so it survives a server restart. TransportAnimation::EpochOffset then
// only has to absorb the difference between where our seeded keyframe table starts and where the
// client's own animation data starts - a per-entry constant that has to be measured in-game and is
// 0 until it is.
//
// Measuring it: park a GM next to a car, and at the instant it reaches the end that corresponds to
// keyframe TimeSeg t_end, log m = getMSTime() % TotalTime. The phase this class uses is
// (getMSTime() + EpochOffset) % TotalTime, so the value to store is not m itself but what makes
// that phase come out at t_end:
//
//     epoch_offset = (t_end + TotalTime - m) % TotalTime
//
// Storing the raw m instead only happens to be right when t_end is 0, and otherwise miscalibrates
// the entry by t_end.
class LocalTransport : public GenericTransport
{
    public:
        LocalTransport() : GenericTransport() {}

        bool Create(uint32 guidlow, uint32 name_id, Map* map, float x, float y, float z, float ang, float rotation0, float rotation1, float rotation2, float rotation3, uint32 animprogress, GOState go_state) override;

        void RemoveFromWorld() override;

        using GenericTransport::AddPassenger;   // keep the (passenger, advised) overload visible
        void AddPassenger(WorldObject* passenger) override;
        void RemovePassenger(WorldObject* passenger) override;

        // Where the animation says the object is right now: the spawn pose plus the interpolated
        // keyframe offset. Only ever used as a passenger frame - see the header comment.
        void GetCarryPosition(float& x, float& y, float& z, float& o) const override;

        // Refused: we are a grid object, and moving one is not something this core can do. See
        // GenericTransport::UpdatePosition.
        void UpdatePosition(float x, float y, float z, float o) override;

        // Drag the passengers to the pose the animation puts us at now. Driven once per map tick
        // from Map::UpdateCarryingTransports while anything is aboard, because the cell we are
        // registered in is only visited while a player happens to be near it - a bot riding a tram
        // car 2000 yards down the tunnel would otherwise freeze mid-ride.
        void MovePassengers();

        uint32 GetPeriod() const { return _animation ? _animation->TotalTime : 0; }

        // Distance from the spawn pose to where the animation says we are. A passenger boards on a
        // point picked off the parked model, so this is how far out of place that boarding point is
        // - the calibration signal for the phase (see the header comment).
        float GetAnimationOffsetDistance() const;

    private:
        // Interpolate the animation offset at `msTime` into the loop, in the object's local frame.
        void ComputeOffsetAt(uint32 msTime, float& dx, float& dy, float& dz) const;
        // Where in the loop the mirror is right now.
        uint32 GetAnimationTime() const;

        TransportAnimation const* _animation = nullptr;
};

#endif
