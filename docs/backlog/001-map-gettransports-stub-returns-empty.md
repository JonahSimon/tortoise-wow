---
status: pending
risk: low
area: playerbots/transport
---

# Map::GetTransports() stub returns empty, so bots never find boats/zeppelins

**Problem:** Boats and zeppelins never board. `Map::GetTransports()`
(`src/game/Maps/Map.h:466`) is a compat stub that always returns an empty
vector, so every bot lookup for a live MO-transport (boat/zeppelin) on the map
finds nothing. The real container, `Map::_transports`, is correctly
maintained — inserted at `Map::Add(Transport*)` (`Map.cpp:497`), erased at
`Map::Remove(Transport*, bool)` (`Map.cpp:1266`) — but the stub never reads
it. A bot routed to a dock stands there indefinitely reporting "Waiting for
transport on different map" under the `debug move` strategy even when the
vessel is moored ten yards away.

**Suspected cause / area:** `src/game/Maps/Map.h:466` (`GetTransports` stub);
caller is `src/modules/PlayerBots/playerbot/WorldPosition.cpp:564`
(`getTransports`).

**Acceptance criteria:**
- `Map::GetTransports()` returns the live contents of `_transports` (copy
  `std::set<Transport*>` into a `std::vector<Transport*>`), not an empty
  vector.
- A bot routed onto a boat dock (e.g. Menethil Harbor ↔ Theramore Isle) or a
  zeppelin tower (e.g. Orgrimmar ↔ Undercity) finds and boards the vessel
  instead of standing at the dock reporting "Waiting for transport on
  different map".
- Other existing callers of `Map::GetTransports()`, if any, are unaffected.

**Notes:** One-line fix per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md`
("D1"). `_transports` is `private:` and `GetTransports()` is declared
`public:` in the same class, so no access-level change is needed.
