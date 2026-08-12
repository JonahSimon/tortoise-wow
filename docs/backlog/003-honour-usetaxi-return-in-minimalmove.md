---
status: pending
risk: low
area: playerbots/transport
---

# MinimalMove ignores whether UseTaxi actually boarded the flight

**Problem:** In the unobserved-bot movement executor, `MinimalMove`
(`MovementActions.cpp:546`) calls `UseTaxi(...)` to board a flight and
discards the result (`didTaxi` is computed but never read). The flight-path
leg is stripped from the bot's path whether or not boarding actually
succeeded, so a failed taxi boarding is silently skipped rather than retried
— the bot teleports onward along the rest of the path without ever taking the
flight.

**Suspected cause / area:**
`src/modules/PlayerBots/playerbot/strategy/actions/MovementActions.cpp:546`
(`MinimalMove`).

**Acceptance criteria:**
- `MinimalMove` checks `UseTaxi`'s return value; when it returns `false`, the
  flight-path leg is kept in the path (the function returns without cutting
  to the next step) so the same leg is retried on a later tick, instead of
  being unconditionally stripped.
- A bot whose taxi boarding fails on a given tick (unknown node, still
  mid-flight, etc.) retries the same leg rather than the leg silently
  vanishing from its path.

**Notes:** Per `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` ("T3").
Combined with `T1` (checking both taxi endpoints), this removes the main
source of invisible flight-path failures for unobserved bots — the failure
was masked precisely because unobserved bots teleport onward and paper over
the skipped leg.
