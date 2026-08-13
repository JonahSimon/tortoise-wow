---
status: done
risk: medium
area: playerbots/transport
---

# Transport dock hops are seeded with object=0, forcing a whole-map GO scan instead of a direct lookup

**Problem:** `TravelNodeMap::makeDockNode` (`TravelNode.cpp:2362`) creates
the dock↔vehicle hop of every transport route with `object = 0` (the
`TravelNodePathType::transport` link's `pathObject`) instead of the specific
vehicle's GO entry. This forces `MovementAction::UseTransport` to be called
with `entry == 0` at the dock step, which falls into
`WorldPosition::getTransports(0)` and scans every gameobject spawn on the map
instead of resolving the one vehicle the bot actually needs. This is the
exact failure the Deeprun Tram hits: the bot reaches the platform correctly
via an ordinary area-trigger route, and only fails at the last step because
`UseTransport(ai, 0, ...)` can never resolve which of the six tram cars to
board.

**Suspected cause / area:**
`src/modules/PlayerBots/playerbot/TravelNode.cpp:2362` (`makeDockNode`) and
its four call sites (`:2454`, `:2506`, `:2556`); seeded rows in
`ai_playerbot_travelnode_link` (type 3, `object = 0`).

**Acceptance criteria:**
- `makeDockNode` takes a `transportEntry` parameter and sets it as the dock
  hop's `TravelNodePath` object, instead of hard-coding `0`.
- All four call sites pass the relevant transport's entry through.
- A SQL migration under `sql/database_updates/` backfills existing seeded
  dock-hop rows (`ai_playerbot_travelnode_link` where `type = 3 AND object =
  0`) with the entry copied from the matching ride link on the same node, per
  the `UPDATE ... JOIN` given in
  `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` §3.4.
- After the code change and migration are both applied, `UseTransport` is
  invoked with the correct transport entry (not `0`) for tram/elevator dock
  hops, and no longer performs a whole-map GO scan for these lookups.

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("§3.4").
This alone does not make the tram move — that needs the `LocalTransport`
backlog item — but it is a prerequisite: without it, `UseTransport` can never
look up the right car even once it's capable of moving.

**Result:** PR opened at https://github.com/ChrisMiho/tortoise-wow/pull/15
