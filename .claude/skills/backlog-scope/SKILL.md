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
