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
- **in-progress** — `backlog-drain` is currently working it.
- **done** — a PR was opened; see the artifact's `**Result:**` line for the URL.
- **failed** — drain attempted it and gave up; see `**Failure notes:**`. Not
  retried automatically — fix the artifact and reset to `pending` to retry.

## Producing artifacts

Use the `backlog-scope` skill (`/backlog-scope`) — it interviews you for the
sections above and writes the file with the right number and slug.

## Draining artifacts

Use the `backlog-drain` skill via `/loop backlog-drain` — it works through
`pending` artifacts one at a time, fully autonomously through to an opened PR.
See `docs/superpowers/specs/2026-08-11-backlog-workflow-design.md` for the
full design.
