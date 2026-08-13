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

#include "LocalTransport.h"
#include "TransportMgr.h"
#include "Map.h"
#include "Log.h"
#include "Timer.h"
#include <iterator>
#include <algorithm>
#include <cmath>

bool LocalTransport::Create(uint32 guidlow, uint32 name_id, Map* map, float x, float y, float z, float ang, float rotation0, float rotation1, float rotation2, float rotation3, uint32 animprogress, GOState go_state)
{
    if (!GameObject::Create(guidlow, name_id, map, x, y, z, ang, rotation0, rotation1, rotation2, rotation3, animprogress, go_state))
        return false;

    // GameObject::Create has recorded the spawn pose in m_stationaryPosition. That pose is what
    // every real client animates the model from, and the animation offsets are relative to it.
    _animation = sTransportMgr.GetTransportAnimInfo(name_id);
    if (!_animation)
        DETAIL_LOG("LocalTransport %u (%s) has no `transport_animation` rows; it will carry passengers at its spawn pose.", name_id, GetName());

    return true;
}

void LocalTransport::RemoveFromWorld()
{
    // Must not be left in the map's carry list with a dangling pointer.
    if (Map* map = FindMap())
        map->RemoveCarryingTransport(this);

    GameObject::RemoveFromWorld();
}

void LocalTransport::AddPassenger(WorldObject* passenger)
{
    GenericTransport::AddPassenger(passenger);

    if (_passengers.find(passenger) == _passengers.end())
        return;

    // A passenger picks its boarding point off the parked model (the only pose the collision model
    // and every grid search know about), so a boarding that happens while the mirror says we are
    // somewhere else means the phase is not calibrated for this entry yet. The ride itself is still
    // consistent - the offset below is measured against the same pose it will be re-applied to, so
    // the passenger does not jump - but it rides the animation's displacement from the wrong point.
    // This is the in-game calibration signal; see LocalTransport.h.
    float const offsetDist = GetAnimationOffsetDistance();
    if (offsetDist > 2.0f)
        DEBUG_LOG("LocalTransport %u (%s) boarded %s while its animation mirror is %.1f yards from the spawn pose - phase not calibrated?",
                  GetEntry(), GetName(), passenger->GetName(), offsetDist);

    // While anything is aboard we have to drag it every tick, wherever it has got to. The cell we
    // are registered in is only visited while a player happens to be near it, so the map drives us
    // directly instead. An entry with no animation never moves anybody, so it is not worth a tick.
    if (_animation && _animation->TotalTime)
        if (Map* map = FindMap())
            map->AddCarryingTransport(this);
}

void LocalTransport::RemovePassenger(WorldObject* passenger)
{
    GenericTransport::RemovePassenger(passenger);

    if (_passengers.empty())
        if (Map* map = FindMap())
            map->RemoveCarryingTransport(this);
}

void LocalTransport::MovePassengers()
{
    if (!IsInWorld() || _passengers.empty())
        return;

    UpdatePassengerPositions(_passengers);
}

void LocalTransport::UpdatePosition(float /*x*/, float /*y*/, float /*z*/, float /*o*/)
{
    // A LocalTransport is an ordinary grid gameobject: relocating it would leave it linked in the
    // cell it was spawned in, and would drag its collision model away from where clients draw it.
    // Passengers are carried through GetCarryPosition() instead.
    sLog.outError("[TRANSPORTS] LocalTransport %u (%s) cannot be moved with UpdatePosition; it is a grid object.", GetEntry(), GetName());
}

void LocalTransport::GetCarryPosition(float& x, float& y, float& z, float& o) const
{
    x = GetStationaryX();
    y = GetStationaryY();
    z = GetStationaryZ();
    o = GetStationaryO();

    if (!_animation || !_animation->TotalTime || _animation->Path.empty())
        return;

    float dx, dy, dz;
    ComputeOffsetAt(GetAnimationTime(), dx, dy, dz);

    // The animation offsets are in the object's local frame; rotate into world space by the spawn
    // orientation. This is the same transform generateTransportNodes uses.
    x += std::cos(o) * dx - std::sin(o) * dy;
    y += std::sin(o) * dx + std::cos(o) * dy;
    z += dz;
}

float LocalTransport::GetAnimationOffsetDistance() const
{
    if (!_animation || !_animation->TotalTime || _animation->Path.empty())
        return 0.0f;

    float dx, dy, dz;
    ComputeOffsetAt(GetAnimationTime(), dx, dy, dz);

    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

uint32 LocalTransport::GetAnimationTime() const
{
    // getMSTime() is the clock the client's own loop is seeded with (see LocalTransport.h), so the
    // mirror and the client stay in phase across restarts without any wall-clock anchoring.
    //
    // Both terms are reduced before they are added: getMSTime() + EpochOffset would wrap uint32
    // EpochOffset milliseconds *before* the tick counter itself does, putting the mirror at an
    // unrelated point in the loop - and dragging its passengers there - up to ~49.7 days earlier
    // than it has any reason to.
    //
    // The counter's own ~49.7-day wrap still moves the phase by (2^32 % TotalTime), because 2^32 is
    // not a whole number of loops. That one is inherent to riding a wrapping 32-bit counter and is
    // not fixable here: the client is seeded from this same counter, so which side jumps, and
    // whether they jump together, depends on client behaviour we cannot read. Verify it in the same
    // in-game pass that measures EpochOffset - a server up for more than 49 days is the case to
    // watch - rather than "fixing" it blind against a 64-bit clock no client ever sees.
    uint32 const period = _animation->TotalTime;

    return ((WorldTimer::getMSTime() % period) + (_animation->EpochOffset % period)) % period;
}

void LocalTransport::ComputeOffsetAt(uint32 msTime, float& dx, float& dy, float& dz) const
{
    // The keyframe at TotalTime closes the loop and msTime is always below it, so there is always
    // a keyframe strictly after msTime to interpolate towards.
    auto next = _animation->Path.upper_bound(msTime);
    if (next == _animation->Path.end())
        next = std::prev(next);

    auto prev = (next == _animation->Path.begin()) ? next : std::prev(next);

    uint32 span = (next->second->TimeSeg > prev->second->TimeSeg)
                ? next->second->TimeSeg - prev->second->TimeSeg
                : 1;
    // Signed, so that an msTime before the first keyframe clamps to 0 instead of wrapping around.
    float t = float(int64(msTime) - int64(prev->second->TimeSeg)) / float(span);
    t = std::min(std::max(t, 0.0f), 1.0f);

    dx = prev->second->X + (next->second->X - prev->second->X) * t;
    dy = prev->second->Y + (next->second->Y - prev->second->Y) * t;
    dz = prev->second->Z + (next->second->Z - prev->second->Z) * t;
}
