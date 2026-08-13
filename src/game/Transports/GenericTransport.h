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

#ifndef GENERIC_TRANSPORT_H
#define GENERIC_TRANSPORT_H

#include "GameObject.h"
#include "MapManager.h"

// Anything that carries passengers around by moving itself. Two subclasses:
//
//   Transport      - GAMEOBJECT_TYPE_MO_TRANSPORT (15): boats and zeppelins. Moved by the
//                    server along a taxi path, broadcast to clients.
//   LocalTransport - GAMEOBJECT_TYPE_TRANSPORT (11): elevators, lifts and the Deeprun Tram
//                    cars. The client animates the model itself; the server mirrors that
//                    animation so passengers without a client (bots) can be carried too.
//
// Passenger book-keeping and the transport <-> world offset math live here, so both kinds of
// transport share one implementation and a caller only ever needs a GenericTransport*.
class GenericTransport : public GameObject
{
    public:
        typedef std::set<WorldObject*> PassengerSet;

        void CleanupsBeforeDelete() override;

        virtual void AddPassenger(WorldObject* passenger);
        // cmangos passes a 2nd bool (advised, ignored).
        void AddPassenger(WorldObject* passenger, bool /*advised*/) { AddPassenger(passenger); }
        virtual void RemovePassenger(WorldObject* passenger);
        PassengerSet const& GetPassengers() const { return _passengers; }

        // The pose passengers are carried at, i.e. the frame both offset transforms below are
        // relative to. An MO transport is not registered in a grid cell and really does move
        // itself, so its own position is that pose. A LocalTransport must stay parked where it is
        // linked into the grid and reports the pose the animation says it is at instead - see
        // LocalTransport.h for why. Anything that needs "where do I put a passenger" must go
        // through here rather than reading GetPosition* off a transport.
        virtual void GetCarryPosition(float& x, float& y, float& z, float& o) const
        {
            x = GetPositionX();
            y = GetPositionY();
            z = GetPositionZ();
            o = GetOrientation();
        }

        /// This method transforms supplied transport offsets into global coordinates
        void CalculatePassengerPosition(float& x, float& y, float& z, float* o = nullptr) const
        {
            float tx, ty, tz, to;
            GetCarryPosition(tx, ty, tz, to);
            CalculatePassengerPosition(x, y, z, o, tx, ty, tz, to);
        }

        /// This method transforms supplied global coordinates into local offsets
        void CalculatePassengerOffset(float& x, float& y, float& z, float* o = nullptr) const
        {
            float tx, ty, tz, to;
            GetCarryPosition(tx, ty, tz, to);
            CalculatePassengerOffset(x, y, z, o, tx, ty, tz, to);
        }

        static void CalculatePassengerPosition(float& x, float& y, float& z, float* o, float transX, float transY, float transZ, float transO)
        {
            float inx = x, iny = y, inz = z;
            if (o)
                *o = MapManager::NormalizeOrientation(transO + *o);

            x = transX + inx * std::cos(transO) - iny * std::sin(transO);
            y = transY + iny * std::cos(transO) + inx * std::sin(transO);
            z = transZ + inz;
        }

        static void CalculatePassengerOffset(float& x, float& y, float& z, float* o, float transX, float transY, float transZ, float transO)
        {
            if (o)
                *o = MapManager::NormalizeOrientation(*o - transO);

            z -= transZ;
            y -= transY;    // y = searchedY * std::cos(o) + searchedX * std::sin(o)
            x -= transX;    // x = searchedX * std::cos(o) + searchedY * std::sin(o + pi)
            float inx = x, iny = y;
            y = (iny - inx * std::tan(transO)) / (std::cos(transO) + std::sin(transO) * std::tan(transO));
            x = (inx + iny * std::tan(transO)) / (std::cos(transO) + std::sin(transO) * std::tan(transO));
        }

        void UpdatePassengerPosition(WorldObject* passenger);
        void UpdatePassengerPositions(PassengerSet& passengers);

        // Move the server's copy of the transport and drag its passengers along. Does not touch
        // GAMEOBJECT_POS_*, so it never generates an update packet.
        //
        // Only valid for a transport that is not linked into a grid cell (an MO transport lives in
        // Map::_transports, not in the grid): this relocates with bare WorldObject::Relocate, and
        // this core has no GameObject grid relocation, so a grid object moved this way would stay
        // linked in the cell it was spawned in - invisible to grid searches at its real position,
        // updated only while somebody stands near its old cell, and removed from the wrong cell by
        // Map::Remove. LocalTransport overrides this to refuse for exactly that reason.
        virtual void UpdatePosition(float x, float y, float z, float o);

    protected:
        GenericTransport() : GameObject(), _passengerTeleportItr(_passengers.begin()) {}

        // A passenger is only relocated while it shares a map with us. An MO transport spans
        // several continent instances at once and overrides this; a LocalTransport lives on
        // exactly one map.
        virtual bool SharesMapWith(WorldObject const* passenger) const { return passenger->FindMap() == GetMap(); }

        PassengerSet _passengers;
        PassengerSet::iterator _passengerTeleportItr;
};

#endif
