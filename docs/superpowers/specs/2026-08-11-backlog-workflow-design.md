# Backlog Scoping & Autonomous Drain Workflow — Design

## Goal

A repeatable path from "I noticed something is wrong" to "a reviewable PR exists,"
covering both the existing backlog of known issues and new ones as they come up,
with minimal manual effort on the implementation side.

## Context

Tortoise-WoW is a large C++ mangos-based server fork (~1076 `.cpp` / ~942 `.h` files
under `src/`) running ~1000 playerbots. Most historical fixes trace back to something
observed live and root-caused afterward (see README's fix tables). The repo has:

- No CI (`.github/` contains only issue templates).
- No automated test suite (the `PlayerBots/.../tests/` directory is an in-game
  scripted bot-behavior harness, not something runnable headlessly).
- No build toolchain currently configured in the Claude Code session environment.
- `CONTRIBUTING.md` guidance: verify manually, in-game, before opening a PR.

This means the workflow's only real safety net beyond code review is a human
reading the PR and testing in-game — there's nothing to autonomously gate merges on.

This is a separate, unrelated effort from `docs/migration/migrationHandoff.md`,
which another agent is producing to reconcile this branch with pre-GitHub private
development. That work is out of scope here and untouched by this design.

## Architecture

Two decoupled phases, connected only through files on disk:

```
 Phase 1 (manual, now)              Phase 2 (autonomous, later)
 ─────────────────────              ───────────────────────────
 brain-dump idea                     scan docs/backlog/ for status:pending
     │                                        │
     ▼                                        ▼
 one-at-a-time clarification         pick lowest-numbered pending item
     │                                        │
     ▼                                        ▼
 write docs/backlog/NNN-slug.md      run per-issue Workflow graph
 (status: pending)                   (implement → review → verify → PR)
                                              │
                                              ▼
                                     flip status: done | failed
                                              │
                                              ▼
                                     more pending? loop : stop
```

Phase 1 can keep producing artifacts while Phase 2 drains previously-produced ones —
there's no ordering dependency between scoping new issues and draining old ones.

## Phase 1: Scoping

A brain-dumped idea goes through the same one-question-at-a-time clarification
used in this design conversation, scaled down to fit a single bug-fix-sized issue
rather than a full feature. Since Phase 2 has no mid-flight checkpoint, this is the
*only* checkpoint before code lands — the artifact must carry everything the
implementer will ever get.

### Artifact format

`docs/backlog/<NNN>-<slug>.md`, numbered for stable pick order:

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

**Notes:** anything else the implementer needs — related past fixes, things
already ruled out, constraints.
```

`status` lets Phase 2 find "next pending" and lets a directory listing show
backlog progress at a glance. `risk` doesn't gate anything (implementation is
fully autonomous to PR either way) but lands in the PR description to help
prioritize review.

## Phase 2: Autonomous drain

Self-paced loop (via the `loop` skill, no fixed interval). Each tick:

1. Check for a stop request: if `docs/backlog/.stop` exists, report a
   done/failed/pending summary, delete the sentinel, and stop the loop
   (`ScheduleWakeup({stop: true})`) without picking a new item.
2. Scan `docs/backlog/*.md` for `status: pending`; take the lowest-numbered one.
   If none remain, report a done/failed summary and stop the loop
   (`ScheduleWakeup({stop: true})`).
3. Flip the chosen artifact to `status: in-progress` before starting work, so a
   crash mid-tick can't cause it to be silently re-picked.
4. Run the per-issue Workflow graph (below) against it.
5. On success: flip to `status: done`, record the PR URL in the artifact.
6. On failure (unfixable review finding, agent got stuck): flip to
   `status: failed` with a `**Failure notes:**` explanation. Failed items are
   skipped on future ticks — a bad artifact can't stall the backlog. Triage is
   manual: fix the artifact and reset to `pending`, or drop it.
7. Schedule the next tick and repeat.

**Cadence:** runs to backlog-empty in one continuous drain, not paused every N
issues. Review happens whenever the PR queue is checked, not gated by the loop.

**Stopping mid-drain:** creating `docs/backlog/.stop` (an empty file, e.g. via
`touch`) at any time signals the loop to halt. It's checked at the start of
each tick — before a new issue is picked — so a stop request always finishes
whatever issue is already in flight rather than aborting mid-implementation
and leaving an orphaned branch/worktree. This works whether or not anyone is
actively watching the conversation.

### Branch strategy

Every issue's branch is cut independently from the current tip of
`cm-main` at the moment that issue's tick starts — branches
are **not** stacked on each other's unmerged work. Rationale:

- Artifacts are scoped to be independent problems, so most branches won't
  touch the same files at all.
- Stacking would make PR #2 depend on PR #1's unreviewed diff — rejecting #1
  would break #2, and #2 couldn't be reviewed in isolation.
- When two issues *do* overlap, an independent-branch setup surfaces as a
  normal GitHub merge conflict on whichever PR merges second — resolved by
  hand, not auto-reconciled mid-drain.

"Done" means "PR opened," not "PR merged." The loop never waits on a human
merge and never rebases anything.

### Per-issue Workflow graph

A named Workflow (`.claude/workflows/backlog-issue.js`), invoked by the drain
skill as `Workflow({name: "backlog-issue", args: {artifactPath}})`:

1. **Implement** — worktree-isolated, branch cut from `cm-main`.
   One agent implements the fix per the artifact's problem/cause/acceptance
   criteria.
2. **Review** — the real safety net given no CI/tests exist. Parallel
   multi-lens reviewers (correctness; pointer-lifetime/threading, since that's
   where several of this repo's past bugs lived) check the diff; findings are
   fixed inline before proceeding.
3. **Verify (best-effort)** — attempt a build only if a toolchain is actually
   configured in the run environment. If not, skip and say so plainly in the
   PR body rather than silently implying it compiles. Either way, the PR
   description flags manual in-game testing as the real verification gate,
   per `CONTRIBUTING.md`.
4. **PR** — commit, push, `gh pr create` against `cm-main`,
   referencing the artifact and its acceptance criteria.

## Packaging

New, additive-only structure (`.claude/skills/` and `.claude/workflows/` don't
exist in this repo yet):

- **`.claude/skills/backlog-scope/`** — Phase 1 skill. Conversational,
  one question at a time; writes `docs/backlog/<NNN>-<slug>.md`.
- **`.claude/skills/backlog-drain/`** — Phase 2 skill. Implements the tick
  loop (scan → pick → run graph → update status → schedule next), driven via
  the `loop` skill in self-paced mode.
- **`.claude/workflows/backlog-issue.js`** — the named Workflow script
  (Implement → Review → Verify → PR) described above.
- **`docs/backlog/`** — artifact directory. Read/written only by the two
  skills above. Also holds the `.stop` sentinel file (created by the user,
  deleted by `backlog-drain` once honored) used to halt the drain loop.

Nothing here touches `docs/migration/` or any other in-flight work.

## Risks / constraints carried forward

- No CI and no headless test suite: review quality in step 2 of the
  per-issue graph is the only automated defense against a bad autonomous
  change; a human still has to test in-game before trusting a fix.
- No build toolchain detected in the current session: "verify" starts as
  best-effort/skippable and should be revisited once a toolchain is
  available to the agent, so the build step becomes a real gate rather than
  a note in the PR body.
- Running to backlog-empty means PRs can accumulate faster than they're
  reviewed — acceptable per current preference, but worth revisiting if the
  backlog turns out to be large.
