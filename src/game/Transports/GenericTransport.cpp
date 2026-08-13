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

#include "GenericTransport.h"
#include "Creature.h"
#include "Player.h"
#include "Log.h"

void GenericTransport::CleanupsBeforeDelete()
{
    // Nobody may be left holding a pointer to us.
    while (!_passengers.empty())
        RemovePassenger(*_passengers.begin());

    GameObject::CleanupsBeforeDelete();
}

void GenericTransport::AddPassenger(WorldObject* passenger)
{
    if (!IsInWorld())
        return;

    if (_passengers.insert(passenger).second)
    {
        DEBUG_LOG("Object %s added to transport %s.", passenger->GetName(), GetName());
        passenger->SetTransport(this);
        passenger->m_movementInfo.AddMovementFlag(MOVEFLAG_ONTRANSPORT);
        passenger->m_movementInfo.t_guid = GetObjectGuid();
        if (!passenger->m_movementInfo.t_pos.x)
        {
            passenger->m_movementInfo.t_pos.x = passenger->GetPositionX();
            passenger->m_movementInfo.t_pos.y = passenger->GetPositionY();
            passenger->m_movementInfo.t_pos.z = passenger->GetPositionZ();
            passenger->m_movementInfo.t_pos.o = passenger->GetOrientation();
            CalculatePassengerOffset(passenger->m_movementInfo.t_pos.x, passenger->m_movementInfo.t_pos.y, passenger->m_movementInfo.t_pos.z, &passenger->m_movementInfo.t_pos.o);
        }
    }
}

void GenericTransport::RemovePassenger(WorldObject* passenger)
{
    bool erased = false;
    if (_passengerTeleportItr != _passengers.end())
    {
        PassengerSet::iterator itr = _passengers.find(passenger);
        if (itr != _passengers.end())
        {
            if (itr == _passengerTeleportItr)
                ++_passengerTeleportItr;

            _passengers.erase(itr);
            erased = true;
        }
    }
    else
        erased = _passengers.erase(passenger) > 0;

    if (erased)
    {
        passenger->SetTransport(nullptr);
        passenger->m_movementInfo.ClearTransportData();
        DEBUG_LOG("Object %s removed from transport %s.", passenger->GetName(), GetName());
    }
}

void GenericTransport::UpdatePosition(float x, float y, float z, float o)
{
    Relocate(x, y, z, o);
    UpdateModelPosition();

    UpdatePassengerPositions(_passengers);
}

void GenericTransport::UpdatePassengerPositions(PassengerSet& passengers)
{
    for (const auto passenger : passengers)
        UpdatePassengerPosition(passenger);
}

void GenericTransport::UpdatePassengerPosition(WorldObject* passenger)
{
    // transport teleported but passenger not yet (can happen for players)
    if (!SharesMapWith(passenger))
        return;

    // Do not use Unit::UpdatePosition here, we don't want to remove auras
    // as if regular movement occurred
    float x, y, z, o;
    x = passenger->GetTransOffsetX();
    y = passenger->GetTransOffsetY();
    z = passenger->GetTransOffsetZ();
    o = passenger->GetTransOffsetO();
    CalculatePassengerPosition(x, y, z, &o);
    if (!MaNGOS::IsValidMapCoord(x, y, z))
    {
        sLog.outError("[TRANSPORTS] Object %s [guid %u] has invalid position on transport.", passenger->GetName(), passenger->GetGUIDLow());
        return;
    }
    switch (passenger->GetTypeId())
    {
        case TYPEID_UNIT:
        {
            Creature* creature = passenger->ToCreature();
            passenger->GetMap()->CreatureRelocation(creature, x, y, z, o);
            break;
        }
        case TYPEID_PLAYER:
            //relocate only passengers in world and skip any player that might be still logging in/teleporting
            if (passenger->IsInWorld())
                passenger->GetMap()->PlayerRelocation(passenger->ToPlayer(), x, y, z, o);

            break;
        case TYPEID_GAMEOBJECT:
            //passenger->GetMap()->GameObjectRelocation(passenger->ToGameObject(), x, y, z, o, false);
            break;
        case TYPEID_DYNAMICOBJECT:
            //passenger->GetMap()->DynamicObjectRelocation(passenger->ToDynObject(), x, y, z, o);
            break;
        default:
            break;
    }
}
