# Bot World-Traversal Investigation

**Date:** 2026-08-12
**Branch:** `feature/oops`
**Method:** static analysis of `src/modules/PlayerBots/playerbot/` (vendored ike3 playerbots),
`src/game/Transports/`, `src/game/Maps/`, `src/game/Objects/`, and the shipped SQL seeds.
No runtime/log evidence was used — every claim below is traceable to a file and line.

**Scope:** every way a bot crosses the world — boats, zeppelins, elevators, the Deeprun Tram,
flight paths, instance portals / area triggers, static (spellcaster) portals, teleport spells and
hearthstone, and summons.

**Fix philosophy:** faithful transport use. Bots should genuinely board and ride, and genuinely
take flights, the way a player does. Where the current code teleports as a shortcut, the fix
below restores real riding rather than papering over it.

---

## TL;DR

| Mode | Graph data present? | Can a bot actually do it? | Root cause |
|---|---|---|---|
| Boats / ships | ✅ 233 transport links | ❌ never boards | D1 + D2 |
| Zeppelins | ✅ (same links) | ❌ never boards | D1 + D2 |
| Elevators / lifts | ✅ nodes + links shipped | ❌ never boards | D3 (and D2) |
| **Deeprun Tram** | ✅ 24 nodes, 12 ride links | ❌ **never boards** | **D3** (and D2) |
| Flight paths | ✅ 270 flightPath links | ⚠️ partially — fails near players | T1, T2, T3 |
| Instance portals / area triggers | ✅ 100 areaTrigger links | ✅ works | — |
| Static portals (spellcaster GOs) | ❌ **zero links in the shipped store** | ❌ never routed | P1 |
| Teleport spells / hearthstone | n/a — synthesised per-route | ✅ works | — |
| Summons (meeting stone / warlock) | n/a — event driven | ✅ works | — |

Three shared defects (**D1–D3**) account for every transport failure. They are independent of
the bot AI's decision-making: the routing graph is correct and the bots *choose* to use the tram,
the boat and the lift — they simply cannot execute the boarding step.

---

## Part 1 — The shared machinery

Understanding any one mode requires understanding the two layers all of them share.

### 1.1 The travel graph

Bots do long-distance routing over a node graph (`TravelNodeMap`) loaded from three world-DB
tables and *only* regenerated when those tables are empty
(`src/modules/PlayerBots/playerbot/TravelNode.cpp:3177`):

```cpp
void TravelNodeMap::generateAll()
{
    if (hasToGen || hasToFullGen)
        LoadMaps();

    if (hasToFullGen)
        generateNodes();
    ...
}
```

`hasToFullGen` is only set when `SELECT ... FROM ai_playerbot_travelnode` returns nothing
(`TravelNode.cpp:3446`). On this fork the tables are seeded from
`src/modules/PlayerBots/sql/world/classic/ai_playerbot_travel_nodes.sql`, so **the runtime
generators never run**. This matters a great deal, and is explained in D3 below.

Each link carries a type (`TravelNode.h:31`):

```cpp
enum class TravelNodePathType : uint8
{
    none = 0,
    walk = 1,
    areaTrigger = 2,
    transport = 3,
    flightPath = 4,
    teleportSpell = 5,
    staticPortal = 6
};
```

Link-type census of the shipped store (`ai_playerbot_travelnode_link`):

| type | meaning | rows |
|---|---|---|
| 1 | walk | 5597 |
| 2 | areaTrigger | 100 |
| 3 | transport | 233 |
| 4 | flightPath | 270 |
| 5 | teleportSpell | 0 |
| 6 | staticPortal | 0 |

### 1.2 From graph route to executable path

`TravelNodeRoute::buildPath` flattens a node route into a list of `PathNodePoint`s, tagging each
point with how the bot is meant to traverse it (`TravelNode.cpp:1288`):

```cpp
else if (nodePath->getPathType() == TravelNodePathType::transport) //Move onto transport
{
    travelPath.addPath(nodePath->getPath(), PathNodeType::NODE_TRANSPORT, nodePath->getPathObject());
}
else if (nodePath->getPathType() == TravelNodePathType::flightPath) //Use the flightpath
{
    travelPath.addPath(nodePath->getPath(), PathNodeType::NODE_FLIGHTPATH, nodePath->getPathObject());
}
```

`getPathObject()` is the transport's **GO entry** for a ride link, and `0` for the dock↔vehicle
hop that `makeDockNode` creates (`TravelNode.cpp:2375`):

```cpp
TravelNodePath travelPath(exitPos.distance(pos), 0.1f, (uint8)TravelNodePathType::transport, 0, true);
```

Non-walk points are excluded from ordinary pathing (`TravelNode.h:270`):

```cpp
bool isWalkable() const { return (uint8)type <= (uint8)PathNodeType::NODE_NODE; }
```

### 1.3 The two executors

Bots move through one of two code paths:

* **`MovementAction::MoveTo`** — used when a real player may be watching. Consults
  `TravelPath::UpcommingSpecialMovement` and dispatches on the leading point's type
  (`MovementActions.cpp:1571`).
* **`MovementAction::MinimalMove`** — used for unobserved random bots
  (`AiPlayerbot.EnableMinimalMove = 1` by default). Teleports along the path in hops
  (`MovementActions.cpp:506`).

Both funnel every transport point into the same function, `MovementAction::UseTransport`, and
every flight point into `MovementAction::UseTaxi`.

### 1.4 `TransportTeleportType`

`src/modules/PlayerBots/playerbot/PlayerbotAIConfig.cpp:308`:

```cpp
transportTeleportType = config.GetIntDefault("AiPlayerbot.TransportTeleportType", 2);
```

and the shipped documentation for it
(`src/modules/PlayerBots/playerbot/aiplayerbot.conf.dist.in:171`):

```
# Bot transport use style: 0 = walk on transports (currently not available), 1 = teleport on transports and ride it, 2 = teleport from dock to dock bypassing transport.
# AiPlayerbot.TransportTeleportType = 2
```

Upstream ships mode `2` — dock-to-dock teleport, bypassing the vehicle entirely — and marks
mode `0` (genuine walk-on) as *not available*. Restoring faithful riding therefore means fixing
D1–D3 **and** making mode `0`/`1` functional; the fixes below do both.

---

## Part 2 — The three shared defects

### D1 — `Map::GetTransports()` is a compat stub that returns nothing

`src/game/Maps/Map.h:466`:

```cpp
// GetTransports: cmangos has Map::GetTransports returning a set/vector. Stub returns empty vector.
// Note: GenericTransport is a typedef in shim; forward-decl as struct avoids "class" keyword conflict.
std::vector<class Transport*> GetTransports() const { return {}; }
```

Every boat and zeppelin lookup in the bot module goes through this stub, so it always finds
nothing. The real container exists and is correctly maintained — `Map::_transports`
(`src/game/Maps/Map.h:745`), inserted at `Map::Add(Transport*)` (`Map.cpp:497`) and erased at
`Map::Remove(Transport*, bool)` (`Map.cpp:1266`). The stub simply never reads it.

**Fix — return the real container.** `src/game/Maps/Map.h:466`:

```cpp
// Live MO-transports (boats/zeppelins) on this map. Used by the bot module to find a
// vehicle to board; previously a stub returning {} which made every boat unboardable.
std::vector<Transport*> GetTransports() const
{
    return std::vector<Transport*>(_transports.begin(), _transports.end());
}
```

`_transports` is `private:` and `GetTransports()` is declared in the `public:` block of the same
class, so no access change is needed. Note the member is a `std::set<Transport*>`
(`Map.h:744`), so the copy is required to satisfy the existing `std::vector` return type that the
bot module already compiles against.

### D2 — the elevator/tram fallback looks GOs up with a spawn id, not an `ObjectGuid`

`src/modules/PlayerBots/playerbot/WorldPosition.cpp:564`:

```cpp
std::set<GenericTransport*> WorldPosition::getTransports(uint32 entry)
{
    std::set<GenericTransport*> transports;
    for (auto transport : getMap(getFirstInstanceId())->GetTransports()) //Boats&Zeppelins.
        if (!entry || transport->GetEntry() == entry)
            transports.insert(transport);

    if (transports.empty() || !entry) //Elevators&rams
    {
        for (auto gopair : getGameObjectsNear(0.0f, entry))
            if (GameObject* go = getMap(getFirstInstanceId())->GetGameObject(gopair->first))
                if (GenericTransport* transport = dynamic_cast<GenericTransport*>(go))
                    transports.insert(transport);
    }

    return transports;
}
```

Two separate bugs in the fallback block:

**(a) The guid is wrong.** `gopair->first` is the *spawn id* — `GameObjectDataPair` is
`robin_hood::unordered_map<uint32, GameObjectData>::value_type` (`src/game/ObjectMgr.h:288`),
so `first` is a bare `uint32` dbGuid. `Map::GetGameObject` takes a full `ObjectGuid`
(`src/game/Maps/Map.h:541`), and `ObjectGuid` has an *implicit* converting constructor
(`src/game/ObjectGuid.h:128`):

```cpp
ObjectGuid(uint64 const& guid) : m_guid(guid) {}    // temporary allowed implicit cast, really bad in connection with operator uint64()
```

so the call compiles and silently builds a guid whose high-guid bits are `0` instead of
`HIGHGUID_GAMEOBJECT`. The object store lookup can never match. **This branch returns nothing,
always.**

**(b) It scans every gameobject spawn on the map.** `getGameObjectsNear(0.0f, entry)` passes
radius `0`, and radius `0` disables the distance filter entirely
(`WorldPosition.cpp:1275`):

```cpp
bool FindPointGameObjectData::operator()(GameObjectDataPair const& dataPair)
{
    if (!entry || dataPair.second.id == entry)
        if ((!point || dataPair.second.position.mapid == point.getMapId()) && (!radius || point.sqDistance(...) < radius * radius))
        {
            data.push_back(&dataPair);
        }

    return false;
}
```

`sObjectMgr.DoGOData(worker)` walks the whole `m_GameObjectDataMap` (`ObjectMgr.h:1234`). This
runs on every `UseTransport` call, for every bot waiting at a dock, every tick. With ~1000 bots
that is a meaningful CPU sink on its own, independent of the correctness bug.

**A radius bound is the wrong fix for (b), on both counts.** An earlier revision of this section
proposed passing `200.0f`. That was wrong twice over:

- *It does not reduce the walk.* Read the predicate above again: `DoGOData` iterates the entire
  `m_GameObjectDataMap` and the predicate always returns `false`, so it never breaks early. The
  radius only decides which spawns get pushed into the result vector. The scan costs exactly the
  same at `200.0f` as at `0.0f`; the bound buys nothing at all.
- *It silently breaks the tram, the very spawn this fix exists to locate.* Callers pass a **dock**
  position and pick the nearest match themselves afterwards, so the transport is routinely far
  from the query point. The six Deeprun Tram cars (`sql/base/tw_world_gameobject.sql`, guids
  18802-18807, entries 176080-176085) sit in two clusters on map 369 — three at y≈2472-2512 and
  three at y≈-11-28, ~2460y apart. A bot at one platform is >2400y from three of the cars, so a
  200y filter returns nothing where the unbounded scan at least located the spawn row.

Making this branch cheap requires an **entry-indexed lookup** into the GO spawn data (or caching
the per-map/per-entry result), not a distance filter. That is tracked separately as backlog
artifact `010-index-go-spawn-lookup-by-entry.md`; the fix below deliberately leaves the scan
unbounded and fixes only the guid bug and the missing null check.

**Fix — resolve through the live map, and leave the search unbounded.**

```cpp
std::set<GenericTransport*> WorldPosition::getTransports(uint32 entry)
{
    std::set<GenericTransport*> transports;

    Map* map = getMap(getFirstInstanceId());
    if (!map)
        return transports;

    for (auto transport : map->GetTransports()) //Boats&Zeppelins.
        if (!entry || transport->GetEntry() == entry)
            transports.insert(transport);

    if (transports.empty() || !entry) //Elevators&trams
    {
        // Deliberately unbounded. Callers pass a *dock* position and do their own
        // distance selection afterwards, so the transport itself is routinely far away:
        // the six Deeprun Tram cars sit in two clusters ~2460y apart on map 369, so any
        // radius small enough to be worth calling a bound hides half of them from the
        // dock the bot is standing on. A radius would not buy anything here either —
        // DoGOData walks the whole spawn map regardless (the predicate never breaks) and
        // the radius only prunes the result vector. Making this cheap needs an
        // entry-indexed lookup, not a distance filter.
        for (auto gopair : getGameObjectsNear(0.0f, entry))
        {
            // gopair->first is a spawn id, not an ObjectGuid — build the full guid before lookup.
            ObjectGuid guid(HIGHGUID_GAMEOBJECT, gopair->second.id, gopair->first);

            if (GameObject* go = map->GetGameObject(guid))
                if (GenericTransport* transport = dynamic_cast<GenericTransport*>(go))
                    transports.insert(transport);
        }
    }

    return transports;
}
```

`ObjectGuid(HighGuid, uint32 entry, uint32 counter)` is the three-argument constructor at
`src/game/ObjectGuid.h:129`, and `GameObjectData::id` is the GO entry.

Note that this alone does not fix elevators or the tram — see D3. It makes the lookup correct; the
per-tick full-map scan (b) is still there, and needs the entry index tracked in artifact 010.

### D3 — this core has no server-side transport type for elevators and trams

This is the reason the tram specifically is unusable, and it is a genuine core-architecture gap,
not a bug in the bot module.

The bot module was written against cmangos, where `GenericTransport` is a *base class* with two
subclasses — `Transport` (MO transports: boats, zeppelins) and an elevator/local transport for
GO type 11. This fork's compat shim collapses that hierarchy to a typedef
(`src/modules/PlayerBots/cmangos-compat-shim.h:31`):

```cpp
// cmangos's Transport class is called GenericTransport in WotLK builds and
// Transport in Classic. Penqle uses Transport. Provide both names.
class Transport;
typedef Transport GenericTransport;
```

But `Transport` here is *exclusively* the MO-transport class: it is instantiated only by
`TransportMgr::CreateTransport` (`src/game/Transports/Transport.h:30`) and hard-sets its own
type (`Transport.cpp:121`):

```cpp
SetGoType(GAMEOBJECT_TYPE_MO_TRANSPORT);
```

GO type 11 (`GAMEOBJECT_TYPE_TRANSPORT`) — every elevator and every tram car — is created as a
plain `GameObject` with nothing but two flags (`src/game/Objects/GameObject.cpp:247`):

```cpp
if (goinfo->type == GAMEOBJECT_TYPE_TRANSPORT)
    SetFlag(GAMEOBJECT_FLAGS, (GO_FLAG_TRANSPORT | GO_FLAG_NODESPAWN));
```

The consequences compound:

* `dynamic_cast<GenericTransport*>(go)` in `getTransports` can never succeed for a type-11 GO —
  even with D2 fixed.
* `GameObject::ToTransport()` returns null for type 11 (`GameObject.h:878`):
  ```cpp
  Transport* ToTransport() { if (GetGOInfo()->type == GAMEOBJECT_TYPE_MO_TRANSPORT) return reinterpret_cast<Transport*>(this); else return nullptr; }
  ```
* `Transport::AddPassenger` — the only mechanism that sets `MOVEFLAG_ONTRANSPORT`, `t_guid` and
  the passenger offset (`Transport.cpp:206`) — has no type-11 equivalent.
* Nothing moves type-11 GOs server-side. There is no `Update` override and no path data. The
  tram cars sit at their spawn coordinates forever (`sql/base/tw_world_gameobject.sql`):

  ```sql
  (18802,176080,369,-45.3934,2472.93,6.90526,1.5708,...)
  (18803,176081,369,4.52807,8.43529,6.90526,1.5708,...)
  (18804,176082,369,-45.4005,2492.79,6.90526,1.5708,...)
  (18805,176083,369,-45.4007,2512.15,6.90526,1.5708,...)
  (18806,176084,369,4.58065,28.2097,6.90526,1.5708,...)
  (18807,176085,369,4.49883,-11.3475,6.90526,1.5708,...)
  ```

  Six cars, each parked permanently at one end of its run. Real players ride the tram because the
  **client** animates the model locally and reports its own position back with the transport guid
  attached (`src/game/Handlers/MovementHandler.cpp:1098`):

  ```cpp
  if (movementInfo.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
  {
      ...
      if (!pPlayerMover->GetTransport())
      {
          if (Transport* t = pPlayerMover->GetMap()->GetTransport(movementInfo.GetTransportGuid()))
          {
              t->AddPassenger(pPlayerMover);
  ```

  A bot has no client, so there is nothing to tell the server where the car is or to carry the
  bot along with it.

* The runtime node generator for elevators/trams is also dead. It depends on transport animation
  data that this core does not ship (`src/game/Transports/TransportMgr.h:93`):

  ```cpp
  // bot calls GetTransportAnimInfo for elevator pathing.
  // Penqle has no TransportAnim.dbc; stub returns nullptr.
  TransportAnimation const* GetTransportAnimInfo(uint32 /*entry*/) const { return nullptr; }
  ```

  and `generateTransportNodes` bails immediately for the whole elevator/tram branch
  (`TravelNode.cpp:2410`):

  ```cpp
  //Elevators/Trams
  if (path.empty())
  {
      if (animation)      // <-- always nullptr on this fork
      {
          TransportPathContainer aPath = animation->Path;
          ...
  ```

  This is currently masked because the node store is seeded from SQL and never regenerated
  (§1.1) — **but it is a live landmine**: any operation that empties
  `ai_playerbot_travelnode` (a wipe, a reimport, `hasToFullGen`) permanently deletes every
  elevator and tram node from the graph, with no way to regenerate them on this core.

**Fix — reinstate the real class hierarchy and give type-11 transports server-side motion.**

This is the largest change in this document and it is unavoidable if bots are to ride trams and
lifts faithfully. Four pieces:

**(i) Split a `GenericTransport` base out of `Transport`.** Move passenger book-keeping and the
offset math up; leave waypoint/keyframe motion in `Transport`.
`src/game/Transports/GenericTransport.h` (new):

```cpp
class GenericTransport : public GameObject
{
    public:
        typedef std::set<WorldObject*> PassengerSet;

        virtual void AddPassenger(WorldObject* passenger);
        // cmangos passes a 2nd bool (advised, ignored).
        void AddPassenger(WorldObject* passenger, bool /*advised*/) { AddPassenger(passenger); }
        virtual void RemovePassenger(WorldObject* passenger);
        PassengerSet const& GetPassengers() const { return _passengers; }

        void CalculatePassengerPosition(float& x, float& y, float& z, float* o = nullptr) const;
        void CalculatePassengerOffset(float& x, float& y, float& z, float* o = nullptr) const;

        static void CalculatePassengerPosition(float& x, float& y, float& z, float* o,
                                               float transX, float transY, float transZ, float transO);
        static void CalculatePassengerOffset(float& x, float& y, float& z, float* o,
                                             float transX, float transY, float transZ, float transO);

        void UpdatePassengerPosition(WorldObject* object);
        void UpdatePassengerPositions(PassengerSet& passengers);

    protected:
        PassengerSet _passengers;
        PassengerSet::iterator _passengerTeleportItr;
};

class Transport : public GenericTransport { /* existing MO-transport body, minus the moved members */ };
```

The bodies move verbatim from `src/game/Transports/Transport.h:48-99` and
`Transport.cpp:206-252, 408-455` — no behavioural change for boats.

**(ii) Delete the shim typedef.** `src/modules/PlayerBots/cmangos-compat-shim.h:33`:

```cpp
// GenericTransport is now a real base class (Transports/GenericTransport.h), matching cmangos:
//   GenericTransport -> Transport (MO transports) and LocalTransport (GO type 11).
// The old `typedef Transport GenericTransport` made every dynamic_cast in the bot module fail
// for elevators and trams.
class GenericTransport;
```

**(iii) Add `LocalTransport` for GO type 11.**
`src/game/Transports/LocalTransport.h` (new):

```cpp
// GAMEOBJECT_TYPE_TRANSPORT: elevators, lifts and the Deeprun Tram cars.
// The 1.12 client animates these models locally on a fixed loop. The server has historically
// ignored them entirely, which is why bots (who have no client) can never ride them.
// LocalTransport mirrors that loop server-side so passengers can be carried.
//
// It deliberately never broadcasts its position: real clients keep animating the model
// themselves, exactly as before. Only the server's internal notion of "where is the car"
// changes, which is all a bot needs.
class LocalTransport : public GenericTransport
{
    public:
        bool Create(uint32 guidlow, uint32 entry, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress);

        void Update(uint32 update_diff, uint32 time_diff) override;

        uint32 GetPeriod() const { return _animation ? _animation->TotalTime : 0; }

    private:
        // Interpolate the animation offset at `msTime` into the loop and apply it to the
        // spawn (stationary) position.
        void ComputePositionAt(uint32 msTime, float& x, float& y, float& z) const;

        TransportAnimation const* _animation = nullptr;
        Position                  _stationary;         // spawn pose; the animation is relative to it
};
```

`src/game/Transports/LocalTransport.cpp` (new), the motion core:

```cpp
void LocalTransport::Update(uint32 /*update_diff*/, uint32 /*time_diff*/)
{
    if (!_animation || !_animation->TotalTime)
        return;

    if (_passengers.empty())
        return;   // nothing to carry — do not burn cycles on an unobserved lift

    float x, y, z;
    ComputePositionAt(WorldTimer::getMSTime() % _animation->TotalTime, x, y, z);

    // Relocate() moves the server's copy without touching GAMEOBJECT_POS_*, so no update
    // packet is generated and real clients are unaffected. This mirrors Transport::UpdatePosition.
    Relocate(x, y, z, GetOrientation());
    UpdateModelPosition();

    UpdatePassengerPositions(_passengers);
}

void LocalTransport::ComputePositionAt(uint32 msTime, float& x, float& y, float& z) const
{
    auto next = _animation->Path.lower_bound(msTime);
    if (next == _animation->Path.end())
        next = _animation->Path.begin();

    auto prev = (next == _animation->Path.begin()) ? std::prev(_animation->Path.end()) : std::prev(next);

    uint32 span = (next->second->TimeSeg > prev->second->TimeSeg)
                ? next->second->TimeSeg - prev->second->TimeSeg
                : 1;
    float t = float(msTime - prev->second->TimeSeg) / float(span);
    t = std::min(std::max(t, 0.0f), 1.0f);

    float dx = prev->second->X + (next->second->X - prev->second->X) * t;
    float dy = prev->second->Y + (next->second->Y - prev->second->Y) * t;
    float dz = prev->second->Z + (next->second->Z - prev->second->Z) * t;

    // The animation offsets are in the object's local frame; rotate into world space by the
    // spawn orientation. This is the same transform generateTransportNodes uses (TravelNode.cpp:2427).
    x = _stationary.x + std::cos(_stationary.o) * dx - std::sin(_stationary.o) * dy;
    y = _stationary.y + std::sin(_stationary.o) * dx + std::cos(_stationary.o) * dy;
    z = _stationary.z + dz;
}
```

Instantiate it alongside `GameObject` at the single spawn site — `GameObject::Create` is called
from `Map::LoadGameObjectSpawn` / `ObjectMgr` GO loading; the type switch belongs there:

```cpp
GameObjectInfo const* info = ObjectMgr::GetGameObjectInfo(entry);
GameObject* pGameObject = (info && info->type == GAMEOBJECT_TYPE_TRANSPORT)
                        ? new LocalTransport()
                        : new GameObject();
```

**(iv) Supply the animation data.** `GetTransportAnimInfo` must stop returning `nullptr`.
`TransportAnim.dbc` does not exist in 1.12 client data, so seed a world table instead —
the same shape the stub struct already declares
(`src/modules/PlayerBots/cmangos-compat-shim.h:732`):

```sql
CREATE TABLE IF NOT EXISTS `transport_animation` (
  `entry`      mediumint(8) unsigned NOT NULL,  -- gameobject_template.entry (type 11)
  `time_seg`   int(10) unsigned      NOT NULL,  -- ms into the loop
  `x`          float NOT NULL,                  -- offset from the spawn pose, local frame
  `y`          float NOT NULL,
  `z`          float NOT NULL,
  PRIMARY KEY (`entry`,`time_seg`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 COMMENT='Client-side transport model animation, server mirror';
```

with a loader in `TransportMgr` replacing the stub (`src/game/Transports/TransportMgr.h:93`):

```cpp
TransportAnimation const* GetTransportAnimInfo(uint32 entry) const
{
    auto itr = _transportAnimations.find(entry);
    return itr != _transportAnimations.end() ? &itr->second : nullptr;
}
```

**The seed data already exists in this repo.** `ai_playerbot_travelnode_path` holds the sampled
motion of every tram car, in world coordinates on map 369, generated on a cmangos build that
*did* have `TransportAnim.dbc`. For tram car `176084` (link node 1370 → 1373) there are 28
points; subtracting the spawn pose gives the local-frame offsets, and the link's `extra_cost`
gives the one-way duration:

```sql
(1370, 1373, 3, 176084, 0.1, 0, 71.667, 1, 0, 0, 0),   -- 71.667s one way
(1370, 1373, 0, 369, -45.3934, 2472.93, 6.9886),
...
(1370, 1373, 26, 369, -45.3842, -10.3899, 7.17514),
(1370, 1373, 27, 369, -45.3842, -10.3899, 7.17514),
```

so `TotalTime` for a tram car ≈ 143,000 ms (two ~71.7s legs).

> **Calibration caveat, stated plainly.** The offsets and the period are recoverable from the
> data above, but the *phase* — where in the loop the client thinks the car is at server time
> `T` — is not derivable statically. It must be measured in-game once (park a GM next to a car,
> log `WorldTimer::getMSTime() % TotalTime` at the moment the car reaches a known end) and baked
> in as a per-entry epoch offset. Until that is done, bots will ride a car that is out of phase
> with what players see. This is the one part of the fix that cannot be completed from static
> analysis, and it should be the first thing verified after the code lands.

---

## Part 3 — Mode by mode

### 3.1 Boats and ships

**How it works today.** Boats are true MO transports. The world DB defines 13 of them
(`sql/base/tw_world_transports.sql`) — e.g. `(3,176231,'Menethil Harbor and Theramore Isle',329313)` —
and the core moves them and carries their passengers correctly
(`Transport.cpp:254`, `Transport.cpp:408`).

The graph models each route as: *walk to dock node* → `transport` link (object `0`) onto the
vessel → `transport` link (object = GO entry) across the water → `transport` link (object `0`)
back to the arrival dock. `generateTransportNodes` builds this
(`TravelNode.cpp:2531`):

```cpp
else //Boats/Zepelins
{
    //Loop over the path and connect stop locations.
    for (auto& p : path)
    {
        WorldPosition pos = WorldPosition(p->mapid, p->x, p->y, p->z, 0);
        ...
        if (p->delay > 0)
        {
            TravelNode* node = sTravelNodeMap.addNode(pos, data->name, true, true, true, entry);

            WorldPosition exitPos = pos;

            if (data->displayId == 3015)  //Boat
                exitPos.setZ(exitPos.getZ() + 6.0f);
            else if (data->displayId == 3031) //Zepelin
                exitPos.setZ(exitPos.getZ() - 17.0f);
            else if (data->displayId == 7087) //Moonspray
                exitPos.setZ(exitPos.getZ() + 4.88f);

            makeDockNode(node, exitPos, "dock");
```

At execution, `MoveTo` reaches the transport branch (`MovementActions.cpp:1688`):

```cpp
if (pathType == PathNodeType::NODE_TRANSPORT)
{
    WorldPosition telePosition = std::next(movePath.getPath().begin())->point;
    bool usedTransport = UseTransport(ai, entry, bot->GetTransport() ? telePosition : movePosition, movePosition, sPlayerbotAIConfig.transportTeleportType > 0);
    if (!usedTransport)
    {
        if (bot->GetTransport())
            lastMove.lastTransportEntry = entry;

        WaitForReach(1000.0f);
    }
    ...
    return true;
}
```

and `UseTransport` tries to find the vessel (`MovementActions.cpp:471`):

```cpp
float minDist = 0;

std::string transportName;

for (auto& trans : dockPosition.getTransports(entry))
{
    float distance = dockPosition.sqDistance2d(trans);
    ...
}

if (transport && dockPosition.mapid == bot->GetMapId() && dockPosition.sqDistance2d(transport) < INTERACTION_DISTANCE * INTERACTION_DISTANCE)
{
    MoveOnTransport(ai, transport, doTeleport);

    return true;
}

if (transportName.empty())
    ai->TellDebug(ai->GetMaster(), "Waiting for transport on different map.", "debug move");
```

**What breaks.** `getTransports(entry)` returns an empty set — D1 kills the MO-transport branch,
D2 kills the fallback. `transport` stays null, the function returns `false`, and `MoveTo`
answers with `WaitForReach(1000.0f); return true`. The bot stands on the dock indefinitely,
reporting *"Waiting for transport on different map"* under the `debug move` strategy even when
the boat is moored ten yards away.

**Fix.** D1 alone restores boats and zeppelins. With `GetTransports()` returning `_transports`,
`UseTransport` finds the vessel, and boarding proceeds through the existing
`MoveOnTransport` (`MovementActions.cpp:314`).

For *faithful* boarding, also stop teleporting onto the deck. `MoveOnTransport` already has a
walk-on branch; it is only unreachable because `doTeleport` is forced true by config default:

```cpp
if (doTeleport)
{
    bot->GetMap()->PlayerRelocation(bot, transPos.getX(), transPos.getY(), transPos.getZ(), bot->GetOrientation());
    transport->AddPassenger(bot, true);
    bot->SendHeartBeat();
    return true;
}

bot->SetTransport(botTrans);

if (path.empty())
{
    path = WorldPosition(transport).getPathStepFrom(botPos, bot);

    if (path.empty())
        return false;
}
else
{
    transport->AddPassenger(bot, true);
    ...
```

Set `AiPlayerbot.TransportTeleportType = 0` once D1 and D3 land, and add the guard that makes
mode 0 safe — walk on only while the vessel is actually stopped, otherwise keep waiting.

`Transport` already tracks this, but the accessor is private (`src/game/Transports/Transport.h:112`):

```cpp
//! Helpers to know if stop frame was reached
bool IsMoving() const { return _isMoving; }
void SetMoving(bool val) { _isMoving = val; }
```

Move `IsMoving()` to the `public:` block (leave `SetMoving` private), then:

```cpp
// MovementActions.cpp, UseTransport() — replace the unconditional board at :491
if (transport && dockPosition.mapid == bot->GetMapId() && dockPosition.sqDistance2d(transport) < INTERACTION_DISTANCE * INTERACTION_DISTANCE)
{
    // Only walk aboard a vessel that has actually stopped at the dock. Pathfinding onto a
    // moving boat lands the bot in the water.
    if (!doTeleport && transport->IsMoving())
    {
        ai->TellDebug(ai->GetMaster(), "Transport still moving, waiting to board.", "debug move");
        return false;
    }

    MoveOnTransport(ai, transport, doTeleport);
    return true;
}
```

There is also a latent bug worth fixing while in here — `MoveOnTransport` re-attaches the bot's
*previous* transport before boarding the new one (`MovementActions.cpp:339`):

```cpp
bot->SetTransport(botTrans);
```

`botTrans` was captured at `:322` as the transport the bot is currently on. On a dock-to-dock
transfer this silently reinstates the old vessel. It should be dropped; `AddPassenger` sets the
transport correctly (`Transport.cpp:214`).

### 3.2 Zeppelins

Identical to boats in every respect — same `generateTransportNodes` branch, same
`transports` table (`(6,164871,'Orgrimmar and Undercity',356284)`, `(7,175080,...)`,
`(8,176495,...)`), same `UseTransport` path. The only difference is the dock Z-offset
(`-17.0f` for `displayId == 3031` vs `+6.0f` for boats, `TravelNode.cpp:2549`).

**Fix:** covered entirely by D1 plus the §3.1 walk-on changes. No zeppelin-specific work.

### 3.3 Elevators and lifts

**How it works today.** The shipped graph *does* contain them — `Elevator` /
`Elevatorentry` nodes for the Undercity lifts and `Mesa Elevator` nodes for Thunder Bluff, the
Great Lift, and the instance lifts on maps 47 and 209
(`ai_playerbot_travel_nodes.sql:613-963`):

```sql
(580, 'Elevator', 0, -5067.46, 438.984, 423.758, 1),
(581, 'Elevator', 0, -5068.13, 438.793, 410.931, 1),
(907, 'Mesa Elevator', 1, -1028.04, -28.3568, 69.0226, 1),
(908, 'Mesa Elevator', 1, -1028.04, -28.3568, 140.499, 1),
```

The per-model floor offsets used to place the boarding node are hard-coded
(`WorldPosition.cpp:614`):

```cpp
float WorldPosition::GetTransporFloorOffset(uint32 entry)
{
    auto data = sGOStorage.LookupEntry<GameObjectInfo>(entry);
    switch (data->displayId)
    {
        case 3831: //Subway
            return -10.0f;
        case 807: //Vator
            return -1.25f;
        case 455: //Undervator
            return -0.46f;
        case 3015: //Boat
            return 6.0f;
        case 3031: //Zepelin
            return -17.0f;
        case 7087: //Moonspray
            return 4.88f;
        default:
            return 0.0f;
    }
```

Execution takes exactly the same `UseTransport` route as boats.

**What breaks.** D3. An elevator is GO type 11, so it is a plain `GameObject`: the
`dynamic_cast<GenericTransport*>` cannot succeed, there is no `AddPassenger`, and the platform
does not move server-side. The bot walks to the shaft and stops. Where the graph offers a
walkable alternative the bot detours; where it does not (Undercity's ziggurat lifts, the Great
Lift), it is stranded.

**Fix.** D3 parts (i)–(iv), then the elevator rides on the same `LocalTransport` machinery as the
tram. Nothing elevator-specific is needed beyond the animation seed rows for
`displayId` 807 and 455.

One caveat to record: `GetTransporFloorOffset` dereferences `sGOStorage.LookupEntry(entry)`
without a null check (`WorldPosition.cpp:616`) — a bad entry crashes. Worth hardening in the same
pass:

```cpp
float WorldPosition::GetTransporFloorOffset(uint32 entry)
{
    auto data = sGOStorage.LookupEntry<GameObjectInfo>(entry);
    if (!data)
        return 0.0f;

    switch (data->displayId)
```

### 3.4 The Deeprun Tram

This is the mode the report singled out, and it is the worst case: everything upstream of the
boarding step is correct, and the boarding step is architecturally impossible.

**How it works today.** The tram is six GO type-11 cars, `displayId` 3831
(`sql/base/tw_world_gameobject_template.sql`):

```sql
(176080,11,3831,'Subway',0,40,1,0,0,0,...)
(176081,11,3831,'Subway',0,40,1,0,0,0,...)
(176082,11,3831,'Subway',0,40,1,0,0,0,...)
(176083,11,3831,'Subway',0,40,1,0,0,0,...)
(176084,11,3831,'Subway',0,40,1,0,0,2147483647,...)
(176085,11,3831,'Subway',0,40,1,0,0,0,...)
```

They are **not** in the `transports` table — that table contains only the 13 boats and zeppelins.

The graph is complete and correct. Twelve `Subway` nodes (the two ends of each car's run) and
twelve `Subwayentry` dock nodes on the platforms, all on map 369
(`ai_playerbot_travel_nodes.sql:1403-1426`):

```sql
(1370, 'Subway',      369, -45.3842, -10.3899,  7.17514, 1),
(1373, 'Subway',      369, -45.3934,  2472.93,  6.9886,  1),
(1387, 'Subwayentry', 369, -39.7335, -10.3899, -3.91574, 1),
(1389, 'Subwayentry', 369, -50.9334,  2472.93, -3.91574, 1),
```

wired platform → car → other platform (`ai_playerbot_travel_nodes.sql:6481`):

```sql
(1370, 1373, 3, 176084, 0.1, 0, 71.667, 1, 0, 0, 0),   -- ride, 71.7s
(1370, 1387, 3, 0,      5.75507, 0, 0.1, 1, 0, 0, 0),  -- car <-> platform
(1387, 1370, 3, 0,      5.75507, 0, 0.1, 1, 0, 0, 0),
```

and the map-369 endpoints connect to Ironforge and Stormwind through ordinary area triggers
(`Deeprun Tram entrance` / `Deeprun Tram exit` nodes at `ai_playerbot_travel_nodes.sql:422-428`).
A bot routing from Stormwind to Ironforge *does* pick the tram, walks into the tunnel, and
reaches the platform.

**What breaks.** D3, in its purest form. Concretely, tracing a bot standing on the platform with
the default `TransportTeleportType = 2`:

1. `UpcommingSpecialMovement` takes the type-2 branch (`TravelNode.cpp:1106`) and cuts the path
   so the leading point is the platform-side `NODE_TRANSPORT` point, whose `entry` is `0` (the
   dock hop created by `makeDockNode`).
2. `MoveTo` dispatches on that point (`MovementActions.cpp:1688`) and calls
   `UseTransport(ai, 0, ...)` — **entry `0`**.
3. `UseTransport` calls `dockPosition.getTransports(0)`. With `entry == 0` both branches of
   `getTransports` run: the MO branch returns nothing (D1), and the fallback scans *every
   gameobject spawn on map 369* and resolves none of them (D2).
4. `transport` stays null; `UseTransport` returns `false`.
5. `MoveTo` calls `WaitForReach(1000.0f)` and returns `true` — a successful "action", so nothing
   escalates or re-routes.

The bot stands on the tram platform forever, and burns a full-map GO scan doing it. Even with
D1 and D2 fixed, step 3 would find a `GameObject` that is not a `GenericTransport`, and step 4
would still fail — and even if it somehow attached, the car never moves.

**Fix.** D3 in full, plus one bot-module correction: the dock hop should carry the transport's
entry rather than `0`, so `UseTransport` searches for the right car instead of every GO on the
map. `TravelNode.cpp:2362`:

```cpp
void TravelNodeMap::makeDockNode(TravelNode* node, WorldPosition pos, std::string dockName, uint32 transportEntry)
{
    pos.loadMapAndVMap(0);
    WorldPosition exitPos = pos;

    if (exitPos.ClosestCorrectPoint(20.0f, 1.0f, 0))
    {
        TravelNode* exitNode = getNode(exitPos, nullptr, 1.0f);

        if (!exitNode) //Only add paths if we are adding a new node or
        {
            exitNode = sTravelNodeMap.addNode(exitPos, node->getName() + dockName, true, false);

            // Carry the transport entry on the dock hop too. With 0 here, UseTransport()
            // is called with entry 0 and has to search every gameobject on the map.
            TravelNodePath travelPath(exitPos.distance(pos), 0.1f, (uint8)TravelNodePathType::transport, transportEntry, true);
            travelPath.setComplete(true);
            travelPath.setPath({ exitPos, pos });
            exitNode->setPathTo(node, travelPath, true);
            travelPath.setPath({ pos, exitPos });
            node->setPathTo(exitNode, travelPath, true);
            node->setLinked(true);
        }
    }
}
```

with the four call sites updated to pass `entry` (`TravelNode.cpp:2454`, `:2506`, `:2556`), e.g.:

```cpp
makeDockNode(node, exitPos, "entry", entry);
```

Because the shipped store is never regenerated, the equivalent correction must also be applied
to the seeded rows — a data migration under `sql/database_updates/`:

```sql
-- Dock hops were seeded with object = 0, forcing UseTransport() into a whole-map GO scan.
-- Copy the entry from the matching ride link on the same transport node.
UPDATE `ai_playerbot_travelnode_link` AS dock
JOIN (
    SELECT `node_id`, MAX(`object`) AS `entry`
    FROM `ai_playerbot_travelnode_link`
    WHERE `type` = 3 AND `object` <> 0
    GROUP BY `node_id`
) AS ride ON ride.`node_id` = dock.`to_node_id`
SET dock.`object` = ride.`entry`
WHERE dock.`type` = 3 AND dock.`object` = 0;
```

Finally, once `LocalTransport` exists, set `AiPlayerbot.TransportTeleportType = 0` so bots walk
into the car and ride it rather than teleporting dock to dock.

### 3.5 Flight paths

**How it works today.** `generateTaxiPaths` links every flight-master node pair
(`TravelNode.cpp:2966`):

```cpp
float totalTime = startPos.getPathLength(ppath) / (450 * 8.0f);

TravelNodePath travelPath(0.1f, totalTime, (uint8)TravelNodePathType::flightPath, i, true);
travelPath.setPath(ppath);

startNode->setPathTo(endNode, travelPath);
```

270 such links ship in the store. Routing cost-checks each candidate flight
(`TravelNode.cpp:155`):

```cpp
if (getPathType() == TravelNodePathType::flightPath && pathObject)
{
    if (!bot->IsAlive())
        return -1;

    TaxiPathEntry const* taxiPath = sTaxiPathStore.LookupEntry(pathObject);

    if (taxiPath)
    {

        if (!bot->isTaxiCheater() && taxiPath->price > cGold)
            return -1;

        if (!bot->isTaxiCheater() && !bot->m_taxi.IsTaximaskNodeKnown(taxiPath->to))
            return -1;
        ...
```

and execution goes through `UseTaxi` (`MovementActions.cpp:254`):

```cpp
    if (unit && !bot->m_taxi.IsTaximaskNodeKnown(tEntry->from))
    {
        bot->GetSession()->SendLearnNewTaxiNode(unit);

        unit->SetFacingTo(unit->GetAngle(bot));
    }
}

uint32 botMoney = bot->GetMoney();
if (ai->HasCheat(BotCheatMask::gold) || ai->HasCheat(BotCheatMask::taxi))
{
    bot->SetMoney(botMoney + tEntry->price);
}

bot->OnTaxiFlightEject(true);

ai->Unmount();

bool goTaxi = bot->ActivateTaxiPathTo({tEntry->from, tEntry->to}, unit, 1);
```

Random bots learn nodes at creation via `PlayerbotFactory::InitTaxiNodes`
(`PlayerbotFactory.cpp:355`), gated by level and continent.

**What breaks.** Flights are the one mode that *sometimes* works. Three distinct defects:

**T1 — the routing cost check tests only the destination node.** `getCost` checks
`IsTaximaskNodeKnown(taxiPath->to)` (`TravelNode.cpp:168`) but the core's
`ActivateTaxiPathTo` checks **both** ends (`src/game/Objects/Player.cpp:19733`):

```cpp
// No hack here
if (!IsTaxiCheater() && !nocheck)
    for (uint32 node : nodes)
        if (!m_taxi.IsTaximaskNodeKnown(node))
        {
            GetSession()->ProcessAnticheatAction("PassiveAnticheat", "Taxi: Attempt to use unknown node.", CHEAT_ACTION_INFO_LOG);
            return false;
        }
```

So the router happily selects a flight whose *source* node the bot has not learned, and the core
rejects it — while also logging an anticheat entry for every such bot, every attempt. With ~1000
bots that is log noise as well as a stall.

**T2 — `OnTaxiFlightEject` is a no-op stub.** `src/game/Objects/Player.h:2488`:

```cpp
// OnTaxiFlightEject: cmangos handler called when bot is forced off taxi.
void OnTaxiFlightEject(bool /*force*/ = false) {}
```

`UseTaxi` calls it specifically to clear a flight already in progress before starting a new one.
Being empty, a bot that is mid-flight cannot start the next hop.

**T3 — `MinimalMove` ignores whether the flight actually started.**
`MovementActions.cpp:546`:

```cpp
bool didTaxi = UseTaxi(ai, nextStep->entry, false);

for (auto& step : path)
{
    if (step.type == PathNodeType::NODE_FLIGHTPATH && step.entry == nextStep->entry)
        continue;

    lastMove.lastPath.cutTo(step, false); //Remove path until next walk or taxi.
    break;
}

return true;
```

`didTaxi` is never read. The flight leg is stripped from the path whether or not the bot boarded.
Unobserved bots paper over this by teleporting onward, which is why the failure is mostly
invisible — but it means the taxi is silently skipped rather than retried, and it is also why the
observed symptom is worse near real players (where `MoveTo`, not `MinimalMove`, runs).

**Fix.**

T1 — check both endpoints in the cost function, so unusable flights are never routed
(`TravelNode.cpp:168`):

```cpp
// ActivateTaxiPathTo() validates BOTH endpoints (Player.cpp:19733). Checking only `to` here
// lets the router pick flights the core will refuse, stalling the bot at the flight master
// and logging an anticheat entry each attempt.
if (!bot->isTaxiCheater() && (!bot->m_taxi.IsTaximaskNodeKnown(taxiPath->from) ||
                              !bot->m_taxi.IsTaximaskNodeKnown(taxiPath->to)))
    return -1;
```

T2 — implement the eject instead of stubbing it (`src/game/Objects/Player.h:2488`):

```cpp
// Force the player off an in-progress taxi flight. Bots call this before starting a new
// flight; leaving it empty made every subsequent hop fail.
void OnTaxiFlightEject(bool force = false)
{
    if (!IsTaxiFlying())
        return;

    m_taxi.ClearTaxiDestinations();
    GetMotionMaster()->MovementExpired();
    ClearUnitState(UNIT_STAT_TAXI_FLIGHT);
    RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_TAXI_FLIGHT | UNIT_FLAG_DISABLE_MOVE);

    if (force)
        SetFallInformation(0, GetPositionZ());
}
```

T3 — respect the result and retry rather than discarding the leg
(`MovementActions.cpp:546`):

```cpp
if (!UseTaxi(ai, nextStep->entry, false))
{
    // Boarding failed (unknown node, no money, still flying). Keep the leg and retry next
    // tick instead of silently dropping the flight from the path.
    return true;
}

for (auto& step : path)
{
    if (step.type == PathNodeType::NODE_FLIGHTPATH && step.entry == nextStep->entry)
        continue;

    lastMove.lastPath.cutTo(step, false); //Remove path until next walk or taxi.
    break;
}

return true;
```

For faithful behaviour also prefer the NPC form: `MoveTo` already passes `needNpc = true`
(`MovementActions.cpp:1833`), which routes through `SendLearnNewTaxiNode` and charges the fare.
`MinimalMove`'s `needNpc = false` form bypasses the flight master entirely; with T1 fixed the
unknown-source case disappears, so it can be tightened to `true` as well once T1 is in.

### 3.6 Instance portals and area triggers

**How it works today.** `generateAreaTriggerNodes` (`TravelNode.cpp:2220`) creates an entry node
at the trigger and an exit node at its destination, linked by an `areaTrigger` path
(`TravelNode.cpp:2280`):

```cpp
//Portal link from area trigger to area trigger destination.
if (outNode && inNode)
{
    TravelNodePath travelPath(0.1f, 3.0f, (uint8)TravelNodePathType::areaTrigger, i, true);
    travelPath.setPath({ *inNode->getPosition(), *outNode->getPosition() });
    inNode->setPathTo(outNode, travelPath);
}
```

`UpcommingSpecialMovement` requires the bot to be physically inside the trigger box before firing
(`TravelNode.cpp:1039`):

```cpp
if (startP->type == PathNodeType::NODE_AREA_TRIGGER)
{
    if (startP->entry) //For area triggers we need to be close enough to trigger it's activation.
    {
        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(startP->entry);
        if (!atEntry)
            return false;
        ...
        if(!IsPointInAreaTriggerZone(atEntry, startPos.getMapId(), startPos.getX(), startPos.getY(), startPos.getZ(), 0.5f))
            return false;
    }

    cutTo(*startP, false);

    return true;
}
```

`MoveTo` then records the trigger (`MovementActions.cpp:1680`) and `AreaTriggerAction` replays it
as a genuine client packet (`AreaTriggerAction.cpp:69`):

```cpp
WorldPacket p(CMSG_AREATRIGGER);
p << triggerId;
p.rpos(0);
bot->GetSession()->HandleAreaTriggerOpcode(p);
```

**Assessment: working, and already faithful.** 100 `areaTrigger` links ship in the store; the bot
walks into the trigger and the server handles it exactly as it would for a player. This is the
mode the other three should be modelled on. No fix proposed.

The one thing to note is that this is *also* how bots reach the Deeprun Tram tunnel (map 369) —
so tram routing gets the bot to the platform correctly and then fails at the last step, which
matches the reported symptom precisely.

### 3.7 Static portals (spellcaster gameobjects)

**How it works today.** `generatePortalNodes` scans for `GAMEOBJECT_TYPE_SPELLCASTER` objects
whose spell teleports (`TravelNode.cpp:2292`):

```cpp
for (auto goData : WorldPosition().getGameObjectsNear(0, 0))
{
    GuidPosition go(goData);

    auto data = sGOStorage.LookupEntry<GameObjectInfo>(go.GetEntry());
    ...
    if (data->type != GAMEOBJECT_TYPE_SPELLCASTER)
        continue;
    ...
    TravelNodePath travelPath(0.1f, 3.0f, (uint8)TravelNodePathType::staticPortal, go.GetEntry(), true);
```

and `MoveTo` uses them by sending a real `CMSG_GAMEOBJ_USE` (`MovementActions.cpp:1639`):

```cpp
std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
*packet << *i;
bot->GetSession()->QueuePacket(std::move(packet));
return true;
```

**What breaks — P1.** The execution code is fine and faithful, but **the shipped node store
contains zero `staticPortal` links** (type 6: 0 rows, §1.1). `generatePortalNodes` only runs
under `hasToFullGen`, which never fires because the store is seeded. So no route ever includes a
static portal. On a 1.18.1 Turtle world this matters for any custom portal hub the server adds —
none of them are reachable by bots.

**Fix.** Generate the missing links once and ship them as a migration, rather than forcing a full
graph regeneration (which, per D3, would destroy the elevator and tram nodes on this core). Add a
narrow entry point next to the existing debug commands
(`DebugAction.cpp:4254` already exposes `generateNodes` / `generatePaths` / `saveNodeStore`):

```cpp
// Regenerate ONLY the static-portal nodes and links, then persist. Unlike a full
// regeneration this leaves the seeded elevator/tram nodes intact — they cannot be
// rebuilt on this core (no TransportAnim data, see TransportMgr.h:93).
sTravelNodeMap.generatePortalNodes();
sTravelNodeMap.saveNodeStore(true);
```

Run it once on a scratch world DB, diff the resulting `ai_playerbot_travelnode` /
`_travelnode_link` rows, and commit the delta as `sql/database_updates/`.

### 3.8 Teleport spells and hearthstone

**How it works today.** These are not stored in the graph at all — they are synthesised per
route, per bot, during A\*. Hearthstone (`TravelNode.cpp:1558`):

```cpp
if (AI_VALUE2(bool, "action useful", "hearthstone") && bot->IsAlive())
{
    TravelNode* homeNode = sTravelNodeMap.getNode(AI_VALUE(WorldPosition, "home bind"), nullptr, 50.0f);
    if (homeNode)
    {
        PortalNode* portNode = new PortalNode(start);
        portNode->SetPortal(start, homeNode, 8690);
        ...
        childNode->m_g = std::max((uint32)2, (10 - AI_VALUE(uint32, "death count")) * MINUTE); //If we can walk there in 10 minutes, walk instead.
```

and mage teleports (`TravelNode.cpp:1582`):

```cpp
std::vector<uint32> teleSpells = { 3561,3562,3563,3565,3566,3567,18960 };

for (auto spellId : teleSpells)
{
    ...
    if (!bot->HasSpell(spellId))
        continue;

    if (!sServerFacade.IsSpellReady(bot, spellId))
        continue;
    ...
    if (AI_VALUE2(uint32, "has reagents for", spellId) == 0)
        continue;
```

Execution casts the spell for real (`MovementActions.cpp:1836`):

```cpp
if (pathType == PathNodeType::NODE_TELEPORT && entry)
{
    if (entry == 8690)
    {
        if (AI_VALUE2(bool, "action useful", "hearthstone") && (!bot->IsFlying() || WorldPosition(bot).currentHeight() < 10.0f))
        {
            return ai->DoSpecificAction("hearthstone", Event("move action"), true);
        }
        ...
    }
    else
    {
        if (sServerFacade.IsSpellReady(bot, entry) && ... && AI_VALUE2(uint32, "has reagents for", entry) > 0)
        {
            ...
            if (ai->DoSpecificAction("cast", Event("rpg action", chat->formatWorldobject(bot) + " " + std::to_string(entry)), true))
                return true;
```

**Assessment: working and faithful.** Cooldown, reagent, combat and flying state are all
respected, and the spell is genuinely cast. No fix proposed.

Note the spell list is Classic-only (7 entries, no Theramore/Stonard). That is correct for 1.18.1
but is a hard-coded list — if this fork ever adds a teleport spell, it must be added here.

### 3.9 Summons (meeting stone and warlock)

**How it works today.** Event-driven, not graph-driven. `UseMeetingStoneAction` responds to a
summon packet and validates the object before accepting
(`UseMeetingStoneAction.cpp:44`):

```cpp
GameObject *gameObject = map->GetGameObject(guid);
if (!gameObject)
    return false;

const GameObjectInfo* goInfo = gameObject->GetGOInfo();
if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_SUMMONING_RITUAL)
    return false;

return Teleport(requester, requester, bot);
```

with group membership, combat and teleport-in-progress guards above it
(`UseMeetingStoneAction.cpp:26-38`).

**Assessment: working.** Teleporting *is* the faithful behaviour here — that is what a summon
does. No fix proposed.

### 3.10 The Dark Portal

Not applicable. This fork targets 1.18.1 (vanilla + Turtle content); there is no Outland map and
no Dark Portal area trigger in `sql/base/tw_world_areatrigger_teleport.sql`. Recorded here only
so the audit is complete.

---

## Part 4 — Suggested ordering

The defects are independent, so this is ordered by ratio of benefit to risk.

| # | Change | Files | Unblocks | Risk |
|---|---|---|---|---|
| 1 | **D1** — `Map::GetTransports()` returns `_transports` | `src/game/Maps/Map.h:466` | boats, zeppelins | very low — one line, no behaviour change for non-bots |
| 2 | **T1** — cost check both taxi endpoints | `TravelNode.cpp:168` | flight-path stalls, anticheat log noise | very low |
| 3 | **T3** — honour `UseTaxi`'s return in `MinimalMove` | `MovementActions.cpp:546` | silently skipped flights | low |
| 4 | **D2** — correct guid + bounded GO scan | `WorldPosition.cpp:564` | removes a per-tick full-map scan | low |
| 5 | **T2** — implement `OnTaxiFlightEject` | `Player.h:2488` | consecutive flight hops | low — touches core taxi state |
| 6 | **§3.1** — walk-on guard, drop stale `SetTransport` | `MovementActions.cpp:339,491` | faithful boat boarding | medium — needs `TransportTeleportType = 0` |
| 7 | **P1** — regenerate static-portal links, ship as migration | `DebugAction.cpp`, `sql/database_updates/` | static portals | medium — data migration |
| 8 | **§3.4** — dock hops carry the transport entry (code + SQL migration) | `TravelNode.cpp:2362`, `sql/database_updates/` | correct transport lookup | medium — data migration |
| 9 | **D3** — `GenericTransport` base, `LocalTransport`, `transport_animation` table | `src/game/Transports/*`, `cmangos-compat-shim.h:33`, `GameObject::Create` | **elevators, the tram** | high — new core subsystem, needs in-game phase calibration |

Items 1–5 are small and independent; they can land together and will visibly fix boats,
zeppelins and flights. Item 9 is the only way to make the tram and the lifts work faithfully, and
it is the only item that cannot be fully validated from static analysis — the animation phase
calibration described in D3(iv) has to be measured in-game.

**One standing risk to record regardless of what is implemented:** because
`generateTransportNodes` is dead on this core (D3), any operation that empties
`ai_playerbot_travelnode` permanently destroys every elevator and tram node in the graph. Until
item 9 lands, `src/modules/PlayerBots/sql/world/classic/ai_playerbot_travel_nodes.sql` is the
*only* copy of that data.

---

## Appendix — code map

| Concern | Location |
|---|---|
| Travel graph load / generate | `playerbot/TravelNode.cpp:3177` (`generateAll`), `:3414` (`loadNodeStore`) |
| Transport node generation | `playerbot/TravelNode.cpp:2386` (`generateTransportNodes`), `:2362` (`makeDockNode`) |
| Taxi link generation | `playerbot/TravelNode.cpp:2966` (`generateTaxiPaths`) |
| Area-trigger / portal generation | `playerbot/TravelNode.cpp:2220` (`generateAreaTriggerNodes`), `:2289` (`generatePortalNodes`) |
| Route → executable path | `playerbot/TravelNode.cpp:1232` (`buildPath`) |
| Special-movement detection | `playerbot/TravelNode.cpp:1025` (`UpcommingSpecialMovement`) |
| Path-point advance rules | `playerbot/TravelNode.cpp:904` (`shouldMoveToNextPoint`) |
| Transport boarding | `playerbot/strategy/actions/MovementActions.cpp:441` (`UseTransport`), `:314` (`MoveOnTransport`), `:394` (`MoveOffTransport`), `:651` (`WaitForTransport`) |
| Taxi boarding | `playerbot/strategy/actions/MovementActions.cpp:254` (`UseTaxi`) |
| Observed-move executor | `playerbot/strategy/actions/MovementActions.cpp:1571` onward |
| Unobserved-move executor | `playerbot/strategy/actions/MovementActions.cpp:506` (`MinimalMove`) |
| Transport discovery | `playerbot/WorldPosition.cpp:564` (`getTransports`), `:1275` (`FindPointGameObjectData`) |
| MO transport core | `src/game/Transports/Transport.{h,cpp}` |
| Type-11 handling (absent) | `src/game/Objects/GameObject.cpp:247`, `GameObject.h:878` |
| Map transport registry | `src/game/Maps/Map.h:464` (stub), `:744` (`_transports`), `Map.cpp:2626` (`GetTransport`) |
| Client transport attach | `src/game/Handlers/MovementHandler.cpp:1098` |
| Taxi core | `src/game/Objects/Player.cpp:19715` (`ActivateTaxiPathTo`), `Player.h:2488` (`OnTaxiFlightEject`) |
| Config | `playerbot/PlayerbotAIConfig.cpp:308`, `aiplayerbot.conf.dist.in:171` |
| Seed data | `src/modules/PlayerBots/sql/world/classic/ai_playerbot_travel_nodes.sql`, `sql/base/tw_world_transports.sql` |
