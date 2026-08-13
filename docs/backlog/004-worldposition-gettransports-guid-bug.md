---
status: done
risk: low
area: playerbots/transport
---

# getTransports elevator/tram fallback uses a spawn id as an ObjectGuid, and scans the whole map

**Problem:** The elevator/tram fallback in `WorldPosition::getTransports`
(`WorldPosition.cpp:564`) has two independent bugs.

(a) It builds a gameobject lookup guid from `gopair->first`, which is a bare
spawn id (`uint32`), not an `ObjectGuid`. `ObjectGuid`'s implicit converting
constructor (`ObjectGuid(uint64 const&)`) lets this compile, but the
resulting guid has high-guid bits of 0 instead of `HIGHGUID_GAMEOBJECT`, so
`Map::GetGameObject` can never match it. This branch returns nothing, always.

(b) It calls `getGameObjectsNear(0.0f, entry)`; radius `0` disables the
distance filter entirely, so `sObjectMgr.DoGOData` walks every gameobject
spawn on the *entire map*. This runs on every `UseTransport` call, for every
bot waiting at a transport, every tick — a meaningful CPU sink independent of
bug (a).

**Suspected cause / area:**
`src/modules/PlayerBots/playerbot/WorldPosition.cpp:564` (`getTransports`),
`:1275` (`FindPointGameObjectData`, the radius-0 distance-filter bypass).

**Acceptance criteria:**
- The elevator/tram fallback branch constructs the lookup guid via the
  three-argument `ObjectGuid(HIGHGUID_GAMEOBJECT, gopair->second.id,
  gopair->first)` constructor (`ObjectGuid.h:129`), not an implicit
  uint32-to-`ObjectGuid` conversion.
- The fallback scan passes a bounded radius (e.g. 200.0f, per the
  investigation doc's reasoning) instead of `0.0f`, so it no longer walks
  every GO spawn on the map on every call.
- `getMap(getFirstInstanceId())` is null-checked before use (the fix in the
  investigation doc adds this guard).

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("D2"). This
fix alone does **not** make elevators or the Deeprun Tram boardable — that
also requires a server-side transport type for GO type 11 (see the separate
`LocalTransport` backlog artifact) — but it makes the lookup correct and
removes the per-tick full-map scan regardless.

**Result:** PR opened at https://github.com/ChrisMiho/tortoise-wow/pull/12
