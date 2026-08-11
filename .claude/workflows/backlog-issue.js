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
