---
status: pending
risk: high
area: playerbots/transport
---

# No server-side transport type for GO type 11 — elevators and the Deeprun Tram never move

**Problem:** This core has no server-side transport type for
`GAMEOBJECT_TYPE_TRANSPORT` (GO type 11) — every elevator, lift, and Deeprun
Tram car. They're created as plain `GameObject`s with only two flags set;
nothing moves them server-side, there's no `Update` override and no path
data, and `Transport::AddPassenger` — the only mechanism that sets
`MOVEFLAG_ONTRANSPORT`, `t_guid`, and the passenger offset — has no type-11
equivalent. Real players ride these because the 1.12 client animates the
model locally on a fixed loop and reports its own transport-relative position
back to the server; a bot has no client, so nothing tells the server where
the car is or carries the bot along with it. This is the root architectural
cause of "the tram never moves" and "elevators are unboardable" — it is a
genuine core gap, not a bug in the bot module. It also means the runtime node
generator for elevators/trams is permanently dead
(`TransportMgr::GetTransportAnimInfo` stubs to `nullptr`), so the graph's
elevator/tram nodes exist only as long as nobody empties
`ai_playerbot_travelnode`.

**Suspected cause / area:**
`src/modules/PlayerBots/cmangos-compat-shim.h:31` (the `GenericTransport`
typedef collapses cmangos's base-class hierarchy down to `Transport`, so
`dynamic_cast<GenericTransport*>` can never succeed for a type-11 GO);
`src/game/Transports/Transport.{h,cpp}`; `src/game/Objects/GameObject.cpp:247`
and `GameObject.h:878` (type 11 has no `Transport` equivalent);
`src/game/Transports/TransportMgr.h:93` (`GetTransportAnimInfo` stub).

**Acceptance criteria:**
- A `GenericTransport` base class exists (new
  `src/game/Transports/GenericTransport.h`) holding passenger book-keeping
  and offset math moved out of `Transport`; `Transport` (MO transports:
  boats/zeppelins) derives from it with no behavioral change.
- The bot module's compat-shim typedef (`typedef Transport GenericTransport`)
  is removed in favor of the real base class.
- A new `LocalTransport` class (`src/game/Transports/LocalTransport.{h,cpp}`)
  handles GO type 11: it mirrors the client's local animation loop
  server-side (interpolating position from seeded animation keyframes),
  updates only while it has passengers, and never broadcasts its position to
  real clients (who keep animating locally, unaffected — no update packet is
  generated).
- `GameObject::Create`'s spawn path instantiates `LocalTransport` instead of a
  plain `GameObject` when `GameObjectInfo::type ==
  GAMEOBJECT_TYPE_TRANSPORT`.
- A `transport_animation` world table (`entry`, `time_seg`, `x`, `y`, `z`) is
  added and loaded by `TransportMgr`, replacing the `GetTransportAnimInfo`
  stub; it's seeded for the Deeprun Tram cars and the Undercity / Thunder
  Bluff / Great Lift elevators, with offsets derived from the existing
  `ai_playerbot_travelnode_path` sampled data (see the investigation doc's
  worked example for tram car 176084).
- With the `Map::GetTransports` stub fix, the `getTransports` guid/scan fix,
  and the dock-hop transport-entry fix all in place, and
  `AiPlayerbot.TransportTeleportType` set to 0 or 1, a bot standing on a
  Deeprun Tram platform or at an elevator successfully boards, rides, and
  disembarks — verified **in-game**, not just by code inspection.
- The animation phase offset (where in the loop each car/lift actually is at
  a given server time) is measured in-game per the investigation doc's
  calibration method — park a GM next to a car, log
  `WorldTimer::getMSTime() % TotalTime` at the moment it reaches a known
  end — and baked in as a per-entry epoch offset, so a bot-ridden car is in
  phase with what real clients see.

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("D3"). This
is the largest and only architecturally risky item in the investigation, and
the phase-calibration step cannot be completed from static analysis — it
must be verified in-game after the code lands, and should be the first thing
checked. Depends on the `getTransports` guid/scan fix and the dock-hop
transport-entry fix for the lookup path to find the right car/lift once it
can move.
