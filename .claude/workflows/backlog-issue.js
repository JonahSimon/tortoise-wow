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

     Branch "${implemented.branchName}" has the change. Branch refs are shared
     across worktrees in this repository, so run "git diff playerbots-integration-gh...${implemented.branchName}"
     directly from wherever you are -- no need to locate or check out that
     branch's worktree.
     Artifact for context: ${artifactPath}.

     Report every real finding with a one-sentence summary, the file it's in,
     and a severity of "blocking" or "minor". Return an empty findings array
     if there's nothing to flag.`,
    { phase: 'Review', label: `review:${lens.key}`, schema: REVIEW_SCHEMA }
  )
))

const returnedReviews = reviews.filter(Boolean)
if (returnedReviews.length < lenses.length) {
  return { success: false, reason: 'a review lens did not return a result', branchName: implemented.branchName }
}

const blocking = returnedReviews.flatMap((r) => r.findings || []).filter((f) => f.severity === 'blocking')
if (blocking.length > 0) {
  const fixResult = await agent(
    `On branch "${implemented.branchName}", fix these blocking review findings, then amend
     or add a commit:
     ${blocking.map((f) => `- ${f.file}: ${f.summary}`).join('\n')}`,
    { phase: 'Review', label: 'apply-fixes' }
  )
  if (!fixResult) {
    return { success: false, reason: 'blocking findings not addressed', branchName: implemented.branchName }
  }
}

phase('Verify')
const verifyNote = await agent(
  `Check whether a C++ build toolchain (cmake plus a compiler) is available in this
   environment. If so, attempt to configure and build the affected target from branch
   "${implemented.branchName}". Branch refs are shared across worktrees in this
   repository, but building needs actual files on disk: locate an existing checkout of
   that branch with "git worktree list" (the Implement phase's worktree, not yet
   cleaned up) rather than assuming your current directory has it checked out, and
   confirm you're on the right commit ("git rev-parse HEAD" should match "git rev-parse
   ${implemented.branchName}") before concluding anything about whether it builds.
   Report whether the build succeeded. If no toolchain is available, or a build isn't
   reasonably feasible here, say so plainly rather than implying it compiles. Keep the
   answer to 2-3 sentences — it goes verbatim into a PR description.`,
  { phase: 'Verify', label: 'verify' }
)

phase('PR')
if (dryRun) {
  log(`[dry run] would push ${implemented.branchName} and open a PR against playerbots-integration-gh`)
  return { success: true, dryRun: true, branchName: implemented.branchName, verifyNote: verifyNote || '' }
}

const prUrl = await agent(
  `Push branch "${implemented.branchName}" to origin, then open a pull request for it
   against base branch playerbots-integration-gh. Branch refs are shared across
   worktrees in this repository, so you do not need to check out or locate that
   branch's worktree first -- from your current checkout, run "git push origin
   ${implemented.branchName}" directly, then "gh pr create --head ${implemented.branchName}
   --base playerbots-integration-gh" with the title and body below (the explicit
   --head/--base flags avoid relying on whichever branch happens to be checked out
   where you're running).

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
