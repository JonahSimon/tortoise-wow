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
#include "Log.h"
#include "Timer.h"
#include <iterator>
#include <algorithm>

bool LocalTransport::Create(uint32 guidlow, uint32 name_id, Map* map, float x, float y, float z, float ang, float rotation0, float rotation1, float rotation2, float rotation3, uint32 animprogress, GOState go_state)
{
    if (!GameObject::Create(guidlow, name_id, map, x, y, z, ang, rotation0, rotation1, rotation2, rotation3, animprogress, go_state))
        return false;

    // The animation offsets are relative to the spawn pose, which never changes - GAMEOBJECT_POS_*
    // keeps holding it, so this is also the position every real client animates from.
    _stationary = Position(x, y, z, ang);

    _animation = sTransportMgr.GetTransportAnimInfo(name_id);
    if (!_animation)
        DETAIL_LOG("LocalTransport %u (%s) has no `transport_animation` rows; it will stay parked server-side.", name_id, GetName());

    return true;
}

void LocalTransport::Update(uint32 update_diff, uint32 time_diff)
{
    GameObject::Update(update_diff, time_diff);

    if (!_animation || !_animation->TotalTime)
        return;

    if (_passengers.empty())
        return;   // nothing to carry - do not burn cycles on an unobserved lift

    RefreshPosition();

    UpdatePassengerPositions(_passengers);
}

void LocalTransport::RefreshPosition()
{
    if (!_animation || !_animation->TotalTime)
        return;

    float x, y, z;
    ComputePositionAt((WorldTimer::getMSTime() + _animation->EpochOffset) % _animation->TotalTime, x, y, z);

    // Relocate() moves the server's copy without touching GAMEOBJECT_POS_*, so no update packet
    // is generated and real clients are unaffected. This mirrors Transport::UpdatePosition.
    Relocate(x, y, z, _stationary.o);
    UpdateModelPosition();
}

void LocalTransport::AddPassenger(WorldObject* passenger)
{
    // The boarding offset is measured against our position, so make sure it is the position the
    // animation says we are at right now and not wherever we were when the last rider left.
    RefreshPosition();

    GenericTransport::AddPassenger(passenger);
}

void LocalTransport::ComputePositionAt(uint32 msTime, float& x, float& y, float& z) const
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

    float dx = prev->second->X + (next->second->X - prev->second->X) * t;
    float dy = prev->second->Y + (next->second->Y - prev->second->Y) * t;
    float dz = prev->second->Z + (next->second->Z - prev->second->Z) * t;

    // The animation offsets are in the object's local frame; rotate into world space by the
    // spawn orientation. This is the same transform generateTransportNodes uses.
    x = _stationary.x + std::cos(_stationary.o) * dx - std::sin(_stationary.o) * dy;
    y = _stationary.y + std::sin(_stationary.o) * dx + std::cos(_stationary.o) * dy;
    z = _stationary.z + dz;
}
