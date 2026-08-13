# Handoff: refining the backlog drain loop

Written 2026-08-12, after the first end-to-end run of `/loop backlog-drain`
against a real backlog. This is the pick-up point for improving the loop itself
— an index and a priority order, with the evidence for each item living in the
document named beside it.

## What happened, in one paragraph

Nine scoped artifacts in `docs/backlog/`. The drain opened eight PRs (#9–#16)
across nine ticks, ~3.12M subagent tokens and ~3h55m of wall clock. Two ticks
failed, both at the review gate, neither because the implementation was bad.
All eight PRs were then integrated in three build-gated waves and merged to
`cm-main` as #17. Nothing has been verified in-game.

## Refinements, in priority order

### 1. Give the drain the dependency information it already has

**Highest value — every integration problem in the run traces to this.**

Branches are cut fresh from `cm-main`, so a later PR's branch carries *pre-fix*
code for files an earlier PR repaired. All four merge conflicts were this shape,
and each would have silently reverted finished work if resolved toward the newer
side. It also produced a compile break belonging to no PR: `GenericTransport` was
a typedef for `Transport` until #16, so #14's `IsMoving()` call compiled by
accident and stopped compiling once both were present.

The deeper cost is not merge text. #16 was *designed* against a core where
`Map::GetTransports()` returns empty — resolving a conflict repairs characters,
not a design reasoned out against code that no longer exists.

The artifacts already declared their dependencies in prose (006 says it depends
on the `GetTransports` fix landing first; 009 names 004 and 008). The drain
discards it.

**Proposal:** a `depends-on:` frontmatter field; at tick start cut from the
dependency's branch when that dependency is still unmerged, else from `cm-main`.
Targeted, not blanket, stacking — independent artifacts keep the failure
isolation that genuinely paid off here (`007` died and `009` failed its first
pass without costing the other PRs anything). `backlog-scope` already interviews
for this and only needs to write the field.

→ Detail and the rejected alternatives: the "Field report" section of
`docs/superpowers/specs/2026-08-11-backlog-workflow-design.md`.

### 2. Break the review-gate deadlock

Both failures were the review gate, and the workflow has no tiebreaker. When the
implementer and a review lens disagree, it cannot distinguish *correct code
blocked by a wrong finding* from *bad code correctly blocked* — both surface
identically as `success: false` with the work stranded on an unpushed branch.

`009` was exactly the first case: the lens demanded wall-clock time, the
implementer refused with a correct argument from the core's own wire format, and
the PR never opened. A human had to adjudicate. That review *was* still worth
having — bundled into the same finding were two real defects (a `uint32` phase
wrap and a calibration formula that told operators to store the wrong number).
So the lesson is not "trust the implementer"; it is that a disagreement needs a
resolution path.

**Proposal:** either an explicit adjudication step for disputed findings, or let
the implementer's rebuttal ride along in a PR marked *contested*, so a human
resolves it at review — which is where this workflow is supposed to put decisions
anyway. Note this is not fixable by raising the model tier; a stronger lens can
still be wrong about a specific claim.

### 3. Make generated migration filenames unique

`#15` and `#16` independently created
`sql/database_updates/20260812120000_world.sql` — same name, entirely different
migrations, colliding as add/add. Two ticks on the same day will always collide;
nothing in the naming scheme prevents it. Resolved by hand here (`#16`'s moved to
`20260812130000`). Include the artifact number or a hash in the stamp.

### 4. Distinguish "impossible" from "failed"

`007` was marked `failed` for acceptance criteria that were never satisfiable —
and the drain blamed the environment, which was the *wrong reason*. The real
blocker was data: both portal GOs are spawned on **map 42, "Collin's Test"**, a
developer test map. The environment objection later dissolved entirely (the core
now builds, the stack has a world DB), and the item is still correctly closed.

An artifact whose criteria cannot be met is not the same as an implementation
that failed. The `out-of-scope` status added during this run covers the
bookkeeping (and clears the circuit breaker, since it only counts artifacts whose
frontmatter still reads `failed`), but the *drain* still can't tell them apart.
Consider a distinct `blocked` outcome, and an environment/data feasibility check
at scope time rather than at implement time.

### 5. Model and effort tuning — with a caveat that cuts against it

Per-agent `model`/`effort` overrides are unset, so all six agents inherit the
session tier including two that are pure mechanics (`verify` runs a build check,
`PR` pushes and calls `gh`).

**But tick 7 is evidence against downgrading the review lenses.** The cheap wrong
answer there — hand-write the INSERTs, declare the criteria met, open the PR —
was available and plausible, and would have passed every check the loop performs
while shipping unvalidated SQL against a live world DB. The review stage is where
this loop's autonomy is load-bearing. Downgrade `verify` and `PR` first.

→ Full per-agent table, the nine-tick cost baseline, and why cost tracked neither
the `risk:` label nor diff size:
`docs/superpowers/plans/2026-08-12-backlog-issue-model-tuning.md`.

### 6. Stop leaking worktrees and branches

The run left ~400 MB worktrees plus a local branch per tick, and eleven stale
`worktree-wf_*` branches accumulated from the Workflow isolation mechanism. The
drain cleans up on `done` but deliberately not on `failed`, which is right — but
nothing ever sweeps the leftovers, and one orphaned commit (`d5c2de8`) survived
on a `worktree-wf_*` branch and nowhere else.

## Already done in this run

- `out-of-scope` lifecycle state, documented in `docs/backlog/README.md`.
- Cost baseline for all nine ticks (see item 5).
- Field report on branch topology appended to the design spec (item 1).
- The two WSL/Git Bash silent-corruption traps and the working recipe, in
  `docs/superpowers/plans/2026-08-11-docker-build-from-this-checkout.md`. Read
  this before automating anything against the stack: a wrapped
  `wsl -- bash -lc '...'` one-liner containing variables returns plausible wrong
  output rather than failing, and Git Bash separately rewrites absolute paths.
- Merge-wave strategy and conflict matrix:
  `docs/superpowers/plans/2026-08-12-transport-stack-merge.md`.

## Loose ends left on purpose

- **`docs/backlog/010-index-go-spawn-lookup-by-entry.md` is `pending`** — created
  by #12's implementer when it found the full-map GO scan could not be fixed by a
  radius bound. It is the next drain candidate.
- **`origin/backlog/generate-and-ship-static-portal-links` is kept
  deliberately**, against the usual prune-by-default habit. `bb5aa26` and
  `76016c6` implement 007's *code* side — a `gen portal` command that mutates the
  travel-node store under `m_nMapMtx` and leaves the graph rebuildable — and
  exist nowhere else. Only the data side is impossible. If content ever spawns a
  reachable portal, start from that branch rather than from scratch.
- **`worktree-wf_6d15c61b-144-1`** holds `d5c2de8`, a PR #7 merge commit
  reachable from no other ref. Content survives via `feature/oops` → `cm-main`;
  the commit itself does not.
- **Nothing is verified in-game.** Boats, zeppelins, flight paths, tram rides.
  And every `transport_animation_phase` row ships `epoch_offset = 0`, so a
  bot-ridden tram or lift is on the right path at the wrong *time* until each
  entry is calibrated with the corrected formula in artifact 009. Expected, not a
  regression, and the first thing to check.
- `ai_playerbot_travelnode_link_bak_wave3` is a pre-migration snapshot of the
  6200-row table `#15` updated in place. Drop it once the change is trusted.
