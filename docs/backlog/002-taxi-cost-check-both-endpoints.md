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

**Correction found while implementing:** an unlearned *source* node is not
actually fatal to the bot. `MovementAction::UseTaxi` learns it right before
calling `ActivateTaxiPathTo` whenever a flight master is in range
(`SendLearnNewTaxiNode`, `MovementActions.cpp:286-292`), which covers the
`needNpc = true` callers (`MovementActions.cpp:825`, `:1833`). Because
`PlayerbotFactory::InitTaxiNodes` grants each bot only a random, level-gated
subset of nodes, unlearned source nodes are the common case, so rejecting
those legs in `getCost` would have stripped a large share of *working* flight
travel from the router. The only caller that really trips the anticheat is the
passive-teleport path `UseTaxi(ai, nextStep->entry, false)`
(`MovementActions.cpp:546`): it passes `needNpc = false`, has no flight master
to talk to, and therefore never learns the node it was teleported onto.

**Suspected cause / area:**
`src/modules/PlayerBots/playerbot/strategy/actions/MovementActions.cpp:254`
(`UseTaxi`, `needNpc = false` branch).

**Acceptance criteria:**
- `getCost` still only requires `taxiPath->to` to be known, so legs whose
  source node `UseTaxi` can learn stay available to the router.
- `UseTaxi` guarantees the source node is known before calling
  `ActivateTaxiPathTo` on both paths: via `SendLearnNewTaxiNode` when a flight
  master is in range, and directly for the no-NPC teleport path.
- Bots no longer generate `PassiveAnticheat` "Attempt to use unknown node"
  entries for flights whose source node they had not learned.

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("T1").
Independently shippable, but pairs naturally with the `T3` fix (honour
`UseTaxi`'s return value) and the `T2` fix (`OnTaxiFlightEject`) — together
they cover reliable flight-path travel.
