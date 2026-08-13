---
name: backlog-drain
description: Drain docs/backlog/ one pending artifact at a time by running the backlog-issue Workflow against each and opening a PR, looping via /loop until the backlog is empty
---

# Backlog Drain

Runs one tick of backlog draining: pick the oldest pending issue, implement and
PR it via the `backlog-issue` Workflow, record the outcome, then schedule the
next tick. Meant to be started with `/loop backlog-drain` (self-paced, no fixed
interval) so it keeps going across turns without you re-invoking it.

**Announce when starting a drain session:** "Starting the backlog drain loop."

**First run in an environment?** Do a single supervised tick against a throwaway
artifact before turning this loose on a real backlog — see
[Before trusting an unattended run](#before-trusting-an-unattended-run).

## One tick

1. Check for a stop request first: if `docs/backlog/.stop` exists, this is
   the terminal tick:
   - Report a summary: how many artifacts are `done`, `failed`, stuck
     `in-progress`, and still `pending` (with paths, so they can be picked up
     — or, for `in-progress`, investigated — again later).
   - Delete `docs/backlog/.stop`.
   - Run step 9a (the worktree-isolation branch sweep).
   - Call `ScheduleWakeup({ stop: true })` and stop. Do not pick a new item.
2. List `docs/backlog/*.md`. Read each file's frontmatter. If any file has
   `status: in-progress`, it's left over from a previous tick that crashed or
   otherwise ended before recording an outcome — report it now (e.g. "N
   artifact(s) stuck at in-progress: <paths>"). Never re-pick, edit, or
   restart an `in-progress` file automatically: a human needs to check
   whether a PR was already opened for it before resetting its status to
   `pending` by hand.
3. Check the recent failure history before starting new work — this is the
   circuit breaker. This skill keeps no state between ticks other than the
   filesystem, so read the history out of git: every completed tick commits
   exactly one subject of the form `backlog: mark <artifact-name> done|failed`
   (step 10). Run:

   ```
   git log -n 2 --pretty=format:%s --grep "^backlog: mark " -- :/docs/backlog
   ```

   (`:/docs/backlog` is deliberate: a plain `docs/backlog` pathspec silently
   matches nothing when run from a subdirectory, which would silently disable
   this breaker. `-n 2` applies to matching commits, so unrelated commits in
   between don't hide the history.)

   If both subjects end in `failed` **and** both of those artifacts (subject
   `backlog: mark <name> failed` means `docs/backlog/<name>.md`) still have
   `status: failed` in their frontmatter, this is the terminal tick:
   - Report both artifacts by path with their `**Failure notes:**`, say the
     loop stopped deliberately after two consecutive failures rather than
     grinding the rest of the backlog into `failed`, and list what's still
     `pending` and untouched.
   - Call `ScheduleWakeup({ stop: true })` and stop. Do not pick a new item.

   Fewer than two such commits (a fresh backlog) never trips this. An artifact
   that's been triaged back to `pending` no longer counts as failed here —
   that's how you clear the breaker before restarting the loop.
4. If no file has `status: pending`, this is the terminal tick:
   - Report a summary: how many artifacts are `done`, how many `failed` (with
     their paths, so they can be triaged), how many are stuck `in-progress`
     (with paths, same human-check caveat as step 2), and that the backlog is
     drained.
   - Run step 9a (the worktree-isolation branch sweep).
   - Call `ScheduleWakeup({ stop: true })` and stop. Do not continue.
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

     If the `git merge-base` command itself errors — rather than cleanly
     exiting non-zero for "not an ancestor" — do not treat that as either
     "merged" or "still open". This happens when `origin/backlog/<dep-slug>`
     doesn't exist on the remote at all, which can only mean the dependency
     artifact's `status: done` is stale or wrong (the branch was deleted
     after merging, or never pushed). Treat the dependency as **not ready**:
     log a warning naming the missing branch and fall through to the
     next-lowest-numbered pending artifact, exactly as step 5 does for a
     dependency whose `status` isn't `done`.
6. Edit that file's frontmatter to `status: in-progress` before doing anything
   else, so a crash mid-tick can't cause it to be picked again.
7. Run the workflow against it, passing an **absolute** `artifactPath`:

   ```
   Workflow({ name: "backlog-issue", args: { artifactPath: "<absolute path to that file>", dryRun: false, baseBranch: "<resolved in step 5a>", dependsOnPrUrl: "<resolved in step 5a, or omit if none>" } })
   ```

   Build the absolute path from the repo root (`git rev-parse --show-toplevel`)
   plus `docs/backlog/<filename>` — e.g.
   `D:/CodingProjects/tortoise-wow/tortoise-wow/docs/backlog/003-bots-stuck-at-spirit-healer.md`.
   Forward slashes are fine on Windows. Do not pass a bare relative path: the
   artifact is still uncommitted at this point and the Implement phase reads it
   from its own worktree after switching branches, so a relative path resolves
   against a working directory this skill doesn't control. The workflow strips
   the leading directories back off for the PR body, so an absolute path here
   doesn't end up pasted into GitHub.
8. Record the outcome. There are three, and they are **not** interchangeable —
   see [Systemic vs. per-artifact failures](#systemic-vs-per-artifact-failures)
   for how to tell the last two apart:
   - **Success** — `success: true` **and** a `prUrl` present:
     - If `contested` is **not** `true`: edit the artifact's frontmatter to
       `status: done` and append a `**Result:** PR opened at <prUrl>` line.
     - If `contested` **is** `true`: edit the artifact's frontmatter to
       `status: contested` instead, and append
       `**Result:** PR opened at <prUrl> — contested, see the PR's "Contested"
       section for the disputed finding.` A contested outcome is not a
       failure — do not count it toward the circuit breaker in step 3, and do
       not treat it as a per-artifact or systemic failure below.
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
   - **Systemic failure** — the invocation itself is broken, not this artifact:
     - Do **not** mark it `failed`. Set its frontmatter back to
       `status: pending` (reverse the step 6 edit; `git checkout -- <path>`
       also works if the artifact was already committed) and don't append
       failure notes — nothing is wrong with the artifact.
     - Don't commit a status change; there's no outcome to record. Discard the
       working-tree change instead of committing it.
     - Report it loudly and specifically, e.g. "This looks **systemic** — the
       `backlog-issue` invocation itself failed, not
       `docs/backlog/003-....md`. Nothing was marked failed; that artifact is
       back at `pending`. N artifacts remain pending and untouched." Quote the
       raw result or error verbatim, and the args you actually passed.
     - Skip step 9's cleanup (treat it like `failed`: leave any worktree and
       branch in place, report the path if one exists).
     - Call `ScheduleWakeup({ stop: true })` and stop. Do **not** continue to
       the next tick: a broken invocation would burn the entire backlog to
       `failed` in minutes, every artifact blamed for something that wasn't
       its fault.
9. Find the Implement worktree and clean it up (or deliberately don't). The
   workflow's Implement phase runs with `isolation: 'worktree'` and always
   leaves a commit behind, so Workflow's own auto-cleanup-if-unchanged never
   triggers — each issue otherwise leaves ~400 MB of checkout plus a local
   branch on disk forever. Locate it by branch name (the result's `branchName`,
   or `backlog/<artifact filename minus the NNN- prefix and .md>` if the result
   didn't carry one):

   ```
   git worktree list --porcelain
   ```

   Each record is a `worktree <path>` line followed by a
   `branch refs/heads/<name>` line; take the path whose branch matches.
   - **On `done`:** confirm the branch really reached origin *before* deleting
     anything. A well-formed `prUrl` only proves an agent returned a URL-shaped
     string; the worktree may still hold the only copy of the work. Run:

     ```
     git ls-remote --heads origin <branch>
     ```

     and require a line ending in exactly `refs/heads/<branch>`. Judge by the
     output, not the exit status: `git ls-remote` exits 0 with empty output when
     nothing matches, and it matches by ref tail, so a bare component like
     `my-fix` would also match `refs/heads/backlog/my-fix`.
     - **On origin:** the local copy is disposable. Run
       `git worktree remove <path>` then `git branch -D <branch>`, both from the
       main checkout rather than from inside the worktree. If `git worktree
       remove` refuses because of leftover untracked files (build output), a
       `--force` is acceptable *here specifically*, because the work is safely
       on origin. If no worktree matches, it's already gone — skip.
     - **Not on origin:** delete nothing — not the worktree, not the branch.
       The PR phase reported a URL for a branch that isn't on the remote, so
       either the push never happened or the URL was invented, and the local
       branch is the only copy of the change. Keep both exactly as the `failed`
       path does, report the discrepancy prominently (quote the `prUrl` and say
       the branch is absent from origin), and record the worktree path in the
       artifact alongside its `**Result:**` line so a human can check before
       anything is lost.
   - **On `failed` (or a systemic failure):** remove nothing. The worktree may
     hold unpushed work worth reading before deciding what to do with the
     artifact, and the branch is the only copy of it. Record the path in the
     failure note instead (step 8), e.g. `**Failure notes:** <reason>
     (worktree left at <path>, branch <branch> — remove both with "git worktree
     remove <path>" and "git branch -D <branch>" before resetting this artifact
     to pending)`. If you already wrote that note without the path, edit the
     line now to add it. If no worktree exists for that branch, say that
     instead.
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

    This sweep also runs as the last action before either terminal-tick exit
    (`docs/backlog/.stop` present, or no `pending` artifacts remain) and
    before the circuit-breaker stop — not only during an ordinary tick — so a
    drain session that halts early still leaves the repo swept rather than
    accumulating scaffold branches across every future session.
10. Unless step 8 took the systemic path, stage and commit the artifact's
    status change with a subject in exactly this form — step 3's circuit
    breaker reads it back:
    `backlog: mark <artifact filename without .md> <done|failed>`, e.g.
    `git add docs/backlog/003-bots-stuck-at-spirit-healer.md && git commit -m
    "backlog: mark 003-bots-stuck-at-spirit-healer done"`. For a contested
    outcome, use `backlog: mark <artifact filename without .md> contested` —
    step 3's circuit breaker only matches subjects ending in `failed`, so this
    form is already exempt without further changes there. For a blocked
    outcome, use `backlog: mark <artifact filename without .md> blocked` —
    same reasoning: step 3's circuit breaker only matches subjects ending in
    `failed`, so this form is automatically exempt too, with no change needed
    to the grep itself.
11. If the outcome you just recorded was `failed`, re-run step 3's check now
    (it now includes this tick's commit). If it trips, report as step 3
    describes and call `ScheduleWakeup({ stop: true })` instead of scheduling
    another tick. Otherwise call `ScheduleWakeup` to continue:
    - `delaySeconds: 60` (the minimum — there's no external event to wait on,
      just the next tick starting promptly)
    - `prompt`: the same input you'd give `/loop` to restart this skill —
      `backlog-drain` (matching how this skill is started: `/loop
      backlog-drain`)
    - `reason`: one line, e.g. `"continuing backlog drain, N pending remaining"`
    - `noop: false` (a real tick of work happened)

## Systemic vs. per-artifact failures

Treating these the same is how a single broken invocation turns an entire
backlog into `failed` artifacts, each one blamed for the wrong reason.

**Systemic** — the workflow never really ran against this artifact:

- `reason` is `no artifactPath supplied`, or otherwise says the args never
  arrived. Step 7 always passes one, so this means it didn't get through.
- `success: true` with **no `prUrl`** — a dry-run-shaped result. This tick
  always passes `dryRun: false`; `backlog-issue` only falls back to a dry run
  when it can't confidently tell its args arrived intact, so a dry-run-shaped
  result on a real tick means the args were mangled in transit. Never record it
  as a completed PR.
- The `Workflow` call threw.
- The result is unrecognizable — not an object, or no `success` field at all.

**Per-artifact** — the workflow ran, and something about *this* issue failed:
`implement phase failed to produce a change`, `implement phase returned an
unexpected branch name: ...`, `a review lens did not return a result`,
`blocking findings not addressed: ...`, `PR phase did not return a pull request
URL, got: ...`. These are the ones worth marking `failed` and triaging.

The line isn't always crisp — a per-artifact reason that repeats verbatim across
different artifacts is systemic in effect, whatever it says. That's what step
3's circuit breaker is for: two consecutive failures stop the loop regardless of
how each one was classified.

## Stopping the loop

Create `docs/backlog/.stop` (an empty file, e.g. `touch docs/backlog/.stop`)
at any time to halt the drain after the current issue finishes. It's checked
at the very start of each tick, before a new issue is picked, so a stop
request never aborts an issue mid-implementation — it lets whatever's already
in flight finish (commit, and PR if not dry-run), then halts before starting
another. This works even if no one is watching the conversation when the
sentinel is created.

The loop also stops on its own for three reasons: the backlog is drained (step
4), two consecutive artifacts failed (step 3), or a failure looked systemic
(step 8). All three report before stopping.

## Before trusting an unattended run

The exact call this skill makes — `Workflow({ name: "backlog-issue", args:
{ artifactPath: "...", dryRun: false } })` — has never been exercised
successfully end to end. During development only an internal `workflow()`
wrapper form was verified, and args failed to arrive correctly twice while the
workflow was being built. So before starting a real unattended drain in an
environment where it hasn't run before:

1. Write one throwaway artifact into `docs/backlog/` scoping a trivial,
   harmless change.
2. Run **one** tick by hand, with a human watching, and confirm all of:
   the workflow got its `artifactPath` (no `no artifactPath supplied`); the
   result carried a real `prUrl` rather than a dry-run-shaped
   `{ success: true, dryRun: true }`; the branch is actually on origin; the PR
   actually exists.
3. Close the throwaway PR, delete its branch and worktree, and delete the
   throwaway artifact.

Only then start `/loop backlog-drain` against the real backlog. If the pilot
tick comes back dry-run-shaped or complaining about `artifactPath`, the
invocation is broken in this environment — fix that before feeding it a
backlog. Even after a clean pilot, watch the first few real ticks rather than
walking away: every non-dry-run tick pushes a branch and opens a PR.

## Notes

- Every real (non-dry-run) tick pushes a branch and opens a PR on GitHub. This
  is autonomous by design (see
  `docs/superpowers/specs/2026-08-11-backlog-workflow-design.md`) — review
  happens at the PR, not before.
- `failed` artifacts are never retried automatically. To give one another
  attempt, in this order: read its `**Failure notes:**`; remove the stale
  worktree and branch the failed attempt left behind (`git worktree remove
  <path>` then `git branch -D <branch>` — the note records both); fix the
  artifact or the underlying ambiguity that caused the failure; then set
  `status: pending` by hand. Skipping the worktree removal makes the retry fail
  again with a confusing, unrelated-looking error — git refuses to check out a
  branch that's already checked out in another worktree. If the failed attempt
  got as far as pushing (the failure was `PR phase did not return a pull request
  URL`), delete the remote branch too (`git push origin --delete <branch>`), or
  the retry's push will be rejected as non-fast-forward.
- `in-progress` artifacts left behind by a crashed tick are surfaced every
  tick (never silently ignored) but never auto-recovered: check whether a PR
  was already opened for it before resetting its `status` to `pending` by
  hand, and clean up its worktree and branch first, exactly as for `failed`.
- Branches are cut fresh from `origin/cm-main` when their tick starts, unless
  the artifact declares `depends-on:` on a still-unmerged dependency — see
  `docs/backlog/README.md#dependencies` — in which case the branch is cut
  from the dependency's branch instead. Independent artifacts (no
  `depends-on:`, or one whose dependency already merged) keep the original
  failure isolation: one artifact failing costs nothing to any other branch.
