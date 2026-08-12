# Tune model and effort per agent in the backlog-issue workflow

**Status:** deferred on purpose. The current all-Opus-high drain of
`docs/backlog/` is being left untuned so it produces a cost baseline to measure
against. Do not apply this while that drain is running.

## The observation

`.claude/workflows/backlog-issue.js` sets no `model` or `effort` on any of its
six `agent()` calls, so every one inherits the session model — currently Opus at
high effort. That includes two agents that do no reasoning worth the tier: the
Verify agent (build check) and the PR agent (`git push` + `gh pr create`).

The `backlog-drain` orchestrator loop runs at the session model too, and its
per-tick work is checklist-shaped: read frontmatter, grep git log for two
subjects, edit a status line, verify a branch on origin, remove a worktree,
commit. Its one consequential judgment — whether a reported `prUrl` corresponds
to a branch that actually reached origin — is settled by `git ls-remote` output
rather than by reasoning depth.

## Proposed overrides

| Agent | Current | Proposed | Reasoning |
| --- | --- | --- | --- |
| implement | session (Opus high) | keep | Orienting in a large C++ server core is where the tier pays for itself, and a wrong patch costs the most reviewer time. |
| review lenses (×3) | session (Opus high) | `effort: 'medium'` | Three full-effort lenses per tick is a large slice of the token spend. |
| fix | session (Opus high) | keep | Acts on review findings; same profile as implement. |
| verify | session (Opus high) | `model: 'sonnet', effort: 'low'` | Runs a build check and reports. |
| PR | session (Opus high) | `model: 'sonnet', effort: 'low'` | Pushes a branch and opens a PR. |
| `backlog-drain` loop | session (Opus high) | sonnet, or Opus medium | Bookkeeping plus one mechanically-verified check. |

Keep the strong tier regardless for artifacts labelled `risk: high` — notably
`009-generic-transport-base-and-localtransport`, which asks for a new
server-side transport type rather than a patch. Consider reading the artifact's
`risk:` field in the workflow and escalating `implement`/`fix` on
`medium`/`high`.

## Why downgrading looks safe here

The safety comes from the artifacts, not the model. Each one carries an exact
`file:line`, explicit acceptance criteria, and a risk label — the profile where a
mid-tier model performs near-identically to a frontier one. Scoping quality is
the precondition; if future artifacts are vaguer, revisit this.

Two backstops bound the damage from a weaker model:

- A fabricated or stale `prUrl` is caught mechanically. `backlog-drain` requires
  `git ls-remote --heads origin <branch>` to show the exact ref before it
  deletes a worktree or branch.
- Every tick opens a real PR, so a plausible-but-wrong patch is caught at
  review, not merged silently.

The judgment genuinely traded away by downgrading the orchestrator is
classifying a failure as per-artifact vs. systemic — getting that wrong is what
turns one broken invocation into nine `failed` artifacts. The skill's two-strike
circuit breaker backstops it, which is why sonnet still looks acceptable there.

## Baseline to measure against

From the untuned drain. Fill in per tick; compare a tuned run on a comparable
artifact afterwards.

| Tick | Artifact | Risk | Agents | Subagent tokens | Tool uses | Wall clock | Result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `001-map-gettransports-stub-returns-empty` | low | 6 | 252,903 | 133 | 16m16s | PR #9, 3 files, +35/−6 |
| 2 | `002-taxi-cost-check-both-endpoints` | low | 6 | 224,143 | 95 | 10m41s | PR #10, 3 files, +34/−8 |
| 3 | `003-honour-usetaxi-return-in-minimalmove` | low | 6 | 233,953 | 111 | 11m52s | PR #11, 2 files, +42/−2 |

The comparison worth making is tokens-per-tick at equal outcome quality: did the
tuned run still produce a PR that survives review without extra round trips?
Token count alone is not the metric — a cheaper tick that ships a wrong patch
costs more than it saves.

## Applying it

Edit `.claude/workflows/backlog-issue.js` (the durable definition), not the
per-run script persisted under the session directory — that copy is what an
in-flight tick already read, and edits to it do not carry to future runs.
