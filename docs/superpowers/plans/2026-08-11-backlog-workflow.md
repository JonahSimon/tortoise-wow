# Backlog Scoping & Autonomous Drain Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build two project skills and one named Workflow script that together let a raw idea become a scoped backlog artifact (`backlog-scope`) and let a backlog of such artifacts drain autonomously into independently reviewable PRs (`backlog-drain` + `backlog-issue`).

**Architecture:** `docs/backlog/*.md` files are the interface between phases. `backlog-scope` (a conversational skill) writes them. `backlog-drain` (a skill driven by `/loop`) reads them, picks the oldest pending one each tick, and calls the `backlog-issue` named Workflow (a 4-phase agent graph: Implement → Review → Verify → PR) to implement and open a PR for it, then updates the artifact's status.

**Tech Stack:** Claude Code project skills (`.claude/skills/*/SKILL.md`), a Workflow script (`.claude/workflows/backlog-issue.js`, plain JS per the Workflow tool's scripting rules), Markdown artifacts with YAML frontmatter.

## Global Constraints

- Workflow scripts are plain JavaScript, not TypeScript — no type annotations, no `Date.now()`/`Math.random()`/argless `new Date()`.
- Every backlog issue's branch is cut fresh from `playerbots-integration-gh` — never stacked on another backlog branch.
- This repo has no CI and no headless test suite (confirmed: `.github/` holds only issue templates; the only "tests" directory is an in-game bot-behavior harness, not something runnable outside a live server). The Review phase in `backlog-issue.js` is the only automated correctness check — treat it as load-bearing, not decorative.
- A real (non-dry-run) run of `backlog-issue` pushes a branch and opens a real PR on GitHub. Nothing in this plan's own test steps should do that unintentionally — all verification steps below either use `dryRun: true` or stay purely local.
- Artifact numbering is a zero-padded 3-digit prefix (`NNN-slug.md`), lowest-first pick order.

---

## Task 1: Backlog directory and artifact format

**Files:**
- Create: `docs/backlog/README.md`

**Interfaces:**
- Produces: the artifact frontmatter/body schema (`status`, `risk`, `area` + Problem/Suspected cause/Acceptance criteria/Notes) that Tasks 2–4 all read and write.

- [ ] **Step 1: Write the format documentation**

```markdown
# Backlog

Scoped issues for the autonomous drain workflow, one file per issue.

## Format

`<NNN>-<slug>.md`, e.g. `003-bots-stuck-at-spirit-healer.md`. `NNN` is a
zero-padded 3-digit sequence number — `backlog-scope` assigns the next one
when it creates a file; `backlog-drain` picks the lowest-numbered `pending`
file each tick.

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
```

Write this exact content to `docs/backlog/README.md` (the indented frontmatter block above should be a fenced ` ```markdown ` code block in the actual file, not indented — indentation here is only to nest it inside this plan's own code fence).

- [ ] **Step 2: Commit**

```bash
git add docs/backlog/README.md
git commit -m "Document the backlog artifact format"
```

---

## Task 2: `backlog-scope` skill

**Files:**
- Create: `.claude/skills/backlog-scope/SKILL.md`

**Interfaces:**
- Consumes: the artifact format from Task 1 (`docs/backlog/README.md`).
- Produces: `docs/backlog/<NNN>-<slug>.md` files with `status: pending`, consumed by `backlog-drain` (Task 4).

- [ ] **Step 1: Write the skill**

```markdown
---
name: backlog-scope
description: Turn a raw bug/idea into a scoped backlog artifact under docs/backlog/, ready for the autonomous drain workflow to implement
---

# Backlog Scope

Turns one raw idea (a brain-dumped bug report, a "this feels off" observation) into
a single artifact file the `backlog-drain` skill can later pick up and implement
without any further input from you. Since drain runs fully autonomously to PR with
no other checkpoint, this conversation is the only chance to get the scope right.

## Process

1. **Read the idea.** If the user's description already answers a question below,
   don't re-ask it — only ask for what's actually missing.
2. **Ask one question at a time** until you can fill in every section below:
   - **Problem:** what breaks, under what conditions, and (if known) how it was
     traced. Match the terse, bug-report voice of this repo's commit log and
     README fix tables — run `git log --oneline -20` if unsure of the voice.
   - **Suspected cause / area:** file(s) or subsystem, if the user has a hunch.
     "Not sure" is a valid answer — leave it as an area/subsystem guess only.
   - **Acceptance criteria:** concrete, checkable conditions for "this is fixed."
     Push back on vague criteria ("bots behave better") until it's checkable.
   - **Risk:** low / medium / high — your judgment plus the user's, based on
     blast radius (a single spell fix vs. shared threading/pointer code).
   - **Area:** a short subsystem tag, e.g. `playerbots/battlegrounds`,
     `spells/mage`, `navmesh`.
3. **Determine the next artifact number.** List `docs/backlog/*.md`, take the
   highest `NNN-` prefix present, and use the next integer zero-padded to 3
   digits (e.g. `003`). If the directory has no numbered artifacts yet, start
   at `001`.
4. **Slugify the title** (lowercase, hyphens, no punctuation) for the filename.
5. **Write the artifact** to `docs/backlog/<NNN>-<slug>.md`, following the
   format in `docs/backlog/README.md` exactly, with `status: pending`.
6. **Confirm** the file path back to the user and ask if they have another idea
   to scope, looping back to step 1 if so.
```

- [ ] **Step 2: Smoke-test the skill**

Invoke it with a fully pre-answered idea so it can write the artifact without
needing back-and-forth:

> Use the backlog-scope skill for this idea: "Bots occasionally stand still at
> the spirit healer after a battleground loss instead of releasing and running
> back. Suspected area: playerbots/battlegrounds, probably the release/corpse
> logic. Acceptance criteria: a bot that dies in a battleground releases within
> a few seconds and paths back toward the fight. Risk: low."

Expected: it writes `docs/backlog/001-<some-slug>.md` (directory was empty of
numbered artifacts before this) with `status: pending` frontmatter and all four
body sections filled in with real content (not placeholders).

- [ ] **Step 3: Inspect and clean up the smoke-test artifact**

Read the file just written, confirm the frontmatter parses (`status: pending`,
a `risk` value, an `area` tag) and the body has no placeholder text. This was a
synthetic test case, not a real backlog entry — delete it so `docs/backlog/`
stays empty of numbered artifacts until real scoping happens:

```bash
rm docs/backlog/001-*.md
```

- [ ] **Step 4: Commit**

```bash
git add .claude/skills/backlog-scope/SKILL.md
git commit -m "Add backlog-scope skill for turning ideas into backlog artifacts"
```

---

## Task 3: `backlog-issue` Workflow script

**Files:**
- Create: `.claude/workflows/backlog-issue.js`

**Interfaces:**
- Consumes: an artifact path (`args.artifactPath`) in the Task 1 format, and `args.dryRun` (boolean).
- Produces: a return value of shape `{ success: true, branchName, prUrl }`, `{ success: true, dryRun: true, branchName, verifyNote }`, or `{ success: false, reason, branchName? }` — consumed by `backlog-drain` (Task 4).

- [ ] **Step 1: Write the script**

```javascript
export const meta = {
  name: 'backlog-issue',
  description: 'Implement one scoped backlog issue and open a PR for it',
  phases: [
    { title: 'Implement' },
    { title: 'Review' },
    { title: 'Verify' },
    { title: 'PR' },
  ],
}

const REVIEW_SCHEMA = {
  type: 'object',
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          summary: { type: 'string' },
          file: { type: 'string' },
          severity: { type: 'string', enum: ['blocking', 'minor'] },
        },
        required: ['summary', 'file', 'severity'],
      },
    },
  },
  required: ['findings'],
}

const IMPLEMENT_SCHEMA = {
  type: 'object',
  properties: {
    branchName: { type: 'string' },
    summary: { type: 'string' },
  },
  required: ['branchName', 'summary'],
}

const artifactPath = args.artifactPath
const dryRun = Boolean(args && args.dryRun)

phase('Implement')
const implemented = await agent(
  `Read the backlog artifact at ${artifactPath}. It scopes one bug fix for the
   Tortoise-WoW mangos server fork (a C++ codebase). Implement exactly what its
   Problem, Suspected cause/area, and Acceptance criteria sections describe —
   nothing more.

   Create a new branch cut from the current playerbots-integration-gh, named
   backlog/<slug from the artifact filename, dropping the numeric prefix>.
   Commit the change with a message in this repo's existing terse, bug-report
   commit style (run "git log --oneline -20" first to match the voice).
   Do not push and do not open a PR — a later phase does that.

   Return the exact branch name you created and a one-paragraph summary of
   the change.`,
  { phase: 'Implement', isolation: 'worktree', label: 'implement', schema: IMPLEMENT_SCHEMA }
)

if (!implemented) {
  return { success: false, reason: 'implement phase failed to produce a change' }
}

phase('Review')
const lenses = [
  {
    key: 'correctness',
    prompt: 'Review this diff for logic bugs and for behavior that does not match the acceptance criteria in the artifact.',
  },
  {
    key: 'lifetime-threading',
    prompt: 'Review this diff for pointer/reference lifetime issues and unsynchronized access to shared state. This server runs ~1000 concurrent playerbots and has a history of dangling-pointer and missing-lock bugs in exactly this kind of change.',
  },
]
const reviews = await parallel(lenses.map((lens) => () =>
  agent(
    `${lens.prompt}

     Branch "${implemented.branchName}" (in its own worktree) has the change.
     Run "git diff playerbots-integration-gh...HEAD" in that worktree to see it.
     Artifact for context: ${artifactPath}.

     Report every real finding with a one-sentence summary, the file it's in,
     and a severity of "blocking" or "minor". Return an empty findings array
     if there's nothing to flag.`,
    { phase: 'Review', label: `review:${lens.key}`, schema: REVIEW_SCHEMA }
  )
))

const blocking = reviews.filter(Boolean).flatMap((r) => r.findings).filter((f) => f.severity === 'blocking')
if (blocking.length > 0) {
  await agent(
    `On branch "${implemented.branchName}", fix these blocking review findings, then amend
     or add a commit:
     ${blocking.map((f) => `- ${f.file}: ${f.summary}`).join('\n')}`,
    { phase: 'Review', label: 'apply-fixes' }
  )
}

phase('Verify')
const verifyNote = await agent(
  `Check whether a C++ build toolchain (cmake plus a compiler) is available in this
   environment. If so, attempt to configure and build the affected target from branch
   "${implemented.branchName}" and report whether it succeeded. If no toolchain is
   available, or a build isn't reasonably feasible here, say so plainly rather than
   implying it compiles. Keep the answer to 2-3 sentences — it goes verbatim into a PR
   description.`,
  { phase: 'Verify', label: 'verify' }
)

phase('PR')
if (dryRun) {
  log(`[dry run] would push ${implemented.branchName} and open a PR against playerbots-integration-gh`)
  return { success: true, dryRun: true, branchName: implemented.branchName, verifyNote: verifyNote || '' }
}

const prUrl = await agent(
  `On branch "${implemented.branchName}", push it to origin, then run "gh pr create" against
   base branch playerbots-integration-gh.

   Title: a short summary of the fix, in this repo's existing commit-message voice.

   Body must include, in this order:
   1. The artifact path: ${artifactPath}
   2. Its acceptance criteria, copied from the artifact
   3. This verification note, verbatim: "${verifyNote || 'not available'}"
   4. A line stating manual in-game testing is still required before merge

   Return only the PR URL.`,
  { phase: 'PR', label: 'open-pr' }
)

if (!prUrl) {
  return { success: false, reason: 'PR phase failed to open a pull request', branchName: implemented.branchName }
}

return { success: true, branchName: implemented.branchName, prUrl }
```

- [ ] **Step 2: Write a throwaway smoke-test fixture**

```markdown
---
status: pending
risk: low
area: docs
---

# Smoke-test fixture for backlog-issue.js

**Problem:** not a real bug — this artifact exists only to exercise the
backlog-issue Workflow's Implement/Review/Verify phases end-to-end.

**Suspected cause / area:** docs/backlog/README.md

**Acceptance criteria:** a single-line HTML comment `<!-- smoke-tested -->` is
added as the very first line of docs/backlog/README.md, and nothing else
changes.

**Notes:** run with dryRun: true — this fixture is deleted after the test.
```

Save this to `docs/backlog/000-workflow-smoke-test.md`.

- [ ] **Step 3: Run the workflow in dry-run mode**

Invoke `Workflow({ scriptPath: ".claude/workflows/backlog-issue.js", args: { artifactPath: "docs/backlog/000-workflow-smoke-test.md", dryRun: true } })`.

Expected: it completes all four phases and returns `{ success: true, dryRun: true, branchName: "backlog/workflow-smoke-test.js" or similar, verifyNote: "..." }` — no exceptions, no `success: false`. The log output should show the `[dry run] would push ...` line and no actual `gh pr create` call. Confirm no new PR appeared on GitHub (`gh pr list --head backlog/<branch>` returns nothing).

- [ ] **Step 4: Clean up the smoke-test branch and fixture**

The Implement phase ran in an isolated worktree and made a real local commit
there (dry-run only skips the push/PR step), so remove that worktree and its
branch — both are local-only, never pushed, and were created solely by this
test:

```bash
git worktree list
git worktree remove <path shown for the backlog/... worktree>
git branch -D <the backlog/... branch name from the workflow's return value>
rm docs/backlog/000-workflow-smoke-test.md
```

- [ ] **Step 5: Commit**

```bash
git add .claude/workflows/backlog-issue.js
git commit -m "Add backlog-issue Workflow: implement, review, verify, PR"
```

---

## Task 4: `backlog-drain` skill

**Files:**
- Create: `.claude/skills/backlog-drain/SKILL.md`

**Interfaces:**
- Consumes: `docs/backlog/*.md` `status` frontmatter (Task 1/2), the `backlog-issue` return shape (Task 3: `{success, branchName, prUrl, reason, dryRun}`), and `docs/backlog/.stop` (a user-created sentinel file).
- Produces: updated `status`/`**Result:**`/`**Failure notes:**` in artifact files; drives `/loop` via `ScheduleWakeup`; deletes `docs/backlog/.stop` once honored.

- [ ] **Step 1: Write the skill**

```markdown
---
name: backlog-drain
description: Drain docs/backlog/ one pending artifact at a time by running the backlog-issue Workflow against each and opening a PR, looping via /loop until the backlog is empty
---

# Backlog Drain

Runs one tick of backlog draining: pick the oldest pending issue, implement and
PR it via the `backlog-issue` Workflow, record the outcome, then schedule the
next tick. Meant to be started with `/loop backlog-drain` (self-paced, no fixed
interval) so it keeps going across turns without you re-invoking it.

**Announce at start of the first tick:** "Starting the backlog drain loop."

## One tick

1. Check for a stop request first: if `docs/backlog/.stop` exists, this is
   the terminal tick:
   - Report a summary: how many artifacts are `done`, `failed`, and still
     `pending` (with paths, so they can be picked up again later).
   - Delete `docs/backlog/.stop`.
   - Call `ScheduleWakeup({ stop: true })` and stop. Do not pick a new item.
2. List `docs/backlog/*.md`. Read each file's frontmatter.
3. If no file has `status: pending`, this is the terminal tick:
   - Report a summary: how many artifacts are `done`, how many `failed` (with
     their paths, so they can be triaged), and that the backlog is drained.
   - Call `ScheduleWakeup({ stop: true })` and stop. Do not continue.
4. Otherwise, pick the file with the lowest numeric prefix among those with
   `status: pending`.
5. Edit that file's frontmatter to `status: in-progress` before doing anything
   else, so a crash mid-tick can't cause it to be picked again.
6. Run `Workflow({ name: "backlog-issue", args: { artifactPath: "<that file's path>", dryRun: false } })`.
7. On a result with `success: true`:
   - Edit the artifact's frontmatter to `status: done`.
   - Append a `**Result:** PR opened at <prUrl>` line to the artifact body.
8. On a result with `success: false` (or the Workflow call itself throwing):
   - Edit the artifact's frontmatter to `status: failed`.
   - Append a `**Failure notes:** <reason>` line to the artifact body.
9. Either way, commit the artifact's status change with a short message
   (e.g. `git commit -m "backlog: mark 003-bots-stuck-at-spirit-healer done"`).
10. Call `ScheduleWakeup` to continue:
    - `delaySeconds: 60` (the minimum — there's no external event to wait on,
      just the next tick starting promptly)
    - `prompt`: the exact `/loop` invocation used to start this skill (e.g.
      `"/backlog-drain"`)
    - `reason`: one line, e.g. `"continuing backlog drain, N pending remaining"`
    - `noop: false` (a real tick of work happened)

## Stopping the loop

Create `docs/backlog/.stop` (an empty file, e.g. `touch docs/backlog/.stop`)
at any time to halt the drain after the current issue finishes. It's checked
at the very start of each tick, before a new issue is picked, so a stop
request never aborts an issue mid-implementation — it lets whatever's already
in flight finish (commit, and PR if not dry-run), then halts before starting
another. This works even if no one is watching the conversation when the
sentinel is created.

## Notes

- Every real (non-dry-run) tick pushes a branch and opens a PR on GitHub. This
  is autonomous by design (see
  `docs/superpowers/specs/2026-08-11-backlog-workflow-design.md`) — review
  happens at the PR, not before.
- `failed` artifacts are never retried automatically. Fix the artifact (or the
  underlying ambiguity that caused the failure) and reset its `status` to
  `pending` by hand to give it another attempt.
- Branches are independent — each is cut fresh from `playerbots-integration-gh`
  when its tick starts, never from another backlog branch.
```

- [ ] **Step 2: Smoke-test the picking logic**

Create two throwaway fixtures:

```bash
cat > docs/backlog/001-picking-test-a.md <<'EOF'
---
status: done
risk: low
area: docs
---

# Picking-logic smoke test A (should be skipped)

**Problem:** fixture only.
**Suspected cause / area:** n/a
**Acceptance criteria:** n/a
EOF

cat > docs/backlog/002-picking-test-b.md <<'EOF'
---
status: pending
risk: low
area: docs
---

# Picking-logic smoke test B (should be picked)

**Problem:** fixture only.
**Suspected cause / area:** n/a
**Acceptance criteria:** n/a
EOF
```

Then, following the `backlog-drain` skill's step 2–4 instructions by hand
(without running step 6's real Workflow call), confirm the selection lands on
`002-picking-test-b.md` — the lowest-numbered `pending` file — and that
`001-picking-test-a.md` (`status: done`) is correctly skipped.

- [ ] **Step 3: Smoke-test the stop sentinel**

With the same two fixtures still in place, create the sentinel:

```bash
touch docs/backlog/.stop
```

Follow the skill's step 1 instructions by hand: confirm it detects
`docs/backlog/.stop`, would report a summary (0 done, 0 failed, 1 pending —
`002-picking-test-b.md`), and would stop before ever reaching step 2's
pending-file scan (i.e. `001-picking-test-a.md`/`002-picking-test-b.md`
are never touched). Then confirm cleanup of the sentinel itself:

```bash
test -f docs/backlog/.stop && echo "STILL THERE (bug)" || echo "removed as expected"
```

If the skill's own step 1 already deleted it while you traced through the
logic, that's correct — recreate it with `touch` if you need it again for
inspection, and delete it manually when done:

```bash
rm -f docs/backlog/.stop
```

- [ ] **Step 4: Clean up the fixtures**

```bash
rm docs/backlog/001-picking-test-a.md docs/backlog/002-picking-test-b.md
rm -f docs/backlog/.stop
```

- [ ] **Step 5: Commit**

```bash
git add .claude/skills/backlog-drain/SKILL.md
git commit -m "Add backlog-drain skill: loop-driven autonomous backlog drain"
```
