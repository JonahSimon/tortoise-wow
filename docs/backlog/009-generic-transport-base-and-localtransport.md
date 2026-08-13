---
status: failed
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

**Failure notes:** blocking findings not addressed — but unlike `007`, this is a
**technical dispute, not an impossibility**, and needs a human to settle it
before the artifact is retried.

A review lens flagged that `LocalTransport.cpp` keys its animation phase off
`WorldTimer::getMSTime()` rather than wall-clock time. The implementer declined
to change it, arguing the finding is a misdiagnosis:

- `getMSTime()` is the clock the client's own type-11 animation loop is seeded
  with. `src/game/Objects/Object.cpp:503–515` writes
  `WorldTimer::getMSTime()` into the create block under `UPDATEFLAG_TRANSPORT`
  for every gameobject that is not an MO-transport, and re-sends it on every
  create block — so a per-entry epoch offset measured against it stays valid
  across restarts.
- Wall-clock time is never sent to a 1.12 client for these objects, so
  switching to `time(nullptr)` the way `Transport::Create` does would anchor the
  mirror to something no client can see, breaking phase agreement rather than
  fixing it.
- This artifact's own calibration recipe (lines 63–66) specifies logging
  `getMSTime() % TotalTime`, which is consistent with the implementer's choice
  rather than the reviewer's.

The drain verified the central citation: `Object.cpp:503–515` does send
`getMSTime()` for non-MO-transport gameobjects, and that block carries an
existing `@TODO` noting type 11 is not handled there. So the dispute is
grounded rather than hand-waving — but "grounded" is not "correct", and no one
has confirmed phase agreement against a real 1.12 client. Resolve that
question first; if the implementer is right, the fix is to the review
criterion, not to the code.

Commit `ea96243` documents this reasoning in
`src/game/Transports/LocalTransport.h` and adds a DEBUG log in `AddPassenger`
reporting how far the mirror sits from the parked pose, which is the
measurement signal the calibration step needs. The remaining half of that
acceptance criterion — measuring each entry's offset in-game — is not
achievable from static analysis, as this artifact already states.

**Adjudicated (2026-08-12):** the dispute splits — the block was correct, but
not for the reason given.

*On the clock, the implementer is right.* `Object.cpp:503–515` branches on
transport kind: an MO transport sends `GetPathProgress()` (server-computed, so
it needs the restart-stable `time(nullptr) % (pathTime/1000)` that
`Transport.cpp:112` uses), while a type-11 GO sends raw
`WorldTimer::getMSTime()` as a seed the client's own loop rides. Client and
server therefore reset together on restart, and wall-clock is never sent for
these objects. "Be consistent with `Transport::Create`" is a false analogy.
Leave `GetAnimationTime()` keyed off `getMSTime()`.

*Two real issues were bundled into the same finding, and neither is fixed:*

1. **~49.7-day wrap.** `LocalTransport.cpp:137` computes
   `(getMSTime() + EpochOffset) % TotalTime`. Unsigned wraparound is defined, so
   this is not UB, but keying phase off a wrapping 32-bit uptime counter gives a
   phase discontinuity every ~49.7 days, and adding `EpochOffset` shifts where
   it lands. The server-side discontinuity is certain and drags passengers to an
   unrelated point in the loop. Whether the client tracks that jump is an
   inference, not a verified fact — we have no 1.12 client source here — but a
   client advancing its own seeded copy would not wrap at the same instant, so
   the likely result is a desync until each client receives a fresh create block.
2. **The calibration recipe stores the wrong value.** The migration (lines
   31–34) and the `epoch_offset` column comment both instruct the operator to
   record `getMSTime() % TotalTime` at a known end. Since phase is
   `(getMSTime() + EpochOffset) % TotalTime`, the value that puts the car at
   keyframe `T_end` is `(T_end − observed) mod TotalTime`. Following the
   instructions as written silently miscalibrates every entry. This artifact's
   own recipe (lines 63–66) is under-specified the same way, so citing it does
   not settle this half — fix the artifact's recipe too.

Neither requires redoing the implementation. Fix those two, then ship the branch;
the in-game phase measurement remains outstanding by design and belongs in a
follow-up artifact, not here.

Worktree left at
`D:/CodingProjects/tortoise-wow/tortoise-wow/.claude/worktrees/wf_5873c415-d92-1`,
branch `backlog/generic-transport-base-and-localtransport` (never pushed, so
`ea96243` and the whole `LocalTransport` implementation exist only there — read
it with `git -C <worktree> show ea96243` and review the branch before
discarding anything). Remove both with `git worktree remove <path>` and
`git branch -D backlog/generic-transport-base-and-localtransport` before
resetting this artifact to pending.
