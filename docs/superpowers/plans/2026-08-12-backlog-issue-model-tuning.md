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
| 4 | `004-worldposition-gettransports-guid-bug` | low | 6 | 264,943 | 156 | 18m39s | PR #12, 4 files, +130/−18 |
| 5 | `005-implement-ontaxiflighteject` | low | 6 | 339,121 | 151 | 21m41s | PR #13, 2 files, +57/−4 |
| 6 | `006-boat-walk-on-guard-and-stale-transport` | medium | 5 | 246,434 | 114 | 14m56s | PR #14, 2 files, +12/−6 |
| 7 | `007-generate-and-ship-static-portal-links` | medium | 4 | 328,479 | 182 | 29m13s | **failed** — unsatisfiable ACs, no PR |
| 8 | `008-dock-hops-carry-transport-entry` | medium | 6 | 496,141 | 217 | 42m08s | PR #15, 6 files, +255/−150 |

Observations from the untuned run, all five ticks nominally `risk: low`:

- Cost does not track the `risk:` label — these five span 224k–339k tokens and
  10m41s–21m41s while carrying the same label.
- Nor does it track diff size. Tick 5 was the most expensive (339k) with nearly
  the smallest diff (+57/−4); tick 4 produced the largest diff (+130/−18) for
  75k fewer tokens. What tick 5 had was core proximity — its artifact was
  flagged "low-but-touches-core" for changing `Player` taxi-flight state.
- Tick 1 carries first-visit orientation cost that later ticks don't, so it is a
  poor single point of comparison for a tuned run.
- The first `medium` tick (6) came in cheaper than three of the five `low` ticks,
  which further weakens the risk label as a cost predictor.
- Agent count per tick is 5 or 6, not fixed: the fix agent runs only when a
  review lens returns findings to address. Tick 6 was the first 5-agent tick.
  This matters for tuning — dropping the lenses to `medium` effort changes how
  often the fix agent fires, so lens effort and fix-agent frequency have to be
  read together rather than as independent savings.

If per-artifact escalation is implemented, core proximity looks like the better
signal to escalate on than either the risk label or expected diff size. That may
warrant a `touches-core:` field in the artifact format rather than overloading
`risk:`.

### Tick 7 is evidence against downgrading the review lenses

The one failure in this run is the strongest argument in the file *against* part
of its own proposal. Tick 7 failed because a review lens refused to accept
acceptance criteria it judged unsatisfiable: shipping a validated SQL migration
with no world DB, no built core, and no mysql client in the tree, for a portal on
a map with zero travel nodes. The cheap wrong answer was available and plausible
— hand-write the INSERTs, claim the criteria met, open the PR. That would have
produced unvalidated data-migration SQL against a live world DB, and it would
have looked like a success in every field this loop checks.

Catching that required reading the shipped world data and the node store, noticing
the map-42 gap, and reasoning about `saveNodeStore` renumbering ids. Before
dropping the lenses to `medium`, weigh that against the token savings: the review
stage is where this workflow's autonomy is actually load-bearing, because nothing
downstream re-checks whether the acceptance criteria were honestly met. Consider
downgrading `verify` and `PR` first and leaving the lenses alone.

The comparison worth making is tokens-per-tick at equal outcome quality: did the
tuned run still produce a PR that survives review without extra round trips?
Token count alone is not the metric — a cheaper tick that ships a wrong patch
costs more than it saves.

## Applying it

Edit `.claude/workflows/backlog-issue.js` (the durable definition), not the
per-run script persisted under the session directory — that copy is what an
in-flight tick already read, and edits to it do not carry to future runs.
