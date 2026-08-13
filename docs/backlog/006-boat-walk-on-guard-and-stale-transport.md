---
status: done
risk: medium
area: playerbots/transport
---

# Boat boarding has no "vessel has stopped" guard, and reattaches the wrong transport

**Problem:** Two related issues in boat/zeppelin boarding, both only
reachable once bots can actually find the vessel (see the `Map::GetTransports`
backlog item).

(a) `MovementAction::UseTransport` (`MovementActions.cpp:491`) boards the
vessel unconditionally as soon as the bot is within interaction distance —
there's no check that the vessel has actually stopped at the dock. With
`AiPlayerbot.TransportTeleportType = 0` (walk-on), a bot can path onto a
moving boat and land in the water.

(b) `MovementAction::MoveOnTransport` (`MovementActions.cpp:339`) re-attaches
the bot's *previous* transport (`bot->SetTransport(botTrans)`, where
`botTrans` was captured as the transport the bot was already on) before
boarding the new one. On a dock-to-dock transfer this silently reinstates the
old vessel instead of the new one the bot just boarded.

**Suspected cause / area:**
`src/modules/PlayerBots/playerbot/strategy/actions/MovementActions.cpp:339`
(`MoveOnTransport`), `:491` (`UseTransport`).

**Acceptance criteria:**
- `UseTransport` only walks the bot aboard (when `doTeleport == false`) if
  `transport->IsMoving()` is false; while the vessel is moving, it returns
  `false` (and the bot waits) instead of boarding a moving vessel.
- `Transport::IsMoving()` (currently `private:` at `Transport.h:112`) is moved
  to `public:` so callers outside the class can check it; `SetMoving` stays
  `private:`.
- `MoveOnTransport` no longer calls `bot->SetTransport(botTrans)` with the
  stale previously-ridden transport before boarding the new vessel.
- With `AiPlayerbot.TransportTeleportType = 0`, a bot waiting at a dock boards
  a boat only once it has stopped moving, and ends up attached to the vessel
  it actually just boarded, not a previously-ridden one.

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("§3.1").
Depends on the `Map::GetTransports` stub fix landing first, or the bot never
reaches this code path at all. Marked medium risk because it changes default
boarding behavior for boats/zeppelins under the walk-on config mode.

**Result:** PR opened at https://github.com/ChrisMiho/tortoise-wow/pull/14
