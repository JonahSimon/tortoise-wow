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

#ifndef TRANSPORTS_H
#define TRANSPORTS_H

#include "GenericTransport.h"
#include "TransportMgr.h"

struct CreatureData;

class Transport : public GenericTransport
{
        friend Transport* TransportMgr::CreateTransport(uint32, uint32);

        Transport();
    public:
        ~Transport() override;

        void AddToWorld() override;
        void RemoveFromWorld() override;

        bool Create(uint32 guidlow, uint32 entry, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress);

        void Update(uint32 update_diff, uint32 /*time_diff*/) override;

        void BuildUpdate(UpdateDataMapType& data_map);

        uint32 GetPathProgress() const { return _pathProgress; }
        uint32 GetPeriod() const { return GetUInt32Value(GAMEOBJECT_LEVEL); }
        void SetPeriod(uint32 period) { SetUInt32Value(GAMEOBJECT_LEVEL, period); }

        KeyFrameVec const& GetKeyFrames() const { return _transportInfo->keyFrames; }

        void UpdatePosition(float x, float y, float z, float o) override;

        TransportTemplate const* GetTransportTemplate() const { return _transportInfo; }

        void SendOutOfRangeUpdateToMap();
        void SendCreateUpdateToMap();
        void RemoveMapReference(Map* pMap) { m_maps.erase(pMap); }

        //! Helper to know if a stop frame was reached, ie. the vessel is docked.
        //! Public so callers can avoid stepping onto a vessel still under way.
        bool IsMoving() const override { return _isMoving; }

    protected:
        // An MO transport is added to every continent instance of its current map at once, so
        // "same map as the passenger" means "one of the maps we are on".
        bool SharesMapWith(WorldObject const* passenger) const override { return m_maps.find(passenger->FindMap()) != m_maps.end(); }

    private:
        void MoveToNextWaypoint();
        float CalculateSegmentPos(float perc);
        bool TeleportTransport(uint32 newMapid, float x, float y, float z, float o);
        void DoEventIfAny(KeyFrame const& node, bool departure);

        void SetMoving(bool val) { _isMoving = val; }

        TransportTemplate const* _transportInfo;

        KeyFrameVec::const_iterator _currentFrame;
        KeyFrameVec::const_iterator _nextFrame;
        ShortTimeTracker _positionChangeTimer;
        bool _isMoving;
        bool _pendingStop;

        uint32 _pathProgress;
        std::unordered_set<Map*> m_maps;
};

#endif
