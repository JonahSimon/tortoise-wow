---
status: pending
risk: low
area: playerbots/transport
---

# Taxi routing cost check only validates the destination node, core validates both

**Problem:** Bots sometimes stall at a flight master, and every stall logs a
spurious anticheat entry. The bot router's flight-path cost function
(`TravelNode::getCost`, `TravelNode.cpp:168`) only checks that the
*destination* taxi node is known, via
`IsTaximaskNodeKnown(taxiPath->to)`. The core's
`Player::ActivateTaxiPathTo` (`Player.cpp:19733`) requires **both** endpoints
to be known, and rejects the flight while logging a `PassiveAnticheat`
"Attempt to use unknown node" entry when the source node isn't. The router
can therefore select flights the core will always refuse.

**Suspected cause / area:** `src/modules/PlayerBots/playerbot/TravelNode.cpp:168`
(`getCost`, `flightPath` branch).

**Acceptance criteria:**
- `getCost` returns -1 (unusable) for a flight-path link unless both
  `taxiPath->from` and `taxiPath->to` are known via `IsTaximaskNodeKnown`, for
  non-taxi-cheater bots.
- A bot whose route would previously have picked a flight leg with an
  unlearned source node no longer selects that leg, and no longer generates
  a `PassiveAnticheat` "Attempt to use unknown node" log entry for it.

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("T1").
Independently shippable, but pairs naturally with the `T3` fix (honour
`UseTaxi`'s return value) and the `T2` fix (`OnTaxiFlightEject`) — together
they cover reliable flight-path travel.
