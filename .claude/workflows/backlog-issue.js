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

// Review lenses run at medium effort by default (see
// docs/superpowers/plans/2026-08-12-backlog-issue-model-tuning.md) — but keep
// the session's full tier for artifacts with risk: high, where a mid-tier
// lens is more likely to miss a subtle finding. There is no automated
// escalation yet; a human editing this file for a high-risk drain run should
// drop the effort override for that run.
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
    problem: { type: 'string' },
    acceptanceCriteria: { type: 'string' },
  },
  required: ['branchName', 'summary', 'problem', 'acceptanceCriteria'],
}

const FIX_SCHEMA = {
  type: 'object',
  properties: {
    fixed: { type: 'boolean' },
    unresolved: { type: 'array', items: { type: 'string' } },
  },
  required: ['fixed'],
}

const PR_SCHEMA = {
  type: 'object',
  properties: {
    prUrl: { type: 'string' },
  },
  required: ['prUrl'],
}

// The Implement phase's branch name flows straight into a real "git push", so it
// is validated rather than trusted. backlog-scope slugifies titles to lowercase
// words joined by hyphens, and the branch slug is that filename minus its NNN-
// prefix and .md suffix, so a well-formed branch is always backlog/<slug>.
// Underscores are tolerated; dots are not, because they would allow ".." and a
// trailing ".lock" -- both of which git rejects in a ref anyway.
const BRANCH_NAME_PATTERN = /^backlog\/[a-z0-9][a-z0-9_-]*$/
const PR_URL_PATTERN = /^https:\/\/github\.com\/[^\s/]+\/[^\s/]+\/pull\/\d+\/?$/

// Turn whatever an agent actually returned into something short and printable,
// so a rejected result is debuggable from the drain skill's failure notes.
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

const artifactPath = normalizedArgs.artifactPath
if (!artifactPath) {
  return { success: false, reason: 'no artifactPath supplied' }
}

// backlog-drain passes an absolute path (a relative one is fragile across the
// Implement phase's branch switch), but an absolute path pasted into a PR body
// leaks a machine-specific location. Derive a repo-relative label for the PR.
const normalizedArtifactPath = String(artifactPath).replace(/\\/g, '/')
const backlogDirIndex = normalizedArtifactPath.lastIndexOf('docs/backlog/')
const artifactLabel = backlogDirIndex >= 0
  ? normalizedArtifactPath.slice(backlogDirIndex)
  : normalizedArtifactPath.slice(normalizedArtifactPath.lastIndexOf('/') + 1)

// dryRun must be explicitly true or false. Any other value (missing,
// malformed, a truthy-but-non-boolean value) fails safe toward dryRun: true
// rather than silently falling through to a real push+PR -- a plain
// Boolean(args.dryRun) coercion previously turned a missing/undefined
// dryRun into `false` (the dangerous direction) and did exactly that.
let dryRun
if (normalizedArgs.dryRun === false) {
  dryRun = false
} else {
  if (normalizedArgs.dryRun !== true) {
    log(`dryRun was not explicitly true or false (got ${JSON.stringify(normalizedArgs.dryRun)}) -- defaulting to dryRun: true (safe) rather than risking a real push+PR`)
  }
  dryRun = true
}

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

// gh commands without an explicit --repo resolve against whichever remote GitHub
// considers the fork's parent (upstream), not this fork -- also documented in
// docs/BRANCHING.md. An unattended PR-create call must pin this explicitly or it
// can silently target the wrong repository.
const GH_REPO = 'ChrisMiho/tortoise-wow'

phase('Implement')
const implemented = await agent(
  `Read the backlog artifact at ${artifactPath} FIRST, before creating or switching
   to any branch. That file is often still uncommitted when this runs, so reading
   it after a branch switch is unreliable — read it now and keep its contents.
   It scopes one bug fix for the Tortoise-WoW mangos server fork (a C++ codebase).

   Then run "git fetch origin ${BASE_BRANCH}" and cut your new branch
   from origin/${BASE_BRANCH} — not from the plain local
   ${BASE_BRANCH} ref, which can lag arbitrarily far behind origin
   during a long unattended drain, and never from playerbots-integration-gh,
   which is a pristine fast-forward-only mirror this fork never commits to.
   Name the branch
   backlog/<the artifact's filename with its NNN- numeric prefix and .md
   extension stripped> — e.g. 003-bots-stuck-at-spirit-healer.md gives
   backlog/bots-stuck-at-spirit-healer. Nothing else is an acceptable branch
   name; a later phase pushes exactly what you return here.

   On that branch, implement exactly what the artifact's Problem, Suspected
   cause/area, and Acceptance criteria sections describe — nothing more.
   Commit the change with a message in this repo's existing terse, bug-report
   commit style (run "git log --oneline -20" first to match the voice).
   Do not push and do not open a PR — a later phase does that.

   If this fix requires a new SQL migration under sql/database_updates/,
   generate its filename with sql/touch_migration.sh (or sql/make_migration.bat
   on Windows) to get a real UTC timestamp — do not hand-write a timestamp.
   Then rename the resulting file to insert this artifact's number before the
   suffix: <timestamp>_${artifactLabel.match(/(\d{3})-/)?.[1] || 'XXX'}_world.sql
   instead of <timestamp>_world.sql. This guarantees uniqueness even if
   another tick generates a migration with the same timestamp — the artifact
   number differs by construction.

   Return:
   - the exact branch name you created
   - a one-paragraph summary of the change you made
   - the artifact's Problem section, quoted verbatim
   - the artifact's Acceptance criteria section, quoted verbatim
   The last two are copied as-is so a later phase can put them in the PR
   description without re-reading the artifact itself.`,
  { phase: 'Implement', isolation: 'worktree', label: 'implement', schema: IMPLEMENT_SCHEMA }
)

if (!implemented) {
  return { success: false, reason: 'implement phase failed to produce a change' }
}

// Everything downstream -- including a real "git push" -- trusts this string.
// If the agent handed back the base branch (or anything else that isn't the
// backlog/<slug> branch it was told to create), stop here rather than pushing
// to whatever ref it named.
const branchName = typeof implemented.branchName === 'string' ? implemented.branchName.trim() : ''
if (!BRANCH_NAME_PATTERN.test(branchName)) {
  return {
    success: false,
    reason: `implement phase returned an unexpected branch name: ${describe(implemented.branchName)}`,
  }
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

     Branch "${branchName}" has the change. Branch and remote-tracking refs are
     shared across worktrees in this repository, so run
     "git diff origin/${BASE_BRANCH}...${branchName}" directly from
     wherever you are -- no need to locate or check out that branch's worktree.
     Diff against origin/${BASE_BRANCH}, never the bare local
     ${BASE_BRANCH}: the branch was cut from origin (the Implement
     phase fetched it), and the local ref can lag behind, which would drag
     already-merged commits into what you're reviewing as if they were
     part of this change.
     Artifact for context: ${artifactPath}.

     Report every real finding with a one-sentence summary, the file it's in,
     and a severity of "blocking" or "minor". Return an empty findings array
     if there's nothing to flag.`,
    { phase: 'Review', label: `review:${lens.key}`, schema: REVIEW_SCHEMA, effort: 'medium' }
  )
))

const returnedReviews = reviews.filter(Boolean)
if (returnedReviews.length < lenses.length) {
  return { success: false, reason: 'a review lens did not return a result', branchName }
}

const allFindings = returnedReviews.flatMap((r) => r.findings || [])
const blocking = allFindings.filter((f) => f.severity === 'blocking')
const minor = allFindings.filter((f) => f.severity === 'minor')
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

phase('Verify')
const verifyNote = await agent(
  `Check whether a C++ build toolchain (cmake plus a compiler) is available in this
   environment. If so, attempt to configure and build the affected target from branch
   "${branchName}". Branch refs are shared across worktrees in this
   repository, but building needs actual files on disk: locate an existing checkout of
   that branch with "git worktree list" (the Implement phase's worktree, not yet
   cleaned up) rather than assuming your current directory has it checked out, and
   confirm you're on the right commit ("git rev-parse HEAD" should match "git rev-parse
   ${branchName}") before concluding anything about whether it builds.
   Report whether the build succeeded. If no toolchain is available, or a build isn't
   reasonably feasible here, say so plainly rather than implying it compiles. Keep the
   answer to 2-3 sentences — it goes verbatim into a PR description.`,
  { phase: 'Verify', label: 'verify', model: 'sonnet', effort: 'low' }
)

phase('PR')
if (dryRun) {
  log(`[dry run] would push ${branchName} and open a PR against ${BASE_BRANCH} on ${GH_REPO}`)
  return { success: true, dryRun: true, branchName, verifyNote: verifyNote || '' }
}

const stackedSection = BASE_BRANCH !== 'cm-main'
  ? `
   0. A line before everything else: "Stacked on ${dependsOnPrUrl || BASE_BRANCH} — merge that first; this PR's diff will shrink once it does."`
  : ''

// Minor review findings are non-blocking, but this repo has no CI, so the human
// reading the PR is the only one who will ever see them. Surface them there.
const minorSection = minor.length > 0
  ? `
   7. A section headed "Automated review — non-blocking findings", listing exactly
      these and nothing else, one bullet per line:
${minor.map((f) => `      - ${f.file}: ${f.summary}`).join('\n')}`
  : ''

const contestedSection = contestedFindings
  ? `
   ${minorSection ? '8' : '7'}. A section headed "Contested — needs manual adjudication", explaining
      that a review finding and the implementer disagreed and neither could be
      resolved automatically, then listing exactly these and nothing else, one
      bullet per line:
${contestedFindings.map((u) => `      - ${u}`).join('\n')}`
  : ''

const prResult = await agent(
  `Push branch "${branchName}" to origin, then open a pull request for it
   against base branch ${BASE_BRANCH}. Branch refs are shared across
   worktrees in this repository, so you do not need to check out or locate that
   branch's worktree first -- from your current checkout, run "git push origin
   ${branchName}" directly, then "gh pr create --repo ${GH_REPO} --head ${branchName}
   --base ${BASE_BRANCH}" with the title and body below (the explicit
   --repo/--head/--base flags avoid gh's default of resolving against upstream
   instead of this fork, and avoid relying on whichever branch happens to be
   checked out where you're running).

   Title: ${contestedFindings ? '"[contested] " followed by a' : 'a'} short summary of the fix, in this repo's existing commit-message voice.

   Body must include, in this order:${stackedSection}
   1. The backlog artifact this implements: ${artifactLabel}
   2. Problem, quoted verbatim: "${implemented.problem}"
   3. Summary of the change made: ${implemented.summary}
   4. Acceptance criteria, quoted verbatim: "${implemented.acceptanceCriteria}"
   5. This verification note, verbatim: "${verifyNote || 'not available'}"
   6. A line stating manual in-game testing is still required before merge${contestedSection}${minorSection}

   Return the URL of the pull request you opened, and nothing else in that field.
   If you could not push or could not open the PR, say so in prUrl rather than
   inventing a URL — the caller checks that it is a real GitHub PR URL.`,
  { phase: 'PR', label: 'open-pr', schema: PR_SCHEMA, model: 'sonnet', effort: 'low' }
)

// Without this check any truthy text -- including "I couldn't create the PR
// because ..." -- would be recorded as a successfully opened PR.
const prUrl = prResult && typeof prResult === 'object' ? prResult.prUrl : prResult
const trimmedPrUrl = typeof prUrl === 'string' ? prUrl.trim() : ''
if (!PR_URL_PATTERN.test(trimmedPrUrl)) {
  return {
    success: false,
    reason: `PR phase did not return a pull request URL, got: ${describe(prResult)}`,
    branchName,
  }
}

return contestedFindings
  ? { success: true, contested: true, branchName, prUrl: trimmedPrUrl }
  : { success: true, branchName, prUrl: trimmedPrUrl }
