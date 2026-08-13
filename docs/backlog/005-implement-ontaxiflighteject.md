---
status: pending
risk: low
area: playerbots/transport
---

# OnTaxiFlightEject is a no-op stub, so bots can't chain flight legs

**Problem:** `Player::OnTaxiFlightEject` (`Player.h:2488`) is a no-op stub in
this fork (commented "cmangos handler called when bot is forced off taxi").
`MovementAction::UseTaxi` calls it specifically to clear an in-progress
flight before starting the next leg. Because it does nothing, a bot that is
mid-flight can never start its next flight-path hop.

**Suspected cause / area:** `src/game/Objects/Player.h:2488`
(`OnTaxiFlightEject`).

**Acceptance criteria:**
- `OnTaxiFlightEject` actually clears an in-progress taxi flight: clears taxi
  destinations (`m_taxi.ClearTaxiDestinations()`), expires the current
  movement generator, clears `UNIT_STAT_TAXI_FLIGHT` and removes
  `UNIT_FLAG_TAXI_FLIGHT | UNIT_FLAG_DISABLE_MOVE` (`force` additionally
  resets the movement stack and re-runs the flag teardown if anything is
  left behind).
- Fall information ends up **cleared** (`SetFallInformation(0, 0.0f)`), not
  armed at the bot's current position. This fork's `Player::IsFalling()` is
  just `m_fallStartZ != 0`, `PlayerbotAI::CanMove()` refuses to move a
  falling bot, and no client sends the landing packet that would clear it
  for a bot — so a non-zero start Z would strand the bot at flight altitude
  (e.g. the `PlayerbotAI::Reset()` eject, which has no follow-up flight) and
  arm full-altitude fall damage in `Player::HandleFall`.
- `OnTaxiFlightEject` is a no-op when the player/bot is not currently flying
  (`!IsTaxiFlying()`), so real players' in-progress flights are unaffected
  when the method is called defensively.
- A bot that boards a multi-leg flight path completes consecutive legs
  instead of stalling after the first one.

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("T2").
Flagged low-but-touches-core because it changes `Player` taxi-flight state
directly, even though the change itself is small and gated behind
`IsTaxiFlying()`.
