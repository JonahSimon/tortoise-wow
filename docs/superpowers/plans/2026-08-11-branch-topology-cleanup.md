# Branch Topology Cleanup + Upstream Merge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give this fork a trunk it actually owns — `cm-main` — brought up to
upstream's tip by merge rather than rebase, with the one defect a clean merge cannot see
fixed, and every other branch pruned so the repo holds exactly two.

**Architecture:** `ChrisMiho/tortoise-wow` is a fork of `Shyalya/tortoise-wow`. Today every
branch on it except `local`, `backlog-workflow-impl` and `upstream-baseline` is a
byte-identical mirror of an upstream branch — including `main`, which is a stale ancestor of
upstream's real trunk `playerbots-integration-gh`. This plan stops treating a mirror as a
trunk: `playerbots-integration-gh` stays a pristine fast-forward-only mirror and becomes the
sync anchor, `local` is merged up to it and renamed `cm-main` as the fork's default
branch, and everything else is deleted. Nothing is rebased and nothing is force-pushed —
`local` is already public with an open PR against it, so history stays append-only.

**Tech Stack:** git 2.54.0 (Windows, `core.autocrlf=false`), GitHub CLI (`gh`), Docker via
WSL, and this repo's own `scripts/rebuild.sh` build-and-verify tooling.

## Global Constraints

- **Never `docker compose down -v`.** That volume is the entire world. It has been lost once.
- **Never force-push and never rebase `local`.** It is pushed, it is public, and PR #2 is open
  against it. Every integration in this plan is a merge commit.
- **The `ARG`/`LABEL` block stays in the Dockerfile's runtime stage.** Moving it adds ~27
  minutes to every build.
- **`ChrisMiho/tortoise-wow` is public.** Nothing secret enters any commit in it.
- Run all git for this repo from **Windows** (this checkout is a Windows worktree). Run
  `scripts/rebuild.sh` from **WSL**, where Docker lives.
- The `for` loops in Tasks 1 and 6 are POSIX shell — run them through the **Bash tool / Git
  Bash**, not PowerShell 5.1, which parses `for b in ...; do` as a syntax error. Single `git`
  and `gh` commands run fine in either shell.
- Repo root (Windows): `D:\CodingProjects\tortoise-wow\tortoise-wow`.
  Same tree from WSL: `/mnt/d/CodingProjects/tortoise-wow/tortoise-wow`.
- **Do not delete a branch before Task 6.** Task 6 is gated on a green build in Task 4.

---

## Findings this plan is built on

All verified 2026-08-11 against `origin` and the GitHub API.

| Question you asked | Verified answer |
|---|---|
| Is there harm in merging my branch to `main`? | **Yes, but not drift harm.** `origin/main` (`8422872`) is byte-identical to `Shyalya/tortoise-wow`'s `main`. You have **0** commits on it. Merging into it forks a mirror — it could never fast-forward from upstream again — and lands the work on a branch that is not the default branch of either repo. |
| Is `main` even the trunk? | **No.** The default branch of *both* your fork and upstream is `playerbots-integration-gh`. `main` is a strict *ancestor* of it, frozen 2026-07-26, authored entirely by Penqle. `git merge-base --is-ancestor origin/main origin/playerbots-integration-gh` → true. |
| How bad is the drift? | **There is none.** `git merge-tree --write-tree origin/playerbots-integration-gh local` exits **0** and writes tree `429efab`. Zero conflicts. |
| How much do we actually overlap? | Your 30 commits touch **20 files**. Only **3** exist upstream at all. `Dockerfile`, `docker-compose.yml`, `.dockerignore`, `.gitignore`, `.env.example`, `scripts/rebuild.sh`, `docs/`, `.claude/`, `fork-migration/` are all **new files upstream does not have** — zero conflict surface by construction. |

**Why the three shared files do not collide:**

| File | Your change | Upstream commits touching it | Why they miss each other |
|---|---|---|---|
| `PlayerbotMgr.cpp` | +48/−1 | **0** | Untouched upstream. Clean carry. |
| `ObjectMgr.cpp` | +10 | 1 (`d7340a0`) | Yours is in `LoadPlayerCacheData` (~line 623); upstream's is a mutex in `GeneratePetNumber` (~line 6209). ~5,600 lines apart. |
| `BattleGroundJoinAction.cpp` | +8 | 3 | Yours inserts at `isUseful()` line 582; upstream's churn is an anonymous namespace at ~39 and two call sites at ~499 and ~991. Disjoint. |

**The one real problem, and it is not a git problem.** Upstream's 60 commits added
`BotBattlegroundLimitReached()` (`BattleGroundJoinAction.cpp:79`), called *only* from
`shouldJoinBg()` (lines 499, 991). Your `c06b2fb` returns early in `isUseful()` before
`shouldJoinBg()` is ever reached. Verified on today's upstream tip:
`class FreeBGJoinAction : public BGJoinAction` (`BattleGroundJoinAction.h:47`) overrides
**only** `shouldJoinBg` (line 62) — it does **not** override `isUseful`. And
`RpgSubActions.h:258` sets `"bg type"` then returns `"free bg join"`. So an ordinary bot that
wanders up to a battlemaster gets `bg type` set, runs the patched `isUseful()`, hits your
bypass, and skips upstream's new cap — which is exactly the "three Warsong instances at once"
behaviour that cap was written to stop. Task 3 fixes this.

**Branch audit — what is safe to delete:**

| Branch | origin | upstream | Identical mirror? | Verdict |
|---|---|---|---|---|
| `1181-rogue-fixes` | `d8bafbb` | `d8bafbb` | yes | delete — one fetch away |
| `1181dev` | `95efad4` | `95efad4` | yes | delete |
| `challenges` | `20068bf` | `20068bf` | yes | delete |
| `dev` | `93f491c` | `93f491c` | yes | delete |
| `feat-spell-dbc-loader` | `7e7da01` | `7e7da01` | yes | delete |
| `fix/bot-death-loop` | `27fc05e` | `27fc05e` | yes | delete |
| `shop` | `2f99d09` | `2f99d09` | yes | delete |
| `main` | `8422872` | `8422872` | yes | delete — the source of the confusion |
| `upstream-baseline` | `f55f910` | *(yours)* | no | delete — **0** commits beyond upstream's tip; a pure ancestor marker holding nothing |
| `backlog-workflow-impl` | `e7ed4a7` | *(yours)* | no | delete — `git merge-base --is-ancestor` proves it is **already merged into `local`** |
| `worktree-wf_1797503d-72a-1`, `worktree-wf_7e6dc0a8-ca2-1`, `worktree-wf_b327647d-91c-1` | `6a85c09` | — | local-only | delete — orphaned workflow scratch branches sitting exactly on upstream's tip |
| `playerbots-integration-gh` | `6a85c09` | `6a85c09` | yes | **KEEP** — the sync anchor |
| `local` | `e2e7bd3` | *(yours)* | no | **KEEP** — becomes `cm-main` |

Deleting a branch in a fork does not touch upstream. Every mirror above is recoverable with a
single `git push origin upstream/<name>:refs/heads/<name>`.

## File Structure

| Path | Responsibility | Task |
|---|---|---|
| Git refs on `origin` — remote `upstream`, tag `pre-upstream-merge-20260811` | The recovery point and the sync source | 1 |
| `src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.h` | Declares the `honoursCommandedBgType()` predicate and the free-bot override | 3 |
| `src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.cpp` | Gates the commanded-`bg type` bypass on that predicate | 3 |
| `scripts/rebuild.sh` (run only) | Builds, runs acceptance checks, promotes `tortoise-v2:local` only if green | 4 |
| `docs/BRANCHING.md` | New. The topology and the sync procedure, so this never has to be re-derived | 7 |

---

### Task 1: Add the upstream remote and freeze a recovery point

There is currently **no `upstream` remote** — `git remote -v` shows only `origin`. Every
"upstream" reference so far has been to `origin`'s mirror of it. Add the real one, then make
the pre-merge state recoverable by name before anything moves.

**Files:**
- Modify: git refs and remote config only. No file changes.

**Interfaces:**
- Produces: remote `upstream` = `https://github.com/Shyalya/tortoise-wow.git`; annotated tag
  `pre-upstream-merge-20260811` = `e2e7bd3`, pushed to `origin`. Tasks 4 and 6 use this tag as
  the rollback ref.

- [ ] **Step 1: Confirm the tree is clean before touching refs**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow status --short
git -C D:/CodingProjects/tortoise-wow/tortoise-wow stash list
```

Expected: only `?? image.png` (an untracked screenshot, irrelevant), and **no** stash entries.
If there is any `M` line, stop and commit or discard it first — a merge onto a dirty tree
mixes unreviewed work into the merge commit.

- [ ] **Step 2: Add the upstream remote and fetch it**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow remote add upstream https://github.com/Shyalya/tortoise-wow.git
git -C D:/CodingProjects/tortoise-wow/tortoise-wow fetch upstream --prune
git -C D:/CodingProjects/tortoise-wow/tortoise-wow remote -v
```

Expected: `upstream` appears with fetch and push URLs. If `remote add` errors with
`remote upstream already exists`, that is fine — run
`git remote set-url upstream https://github.com/Shyalya/tortoise-wow.git` instead and carry on.

- [ ] **Step 3: Verify origin's mirror is still identical to upstream**

This is the assumption the whole plan rests on. Re-check it rather than trust the table.

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow rev-parse origin/playerbots-integration-gh upstream/playerbots-integration-gh
```

Expected: **two identical SHAs**, both `6a85c09a5a055dbfbc59371c4a56f9c022cb08c0`.

If they differ, upstream has moved since this plan was written. That is not a failure — it
just means the merge in Task 2 brings in more than 60 commits. Re-run the conflict proof in
Task 2 Step 1 and believe its output over this document.

- [ ] **Step 4: Tag the pre-merge state and push the tag**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow tag -a pre-upstream-merge-20260811 local -m "State before merging upstream 6a85c09. local = 30 commits on f55f910."
git -C D:/CodingProjects/tortoise-wow/tortoise-wow push origin pre-upstream-merge-20260811
```

- [ ] **Step 5: Verify the tag exists on the remote**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow ls-remote origin refs/tags/pre-upstream-merge-20260811
```

Expected: one line reading
`e2e7bd3...	refs/tags/pre-upstream-merge-20260811`.

This tag is what makes every later step reversible. Do not proceed to Task 2 until this line
prints.

---

### Task 2: Merge upstream's tip into `local`

Merge, not rebase. `local` is pushed and PR #2 is open against it; rewriting it would force a
force-push and orphan the PR's commits. A merge commit costs one line of history and keeps
everything append-only.

**Files:**
- Modify: git history on branch `local` (merge commit only — no manual file edits)

**Interfaces:**
- Consumes: tag `pre-upstream-merge-20260811` and remote `upstream` from Task 1.
- Produces: `local` containing all of `upstream/playerbots-integration-gh` plus your 20-file
  delta intact. Task 3 edits two files on top of it; Task 4 builds it.

- [ ] **Step 1: Re-prove the merge is clean before starting it**

`merge-tree` performs the whole merge in memory and touches nothing on disk. A zero exit and a
bare tree SHA means no conflicts.

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow merge-tree --write-tree upstream/playerbots-integration-gh local
echo "exit=$?"
```

Expected: a single 40-character tree SHA (`429efabd52beca0c1cd1098f62cae35716759a67` if
upstream has not moved) and `exit=0`.

A **non-zero exit** prints conflicted paths instead. If that happens, stop — upstream moved
into one of your three C++ files. Do not proceed; re-run the overlap analysis from the
Findings table against the new tip first.

- [ ] **Step 2: Record what the delta looks like before merging**

You will compare against this afterwards to prove nothing was silently dropped.

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow diff --stat $(git -C D:/CodingProjects/tortoise-wow/tortoise-wow merge-base local upstream/playerbots-integration-gh)..local -- src/
```

Expected, and write it down:
```
 src/game/ObjectMgr.cpp                             | 10 +++++
 src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp  | 49 +++++++++++++++++++++-
 .../strategy/actions/BattleGroundJoinAction.cpp    |  8 ++++
 3 files changed, 66 insertions(+), 1 deletion(-)
```

- [ ] **Step 3: Do the merge**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow checkout local
git -C D:/CodingProjects/tortoise-wow/tortoise-wow merge upstream/playerbots-integration-gh -m "Merge upstream playerbots-integration-gh (6a85c09) into the fork's work"
```

Expected: a summary listing ~60 commits' worth of changed files and `Merge made by the 'ort' strategy.`
No `CONFLICT` lines — Step 1 already proved there are none.

If a conflict appears anyway, run
`git -C D:/CodingProjects/tortoise-wow/tortoise-wow merge --abort`. That restores `local` to
`e2e7bd3` with nothing lost, and means upstream moved between Step 1 and Step 3.

- [ ] **Step 4: Verify your delta survived the merge byte-for-byte**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow diff --stat upstream/playerbots-integration-gh..local -- src/
```

Expected: **exactly the same three lines and the same `66 insertions(+), 1 deletion(-)`** you
recorded in Step 2. A different count means a hunk was mangled — reset to the tag and stop:
`git reset --hard pre-upstream-merge-20260811`.

- [ ] **Step 5: Verify you now have upstream's new cap code**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow grep -n "BotBattlegroundLimitReached" -- src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.cpp
```

Expected: three hits — a definition around line 79 and two call sites around 499 and 991.
Their presence is what makes Task 3 necessary.

- [ ] **Step 6: Push the merge**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow push origin local
```

Expected: a normal non-forced push. If git demands `--force`, something rewrote history —
stop and investigate rather than forcing.

---

### Task 3: Scope the commanded-`bg type` bypass to the commanded path

`merge-tree` proves the text merges. It cannot see this, and this one is a real defect that
ships silently.

Your bypass is correct and wanted for the tournament path: a forced experience should queue
deterministically, without re-rolling composition heuristics for a choice that was not the
bot's to make. But `FreeBGJoinAction` inherits `BGJoinAction::isUseful()` without overriding
it, and the RPG battlemaster route sets `"bg type"` before dispatching to `"free bg join"` —
so the bypass leaks into the general bot population and skips upstream's new cap.

The fix is a virtual predicate that the free-bot subclass turns off.

**Files:**
- Modify: `src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.h`
- Modify: `src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.cpp:582-589`

**Interfaces:**
- Consumes: the merged `local` from Task 2.
- Produces: `virtual bool BGJoinAction::honoursCommandedBgType() const` returning `true`,
  overridden to `false` in `FreeBGJoinAction`. Task 4 builds it.

- [ ] **Step 1: Confirm the leak exists on the merged branch**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow grep -n "class .*JoinAction\|isUseful\|shouldJoinBg" -- src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.h
```

Expected: `class BGJoinAction : public Action` at line 18 declaring `isUseful` at 35, and
`class FreeBGJoinAction : public BGJoinAction` at line 47 declaring **only** `shouldJoinBg` at
line 62. That absence of `isUseful` under `FreeBGJoinAction` **is** the leak.

- [ ] **Step 2: Add the predicate to `BGJoinAction` in the header**

In `BattleGroundJoinAction.h`, find this line at **line 37** — the one inside `class BGJoinAction`,
immediately followed by `#ifndef MANGOSBOT_ZERO`:

```cpp
    virtual bool shouldJoinBg(BattleGroundQueueTypeId queueTypeId, BattleGroundBracketId bracketId);
```

Insert immediately **after** it:

```cpp
    // Whether an explicitly-set "bg type" overrides the ambient join heuristics.
    // True on the commanded path. FreeBGJoinAction turns it off so the RPG
    // battlemaster route stays subject to the bot battleground cap.
    virtual bool honoursCommandedBgType() const { return true; }
```

Both classes contain an identically-worded `shouldJoinBg` declaration, so match on what
follows: `BGJoinAction`'s is followed by `#ifndef MANGOSBOT_ZERO`, `FreeBGJoinAction`'s is
followed directly by `};`.

- [ ] **Step 3: Override it in `FreeBGJoinAction` in the same header**

Find the `shouldJoinBg` declaration at **line 62** — the one followed directly by `};`.

Insert immediately **after** it:

```cpp
    virtual bool honoursCommandedBgType() const override { return false; }
```

- [ ] **Step 4: Gate the bypass in `isUseful()`**

In `BattleGroundJoinAction.cpp` around line 585, replace this block exactly as it stands:

```cpp
    // An explicitly-set "bg type" is a decision already made -- by an operator via
    // `.rndbot debug <bot> setvalueuin32 bg type,N`, or by the RPG battlemaster path
    // (RpgSubActions.h:258). Execute() honours it directly and never consults bgList,
    // so re-rolling the ambient composition heuristics here would only add variance
    // to a choice that was not ours to make.
    if (AI_VALUE(uint32, "bg type"))
        return true;
```

with:

```cpp
    // An explicitly-set "bg type" is a decision already made -- by an operator via
    // `.rndbot debug <bot> setvalueuin32 bg type,N`, or by the RPG battlemaster path
    // (RpgSubActions.h:258). Execute() honours it directly and never consults bgList,
    // so re-rolling the ambient composition heuristics here would only add variance
    // to a choice that was not ours to make.
    //
    // Only on the commanded path, though. This also skips BotBattlegroundLimitReached(),
    // which is reached only via shouldJoinBg() in the bgList loop below. A tournament
    // wants a forced, deterministic queue; the general population should still be held
    // to one instance per bracket. The battlemaster route sets "bg type" and then
    // dispatches to "free bg join", so FreeBGJoinAction overrides
    // honoursCommandedBgType() to false and falls through to the cap.
    if (honoursCommandedBgType() && AI_VALUE(uint32, "bg type"))
        return true;
```

The only behavioural change is the added `honoursCommandedBgType() &&`.

- [ ] **Step 5: Verify the edit shape before committing**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow diff --stat
```

Expected: **2 files changed, 13 insertions(+), 1 deletion(-)** — 5 header lines, 7 new comment
lines, and the reworked `if`. If you reworded a comment the insertion count shifts; what must
hold is **2 files and exactly 1 deletion**. More than one deletion means you removed something
you should not have.

- [ ] **Step 6: Commit**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow add src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.h src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.cpp
git -C D:/CodingProjects/tortoise-wow/tortoise-wow diff --cached --stat
git -C D:/CodingProjects/tortoise-wow/tortoise-wow commit -m "Hold free bots to the battleground cap, keep the commanded path deterministic"
git -C D:/CodingProjects/tortoise-wow/tortoise-wow push origin local
```

Confirm `--cached --stat` shows exactly those two files before committing.

---

### Task 4: Build the merged tree and verify it

This is the expensive step — roughly 40 minutes — and the only one that can produce a broken
binary. It must pass **before** Task 5 makes this branch the repo default. `rebuild.sh`
builds to `tortoise-v2:candidate` and moves the `:local` tag only if every acceptance check
passes, so a bad build cannot take the running server with it. It does not restart anything.

**Files:**
- Runs: `scripts/rebuild.sh` (no modifications)

**Interfaces:**
- Consumes: the merged and fixed `local` from Tasks 2 and 3.
- Produces: `tortoise-v2:local` pointing at a verified image built from the merged tip, plus a
  `tortoise-v2:<sha>` anchor. Task 5 is gated on this passing.

- [ ] **Step 1: Confirm the tree is clean, so the image is not stamped dirty**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow status --porcelain --untracked-files=no
```

Expected: **empty output**. A non-empty result makes `rebuild.sh` stamp the image `-dirty`,
which is a valid build but a poor rollback anchor.

- [ ] **Step 2: Record the current rollback image before building**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'docker images tortoise-v2 --format "{{.Repository}}:{{.Tag}} {{.ID}} {{.CreatedSince}}"'
```

Note the image ID currently tagged `tortoise-v2:local`. That is what you roll back to.

- [ ] **Step 3: Build**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow && ./scripts/rebuild.sh'
```

If the Docker VM runs out of memory mid-compile, retry with one job:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow && BUILD_JOBS=1 ./scripts/rebuild.sh'
```

Expected tail:
```
  ok: playerbots compiled in
  ok: mapextractor present
  ok: vmapextractor present
==> also tagged tortoise-v2:<sha>
==> promoted to tortoise-v2:local
```

A `FAIL:` line leaves `tortoise-v2:local` untouched and exits non-zero. That is the script
working correctly — the old image is still what runs. Fix the compile error and re-run; do not
proceed to Task 5 on a red build.

- [ ] **Step 4: Confirm the promoted image is the one you just built**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'docker images tortoise-v2 --format "{{.Tag}} {{.ID}} {{.CreatedSince}}"'
```

Expected: `local` and the new `<sha>` tag sharing one image ID, created seconds ago, and the
ID differing from the one you recorded in Step 2.

- [ ] **Step 5: Apply it and check the population, when you are ready for the downtime**

`rebuild.sh` deliberately does not restart the stack. Applying is a separate, deliberate act.

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2 && docker compose up -d && docker compose ps'
```

Expected: all services `Up`. Then follow the "Verify it actually works" section of
`docs/DOCKER.md` — a bound port only proves `docker-proxy` answered; check the bot population.

**Never** `docker compose down -v`.

- [ ] **Step 6: In-game regression check — both paths, they are the point of Task 3**

**6a — the forced path still works:**

1. Log in as GM.
2. `.rndbot debug <botname> setvalueuin32 bg type,2` (2 = Warsong Gulch).
3. Confirm the bot enters the WSG queue.

If it does **not**, your tournament bots are running the RPG strategy and consuming `bg type`
through `"free bg join"` — where Task 3 deliberately disabled the bypass. Do **not** reinstate
the blanket bypass; that re-breaks the general population. Raise the ceiling instead, in
`/home/deck/tortoise-wow-server-V2/etc/aiplayerbot.conf`:

```
AiPlayerbot.BgBotTeamCap = 2:10
```

(`2` = Warsong Gulch, `10` = bots per team — room for a 10v10 while still capping.) Restart and
retest.

**6b — the general population is now capped:**

1. Leave the server running with no commanded queueing for ~30 minutes.
2. Check running battlegrounds.

Expected: **at most one** running Warsong instance per bracket while no real player is queuing.
Several means `FreeBGJoinAction` is not taking the override — re-check Task 3 Step 3.

- [ ] **Step 7: Rollback, if any of the above fails**

Point `TW_IMAGE` at the anchor explicitly. Retagging `:local` is **not** enough — if `.env` has
`TW_IMAGE=tortoise-v2:candidate`, compose resolves `:candidate`, sees no change, prints
"Running", and relaunches the very image you are rolling back from. See `docs/DOCKER.md`.

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2 && TW_IMAGE=tortoise-v2:<old-id-from-step-2> docker compose up -d'
```

The git side needs no recovery — `pre-upstream-merge-20260811` is on `origin`.

---

### Task 5: Promote `local` to `cm-main` and make it the trunk

Only after Task 4 is green. This is the step that changes what the repo calls itself.

**Files:**
- Modify: branch names and default-branch setting on `ChrisMiho/tortoise-wow`; PR #2 state

**Interfaces:**
- Consumes: a built and verified `local` from Task 4.
- Produces: `cm-main` as the fork's default branch, local checkout tracking it, PR #2
  closed. Task 6 prunes against this.

- [ ] **Step 1: Rename the branch locally and push the new name**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow branch -m local cm-main
git -C D:/CodingProjects/tortoise-wow/tortoise-wow push -u origin cm-main
```

Expected: a new remote branch, and local tracking set to `origin/cm-main`.
`origin/local` still exists at this point — that is deliberate, it is the safety net until
Step 4.

- [ ] **Step 2: Set it as the fork's default branch**

```bash
gh repo edit ChrisMiho/tortoise-wow --default-branch cm-main
gh repo view ChrisMiho/tortoise-wow --json defaultBranchRef -q .defaultBranchRef.name
```

Expected: `cm-main`.

Do this **before** deleting anything. GitHub refuses to delete the default branch, so setting
it first is what unblocks Task 6.

- [ ] **Step 3: Close PR #2**

PR #2 is `local -> playerbots-integration-gh`. Under this topology that direction is now
backwards — upstream flows *into* the trunk, not out of it. Close it rather than merge it.

```bash
gh pr close 2 --repo ChrisMiho/tortoise-wow --comment "Superseded: upstream was merged into the trunk instead, and the branch is now cm-main (the fork default). playerbots-integration-gh stays a pristine fast-forward mirror of upstream. See docs/BRANCHING.md."
gh pr view 2 --repo ChrisMiho/tortoise-wow --json state -q .state
```

Expected: `CLOSED`.

- [ ] **Step 4: Delete the old `local` ref on the remote**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow push origin --delete local
git -C D:/CodingProjects/tortoise-wow/tortoise-wow ls-remote origin refs/heads/local
```

Expected: the `ls-remote` prints **nothing**. The commits are unaffected — they are reachable
from `cm-main` and from the tag.

---

### Task 6: Prune the fork to two branches

Everything deleted here is either an identical mirror of an upstream branch (one fetch away) or
already fully contained in `cm-main`. The Findings table records the proof for each.

**Files:**
- Modify: remote and local refs only

**Interfaces:**
- Consumes: `cm-main` as default branch from Task 5.
- Produces: exactly two remote branches — `cm-main` and `playerbots-integration-gh`.

- [ ] **Step 1: Re-prove that every mirror is still identical before deleting it**

Do not trust the table — it was written earlier. Re-run the check.

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow fetch upstream --prune
for b in 1181-rogue-fixes 1181dev challenges dev feat-spell-dbc-loader fix/bot-death-loop shop main; do
  o=$(git -C D:/CodingProjects/tortoise-wow/tortoise-wow rev-parse origin/$b)
  u=$(git -C D:/CodingProjects/tortoise-wow/tortoise-wow rev-parse upstream/$b)
  if [ "$o" = "$u" ]; then echo "SAFE  $b"; else echo "DIFFERS $b -- do not delete"; fi
done
```

Expected: eight `SAFE` lines. Delete only the branches that print `SAFE`.

- [ ] **Step 2: Re-prove the two non-mirror branches hold nothing unique**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow merge-base --is-ancestor origin/backlog-workflow-impl cm-main && echo "SAFE backlog-workflow-impl (fully merged)" || echo "DIFFERS -- do not delete"
git -C D:/CodingProjects/tortoise-wow/tortoise-wow merge-base --is-ancestor origin/upstream-baseline cm-main && echo "SAFE upstream-baseline (pure ancestor)" || echo "DIFFERS -- do not delete"
```

Expected: two `SAFE` lines.

- [ ] **Step 3: Delete the mirrors and the spent branches from the remote**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow push origin --delete 1181-rogue-fixes 1181dev challenges dev feat-spell-dbc-loader fix/bot-death-loop shop main upstream-baseline backlog-workflow-impl
```

Expected: ten `- [deleted]` lines. If GitHub refuses one with "cannot delete the default
branch", Task 5 Step 2 did not take — go back and set the default branch first.

- [ ] **Step 4: Remove the stale worktree and its branch**

`backlog-workflow-impl` still has a worktree checked out at `.worktrees/backlog-workflow`,
which is why the local branch cannot simply be deleted.

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow worktree remove .worktrees/backlog-workflow
git -C D:/CodingProjects/tortoise-wow/tortoise-wow worktree prune
git -C D:/CodingProjects/tortoise-wow/tortoise-wow worktree list
```

Expected: only the main worktree at `D:/CodingProjects/tortoise-wow/tortoise-wow` remains.

If `worktree remove` refuses because the worktree is dirty, inspect it first —
`git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/backlog-workflow status` — and
only then re-run with `--force`.

- [ ] **Step 5: Delete the local branches**

`-d` (not `-D`) refuses unless the commits are reachable elsewhere. That refusal is a free
safety check, so use it and only investigate if it fires.

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow branch -d backlog-workflow-impl worktree-wf_1797503d-72a-1 worktree-wf_7e6dc0a8-ca2-1 worktree-wf_b327647d-91c-1
git -C D:/CodingProjects/tortoise-wow/tortoise-wow remote prune origin
```

Expected: four `Deleted branch` lines. `-d` succeeds for all four because Task 2 merged
`6a85c09` into the trunk, so the three `worktree-wf_*` tips are now reachable from
`cm-main` — they hold zero commits of their own. If `-d` refuses any of them, **stop**:
that branch contains work nothing else references. Inspect it with
`git log --oneline cm-main..<branch>` before deciding.

- [ ] **Step 6: Verify the final shape**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow branch -a
git -C D:/CodingProjects/tortoise-wow/tortoise-wow ls-remote --heads origin
```

Expected on the remote: exactly **two** refs — `refs/heads/cm-main` and
`refs/heads/playerbots-integration-gh`. Locally: `cm-main` (current) and
`playerbots-integration-gh`.

Also confirm the recovery tag survived the pruning:

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow ls-remote --tags origin
```

Expected: `pre-upstream-merge-20260811` still present.

---

### Task 7: Document the topology and the sync procedure

Without this, the next person — or the next agent — re-derives all of it from scratch, which is
exactly what just happened.

**Files:**
- Create: `docs/BRANCHING.md`

**Interfaces:**
- Consumes: the final two-branch shape from Task 6.
- Produces: nothing code depends on. This is the entry point for the next sync.

- [ ] **Step 1: Write `docs/BRANCHING.md`**

```markdown
# Branches in this fork

This is a fork of [`Shyalya/tortoise-wow`](https://github.com/Shyalya/tortoise-wow).
It holds exactly two branches, and the distinction between them is the whole point.

| Branch | What it is | How it moves |
|---|---|---|
| `cm-main` | **The trunk.** The fork's default branch. All work lands here. | Normal commits and merges. Never force-pushed. |
| `playerbots-integration-gh` | **A pristine mirror of upstream's default branch.** Zero commits of our own. | Fast-forward only, from `upstream`. Never commit to it. |

## `cm-main` is ours. There is no plain `main`

The `cm-` prefix is load-bearing. Upstream's default branch is `playerbots-integration-gh`,
**not** `main` — upstream's `main` is a lagging *ancestor* of it, authored by a different
maintainer, and the copy this fork inherited was deleted on 2026-08-11 because it did nothing
but invite the assumption that it was a trunk.

So: `cm-main` is this fork's trunk and always was. Any reference you find to a bare `main` in
this repo means **upstream's** `main`, which is not something we build, merge, or ship from. If
you ever want that mirror back:

```bash
git push origin upstream/main:refs/heads/main
```

The same one-liner restores any other upstream branch this fork used to mirror
(`dev`, `shop`, `challenges`, `1181dev`, `1181-rogue-fixes`, `feat-spell-dbc-loader`,
`fix/bot-death-loop`). None of them were ever ours.

## Syncing with upstream

Because `playerbots-integration-gh` stays pristine, the sync is a fast-forward followed by one
merge — never a rebase, and never a force-push.

```bash
git fetch upstream --prune
git tag -a pre-upstream-merge-$(date +%Y%m%d) cm-main -m "before sync"
git push origin --tags

# 1. Fast-forward the mirror. This must never need --force.
git push origin upstream/playerbots-integration-gh:refs/heads/playerbots-integration-gh

# 2. Prove the merge is clean before doing it. Exit 0 and a bare tree SHA = no conflicts.
git merge-tree --write-tree upstream/playerbots-integration-gh cm-main; echo "exit=$?"

# 3. Merge upstream into the trunk.
git checkout cm-main
git merge upstream/playerbots-integration-gh

# 4. Prove our delta survived. Compare against the same command run before the merge.
git diff --stat upstream/playerbots-integration-gh..cm-main -- src/
```

Then build with `./scripts/rebuild.sh` (see `docs/DOCKER.md`) before applying anything.

**Two things a clean merge does not give you:**

1. **Config.** `etc/aiplayerbot.conf` on the server is bind-mounted and is never regenerated by
   a build. Diff it against `src/modules/PlayerBots/playerbot/aiplayerbot.conf.dist.in` after
   every sync and port new keys by hand.
2. **Semantic overlap.** A clean merge proves only that the text does not collide. Check
   whether upstream added gates in code paths our patches short-circuit. This bit us once
   already: `BGJoinAction::isUseful()` returned early before upstream's new
   `BotBattlegroundLimitReached()` cap could ever be reached, so ordinary bots skipped it.
   `honoursCommandedBgType()` is the fix — see `BattleGroundJoinAction.h`.

## Why the fork's delta stays small on purpose

Our changes touch 20 files, and only **three** of them exist upstream at all
(`ObjectMgr.cpp`, `PlayerbotMgr.cpp`, `BattleGroundJoinAction.cpp`). Everything else —
`Dockerfile`, `docker-compose.yml`, `scripts/`, `docs/`, `.claude/` — is a file upstream does
not have, which is conflict-free by construction. Keep it that way: when there is a choice
between editing an upstream file and adding a new one, add a new one.
```

- [ ] **Step 2: Commit and push**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow add docs/BRANCHING.md
git -C D:/CodingProjects/tortoise-wow/tortoise-wow diff --cached --stat
git -C D:/CodingProjects/tortoise-wow/tortoise-wow commit -m "Document the two-branch topology and how to sync with upstream"
git -C D:/CodingProjects/tortoise-wow/tortoise-wow push origin cm-main
```

Confirm `--cached --stat` shows exactly one file before committing.

---

## Rollback summary

| If this fails | Do this |
|---|---|
| Merge conflicts (Task 2) | `git merge --abort`. Nothing moved; `local` is still `e2e7bd3`. |
| The delta did not survive the merge (Task 2 Step 4) | `git reset --hard pre-upstream-merge-20260811`. |
| Build fails (Task 4) | `rebuild.sh` leaves `tortoise-v2:local` untouched and exits non-zero. The old image still runs. Fix and re-run. |
| Server misbehaves after applying (Task 4) | `TW_IMAGE=<old-id> docker compose up -d`. Retagging `:local` alone is not enough. |
| You deleted a branch you wanted (Task 6) | `git push origin upstream/<name>:refs/heads/<name>` for any mirror. For `local`, `git push origin pre-upstream-merge-20260811:refs/heads/local`. |
| You want the whole pre-merge state back | `git reset --hard pre-upstream-merge-20260811` — the tag is on `origin`. |
