# Backlog Drain Loop Refinements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement all six refinements identified in the loop-refinement handoff after the backlog drain's first real run (9 artifacts, 8 PRs, 4 merge conflicts) — dependency-aware branch stacking, a review-gate tiebreaker, unique migration filenames, a `blocked` outcome distinct from `failed`, per-agent model/effort tuning, and orphaned-branch cleanup.

**Architecture:** All six items land as edits to four existing files (`docs/backlog/README.md`, `.claude/skills/backlog-scope/SKILL.md`, `.claude/workflows/backlog-issue.js`, `.claude/skills/backlog-drain/SKILL.md`) — no new files, no new subsystems. `backlog-issue.js` already threads one `BASE_BRANCH` constant through every phase via template literals, so most of the risk in this plan is *not* algorithmic complexity, it's several tasks wanting to touch the same ~320-line file. The task order and PR layering below exist specifically to keep those touches from colliding with each other.

**Tech Stack:** Claude Code project skills (`.claude/skills/*/SKILL.md`), a Workflow script (`.claude/workflows/backlog-issue.js`, plain JS), Markdown artifacts with YAML frontmatter, `git`/`gh` CLI.

**Spec:** The six items and their evidence live in `docs/superpowers/plans/2026-08-12-loop-refinement-handoff.md` (written after the first real drain run) and its two linked companions: `docs/superpowers/specs/2026-08-11-backlog-workflow-design.md` (§"Field report: the first real drain" and §"Proposed change — targeted stacking" for item 1's design) and `docs/superpowers/plans/2026-08-12-backlog-issue-model-tuning.md` (the full per-agent override table for item 5). `docs/superpowers/plans/2026-08-12-transport-stack-merge.md`'s conflict matrix is the evidence base for the PR-layering decisions below — nearly every real conflict in that matrix (`#10`+`#11`, `#10`+`#15`, `#9`+`#12`, `#9`+`#16`, `#14`+`#16`, `#13`+`#16`) was dependency-shaped, which is exactly what item 1 targets.

## Global Constraints

- Workflow scripts are plain JavaScript, not TypeScript — no type annotations, no `Date.now()`/`Math.random()`/argless `new Date()`.
- **Workflow scripts have no filesystem or Node.js API access.** `backlog-issue.js` cannot itself read an artifact's `depends-on:` frontmatter or run `git merge-base`. Any logic that needs to inspect files or run git commands to make a *decision* (not just execute an agent's instructions) belongs in `backlog-drain`'s SKILL.md (which runs as an agent, with full tool access) — `backlog-drain` resolves the decision and passes the *result* to `backlog-issue.js` as a plain-value `args` field.
- This fork's trunk is `cm-main`. `playerbots-integration-gh` is a pristine fast-forward-only mirror of upstream that is never committed to directly (`docs/BRANCHING.md`) — this was already fixed in the live code (commit `a2c9c5f`) after the original 2026-08-11 plan shipped with the wrong name; nothing in this plan should reintroduce it.
- `gh` commands must pin `--repo ChrisMiho/tortoise-wow` explicitly — without it, `gh` resolves against upstream (the fork's parent), not this fork.
- **All merges into `cm-main` in this repo's history are ordinary merge commits** (`git log` shows `Merge pull request #N from ...`), not squashes. Item 3 (depends-on stacking) relies on `git merge-base --is-ancestor` to detect whether a dependency's branch has merged, which only works for real merge commits. If this repo's merge strategy ever changes to squash-merge, that check silently stops detecting merges.
- This repo has no CI and no headless test suite. The Review phase in `backlog-issue.js` is the only automated correctness check for backlog-drained changes — every task below that touches it must not weaken that gate.
- No task in this plan runs a real (non-dry-run) `backlog-issue` invocation. All verification below uses `dryRun: true`, hand-traced logic against throwaway fixtures, or hand-traced logic against the real, already-documented scenarios from the first drain run (ticks 7 and 9) — never a live push or `gh pr create`.
- Artifact numbering is a zero-padded 3-digit prefix (`NNN-slug.md`), lowest-first pick order — this plan extends, not replaces, that rule.
- **Build/runtime constraints, inherited from `docs/superpowers/plans/2026-08-11-docker-build-from-this-checkout.md` and confirmed still binding by `docs/superpowers/plans/2026-08-12-transport-stack-merge.md` — all apply to Tasks 7-9:**
  - `BUILD_PLAYERBOTS=ON` defaults **OFF**; a build without it yields a bot-free server with no warning anywhere.
  - `CMAKE_INSTALL_PREFIX=/opt/turtle` is compiled in; changing it means no `aiplayerbot.conf` and no bots.
  - Compose project name is pinned to `tortoise-wow-v2`; `tortoise-wow-v2_dbdata` is `external`. **Never `docker compose down -v`** — that volume is the entire world.
  - Docker VM is 4 CPUs / 8 GB; `-j2` is the build parallelism that fits without inviting the OOM killer.
  - `docker build` runs fine from Windows (the build context is just the repo directory). The **runtime** step (`docker compose up`, external volume, `/mnt/d`-relative bind mounts) needs WSL integration enabled in Docker Desktop.
  - A wrapped one-liner (`wsl -d Ubuntu -- bash -lc '...'`) containing variables silently returns plausible-but-wrong output rather than failing — use a script file invoked from PowerShell instead, never an inline wrapped one-liner with `$var` substitution.
- **Session permissions:** an autonomous drain session runs under `.claude/settings.autonomous.json` (`defaultMode: "bypassPermissions"`), so Tasks 7-9's agents can run `docker`/`git`/PowerShell commands without a permission prompt. This does **not** guarantee Docker Desktop is running or WSL integration is enabled — that's an operational precondition on the human running the drain, not something any command can flip. The batch-validate step (Task 8) must still preflight-check and skip cleanly rather than assume.
- This is a **single-developer, no-live-players development server.** "Validate in-game" throughout Tasks 7-9 means the one human running the drain logging in themselves, once, after being handed a build ID and a checklist — never automated login/scripted play as a simulated player, and never assume anyone else might be online.

## PR Layering Strategy

Six tasks, four shared files, and the file most of them touch (`backlog-issue.js`) is exactly the file whose merge-conflict cost this whole plan exists to reduce — so the task order below is deliberately not the handoff doc's priority order. It's ordered to minimize how much any later task has to rebase around an earlier one:

| Task | Touches | Depends on | Can be its own PR? |
| --- | --- | --- | --- |
| 1. Vocabulary foundation | `README.md`, `backlog-scope/SKILL.md` | nothing | Yes — land first, nothing else needs to wait on it logically, but everything else's frontmatter/status text assumes it exists |
| 2. Mechanical fixes (migration filenames + model/effort) | `backlog-issue.js` (narrow, non-overlapping regions) | nothing | Yes — fully independent of every other task's *logic*. Sequenced 2nd only so Task 3's bigger diff isn't written against a file that's about to change underneath it |
| 3. Dependency-aware branch cutting | `backlog-issue.js` (top-of-file `BASE_BRANCH`), `backlog-drain/SKILL.md` (pick logic) | Task 1 (needs `depends-on:` field) | Yes, once Task 1 is merged |
| 4. Review-gate tiebreaker (contested) | `backlog-issue.js` (Review→PR transition), `backlog-drain/SKILL.md` (outcome recording) | Task 1 (needs `contested` status), Task 3 (touches the same PR-body-building code Task 3 parametrizes) | Yes, once Tasks 1 and 3 are merged |
| 5. Blocked vs. failed | `backlog-issue.js` (Implement/Review schemas, top-of-phase checks), `backlog-drain/SKILL.md` (outcome recording) | Task 1 (needs `blocked` status) | Yes, once Task 1 is merged. Touches different lines than Task 4 (Implement-phase and top-of-Review vs. Review's fix-handling block) — **can run in parallel with Task 4 off the same Task 3 tip**, at the cost of a small rebase on whichever merges second, since both edit `backlog-issue.js`'s Review phase region |
| 6. Worktree/branch leak sweep | `backlog-drain/SKILL.md` only (new step, zero overlap with Tasks 1–5's lines) | nothing | Yes, fully independent — safe to do first, last, or in parallel with anything |
| 7. Split Implement+Review from Verify+PR | `backlog-issue.js` (large restructure) | Tasks 1, 3, 4, 5 (moves code those tasks wrote) | No — must land after Tasks 1-6 are all merged; this task's diff assumes their code already exists |
| 8. `backlog-batch.js` — integrate, build once, spin the stack up/down | New file `.claude/workflows/backlog-batch.js` | Task 7 (consumes its trimmed return shape) | Yes, once Task 7 is merged |
| 9. `backlog-drain` batch cadence | `backlog-drain/SKILL.md` (picking/tick logic) | Tasks 7, 8 | No — ties 7 and 8 together into the actual loop behavior |

Tasks 7-9 are a **second wave**, added after Tasks 1-6 were already scoped, in response to two follow-up asks: batching the ~40-minute build so N artifacts don't cost N builds, and giving each PR a concrete build ID plus an in-game checklist since this is a single-developer server with no other tester. They deliberately build **on top of** Tasks 1-6 rather than alongside them — Task 7's diff literally moves PR-body code that Tasks 3 and 4 write, so it cannot be developed in parallel with them without guaranteed conflicts. Tasks 1-6 remain a complete, independently valuable improvement on their own if this second wave is deferred.

**The one thing this plan deliberately does *not* do:** stack Tasks 3, 4, and 5 as three independent PRs all cut from `cm-main` in parallel. That would reproduce, inside this very meta-work, the exact silent-revert-conflict shape the field report documented — Task 4 and Task 5 both edit code adjacent to the `BASE_BRANCH`-aware blocks Task 3 introduces. Do Task 3 first and land it before starting 4 or 5.

---

## Task 1: Artifact vocabulary foundation

**Files:**
- Modify: `docs/backlog/README.md:12-29` (frontmatter block), `docs/backlog/README.md:31-61` (Lifecycle section)
- Modify: `.claude/skills/backlog-scope/SKILL.md:17-28` (interview bullets), `.claude/skills/backlog-scope/SKILL.md:34-35` (write-artifact step)

**Interfaces:**
- Produces: the `depends-on:` frontmatter field and the `contested`/`blocked` status values, consumed by Tasks 3, 4, 5.

- [ ] **Step 1: Extend the frontmatter block and document dependencies**

Replace the frontmatter code block in `docs/backlog/README.md` (currently lines 12-17):

```markdown
---
status: pending        # pending | in-progress | done | failed | out-of-scope
risk: low               # low | medium | high — informational, not a gate
area: playerbots/battlegrounds
---
```

with:

```markdown
---
status: pending        # pending | in-progress | implemented | done | contested | blocked | failed | out-of-scope
risk: low               # low | medium | high — informational, not a gate
area: playerbots/battlegrounds
depends-on:              # optional: NNN-slug.md this must land after; omit if none
---
```

Add three entries to the Lifecycle section (currently lines 31-61), after the existing `out-of-scope` entry:

```markdown
- **implemented** — `backlog-issue` finished Implement and Review and the
  change is committed to a local branch, but no build, in-stack validation,
  or PR has happened yet. Artifacts wait here until `backlog-drain` has
  accumulated enough of them (or run out of `pending` work) to run one batch
  build instead of one per artifact — see
  `docs/superpowers/plans/2026-08-13-backlog-drain-refinements.md` Tasks 7-9.
  Carries a `**Base:**` line recording the branch it was cut from and, if
  applicable, the dependency it's stacked on.
- **contested** — a PR was opened, but a review finding and the implementer
  disagreed and neither could be resolved autonomously. The PR body flags the
  dispute under a "Contested" heading with the finding and the implementer's
  rebuttal. Not the same as `done` — read the disputed finding before trusting
  the diff. Does not count toward the two-consecutive-failure circuit breaker.
- **blocked** — the Implement or Review phase determined the acceptance
  criteria cannot be satisfied in this environment (missing data, missing
  tooling, a decision only a human can make) — distinct from `failed` (a
  fixable defect the implementer got wrong). Not retried automatically and,
  like `out-of-scope`, does not count toward the circuit breaker. Triage:
  either edit the artifact to remove the blocking constraint and reset to
  `pending`, or reclassify to `out-of-scope` if it's permanently infeasible.
```

Add a new section after Lifecycle, before "Producing artifacts":

```markdown
## Dependencies

Set `depends-on: <NNN>-<slug>.md` in an artifact's frontmatter when its fix
can only be correctly implemented on top of another artifact's change — e.g.
it edits a function the dependency adds, or its acceptance criteria assume
the dependency's fix already landed. Leave the field blank for artifacts that
don't depend on unmerged work — most artifacts have no dependency.

`backlog-drain` skips a pending artifact whose `depends-on` target has not
yet reached `status: done`, picking the next eligible pending artifact
instead. Once eligible, `backlog-issue` cuts that artifact's branch from the
dependency's branch (not from `cm-main`) whenever the dependency's PR is
still unmerged, falling back to `cm-main` once it has merged. See
"Branch strategy" in `docs/superpowers/specs/2026-08-11-backlog-workflow-design.md`
for the rationale and the field-report evidence behind it.

A chain (or cycle) of `depends-on` values that never resolves shows up as
every remaining `pending` artifact being ineligible — `backlog-drain` reports
that and stops rather than looping forever.
```

- [ ] **Step 2: Add dependency and feasibility questions to the scope interview**

In `.claude/skills/backlog-scope/SKILL.md`, add two bullets to the numbered list in step 2 (after the existing `Area` bullet, currently ending at line 28):

```markdown
   - **Depends on:** does this fix require another *unimplemented or
     unmerged* backlog artifact's change to already exist — e.g. it edits a
     function that artifact adds, or its acceptance criteria assume that fix
     landed first? If so, ask which artifact (list `docs/backlog/*.md` if the
     user doesn't remember the number) and record it as
     `depends-on: <NNN>-<slug>.md`. Most artifacts have no dependency — leave
     it blank rather than guessing one.
   - **Feasibility:** does implementing or verifying this require data, an
     environment, or a build/tooling capability that might not exist in the
     drain's environment (a specific database state, a running server,
     in-game content that must already exist)? If there's real doubt, note it
     under **Notes** so `backlog-issue` can recognize an infeasible
     acceptance criterion instead of treating it as an ordinary
     implementation failure.
```

Update step 5 ("Write the artifact", currently line 34) to add: "Include `depends-on:` in the frontmatter if step 2 identified one; omit the line's value (leave it blank) otherwise."

- [ ] **Step 3: Verify no other file hardcodes the old status/frontmatter shape**

Run:

```bash
grep -rn "pending | in-progress | done | failed | out-of-scope" .claude/ docs/
grep -rln "depends-on\|contested\|blocked" .claude/skills/backlog-drain/SKILL.md .claude/workflows/backlog-issue.js
```

Expected: the first command finds nothing (confirms no other copy of the old enum comment exists to go stale). The second finds nothing yet — `backlog-drain` and `backlog-issue.js` don't reference these new values until Tasks 3-5 add them. Read both modified files back and confirm the frontmatter block and Dependencies section render as valid Markdown (no unclosed code fences).

- [ ] **Step 4: Commit**

```bash
git add docs/backlog/README.md .claude/skills/backlog-scope/SKILL.md
git commit -m "backlog: add depends-on field and contested/blocked statuses to artifact format"
```

---

## Task 2: Mechanical fixes — unique migration filenames and per-agent model/effort tuning

**Files:**
- Modify: `.claude/workflows/backlog-issue.js:136-168` (Implement phase prompt), `.claude/workflows/backlog-issue.js:186-320` (agent() call sites)

**Interfaces:**
- Consumes: nothing new.
- Produces: no change to the Workflow's `args`/return shape — purely internal prompt and options changes.

- [ ] **Step 1: Instruct the Implement phase to disambiguate migration filenames**

The migration-filename collision in the first run (`#15` and `#16` both wrote `sql/database_updates/20260812120000_world.sql`) happened because the naming convention (`sql/touch_migration.sh` / `sql/make_migration.bat`, both producing `<YYYYMMDDHHmmss>_world.sql`) has second-level resolution but nothing artifact-specific — two ticks landing in the same window produce indistinguishable names.

In the Implement phase's prompt (`.claude/workflows/backlog-issue.js:136-168`), add a paragraph after the existing "On that branch, implement exactly what..." paragraph:

```javascript
   If this fix requires a new SQL migration under sql/database_updates/,
   generate its filename with sql/touch_migration.sh (or sql/make_migration.bat
   on Windows) to get a real UTC timestamp — do not hand-write a timestamp.
   Then rename the resulting file to insert this artifact's number before the
   suffix: <timestamp>_${artifactLabel.match(/(\d{3})-/)?.[1] || 'XXX'}_world.sql
   instead of <timestamp>_world.sql. This guarantees uniqueness even if
   another tick generates a migration with the same timestamp — the artifact
   number differs by construction.
```

Note: `artifactLabel` is already computed above the `phase('Implement')` call (`.claude/workflows/backlog-issue.js:103-107`) as the repo-relative artifact path, e.g. `docs/backlog/016-foo.md` — the regex extracts `016`. Since this is inside a JS template literal building a prompt *string*, the `${...}` expression evaluates at script-run time before the prompt is sent, so the agent receives a literal 3-digit number, not the expression text.

- [ ] **Step 2: Apply the proposed per-agent model/effort overrides**

Per the table in `docs/superpowers/plans/2026-08-12-backlog-issue-model-tuning.md`, add `model`/`effort` to four of the six `agent()` calls' options objects (leave `implement` and `fix` on the session default — they're the two calls where the strong tier earns its keep):

`.claude/workflows/backlog-issue.js:265` (a review lens call inside the `parallel` map — currently `{ phase: 'Review', label: \`review:${lens.key}\`, schema: REVIEW_SCHEMA }`):

```javascript
    { phase: 'Review', label: `review:${lens.key}`, schema: REVIEW_SCHEMA, effort: 'medium' }
```

`.claude/workflows/backlog-issue.js:262` (the Verify call, currently `{ phase: 'Verify', label: 'verify' }`):

```javascript
  { phase: 'Verify', label: 'verify', model: 'sonnet', effort: 'low' }
```

`.claude/workflows/backlog-issue.js:304` (the PR call, currently `{ phase: 'PR', label: 'open-pr', schema: PR_SCHEMA }`):

```javascript
  { phase: 'PR', label: 'open-pr', schema: PR_SCHEMA, model: 'sonnet', effort: 'low' }
```

Add a comment directly above the `REVIEW_SCHEMA`/lens definitions noting the escalation carve-out from the tuning doc:

```javascript
// Review lenses run at medium effort by default (see
// docs/superpowers/plans/2026-08-12-backlog-issue-model-tuning.md) — but keep
// the session's full tier for artifacts with risk: high, where a mid-tier
// lens is more likely to miss a subtle finding. There is no automated
// escalation yet; a human editing this file for a high-risk drain run should
// drop the effort override for that run.
```

- [ ] **Step 3: Dry-run smoke test**

Write a throwaway fixture identical in shape to the original plan's smoke-test fixture:

```markdown
---
status: pending
risk: low
area: docs
---

# Smoke-test fixture for backlog-issue.js model/effort tuning

**Problem:** not a real bug — exercises the Implement/Review/Verify/PR phases
with the new per-agent model/effort options and confirms nothing throws.

**Suspected cause / area:** docs/backlog/README.md

**Acceptance criteria:** a single-line HTML comment `<!-- smoke-tested -->` is
added as the very first line of docs/backlog/README.md, and nothing else
changes.

**Notes:** run with dryRun: true — this fixture is deleted after the test.
```

Save to `docs/backlog/000-tuning-smoke-test.md`. Invoke:

```
Workflow({ scriptPath: ".claude/workflows/backlog-issue.js", args: { artifactPath: "docs/backlog/000-tuning-smoke-test.md", dryRun: true } })
```

Expected: completes all four phases with no exceptions, returns `{ success: true, dryRun: true, branchName: "backlog/tuning-smoke-test", verifyNote: "..." }`. Confirm in the run's progress output that the review lens agents and the verify agent actually ran at the overridden model/effort (not silently ignored) — the Workflow tool surfaces per-agent model/effort in its progress display.

- [ ] **Step 4: Clean up**

```bash
git worktree list
git worktree remove <path shown for the backlog/tuning-smoke-test worktree>
git branch -D backlog/tuning-smoke-test
rm docs/backlog/000-tuning-smoke-test.md
```

- [ ] **Step 5: Commit**

```bash
git add .claude/workflows/backlog-issue.js
git commit -m "backlog-issue: disambiguate migration filenames by artifact number, tune agent tiers"
```

---

## Task 3: Dependency-aware branch cutting (targeted stacking)

**Files:**
- Modify: `.claude/workflows/backlog-issue.js:124-134` (the `BASE_BRANCH` constant and its comment), `.claude/workflows/backlog-issue.js:280-305` (PR body, to note stacking)
- Modify: `.claude/skills/backlog-drain/SKILL.md:68-70` (step 5, picking), `.claude/skills/backlog-drain/SKILL.md:73-87` (step 7, the Workflow call), `.claude/skills/backlog-drain/SKILL.md:274-276` (Notes, branch-independence claim)

**Interfaces:**
- Consumes: `depends-on:` frontmatter (Task 1).
- Produces: two new optional `Workflow` args, `baseBranch` (string, `"cm-main"` or `"backlog/<slug>"`) and `dependsOnPrUrl` (string, only meaningful when `baseBranch` isn't `"cm-main"`) — consumed by `backlog-issue.js`. `backlog-drain` becomes responsible for resolving both before calling `Workflow`.

- [ ] **Step 1: Make `BASE_BRANCH` a validated, caller-supplied value instead of a constant**

Replace `.claude/workflows/backlog-issue.js:124-128`:

```javascript
// This fork's trunk is cm-main, not playerbots-integration-gh -- the latter is a
// pristine fast-forward-only mirror of upstream that is never committed to
// directly (see docs/BRANCHING.md). Every branch/diff/PR-base below must point
// at cm-main.
const BASE_BRANCH = 'cm-main'
```

with:

```javascript
// This fork's trunk is cm-main, not playerbots-integration-gh -- the latter is a
// pristine fast-forward-only mirror of upstream that is never committed to
// directly (see docs/BRANCHING.md).
//
// BASE_BRANCH is normally cm-main, but backlog-drain resolves it to a
// dependency's own backlog/<slug> branch when the artifact declares
// depends-on: and that dependency's PR hasn't merged yet -- targeted
// stacking, not a fresh cm-main cut every tick. Every phase below already
// references BASE_BRANCH by template literal, so this is the only line that
// needs to change for stacking to propagate through Implement/Review/Verify/PR.
//
// Validated rather than trusted, same reasoning as branchName below: this
// flows straight into "git fetch", "git diff", and "gh pr create --base", so
// an unexpected value fails safe to cm-main rather than being passed through.
const BASE_BRANCH_PATTERN = /^(cm-main|backlog\/[a-z0-9][a-z0-9_-]*)$/
const requestedBaseBranch = typeof normalizedArgs.baseBranch === 'string' ? normalizedArgs.baseBranch.trim() : ''
let BASE_BRANCH
if (BASE_BRANCH_PATTERN.test(requestedBaseBranch)) {
  BASE_BRANCH = requestedBaseBranch
} else {
  if (requestedBaseBranch) {
    log(`baseBranch was not a recognized ref (got ${JSON.stringify(normalizedArgs.baseBranch)}) -- defaulting to cm-main`)
  }
  BASE_BRANCH = 'cm-main'
}

// Only meaningful when BASE_BRANCH !== 'cm-main' -- the dependency's PR URL,
// so the PR phase can link it in a stacked PR's body. Not validated as
// strictly as PR_URL_PATTERN below (it's advisory text in a PR body, not a
// value this script acts on), but coerced to a string so a non-string value
// can't break the template literal it's interpolated into.
const dependsOnPrUrl = typeof normalizedArgs.dependsOnPrUrl === 'string' ? normalizedArgs.dependsOnPrUrl.trim() : ''
```

This must come after `normalizedArgs` is computed (`.claude/workflows/backlog-issue.js:83-93`) and before `phase('Implement')` — it sits in the same location the old constant did, no other reordering needed.

- [ ] **Step 2: Note the stacked base in the PR body**

In the PR phase (`.claude/workflows/backlog-issue.js:271-278`, the existing `minorSection` block), add a sibling block above it:

```javascript
const stackedSection = BASE_BRANCH !== 'cm-main'
  ? `
   0. A line before everything else: "Stacked on ${dependsOnPrUrl || BASE_BRANCH} — merge that first; this PR's diff will shrink once it does."`
  : ''
```

Then in the PR agent's prompt body list (`.claude/workflows/backlog-issue.js:293-299`), prepend `${stackedSection}` immediately before item `1.` so it renders first when present:

```javascript
   Body must include, in this order:${stackedSection}
   1. The backlog artifact this implements: ${artifactLabel}
```

- [ ] **Step 3: Resolve dependency state in `backlog-drain` before picking**

Replace step 5 in `.claude/skills/backlog-drain/SKILL.md:68-70`:

```markdown
5. Otherwise, pick the file with the lowest numeric prefix among those with
   `status: pending`, ignoring any files without a valid 3-digit `NNN-`
   numeric prefix.
```

with:

```markdown
5. Otherwise, pick the file with the lowest numeric prefix among those with
   `status: pending` (ignoring any files without a valid 3-digit `NNN-`
   numeric prefix) **whose dependency is ready**:
   - No `depends-on:` value (or blank) — ready, pick it.
   - `depends-on: <NNN>-<slug>.md` set — read that file's frontmatter. Ready
     only if its `status` is `done`. Anything else (`pending`, `in-progress`,
     `contested`, `blocked`, `failed`, `out-of-scope`, or the file missing
     entirely) means not ready: skip this candidate and check the
     next-lowest-numbered pending file instead.
   - If every remaining `pending` file is blocked on an unready dependency,
     this is a terminal tick: report each blocked artifact by path and what
     it's waiting on, call `ScheduleWakeup({ stop: true })`, and stop. Do not
     pick anything. (A dependency cycle surfaces here too, indistinguishable
     from an ordinary not-yet-drained dependency — both are reported the same
     way and require a human to look.)
5a. Resolve the base branch for the picked artifact:
   - No `depends-on:` — `baseBranch: "cm-main"`, no `dependsOnPrUrl`.
   - `depends-on:` set (and therefore, per step 5, that dependency's
     `status: done`) — determine whether its PR already merged:
     ```
     git fetch origin cm-main
     git merge-base --is-ancestor origin/backlog/<dep-slug> origin/cm-main
     ```
     Exit code `0` means it already merged — use `baseBranch: "cm-main"`
     (nothing left to stack on). Non-zero means it's still open — use
     `baseBranch: "backlog/<dep-slug>"`, and read the dependency artifact's
     `**Result:** PR opened at <url>` line for `dependsOnPrUrl`.
```

- [ ] **Step 4: Pass the resolved values into the Workflow call**

Update step 7 in `.claude/skills/backlog-drain/SKILL.md:73-87` — the `Workflow(...)` invocation shown there gains two fields:

```
Workflow({ name: "backlog-issue", args: { artifactPath: "<absolute path to that file>", dryRun: false, baseBranch: "<resolved in step 5a>", dependsOnPrUrl: "<resolved in step 5a, or omit if none>" } })
```

- [ ] **Step 5: Correct the now-false branch-independence claim in Notes**

Replace `.claude/skills/backlog-drain/SKILL.md:274-276`:

```markdown
- Branches are independent — each is cut fresh from
  `origin/cm-main` when its tick starts, never from another
  backlog branch.
```

with:

```markdown
- Branches are cut fresh from `origin/cm-main` when their tick starts, unless
  the artifact declares `depends-on:` on a still-unmerged dependency — see
  `docs/backlog/README.md#dependencies` — in which case the branch is cut
  from the dependency's branch instead. Independent artifacts (no
  `depends-on:`, or one whose dependency already merged) keep the original
  failure isolation: one artifact failing costs nothing to any other branch.
```

- [ ] **Step 6: Smoke-test the picking and resolution logic by hand**

Create two throwaway fixtures:

```bash
cat > docs/backlog/001-depends-test-a.md <<'EOF'
---
status: done
risk: low
area: docs
---

# Depends-on smoke test A (the dependency)

**Problem:** fixture only.
**Suspected cause / area:** n/a
**Acceptance criteria:** n/a

**Result:** PR opened at https://github.com/ChrisMiho/tortoise-wow/pull/9999
EOF

cat > docs/backlog/002-depends-test-b.md <<'EOF'
---
status: pending
risk: low
area: docs
depends-on: 001-depends-test-a.md
---

# Depends-on smoke test B (depends on A)

**Problem:** fixture only.
**Suspected cause / area:** n/a
**Acceptance criteria:** n/a
EOF
```

Following the updated step 5/5a by hand (without calling `Workflow`): confirm `002-depends-test-b.md` is picked (its dependency is `done`), and that resolving its base branch requires checking `git merge-base --is-ancestor origin/backlog/depends-test-a origin/cm-main` — since that branch doesn't really exist, the command errors rather than exiting cleanly 0 or 1. Note this as an edge case the real logic must handle: **treat a `git merge-base` command that errors (rather than a clean non-zero "not an ancestor" exit) as "the dependency branch isn't on origin" — this can only happen if the dependency artifact's `status: done` is stale or wrong (the branch was deleted after merging, or never pushed), and should be treated as "not ready" (fall through to the next pending artifact and log a warning), not as "stack on it."** Add this handling explicitly to step 5a in the SKILL.md edit above before moving on (revise Step 3 of this task if you're implementing top-to-bottom).

Now flip `001-depends-test-a.md` to `status: pending` and re-trace step 5: confirm `002-depends-test-b.md` is now skipped (dependency not ready) and `001-depends-test-a.md` is picked instead (no `depends-on:` of its own).

- [ ] **Step 7: Dry-run the propagation through `backlog-issue.js`**

```
Workflow({ scriptPath: ".claude/workflows/backlog-issue.js", args: { artifactPath: "<abs path to 002-depends-test-b.md>", dryRun: true, baseBranch: "backlog/depends-test-a", dependsOnPrUrl: "https://github.com/ChrisMiho/tortoise-wow/pull/9999" } })
```

Expected: the dry-run log line reads `[dry run] would push backlog/depends-test-b and open a PR against backlog/depends-test-a on ChrisMiho/tortoise-wow` — confirming `BASE_BRANCH` resolved to the dependency branch, not `cm-main`. (The Implement phase will actually try to `git fetch origin backlog/depends-test-a`, which doesn't exist — expect this specific dry run to fail at the Implement phase with a git error, which is fine; the goal is confirming the *value* threading, not a full clean run against a fixture with no real dependency branch.)

- [ ] **Step 8: Clean up**

```bash
rm docs/backlog/001-depends-test-a.md docs/backlog/002-depends-test-b.md
git worktree list
# remove any worktree/branch left by step 7's partial run, if one was created
```

- [ ] **Step 9: Commit**

```bash
git add .claude/workflows/backlog-issue.js .claude/skills/backlog-drain/SKILL.md
git commit -m "backlog-drain: cut dependent issues from their prerequisite's branch, not cm-main"
```

---

## Task 4: Review-gate tiebreaker — contested outcome

**Files:**
- Modify: `.claude/workflows/backlog-issue.js:219-247` (blocking-findings handling), `.claude/workflows/backlog-issue.js:280-320` (PR body and return shape)
- Modify: `.claude/skills/backlog-drain/SKILL.md:88-118` (step 8, outcome recording), `.claude/skills/backlog-drain/SKILL.md:168-173` (step 10, commit subject)

**Interfaces:**
- Consumes: `contested` status (Task 1), the existing `FIX_SCHEMA.unresolved` array (already carries "which finding and why it wasn't fixed" per its existing prompt).
- Produces: a new success-shaped return value `{ success: true, contested: true, branchName, prUrl }`, consumed by `backlog-drain`.

**Design choice:** when the implementer and a review lens can't be reconciled, open a PR anyway, clearly marked contested, rather than stranding the work on an unpushed branch as `failed`. This matches the architecture's own stated principle ("review happens at the PR, not before") and doesn't reduce safety — nothing merges without a human either way, but the work becomes visible and attributable instead of invisible.

- [ ] **Step 1: Distinguish a real rebuttal from a fix agent that just didn't produce anything**

Replace `.claude/workflows/backlog-issue.js:227-247`:

```javascript
if (blocking.length > 0) {
  const fixResult = await agent(
    `On branch "${branchName}", fix these blocking review findings, then amend
     or add a commit:
     ${blocking.map((f) => `- ${f.file}: ${f.summary}`).join('\n')}

     Return fixed: true only if every finding above is actually addressed by a
     commit on that branch. If any of them can't or shouldn't be fixed
     (contradictory acceptance criteria, out of scope, not a real defect),
     return fixed: false and put one entry per unfixed finding in unresolved,
     each saying which finding it is and why it wasn't fixed. Do not report
     fixed: true with caveats — this run only proceeds to a PR on fixed: true.`,
    { phase: 'Review', label: 'apply-fixes', schema: FIX_SCHEMA }
  )
  if (!fixResult || fixResult.fixed !== true) {
    const unresolved = fixResult && Array.isArray(fixResult.unresolved) && fixResult.unresolved.length > 0
      ? fixResult.unresolved.join('; ')
      : describe(fixResult)
    return { success: false, reason: `blocking findings not addressed: ${unresolved}`, branchName }
  }
}
```

with:

```javascript
let contestedFindings = null
if (blocking.length > 0) {
  const fixResult = await agent(
    `On branch "${branchName}", fix these blocking review findings, then amend
     or add a commit:
     ${blocking.map((f) => `- ${f.file}: ${f.summary}`).join('\n')}

     Return fixed: true only if every finding above is actually addressed by a
     commit on that branch. If any of them can't or shouldn't be fixed
     (contradictory acceptance criteria, out of scope, not a real defect),
     return fixed: false and put one entry per unfixed finding in unresolved,
     each saying which finding it is and, specifically, WHY you believe it's
     wrong or shouldn't be fixed -- this rebuttal goes verbatim into the PR
     body for a human to adjudicate, so make the actual argument, not just
     "disagreed". Do not report fixed: true with caveats.`,
    { phase: 'Review', label: 'apply-fixes', schema: FIX_SCHEMA }
  )
  const hasRebuttal = fixResult && Array.isArray(fixResult.unresolved) && fixResult.unresolved.length > 0
  if (!fixResult || (fixResult.fixed !== true && !hasRebuttal)) {
    // No usable result at all -- not a defensible disagreement, a broken fix attempt.
    return { success: false, reason: `blocking findings not addressed: ${describe(fixResult)}`, branchName }
  }
  if (fixResult.fixed !== true) {
    contestedFindings = fixResult.unresolved
  }
}
```

- [ ] **Step 2: Build the contested section of the PR body and mark the title**

In the PR phase, extend the `stackedSection`/`minorSection` pattern from Task 3/existing code with a third section (add after `minorSection`'s definition, `.claude/workflows/backlog-issue.js:271-278`):

```javascript
const contestedSection = contestedFindings
  ? `
   ${minorSection ? '8' : '7'}. A section headed "Contested — needs manual adjudication", explaining
      that a review finding and the implementer disagreed and neither could be
      resolved automatically, then listing exactly these and nothing else, one
      bullet per line:
${contestedFindings.map((u) => `      - ${u}`).join('\n')}`
  : ''
```

Update the PR agent's title instruction (`.claude/workflows/backlog-issue.js:291`) — replace:

```javascript
   Title: a short summary of the fix, in this repo's existing commit-message voice.
```

with:

```javascript
   Title: ${contestedFindings ? '"[contested] " followed by a' : 'a'} short summary of the fix, in this repo's existing commit-message voice.
```

And append `${contestedSection}` to the body list (`.claude/workflows/backlog-issue.js:299`, right after the existing manual-testing line and before `${minorSection}`):

```javascript
   6. A line stating manual in-game testing is still required before merge${minorSection}${contestedSection}
```

(Note added post-authoring, during SDD Task 4's review: the original draft here read `${contestedSection}${minorSection}` — the wrong order given `contestedSection`'s own numbering above is conditional on `minorSection` being present (`8` when it is, `7` when it isn't). `${minorSection}${contestedSection}` is correct: when both are present, minor renders first as `7.` and contested second as `8.`, matching their labels; when only `contestedSection` is present, it's alone and correctly labeled `7.`.)

- [ ] **Step 3: Return the contested flag**

Replace the final return (`.claude/workflows/backlog-issue.js:319`):

```javascript
return { success: true, branchName, prUrl: trimmedPrUrl }
```

with:

```javascript
return contestedFindings
  ? { success: true, contested: true, branchName, prUrl: trimmedPrUrl }
  : { success: true, branchName, prUrl: trimmedPrUrl }
```

- [ ] **Step 4: Record the contested outcome in `backlog-drain`**

In step 8 of `.claude/skills/backlog-drain/SKILL.md:88-118`, the existing "Success" bullet reads:

```markdown
   - **Success** — `success: true` **and** a `prUrl` present:
     - Edit the artifact's frontmatter to `status: done`.
     - Append a `**Result:** PR opened at <prUrl>` line to the artifact body.
```

Replace it with:

```markdown
   - **Success** — `success: true` **and** a `prUrl` present:
     - If `contested` is **not** `true`: edit the artifact's frontmatter to
       `status: done` and append a `**Result:** PR opened at <prUrl>` line.
     - If `contested` **is** `true`: edit the artifact's frontmatter to
       `status: contested` instead, and append
       `**Result:** PR opened at <prUrl> — contested, see the PR's "Contested"
       section for the disputed finding.` A contested outcome is not a
       failure — do not count it toward the circuit breaker in step 3, and do
       not treat it as a per-artifact or systemic failure below.
```

Update step 10's commit-subject instruction (`.claude/skills/backlog-drain/SKILL.md:168-173`) to note the new subject form: append after the existing example, "For a contested outcome, use `backlog: mark <artifact filename without .md> contested` — step 3's circuit breaker only matches subjects ending in `failed`, so this form is already exempt without further changes there."

- [ ] **Step 5: Hand-trace against the real tick-9 disagreement**

`docs/superpowers/plans/2026-08-12-loop-refinement-handoff.md` documents tick 9 (`009-generic-transport-base-and-localtransport`) as exactly this scenario: the `lifetime-threading` lens demanded a change, the implementer argued from the core's own wire format that the finding was a misdiagnosis, and the tick failed with no PR. Re-read that artifact's failure history (`git log --oneline | grep "009"` and the artifact's own `**Failure notes:**` if still present, or `git show` the commit that recorded it) and confirm, by reading the new code path above, that this exact sequence would now produce `{ success: true, contested: true, branchName, prUrl }` with the implementer's wire-format argument embedded in `contestedFindings` and surfaced in the PR body — rather than a stranded unpushed branch. This is not a re-run (009 already succeeded on a later attempt and is `status: done`); it's a design-conformance check against real recorded evidence.

- [ ] **Step 6: Dry-run smoke test of the unaffected happy path**

Reuse the Task 2 fixture pattern (a trivial doc-only change, no blocking findings expected) to confirm the happy path — where `blocking.length === 0` and `contestedFindings` stays `null` — is unaffected: the return shape is still the plain `{ success: true, branchName, prUrl }` with no `contested` key. Clean up the fixture, worktree, and branch afterward exactly as in Task 2 Step 4.

- [ ] **Step 7: Commit**

```bash
git add .claude/workflows/backlog-issue.js .claude/skills/backlog-drain/SKILL.md
git commit -m "backlog-issue: open a contested PR instead of stranding disputed reviews"
```

---

## Task 5: Blocked vs. failed distinction

**Files:**
- Modify: `.claude/workflows/backlog-issue.js:12-49` (schemas), `.claude/workflows/backlog-issue.js:170-172` (Implement-phase failure check), `.claude/workflows/backlog-issue.js:224-227` (blocking-findings collection)
- Modify: `.claude/skills/backlog-drain/SKILL.md:88-118` (step 8, outcome recording)

**Interfaces:**
- Consumes: `blocked` status (Task 1), the feasibility question already added to `backlog-scope` (Task 1).
- Produces: a new failure-shaped return value `{ success: false, blocked: true, reason, branchName }`, consumed by `backlog-drain`.

**Design choice:** infeasibility can surface in two places — the Implement agent may recognize it before writing any code, or a review lens may catch it after the implementer worked around it (this is exactly what happened on the real tick 7: the implementer could have hand-written unvalidated SQL to satisfy the letter of the acceptance criteria; the review lens caught that the criteria were unsatisfiable at all). Both need a signal.

- [ ] **Step 1: Add optional blocked fields to the schemas**

In `IMPLEMENT_SCHEMA` (`.claude/workflows/backlog-issue.js:31-40`), add two optional properties (do not add them to `required`):

```javascript
const IMPLEMENT_SCHEMA = {
  type: 'object',
  properties: {
    branchName: { type: 'string' },
    summary: { type: 'string' },
    problem: { type: 'string' },
    acceptanceCriteria: { type: 'string' },
    blocked: { type: 'boolean' },
    blockedReason: { type: 'string' },
  },
  required: ['branchName', 'summary', 'problem', 'acceptanceCriteria'],
}
```

In `REVIEW_SCHEMA`'s finding items (`.claude/workflows/backlog-issue.js:17-25`), add one optional property:

```javascript
        properties: {
          summary: { type: 'string' },
          file: { type: 'string' },
          severity: { type: 'string', enum: ['blocking', 'minor'] },
          blocked: { type: 'boolean' },
        },
        required: ['summary', 'file', 'severity'],
```

- [ ] **Step 2: Instruct the Implement phase to signal infeasibility instead of faking it**

Add a paragraph to the Implement phase prompt (`.claude/workflows/backlog-issue.js:136-168`), after the migration-filename paragraph added in Task 2 Step 1:

```javascript
   If, after investigating, the artifact's acceptance criteria cannot be
   satisfied in this environment -- missing data, missing tooling, a decision
   only a human can make, not something any code change here can fix -- say
   so plainly. Still create the branch (backlog-drain needs a real branch
   name back either way), but make no commit, return blocked: true, and put
   a specific explanation in blockedReason. Do not fabricate data or write a
   partial implementation to make the criteria look satisfied when they
   aren't really verifiable.
```

- [ ] **Step 3: Check the Implement result for a blocked signal before validating branchName**

Replace `.claude/workflows/backlog-issue.js:170-172`:

```javascript
if (!implemented) {
  return { success: false, reason: 'implement phase failed to produce a change' }
}
```

with:

```javascript
if (!implemented) {
  return { success: false, reason: 'implement phase failed to produce a change' }
}

if (implemented.blocked === true) {
  return {
    success: false,
    blocked: true,
    reason: implemented.blockedReason || 'implement phase reported the acceptance criteria are unsatisfiable in this environment, with no reason given',
    branchName: typeof implemented.branchName === 'string' ? implemented.branchName.trim() : undefined,
  }
}
```

This must come before the existing `BRANCH_NAME_PATTERN` validation (`.claude/workflows/backlog-issue.js:178-184`) — a blocked result may have a branch with no real commit on it, which is fine, since it's never pushed.

- [ ] **Step 4: Instruct review lenses to flag infeasibility distinctly, and check for it before attempting fixes**

Add to both lens prompts (`.claude/workflows/backlog-issue.js:187-196`), after the existing per-lens prompt text: `"If a finding is that the acceptance criteria are fundamentally unsatisfiable in this environment -- not something the implementer coded wrong, but something no code change here can fix -- set blocked: true on that finding in addition to severity: blocking."`

Then, in the blocking-findings handling (`.claude/workflows/backlog-issue.js:224-227`, right after `blocking`/`minor` are computed and before the Task 4 fix-attempt block), insert:

```javascript
const blockingInfeasible = blocking.filter((f) => f.blocked === true)
if (blockingInfeasible.length > 0) {
  return {
    success: false,
    blocked: true,
    reason: `review found the acceptance criteria unsatisfiable in this environment: ${blockingInfeasible.map((f) => f.summary).join('; ')}`,
    branchName,
  }
}
```

This must run before the Task 4 fix-attempt agent call — there's no point asking an agent to "fix" a finding that says nothing can fix it.

- [ ] **Step 5: Record the blocked outcome in `backlog-drain`**

In step 8 of `.claude/skills/backlog-drain/SKILL.md`, the "Per-artifact failure" bullet (`.claude/skills/backlog-drain/SKILL.md:94-100`) currently reads:

```markdown
   - **Per-artifact failure** — `success: false` with a reason that's about
     this issue's own implementation, review, or PR:
     - Edit the artifact's frontmatter to `status: failed`.
     - Append a `**Failure notes:** <reason>` line to the artifact body — use
       the result's `reason` if present, otherwise record what was actually
       returned or thrown so it's triage-able. Include the stale worktree and
       branch location from step 9's lookup in that same line.
```

Replace it with:

```markdown
   - **Per-artifact failure** — `success: false` with a reason that's about
     this issue's own implementation, review, or PR:
     - If `blocked` is **not** `true`: edit the artifact's frontmatter to
       `status: failed` and append a `**Failure notes:** <reason>` line —
       use the result's `reason` if present, otherwise record what was
       actually returned or thrown so it's triage-able. Include the stale
       worktree and branch location from step 9's lookup in that same line.
     - If `blocked` **is** `true`: edit the artifact's frontmatter to
       `status: blocked` instead, and append a `**Blocked:** <reason>` line
       using the result's `reason`. A blocked outcome is not a failure — do
       not count it toward the circuit breaker in step 3.
```

Update step 3's circuit-breaker grep description to note it already only matches subjects ending in `failed`, so `backlog: mark <name> blocked` is automatically exempt — no change needed to the grep itself, only the commit-subject convention (mirroring Task 4 Step 4's note for `contested`).

- [ ] **Step 6: Hand-trace against the real tick-7 infeasibility**

`docs/superpowers/plans/2026-08-12-loop-refinement-handoff.md` documents tick 7 (`007-generate-and-ship-static-portal-links`) as exactly this scenario: both portal game objects are spawned on map 42 ("Collin's Test", a developer test map with no travel nodes), making the acceptance criteria unsatisfiable — and the drain originally blamed "the environment" (no build toolchain, no world DB), which the handoff calls the *wrong reason*, since those objections later dissolved while the item stayed correctly closed. Read `docs/backlog/007-generate-and-ship-static-portal-links.md`'s current `**Result:**`/notes (it's `status: out-of-scope` today, reclassified by hand after the fact) and confirm, by reading the new code paths above, that a fresh run against this artifact's original acceptance criteria would now return `{ success: false, blocked: true, reason: "..." }` directly from either the Implement or Review phase — landing on `status: blocked` immediately instead of requiring a human to first see `failed`, investigate, and manually reclassify to `out-of-scope`.

- [ ] **Step 7: Dry-run smoke test of the unaffected happy path**

Reuse the Task 2/Task 4 fixture pattern again to confirm a normal, non-blocked run is unaffected — `implemented.blocked` and `blockingInfeasible` both stay falsy, and the final return shape is unchanged. Clean up afterward.

- [ ] **Step 8: Commit**

```bash
git add .claude/workflows/backlog-issue.js .claude/skills/backlog-drain/SKILL.md
git commit -m "backlog-issue: distinguish unsatisfiable acceptance criteria from implementation failure"
```

---

## Task 6: Worktree and branch leak sweep

**Files:**
- Modify: `.claude/skills/backlog-drain/SKILL.md:119-167` (insert a new step after the existing step 9)

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing consumed elsewhere — purely a maintenance step.

**Design choice:** the field report found two distinct leaks. The `backlog/<slug>` branch/worktree per artifact is already handled correctly by the existing step 9 (kept on `failed`, removed on confirmed-`done`). The un-handled leak is the Workflow tool's own `isolation: 'worktree'` scaffolding branch (`worktree-wf_*`), created underneath every Implement-phase call and never referenced again once the agent checks out `backlog/<slug>` inside it — Workflow's own auto-cleanup-if-unchanged never triggers because the checked-out ref changed. Eleven of these accumulated in the first real run. One of them (`worktree-wf_6d15c61b-144-1`) turned out to hold a commit reachable from nowhere else — so this sweep must check reachability before deleting, not delete on sight.

- [ ] **Step 1: Add the sweep as a new step after the existing worktree cleanup**

Insert a new step between the current step 9 (`.claude/skills/backlog-drain/SKILL.md:119-167`) and step 10 (commit), renumbering step 10 onward by one:

```markdown
9a. Sweep orphaned Workflow-isolation branches. Every Implement-phase call
    creates a scaffold branch named `worktree-wf_*` before the agent checks
    out `backlog/<slug>` inside that worktree — once that happens, the
    scaffold branch is never referenced again, but nothing deletes it, so it
    accumulates one stale local branch per tick regardless of outcome. Run
    this after step 9's own cleanup, every tick, terminal or not:

    ```
    git branch --list 'worktree-wf_*'
    git worktree list --porcelain
    ```

    For each `worktree-wf_*` branch **not** shown as backing any entry in
    `git worktree list`, check whether it's safe to delete before deleting
    it — its tip must be reachable from somewhere else, or deleting it loses
    the only copy of whatever it holds:

    ```
    git merge-base --is-ancestor <branch tip> origin/cm-main
    ```

    or, for each local `backlog/*` branch still present:

    ```
    git merge-base --is-ancestor <branch tip> backlog/<slug>
    ```

    If either check exits `0`, the branch's content lives on elsewhere —
    delete it: `git branch -D <branch>`. If neither does, **do not delete
    it** — report its name and note that it holds at least one commit
    unreachable from `cm-main` or any live `backlog/*` branch, so a human can
    look before it's lost. (This is exactly what happened to
    `worktree-wf_6d15c61b-144-1` in the first real run: a stray merge-conflict
    resolution commit that survived nowhere else. Content from that specific
    incident is safe — it landed on `cm-main` via a different path — but the
    branch itself was never verified reachable before this sweep existed.)
```

- [ ] **Step 2: Run the sweep at every stop path too, not only mid-drain ticks**

Note in the same new step: "This sweep also runs as the last action before either terminal-tick exit (`docs/backlog/.stop` present, or no `pending` artifacts remain) and before the circuit-breaker stop — not only during an ordinary tick — so a drain session that halts early still leaves the repo swept rather than accumulating scaffold branches across every future session." Add a one-line pointer to this step from each of the existing terminal-tick bullets (step 1's stop-sentinel handling and step 4's drained-backlog handling) in `.claude/skills/backlog-drain/SKILL.md`.

- [ ] **Step 3: Smoke-test the sweep logic by hand**

Set up two fake orphaned branches from the current `HEAD`:

```bash
git branch worktree-wf_smoketest-safe HEAD
git branch worktree-wf_smoketest-unsafe HEAD~1
git commit --allow-empty -m "smoke-test: orphan commit for worktree-wf sweep test"
git branch -f worktree-wf_smoketest-unsafe HEAD
git reset --hard HEAD~1
```

(The sequence above: `worktree-wf_smoketest-safe` points at a commit that's an ancestor of `cm-main` — the safe case. `worktree-wf_smoketest-unsafe` gets force-moved to a throwaway empty commit made on top of `HEAD` and then discarded from the working branch via `reset --hard`, leaving that commit reachable only from the `worktree-wf_smoketest-unsafe` ref — the unsafe case.)

Confirm neither shows up in `git worktree list` (they don't — no worktree was created for them), then trace step 9a by hand: `worktree-wf_smoketest-safe` should pass the `--is-ancestor origin/cm-main` check and be deleted; `worktree-wf_smoketest-unsafe` should fail both checks and be reported, not deleted.

- [ ] **Step 4: Clean up the smoke-test branches**

```bash
git branch -D worktree-wf_smoketest-safe
git branch -D worktree-wf_smoketest-unsafe
git status
```

Confirm `git status` shows a clean tree (the `reset --hard` in step 3 should have already restored `HEAD` to its pre-test commit — verify this explicitly rather than assuming, since step 3 intentionally manipulated `HEAD`).

- [ ] **Step 5: Commit**

```bash
git add .claude/skills/backlog-drain/SKILL.md
git commit -m "backlog-drain: sweep orphaned Workflow-isolation branches every tick"
```

---

## Task 7: Split `backlog-issue.js` into Implement+Review only

**Files:**
- Modify: `.claude/workflows/backlog-issue.js` (remove the Verify and PR phases and everything that only exists to support them; change `meta.phases`; change the return shape)

**Interfaces:**
- Consumes: everything Tasks 1-6 already added to this file (dynamic `BASE_BRANCH`, `dependsOnPrUrl`, `contestedFindings`, `blocked`/`blockedReason`, the migration-filename and model/effort changes).
- Produces: a new return shape, consumed by Task 9's `backlog-drain` changes and Task 8's `backlog-batch.js`:
  - `{ success: false, reason }` — implement or review itself broke (systemic-shaped, same meaning as today).
  - `{ success: false, blocked: true, reason, branchName }` — unchanged from Task 5.
  - `{ success: true, branchName, summary, problem, acceptanceCriteria, inGameCheck, minorFindings, contested, contestedFindings }` — `contested`/`contestedFindings` are only present when Task 4's disagreement path fired; `minorFindings` is the `minor` array Task 4's review already computes, now returned instead of consumed locally.

- [ ] **Step 1: Trim `meta.phases` and remove the Verify/PR phase bodies**

Change `meta.phases` at the top of the file from:

```javascript
  phases: [
    { title: 'Implement' },
    { title: 'Review' },
    { title: 'Verify' },
    { title: 'PR' },
  ],
```

to:

```javascript
  phases: [
    { title: 'Implement' },
    { title: 'Review' },
  ],
```

Delete the entire `phase('Verify')` block and the entire `phase('PR')` block (everything from the `phase('Verify')` line through the final `return` statement that Task 4 last modified) — that logic moves to `.claude/workflows/backlog-batch.js` in Task 8, not away entirely. Also delete the now-unused `PR_SCHEMA`, `PR_URL_PATTERN`, `GH_REPO`, and `describe`'s only remaining caller check — keep `describe` itself, Task 8 needs it too and will redefine it there (Workflow scripts can't `import` between files).

- [ ] **Step 2: Change the final return of the Review phase to carry everything Task 8 needs**

The last thing Task 4 left in this file was a return inside/after the blocking-findings handling. Replace it with:

```javascript
return contestedFindings
  ? { success: true, contested: true, contestedFindings, branchName, summary: implemented.summary, problem: implemented.problem, acceptanceCriteria: implemented.acceptanceCriteria, inGameCheck: implemented.inGameCheck, minorFindings: minor }
  : { success: true, branchName, summary: implemented.summary, problem: implemented.problem, acceptanceCriteria: implemented.acceptanceCriteria, inGameCheck: implemented.inGameCheck, minorFindings: minor }
```

- [ ] **Step 3: Add `inGameCheck` to `IMPLEMENT_SCHEMA` and instruct the Implement agent to fill it in**

Add one required property to `IMPLEMENT_SCHEMA` (already extended by Task 5 with `blocked`/`blockedReason`):

```javascript
const IMPLEMENT_SCHEMA = {
  type: 'object',
  properties: {
    branchName: { type: 'string' },
    summary: { type: 'string' },
    problem: { type: 'string' },
    acceptanceCriteria: { type: 'string' },
    blocked: { type: 'boolean' },
    blockedReason: { type: 'string' },
    inGameCheck: { type: 'string' },
  },
  required: ['branchName', 'summary', 'problem', 'acceptanceCriteria', 'inGameCheck'],
}
```

Add a paragraph to the Implement phase prompt, after the blocked-signaling paragraph from Task 5:

```javascript
   Describe, concretely, how a human would confirm this fix actually works
   in-game once it's running on a live server -- specific enough to follow as
   a checklist (e.g. "board the Menethil Harbor - Theramore boat as a bot and
   confirm it doesn't fall through the deck", not "test transports"). If part
   of that check could be confirmed from server logs or console output rather
   than requiring a human to look (e.g. a specific log line, an absence of a
   specific error), say so explicitly -- a later batch step will attempt
   whatever's actually scriptable and leave the rest for manual testing.
   Return this as inGameCheck. Every artifact needs one, even a low-risk
   change -- if you're confident it needs no in-game confirmation beyond the
   generic "server starts, bots spawn" smoke test, say that explicitly rather
   than leaving it vague.
```

- [ ] **Step 4: Dry-run the trimmed workflow**

Reuse the Task 2/4/5 fixture pattern (a trivial doc-only change). Invoke:

```
Workflow({ scriptPath: ".claude/workflows/backlog-issue.js", args: { artifactPath: "<abs path to fixture>" } })
```

(No `dryRun` — Task 7 removed the concept from this file; nothing pushes or opens anything here anymore, so there's nothing to dry-run.) Expected: completes in two phases, returns the new shape from Step 2 with a non-empty `inGameCheck` string. Confirm the Workflow tool's progress display shows only `Implement`/`Review`, not `Verify`/`PR`.

- [ ] **Step 5: Clean up and commit**

```bash
git worktree remove <path from step 4's run>
git branch -D backlog/<fixture-slug>
rm docs/backlog/<fixture file>
git add .claude/workflows/backlog-issue.js
git commit -m "backlog-issue: trim to Implement+Review, hand Verify/PR off to a batch workflow"
```

---

## Task 8: `backlog-batch.js` — integrate, build once, validate in the stack, open PRs

**Files:**
- Create: `.claude/workflows/backlog-batch.js`

**Interfaces:**
- Consumes: a list of `implemented`-status artifacts, each with a known `branchName` (`backlog/<slug>`) and the fields Task 7's `backlog-issue.js` returned (`summary`, `problem`, `acceptanceCriteria`, `inGameCheck`, `minorFindings`, `contested`/`contestedFindings`), plus each artifact's resolved `baseBranch`/`dependsOnPrUrl` from Task 3 (read back from the `**Base:**` line Task 9 will write when moving an artifact to `implemented`).
- Produces: a return of shape `{ success: true, buildId, results: [{ artifactPath, branchName, prUrl, contested, excluded }] }` or `{ success: false, reason }` (a batch-wide failure — build broke, or nothing to build) — consumed by Task 9's `backlog-drain`.

- [ ] **Step 1: Write the script skeleton and its schemas**

```javascript
export const meta = {
  name: 'backlog-batch',
  description: 'Integrate a batch of implemented backlog branches, build once, validate in the stack, and open a PR per artifact',
  phases: [
    { title: 'Integrate' },
    { title: 'Build' },
    { title: 'Validate' },
    { title: 'PR' },
  ],
}

const PR_SCHEMA = {
  type: 'object',
  properties: {
    prUrl: { type: 'string' },
  },
  required: ['prUrl'],
}

const INTEGRATE_SCHEMA = {
  type: 'object',
  properties: {
    excludedArtifacts: {
      type: 'array',
      items: { type: 'string' },
    },
    integrationBranch: { type: 'string' },
  },
  required: ['integrationBranch'],
}

const BUILD_SCHEMA = {
  type: 'object',
  properties: {
    built: { type: 'boolean' },
    imageTag: { type: 'string' },
    failureNote: { type: 'string' },
  },
  required: ['built'],
}

const VALIDATE_SCHEMA = {
  type: 'object',
  properties: {
    dockerReady: { type: 'boolean' },
    liveness: { type: 'string' },
    perArtifactNotes: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          artifactPath: { type: 'string' },
          note: { type: 'string' },
        },
        required: ['artifactPath', 'note'],
      },
    },
  },
  required: ['dockerReady', 'liveness'],
}

const PR_URL_PATTERN = /^https:\/\/github\.com\/[^\s/]+\/[^\s/]+\/pull\/\d+\/?$/
const GH_REPO = 'ChrisMiho/tortoise-wow'
const BASE_BRANCH_PATTERN = /^(cm-main|backlog\/[a-z0-9][a-z0-9_-]*)$/

const describe = (value) => {
  let text
  try {
    text = typeof value === 'string' ? value : JSON.stringify(value)
  } catch {
    text = null
  }
  if (typeof text !== 'string') {
    text = String(value)
  }
  return text.length > 500 ? `${text.slice(0, 500)} (truncated)` : text
}

let normalizedArgs = args
if (typeof normalizedArgs === 'string') {
  try {
    normalizedArgs = JSON.parse(normalizedArgs)
  } catch {
    normalizedArgs = null
  }
}
if (!normalizedArgs || typeof normalizedArgs !== 'object') {
  normalizedArgs = {}
}

// backlog-drain computes buildId (it has access to a real clock; this script
// cannot use new Date()/Date.now()) and a list of batch entries, each already
// carrying everything Implement+Review produced for that artifact.
const buildId = typeof normalizedArgs.buildId === 'string' ? normalizedArgs.buildId.trim() : ''
const batch = Array.isArray(normalizedArgs.batch) ? normalizedArgs.batch : []

if (!buildId || batch.length === 0) {
  return { success: false, reason: 'no buildId or empty batch supplied' }
}
```

- [ ] **Step 2: Integrate phase — merge the batch onto one scratch branch, dependency order first**

```javascript
phase('Integrate')
const batchDescription = batch.map((b) =>
  `- ${b.artifactPath}: branch ${b.branchName}, base ${b.baseBranch || 'cm-main'}${b.dependsOnPrUrl ? ` (stacked on ${b.dependsOnPrUrl})` : ''}`
).join('\n')

const integrated = await agent(
  `Build a scratch integration branch named integration/${buildId}, cut fresh
   from origin/cm-main after "git fetch origin cm-main", so a build can cover
   this whole batch at once instead of one build per artifact:

   ${batchDescription}

   Merge each artifact's branch onto integration/${buildId} in dependency
   order -- an artifact whose base above is another artifact's backlog/<slug>
   branch (not cm-main) must be merged after that dependency, never before.
   For artifacts with no dependency, order smallest-diff-first, matching the
   "blast radius, smallest first" approach in
   docs/superpowers/plans/2026-08-12-transport-stack-merge.md -- an early
   conflict or build failure is then cheaper to attribute.

   If a merge produces a real conflict, resolve it toward preserving BOTH
   sides' intent -- read docs/superpowers/specs/2026-08-11-backlog-workflow-design.md's
   "Field report" section first if you haven't: a conflict here is a
   silent-revert trap, not routine text reconciliation, because each branch
   was cut before the others' fixes existed. If you cannot confidently
   resolve a conflict without guessing which side is "correct", do NOT
   guess: abort that one artifact's merge (git merge --abort, or drop just
   its commits if you'd already progressed further), leave the rest of the
   batch merging normally, and list that artifact's path in
   excludedArtifacts so it's retried in a later batch instead of silently
   shipped wrong. Do not push integration/${buildId} anywhere -- it's a
   local scratch branch for the Build phase only, never a PR base.

   Return the branch name you created and, if any, the artifactPath values
   you had to exclude.`,
  { phase: 'Integrate', isolation: 'worktree', label: 'integrate', schema: INTEGRATE_SCHEMA }
)

if (!integrated || !integrated.integrationBranch) {
  return { success: false, reason: 'integrate phase failed to produce a branch' }
}

const excluded = new Set(Array.isArray(integrated.excludedArtifacts) ? integrated.excludedArtifacts : [])
const included = batch.filter((b) => !excluded.has(b.artifactPath))
if (included.length === 0) {
  return { success: false, reason: 'every artifact in the batch was excluded during integration -- nothing to build' }
}
```

- [ ] **Step 3: Build phase — one Docker build for the whole integration branch**

```javascript
phase('Build')
const imageTag = `tortoise-wow:${buildId}`
const built = await agent(
  `On branch "${integrated.integrationBranch}" (in its own worktree), build the
   Docker image per docs/superpowers/plans/2026-08-11-docker-build-from-this-checkout.md:
   "docker build" from the repo root of that worktree, with
   -DBUILD_PLAYERBOTS=ON -DCMAKE_INSTALL_PREFIX=/opt/turtle and -j2 (the
   Docker VM here is 4 CPUs/8GB -- higher parallelism invites the OOM
   killer). Tag the resulting image ${imageTag}.

   Run "docker build" itself from Windows PowerShell directly against that
   worktree's path -- the build context is just the repo directory and needs
   no WSL path semantics. Do NOT use a wrapped "wsl -d Ubuntu -- bash -lc
   '...'" one-liner containing any variable -- that has previously returned
   plausible-but-wrong output silently rather than failing. If a WSL step is
   unavoidable for any part of this, write it to a script file first and
   invoke that file from PowerShell, never an inline wrapped one-liner.

   Report whether it built successfully. If it failed, report the actual
   compiler/linker error, not just "build failed" -- this feeds a bisection
   decision, not just a status flag.`,
  { phase: 'Build', label: 'build', schema: BUILD_SCHEMA }
)

if (!built || built.built !== true) {
  // A batch build failure is NOT a per-artifact failure -- nothing in this
  // batch is provably broken individually, the combination might just not
  // compile. Leave every included artifact at status: implemented (backlog-drain
  // does not touch their status on this branch of the return) so a human can
  // bisect or retry, rather than marking N artifacts failed for one build break.
  return {
    success: false,
    reason: `batch build failed: ${built ? (built.failureNote || 'no failure detail returned') : describe(built)}`,
  }
}
```

- [ ] **Step 4: Validate phase — preflight Docker, spin up, smoke test, attempt per-artifact checks, always spin down**

```javascript
phase('Validate')
const inGameChecklist = included.map((b) => `- ${b.artifactPath}: ${b.inGameCheck}`).join('\n')

const validated = await agent(
  `Check Docker readiness first: run "docker info" (from Windows PowerShell,
   the Windows-side CLI works even when the Ubuntu WSL distro itself can't
   see the docker command). If it's not ready or errors, return
   dockerReady: false and liveness: a one-sentence explanation, and do NOT
   attempt docker compose at all -- skip straight to reporting that back.

   If Docker is ready: bring the stack up with the ${imageTag} image (compose
   project name is pinned to tortoise-wow-v2; tortoise-wow-v2_dbdata is an
   external volume -- never use "docker compose down -v", that volume is the
   entire world). This is a single-developer, no-live-players development
   server -- you are not simulating a player, just confirming the server
   comes up correctly.

   Confirm the baseline liveness smoke test: the server starts, aiplayerbot.conf
   loads, bots spawn. Report that in liveness.

   Then, for each artifact below, attempt only what its inGameCheck says is
   confirmable from logs or console output (not everything is -- most checks
   here will legitimately be "not scriptable, needs a human" and that's
   expected, say so plainly rather than guessing at a result):

   ${inGameChecklist}

   For each one, return one entry in perArtifactNotes: what you actually
   attempted, and what you observed or why it wasn't scriptable. Do not claim
   you confirmed something you only assumed.

   Whether or not the build/validation was clean, finish by bringing the
   stack back down (plain "docker compose down", never with -v) before you
   return -- this must happen even if something above failed or looked
   wrong, so the stack is never left running unattended.`,
  { phase: 'Validate', label: 'validate', schema: VALIDATE_SCHEMA }
)

if (!validated) {
  return { success: false, reason: 'validate phase did not return a result' }
}
```

- [ ] **Step 5: PR phase — one PR per included artifact, each against its own base, carrying the build ID and checklist**

```javascript
phase('PR')
const perArtifactNote = (artifactPath) => {
  const entry = Array.isArray(validated.perArtifactNotes)
    ? validated.perArtifactNotes.find((n) => n.artifactPath === artifactPath)
    : null
  return entry ? entry.note : 'not attempted'
}

const results = []
for (const item of included) {
  const base = BASE_BRANCH_PATTERN.test(item.baseBranch || '') ? item.baseBranch : 'cm-main'
  const contestedSection = item.contested
    ? `
   8. A section headed "Contested — needs manual adjudication", listing exactly
      these and nothing else, one bullet per line:
${(item.contestedFindings || []).map((u) => `      - ${u}`).join('\n')}`
    : ''
  const minorSection = item.minorFindings && item.minorFindings.length > 0
    ? `
   9. A section headed "Automated review — non-blocking findings", one bullet
      per line:
${item.minorFindings.map((f) => `      - ${f.file}: ${f.summary}`).join('\n')}`
    : ''
  const stackedSection = base !== 'cm-main'
    ? `
   0. A line before everything else: "Stacked on ${item.dependsOnPrUrl || base} — merge that first."`
    : ''

  const prResult = await agent(
    `Push branch "${item.branchName}" to origin (it is unpushed local work from
     an earlier Implement+Review run), then open a pull request against base
     branch ${base}: "gh pr create --repo ${GH_REPO} --head ${item.branchName}
     --base ${base} --title ... --body ...".

     Title: ${item.contested ? '"[contested] " followed by a' : 'a'} short
     summary of the fix, in this repo's existing commit-message voice.

     Body must include, in this order:${stackedSection}
     1. The backlog artifact this implements: ${item.artifactPath}
     2. Problem, quoted verbatim: "${item.problem}"
     3. Summary of the change made: ${item.summary}
     4. Acceptance criteria, quoted verbatim: "${item.acceptanceCriteria}"
     5. This line, verbatim: "Build: ${imageTag} — run docker compose up
        against this image to test. Compose project is tortoise-wow-v2."
     6. A section headed "In-game validation" containing this artifact's
        checklist item verbatim: "${item.inGameCheck}", followed by what was
        already attempted automatically in the shared batch validation pass:
        "${perArtifactNote(item.artifactPath)}"
     7. A line stating this is a single-developer server: manual in-game
        testing is still required from you before merge${contestedSection}${minorSection}

     Return the URL of the pull request you opened, and nothing else in that
     field. If you could not push or open the PR, say so in prUrl rather than
     inventing a URL.`,
    { phase: 'PR', label: `open-pr:${item.artifactPath}`, schema: PR_SCHEMA, model: 'sonnet', effort: 'low' }
  )

  const prUrl = prResult && typeof prResult === 'object' ? prResult.prUrl : prResult
  const trimmedPrUrl = typeof prUrl === 'string' ? prUrl.trim() : ''
  results.push({
    artifactPath: item.artifactPath,
    branchName: item.branchName,
    contested: Boolean(item.contested),
    excluded: false,
    prUrl: PR_URL_PATTERN.test(trimmedPrUrl) ? trimmedPrUrl : null,
    prReason: PR_URL_PATTERN.test(trimmedPrUrl) ? null : `PR phase did not return a pull request URL, got: ${describe(prResult)}`,
  })
}

for (const artifactPath of excluded) {
  results.push({ artifactPath, excluded: true })
}

return { success: true, buildId, imageTag, dockerReady: validated.dockerReady, results }
```

- [ ] **Step 6: Smoke-test the script structurally without a real batch**

There is no cheap way to dry-run a 40-minute build. Verify this task without running Docker: invoke the workflow with a `batch` of one throwaway doc-only fixture (same fixture shape used in Tasks 2/4/5/7) and confirm the Integrate phase runs cleanly through to a real integration branch — then interrupt/skip before the Build phase actually invokes Docker by checking the Workflow's progress output shows `Integrate` completed and `Build` started with the correct `imageTag` logged, rather than waiting out a real build. This is a structural check (does the script wire together correctly, do the prompts render with real values interpolated) not a functional one — flag in the commit message that Docker-dependent behavior hasn't been exercised end to end yet, matching how `backlog-drain`'s own "Before trusting an unattended run" section already treats a first real invocation as unverified until it's watched once.

- [ ] **Step 7: Clean up and commit**

```bash
git worktree list
# remove the Integrate phase's worktree/branch from step 6
rm docs/backlog/<fixture file used in step 6>
git add .claude/workflows/backlog-batch.js
git commit -m "Add backlog-batch: integrate a batch, build once, validate in the stack, open PRs"
```

---

## Task 9: `backlog-drain` batch cadence

**Files:**
- Modify: `.claude/skills/backlog-drain/SKILL.md` (picking/tick logic, step 5-11, to call `backlog-issue` instead of the old 4-phase workflow, accumulate `implemented` artifacts, and trigger `backlog-batch` at a threshold)

**Interfaces:**
- Consumes: Task 7's trimmed `backlog-issue` return shape, Task 8's `backlog-batch` return shape.
- Produces: the `implemented` status transition and its `**Base:**` line (consumed by Task 8's `backlog-batch.js` args).

- [ ] **Step 1: Document the batch size as a single, easy-to-change constant**

Add near the top of `.claude/skills/backlog-drain/SKILL.md`, right after the existing "First run in an environment?" callout:

```markdown
**Batch size:** 4 — the drain accumulates up to this many freshly-implemented
artifacts before running one `backlog-batch` build/validate/PR pass instead of
one per artifact. Chosen to match the largest wave that worked cleanly in the
first real integration (`docs/superpowers/plans/2026-08-12-transport-stack-merge.md`'s
wave 1, 4 PRs). If a batch build breaks, consider lowering this rather than
raising it — smaller batches are cheaper to bisect.
```

- [ ] **Step 2: Change what a normal tick does — implement into `implemented`, not straight to a PR**

Replace step 6-7 (the `Workflow({ name: "backlog-issue", ... })` call and its surrounding steps) with:

```markdown
6. Edit that file's frontmatter to `status: in-progress` before doing anything
   else, so a crash mid-tick can't cause it to be picked again.
6a. Resolve its base branch exactly as before (depends-on check against
    `status: done`, `git merge-base --is-ancestor` against the dependency
    branch) — see the "Dependency-aware branch cutting" section below.
7. Run `Workflow({ name: "backlog-issue", args: { artifactPath: "<absolute
   path>" } })` — no `dryRun` argument; Task 7 removed the concept from this
   file since it no longer pushes or opens anything.
8. Record the outcome:
   - `{ success: true, ... }` (with or without `contested`) — edit the
     artifact's frontmatter to `status: implemented`, and append these lines
     to the artifact body. All of them get read back verbatim in "Running a
     batch" below — nothing else persists this result between the implement
     tick and the later batch tick:
     - `**Base:** <resolved base branch>`
     - `**Summary:** <result.summary>`
     - `**In-game check:** <result.inGameCheck>`
     - `**Minor findings:** <one bullet per result.minorFindings entry>` —
       omit this line entirely if `minorFindings` is empty.
     - If `contested` is present: `**Contested:** <one bullet per
       contestedFindings entry>`
     Do not open a PR here — that happens in the batch pass.
   - `{ success: false, blocked: true, ... }` — unchanged from Task 5:
     `status: blocked`.
   - `{ success: false, ... }` (no `blocked`) — unchanged from the original
     step 8: `status: failed`.
9. Clean up the Implement-phase worktree exactly as before, except: on the
   `implemented` outcome, do **not** delete the branch — it's needed intact
   for the batch pass. Remove the worktree itself (`git worktree remove
   <path>`) but leave the branch. Run the Task 6 orphaned-`worktree-wf_*`
   sweep as before.
10. Commit the status change, subject `backlog: mark <name> implemented` (a
    new form — like `contested`/`blocked`, it doesn't end in `failed` so it's
    automatically exempt from step 3's circuit breaker).
11. **Check the batch trigger before scheduling the next tick:** count
    artifacts at `status: implemented`. If that count has reached the batch
    size (step 1), or if no `pending` artifacts remain at all (so no more
    accumulation is coming), run the batch pass now — see "Running a batch"
    below — before doing anything else. Otherwise, schedule the next tick as
    before (`delaySeconds: 60`, same `/loop` prompt, `noop: false`).
```

- [ ] **Step 3: Add a "Running a batch" section**

Insert a new top-level section after "One tick":

```markdown
## Running a batch

Triggered from step 11 above. Gather every artifact at `status: implemented`:

1. For each, read back the `**Base:**`, `**Summary:**`, `**In-game check:**`,
   and (if present) `**Minor findings:**`/`**Contested:**` lines appended when
   it moved to `implemented` (step 8 of "One tick" above) — these carry
   `baseBranch`/`summary`/`inGameCheck`/`minorFindings`/`contested`/
   `contestedFindings` forward, since nothing else persists that state
   between the implement tick and the batch pass. `problem` and
   `acceptanceCriteria` don't need their own lines — they're already in the
   artifact's own **Problem:**/**Acceptance criteria:** sections.
2. Compute a `buildId` — a short, sortable, human-readable string such as
   `<date>-<sequence>` (e.g. `20260813-1`). Sequence within a day so two
   batches on the same day don't collide, mirroring Task 2's migration-filename
   fix. Do this with a real shell date command (`date +%Y%m%d` or PowerShell's
   `Get-Date`) — this skill runs as an agent with tool access, unlike the
   Workflow scripts it calls, so it's fine to use a real clock here.
3. Run `Workflow({ name: "backlog-batch", args: { buildId, batch: [...] } })`
   where each batch entry is
   `{ artifactPath, branchName, baseBranch, dependsOnPrUrl, summary, problem, acceptanceCriteria, inGameCheck, minorFindings, contested, contestedFindings }`
   — reassembled from each artifact's file: the lines read back in step 1
   supply `baseBranch`/`summary`/`inGameCheck`/`minorFindings`/`contested`/
   `contestedFindings`, and the artifact's own **Problem:**/**Acceptance
   criteria:** sections supply the rest. `dependsOnPrUrl` isn't persisted
   separately — re-derive it the same way step 5a (Task 3) did, from the
   `depends-on:` frontmatter and the dependency artifact's `**Result:**` line,
   only when `baseBranch` isn't `cm-main`.
4. On `{ success: true, results, buildId, imageTag }`: for each entry in
   `results` with a `prUrl`, edit that artifact's frontmatter to `status: done`
   (or `status: contested` if that entry's `contested` was true) and append
   `**Result:** PR opened at <prUrl>, build ${imageTag}.` For each entry with
   `excluded: true` or a `prReason` instead of a `prUrl`, leave that artifact
   at `status: implemented` and append a `**Batch note:** <prReason or
   "excluded during integration — will retry in a future batch">` line so
   it's visible without blocking the rest of the batch's success.
5. On `{ success: false, reason }`: this is a **batch-wide** failure (a
   build break, or nothing left after integration exclusions) — not a
   per-artifact one. Leave every artifact in the batch at `status:
   implemented` (do not touch their status), and report the failure loudly
   with the `reason` and the full list of artifacts that were in the
   attempted batch, so a human can decide whether to retry, exclude a
   suspected artifact by hand, or investigate the build break directly.
6. Commit whatever status/body changes steps 4-5 produced, subject
   `backlog: batch ${buildId}`.
7. Continue the loop as step 11 would have (schedule the next tick) —
   running a batch does not itself end the drain.
```

- [ ] **Step 4: Update the terminal-tick paths to flush a final partial batch**

Both the `.stop`-sentinel terminal tick and the no-`pending`-remaining terminal tick must run a batch first if any artifacts are sitting at `status: implemented` — otherwise stopping the loop right after the last artifact's Implement+Review would strand a nearly-complete batch indefinitely. Add to both terminal-tick bullets: "Before reporting the summary and calling `ScheduleWakeup({ stop: true })`, if any artifact is at `status: implemented`, run 'Running a batch' once — even below the usual batch-size threshold — so nothing is left waiting on a batch that will never trigger."

- [ ] **Step 5: Hand-trace the batch cadence with fixtures**

Create four throwaway fixtures at `status: implemented` (reusing the doc-only fixture shape from earlier tasks, differing only in filename/number) plus one at `status: pending`. Trace step 11 by hand: confirm the count of `implemented` artifacts (4) meets the batch size from Step 1 (4), so a batch would trigger even though a `pending` artifact still exists — batching is driven by the `implemented` count, not by backlog exhaustion. Then delete one fixture (down to 3 `implemented`) and re-trace: confirm no batch triggers yet. Clean up all fixtures.

- [ ] **Step 6: Commit**

```bash
git add .claude/skills/backlog-drain/SKILL.md
git commit -m "backlog-drain: batch implemented artifacts into one build/validate/PR pass"
```

## Self-Review Notes

- **Spec coverage:** all six original handoff items map to a task — vocabulary/dependency field (Task 1 + 3), review tiebreaker (Task 4), migration filenames (Task 2), blocked vs. failed (Task 5), model/effort tuning (Task 2), worktree/branch leaks (Task 6). The transport-stack-merge conflict matrix grounds Task 3's design and the PR Layering Strategy table. The two follow-up asks — batching the ~40-minute build instead of paying it per artifact, and giving each PR a build ID plus an in-game checklist for a single-developer server — map to Tasks 7-9, built explicitly on top of 1-6 rather than alongside them.
- **Placeholder scan:** every code block in Tasks 1-6 quotes exact current line ranges read from the live files. Tasks 7-9 are new logic with no prior lines to quote, but every block is complete, real code — no "TBD"/stub bodies. Task 8's Docker/build steps are honestly scoped as structurally-tested-but-not-yet-run-against-real-Docker in Step 6, rather than claimed as verified.
- **Type/name consistency:** `baseBranch`/`dependsOnPrUrl` (Task 3), `contested`/`contestedFindings` (Task 4), `blocked`/`blockedReason`/`blockingInfeasible` (Task 5), and `inGameCheck`/`minorFindings`/`buildId`/`imageTag` (Tasks 7-9) are used identically everywhere they're introduced and consumed. The existing `FIX_SCHEMA.unresolved` field is deliberately reused as the contested rebuttal carrier in Task 4 rather than adding a redundant field.
- **Caught during self-review:** Task 9's original "Running a batch" draft tried to reassemble each artifact's `summary`/`inGameCheck`/`minorFindings` for the batch call without ever having persisted them when the artifact moved to `implemented` — those fields exist only in `backlog-issue`'s in-memory return value from Task 7, which doesn't survive to the later batch tick. Fixed by having Task 9 Step 2 append `**Summary:**`/`**In-game check:**`/`**Minor findings:**` lines alongside the original `**Base:**`/`**Contested:**` ones, and having "Running a batch" Step 1 read all of them back.
- **Known gap, left for a human, not silently papered over:** Task 8's Integrate phase asks an agent to resolve real merge conflicts between batch members "toward preserving both sides' intent" — the same judgment call a human made by hand in `transport-stack-merge.md`. This is inherently less reliable delegated to an agent than done by a human who already read the whole diff; the exclude-rather-than-guess fallback (drop the artifact from the batch, don't ship a guess) is the safety net, not a claim that agent-resolved conflicts are as trustworthy as the hand-resolved ones from the first run.
