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

(b) `sObjectMgr.DoGOData` walks every gameobject spawn in the world on every
call, because it iterates the whole `m_GameObjectDataMap` and the predicate
never breaks early. This runs on every `UseTransport` call, for every bot
waiting at a transport, every tick — a meaningful CPU sink independent of bug
(a). Note this is *not* caused by the radius-`0` argument, and cannot be fixed
by passing a radius: the radius only prunes the result vector, not the walk.
Re-scoped to artifact `010-index-go-spawn-lookup-by-entry.md`.

**Suspected cause / area:**
`src/modules/PlayerBots/playerbot/WorldPosition.cpp:564` (`getTransports`),
`:1275` (`FindPointGameObjectData`, the radius-0 distance-filter bypass).

**Acceptance criteria:**
- The elevator/tram fallback branch constructs the lookup guid via the
  three-argument `ObjectGuid(HIGHGUID_GAMEOBJECT, gopair->second.id,
  gopair->first)` constructor (`ObjectGuid.h:129`), not an implicit
  uint32-to-`ObjectGuid` conversion.
- ~~The fallback scan passes a bounded radius (e.g. 200.0f, per the
  investigation doc's reasoning) instead of `0.0f`, so it no longer walks
  every GO spawn on the map on every call.~~ **Withdrawn — this criterion was
  wrong.** A radius does not reduce the walk: `DoGOData` iterates the whole
  `m_GameObjectDataMap` and the predicate never breaks early, so the radius
  only prunes the result vector. Worse, it breaks the case this fix exists to
  serve — callers pass a *dock* position and select the nearest match
  themselves, and the six Deeprun Tram cars sit in two clusters ~2460y apart
  on map 369, so a 200y filter hides three of them from either platform. The
  scan stays unbounded here; the cost is re-scoped to artifact
  `010-index-go-spawn-lookup-by-entry.md`.
- `getMap(getFirstInstanceId())` is null-checked before use (the fix in the
  investigation doc adds this guard).

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("D2"). This
fix alone does **not** make elevators or the Deeprun Tram boardable — that
also requires a server-side transport type for GO type 11 (see the separate
`LocalTransport` backlog artifact) — but it makes the lookup correct
regardless. The per-tick full-map scan is left in place and re-scoped to
artifact 010; see that artifact for why a radius bound cannot fix it.

**Result:** PR opened at https://github.com/ChrisMiho/tortoise-wow/pull/12
