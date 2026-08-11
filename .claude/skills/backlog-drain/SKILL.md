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
