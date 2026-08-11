# Backlog

Scoped issues for the autonomous drain workflow, one file per issue.

## Format

`<NNN>-<slug>.md`, e.g. `003-bots-stuck-at-spirit-healer.md`. `NNN` is a
zero-padded 3-digit sequence number — `backlog-scope` assigns the next one
when it creates a file; `backlog-drain` picks the lowest-numbered `pending`
file each tick.

```markdown
---
status: pending        # pending | in-progress | done | failed
risk: low               # low | medium | high — informational, not a gate
area: playerbots/battlegrounds
---

# <Title>

**Problem:** what's actually wrong, in the same voice as the README's fix
tables — what breaks, under what load, traced to what.

**Suspected cause / area:** file(s) or subsystem, if known.

**Acceptance criteria:** concrete, checkable conditions for "this is fixed."

**Notes:** anything else the implementer needs.
```

## Lifecycle

- **pending** — scoped, not yet picked up.
- **in-progress** — `backlog-drain` is currently working it. An artifact left
  at `in-progress` after a tick ends means that tick crashed or was
  interrupted. Drain reports every stuck `in-progress` artifact at the start
  of each tick — they're never silently ignored — but it never re-picks or
  auto-recovers one. Resetting it is a human job: first check whether a PR was
  already opened for it (drain may have crashed *after* pushing, and a blind
  retry would open a second PR for the same fix), then clean up the stale
  worktree and branch as under **failed** below, then set `status: pending`.
- **done** — a PR was opened; see the artifact's `**Result:**` line for the URL.
  "Done" means PR opened, not merged.
- **failed** — drain attempted it and gave up; see `**Failure notes:**`, which
  also records where the failed attempt's worktree and branch were left. Not
  retried automatically. To retry: remove that worktree and branch
  (`git worktree remove <path>` then `git branch -D <branch>`) — git refuses to
  check out a branch that's still checked out in another worktree, so a retry
  with the old one on disk just fails again with a confusing, unrelated-looking
  error — then fix the artifact and reset it to `pending`.

## Producing artifacts

Use the `backlog-scope` skill (`/backlog-scope`) — it interviews you for the
sections above and writes the file with the right number and slug.

## Draining artifacts

Use the `backlog-drain` skill via `/loop backlog-drain` — it works through
`pending` artifacts one at a time, fully autonomously through to an opened PR.
See `docs/superpowers/specs/2026-08-11-backlog-workflow-design.md` for the
full design.

### Prerequisites

Before a drain run:

- **An authenticated `gh` CLI** (`gh auth status`) for the account that owns
  the fork, with push rights to `origin`. Every non-dry-run tick pushes a
  branch and runs `gh pr create` — no prompting, no fallback.
- **A supervised pilot tick.** This automation is new and has never completed a
  real unattended run, so before trusting it with the backlog: put one
  throwaway artifact scoping a trivial change into this directory, run a single
  tick with a human watching, and confirm the workflow actually received its
  `artifactPath` and came back with a real PR URL (not a dry-run-shaped
  result). Then close the throwaway PR and delete its branch, worktree, and
  artifact. `backlog-drain`'s "Before trusting an unattended run" section spells
  out exactly what to check.
- **A human watching the first few real ticks.** Start the loop, don't walk
  away from it. Each tick opens a PR against `playerbots-integration-gh`; there
  is no CI and no test suite to catch a bad one.

### Stopping a drain

Create `docs/backlog/.stop` (an empty file — `touch docs/backlog/.stop`) to
halt the loop. `backlog-drain` checks for it at the very start of each tick,
before picking a new artifact, so a stop request never aborts an issue
mid-implementation: whatever is already in flight finishes (commit, and PR if
it's a real run), and the loop halts before starting another. The sentinel
works whether or not anyone is watching the conversation. Drain reports a
done/failed/in-progress/pending summary, deletes the sentinel, and stops.

The loop also halts on its own when the backlog is drained, when two artifacts
fail in a row, or when a failure looks systemic (a broken workflow invocation
rather than a bad artifact) — in that last case the artifact is put back to
`pending` rather than marked `failed`.
