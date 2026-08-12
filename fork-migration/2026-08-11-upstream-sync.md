# Upstream Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebase the two local C++ commits onto upstream's 60 newer commits, reconcile the
config drift that comes with them, and validate the result in production — without
force-pushing anything until the new build is proven.

**Architecture:** The local delta is 3 files / 66 lines and `git merge-tree` already proves
it merges cleanly onto upstream's tip. So this is not a conflict-resolution project. It is:
(1) freeze an immutable recovery point on two remotes, (2) rebase onto a *new* branch and
prove the delta survived byte-for-byte with `git range-diff`, (3) reconcile three live
config keys that upstream changed, (4) fix one semantic interaction that a clean git merge
cannot see — the local BG bypass currently leaks into the free-bot population,
(5) build and validate with the existing `ship-cpp-fix.sh` tooling,
(6) only then promote `local` to the synced tip.

**Tech Stack:** git 2.53.0 (in WSL), Docker Compose, the existing
`scripts/ship-cpp-fix.sh` / `scripts/verify-running-commit.sh` provenance tooling.

---

## Findings this plan is built on

All verified 2026-08-11 against the live tree. **Nothing is currently at risk of being lost.**

| Question you asked | Verified answer |
|---|---|
| Are all my changes pushed to the new fork? | **Yes.** Working tree clean (0 modified), no stashes, `local` == `origin/local` == `c06b2fb`. Both local commits also exist on `backup/local`. Two independent remotes hold them. |
| Is there overlap with the new upstream changes? | **No.** `git merge-tree --write-tree` exits **0** (clean, tree `5bec730`). |
| Anything unpushed in the ops repo? | **No.** Every local branch tip is contained in a remote ref. One untracked file: `docs/superpowers/specs/2026-08-11-statusline-handoff.md`. |

**Why there is no conflict** — the local delta is only these 3 files:

| File | Local change | Upstream commits touching it | Why they miss each other |
|---|---|---|---|
| `PlayerbotMgr.cpp` | +48/-1 (the big one) | **0** | Untouched upstream. Clean carry. |
| `ObjectMgr.cpp` | +10 | 1 (`d7340a0`) | Local is in `LoadPlayerCacheData` (~line 623); upstream's is a mutex in `GeneratePetNumber` (~line 6209). ~5,600 lines apart. |
| `BattleGroundJoinAction.cpp` | +8 | 3 | Local inserts into `isUseful()` (~line 650); upstream added an anonymous namespace at ~line 39 and two call sites in `shouldJoinBg` (~496, ~988). Disjoint. |

The handoff's warning about "an 81-line change to `BattleGroundJoinAction.cpp`" is accurate
about the *file* but the churn does not reach the 8 lines the local commit adds. Upstream's
`isUseful()` still contains both anchor lines the patch keys off
(`if (!bot->HasFreeBattleGroundQueueId())` and `// reduce amount of healers in BG`).

**What upstream gets you that is directly relevant to your open workstreams:**

- `ca6b3a5` **pinned bots** — kept logged in, never relocated, "so its run can be followed
  from one level to the next." That is aimed squarely at the bots-stall-at-14 problem.
- `afe60fa`, `3bc503d`, `6a85c09`, `99f16b3` — quest hand-in and travel fixes, plus `+quest`
  added to the default `NonCombatStrategies`.
- `90419a0` **"Ship the bot log off by default"** — upstream already fixed the `bots.log`
  growth problem from `BOTS-LOG-GROWTH-HANDOFF.md`. See Task 3: your live conf overrides it,
  so you do **not** get this for free.
- `94619b0` (three crashes on random-level login), `7a0da62` (vmap concurrency),
  `d7340a0` (pet number races) — stability.

## Global Constraints

- **Never `docker compose down -v`.** That volume is the entire world.
- **Never run git from WSL against a Windows worktree, and never run Windows git against the
  WSL tree.** The C++ repo lives in WSL — all git for it runs *in WSL*. The ops repo
  (`D:\TurtleWow`) is Windows — all git for it runs *in Windows*.
- **Do not use `wsl.exe -- bash -lc '<inline script>'` for anything with variables or `$?`.**
  They arrive empty and have produced three wrong conclusions already. Write the script to
  the scratchpad and run the file:
  `wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < /mnt/c/<path>/x.sh > /tmp/x.sh && bash /tmp/x.sh'`
- **Do not force-push `origin/local` before Task 6.** It is one of only two copies of the work.
- **Nothing secret goes in the C++ repo** — `ChrisMiho/tortoise-wow` is public.
- **The `ARG`/`LABEL` block stays in the Dockerfile's runtime stage.** Moving it adds ~27 min
  to every build.
- **Check `git diff --cached --stat` before every commit** in the ops repo, or use
  `git commit -- <paths>`. A stray staged index has hijacked a commit twice.
- C++ repo path in WSL: `/home/deck/tortoise-wow-server-V2/src`. Stack root (compose, live
  configs): `/home/deck/tortoise-wow-server-V2`.

## File Structure

| Path | Responsibility | Task |
|---|---|---|
| C++ repo refs (WSL) — tag `pre-upstream-sync-20260811`, branch `sync/upstream-6a85c09` | The recovery point and the rebased work | 1, 2 |
| `/home/deck/tortoise-wow-server-V2/etc/aiplayerbot.conf` | **Live** bot config, bind-mounted into the container | 3 |
| `D:\TurtleWow\deploy\etc\aiplayerbot.conf.template` | Version-controlled mirror of the above; must be kept in step | 3 |
| `src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.{h,cpp}` | Scoping the commanded-`bg type` bypass so free bots keep upstream's cap | 4 |
| `D:\TurtleWow\docs\CPP-SOURCE-WORKFLOW.md` | Gains an "upstream sync" section | 6 |
| `D:\TurtleWow\CPP-SOURCE-HANDOFF.md` | §4's "upstream sync is deferred" becomes "done" | 6 |

---

### Task 1: Freeze an immutable recovery point

Before touching anything, make the current state recoverable by name from two independent
remotes. A rebase does not destroy the old commits, but a later force-push could — this tag
means it cannot.

**Files:**
- Modify: C++ repo refs in `/home/deck/tortoise-wow-server-V2/src` (tag only, no file changes)

**Interfaces:**
- Produces: tag `pre-upstream-sync-20260811` == `c06b2fb4570195ab7f2bf4b3de363b7b064198f9`,
  present on `origin` **and** `backup`. Tasks 2 and 5 use this as the rollback ref.

- [ ] **Step 1: Write the state-capture and tag script**

Write to `<scratchpad>/t1-freeze.sh`:

```bash
#!/bin/bash
set -euo pipefail
cd /home/deck/tortoise-wow-server-V2/src

echo "=== PRE-FLIGHT: refuse to proceed unless clean ==="
if [ -n "$(git status --porcelain)" ]; then
  echo "FAIL: working tree is dirty. Commit or stash before syncing."
  git status --short
  exit 1
fi
if [ -n "$(git stash list)" ]; then
  echo "FAIL: stashes exist and would not be carried by the rebase:"
  git stash list
  exit 1
fi
echo "OK: clean tree, no stashes"

echo "=== recording the state we are leaving ==="
git rev-parse HEAD | tee /tmp/pre-sync-head.txt
git log --oneline -3

echo "=== tagging ==="
git tag -f -a pre-upstream-sync-20260811 c06b2fb \
  -m "State before pulling upstream's 60 commits. local = 2 commits on f55f910."
git push origin pre-upstream-sync-20260811
git push backup pre-upstream-sync-20260811

echo "=== VERIFY: tag resolves to c06b2fb on both remotes ==="
echo "--- origin ---"; git ls-remote origin refs/tags/pre-upstream-sync-20260811
echo "--- backup ---"; git ls-remote backup refs/tags/pre-upstream-sync-20260811
```

- [ ] **Step 2: Run it**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < "/mnt/c/Users/mihov/AppData/Local/Temp/claude/D--TurtleWow/2dfcf21b-8058-4bf6-a38f-407fe762b8d5/scratchpad/t1-freeze.sh" > /tmp/t1.sh && bash /tmp/t1.sh'
```

Expected: `OK: clean tree, no stashes`, then both `ls-remote` lines showing
`c06b2fb4570195ab7f2bf4b3de363b7b064198f9	refs/tags/pre-upstream-sync-20260811`.

If the pre-flight fails, **stop** — something was created since this plan was written and
needs committing first. Do not proceed with a dirty tree.

- [ ] **Step 3: Commit the plan to the ops repo**

```bash
git -C D:/TurtleWow add docs/superpowers/plans/2026-08-11-upstream-sync.md
git -C D:/TurtleWow diff --cached --stat
git -C D:/TurtleWow commit -m "docs: plan for syncing the C++ fork with upstream"
```

Confirm `--cached --stat` shows **only** that one file before committing.

---

### Task 2: Rebase onto upstream and prove the delta survived

**Files:**
- Modify: C++ repo, new branch `sync/upstream-6a85c09`

**Interfaces:**
- Consumes: tag `pre-upstream-sync-20260811` from Task 1.
- Produces: branch `sync/upstream-6a85c09`, containing exactly 2 commits on top of
  `upstream/playerbots-integration-gh` (`6a85c09`). `local` is left **untouched** at `c06b2fb`.
  Task 5 builds this branch; Task 6 promotes it.

- [ ] **Step 1: Write the rebase script**

The safety property here is `git range-diff`: it compares the *old* 2-commit series against
the *new* one and reports any change in content. Empty output modulo context = the fixes
carried over exactly.

Write to `<scratchpad>/t2-rebase.sh`:

```bash
#!/bin/bash
set -euo pipefail
cd /home/deck/tortoise-wow-server-V2/src

UP=upstream/playerbots-integration-gh

echo "=== refresh upstream ==="
git fetch upstream --prune
git fetch origin --prune
echo "upstream tip: $(git rev-parse --short "$UP") $(git log -1 --format=%s "$UP")"

echo "=== branch off local, leaving local alone ==="
git branch -f sync/upstream-6a85c09 local
git checkout sync/upstream-6a85c09

echo "=== rebase ==="
git rebase "$UP"

echo "=== VERIFY 1: exactly 2 commits on top of upstream ==="
n=$(git rev-list --count "$UP"..HEAD)
echo "commits ahead of upstream: $n"
[ "$n" = "2" ] || { echo "FAIL: expected 2, got $n"; exit 1; }
git log --oneline "$UP"..HEAD

echo "=== VERIFY 2: range-diff — did the patches change? ==="
echo "(empty output below == the fixes carried over byte-for-byte)"
git range-diff "$UP" pre-upstream-sync-20260811 HEAD

echo "=== VERIFY 3: the 66 lines are still present and still 3 files ==="
git diff --stat "$UP"..HEAD

echo "=== VERIFY 4: local is untouched ==="
echo "local is still at: $(git rev-parse --short local) (expect c06b2fb)"
```

- [ ] **Step 2: Run it**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < "/mnt/c/Users/mihov/AppData/Local/Temp/claude/D--TurtleWow/2dfcf21b-8058-4bf6-a38f-407fe762b8d5/scratchpad/t2-rebase.sh" > /tmp/t2.sh && bash /tmp/t2.sh'
```

Expected:
- `git rebase` completes with **no conflict prompt** (`merge-tree` already proved this).
- `commits ahead of upstream: 2`
- `range-diff` prints two lines showing `1: xxxxxxx = 1: yyyyyyy Register a freshly created bot...`
  and `2: xxxxxxx = 2: yyyyyyy Honour an explicitly-set bg type...`. The **`=`** is what
  matters — it means the patch is identical. A `!` means content changed; stop and inspect.
- `--stat` shows the same 3 files, 66 insertions, 1 deletion.
- `local is still at: c06b2fb`

- [ ] **Step 3: If the rebase conflicts anyway, stop and reassess**

It should not. If it does, run `git rebase --abort` — this returns you to
`sync/upstream-6a85c09` == `c06b2fb` with nothing lost — and re-run the analysis, because a
conflict means upstream moved since this plan was written.

- [ ] **Step 4: Push the sync branch to the fork**

This gives the rebased work a third home before it is built.

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git push -u origin sync/upstream-6a85c09'
```

Expected: a new branch on `ChrisMiho/tortoise-wow`. Verify with
`git ls-remote origin refs/heads/sync/upstream-6a85c09`.

---

### Task 3: Reconcile the config drift

This is the part that a clean merge does **not** give you. Upstream changed defaults in
`aiplayerbot.conf.dist.in`, but the live server reads
`/home/deck/tortoise-wow-server-V2/etc/aiplayerbot.conf` — a separate file, bind-mounted via
`./etc:/opt/turtle/etc`, that is **not** regenerated by a build. Upstream's new defaults do
not reach you unless you edit that file.

Three keys, each an explicit decision:

| Key | Live value now | Upstream's new default | Decision |
|---|---|---|---|
| `AiPlayerbot.BotLogFile` (line 267) | `bots.log` | *(empty — off)* | **Change to empty.** Confirmed by owner 2026-08-11. This is the permanent fix for the gigabyte log growth in `BOTS-LOG-GROWTH-HANDOFF.md`; the explicit live setting currently overrides upstream's fix. |
| `AiPlayerbot.NonCombatStrategies` (line 1023) | no `+quest` | adds `+quest` | **Add `+quest`.** Pairs with the four upstream quest hand-in/travel fixes; relevant to the bot progression workstream. |
| `AiPlayerbot.BgBotTeamCap` | *(absent — key is new)* | absent ⇒ cap `-1` (unlimited) | **Leave absent.** Absent still gives the general population the main enhancement — the "one running instance per bracket while no real player waits" rule fires regardless. Only the per-team ceiling and the `cap == 0` disable need the key. Task 5 Step 6a names the one situation that would call for setting it. |

`BgBotTeamCap` parses as comma-separated `bgTypeId:cap` pairs. Absent means `GetBgBotTeamCap`
returns `-1` for every BG, so the new `cap > 0` and `cap == 0` branches never fire — only the
"one running instance per bracket when no real players" rule applies.

**Files:**
- Modify: `/home/deck/tortoise-wow-server-V2/etc/aiplayerbot.conf` (lines 267, 1023)
- Modify: `D:\TurtleWow\deploy\etc\aiplayerbot.conf.template` (lines 267, 1023)

**Interfaces:**
- Consumes: nothing from earlier tasks — can be done in parallel with Task 2.
- Produces: a live conf with bot-log redirection off and `+quest` enabled. Task 5's restart
  is what makes it take effect.

- [ ] **Step 1: Back up the live conf**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cp -v /home/deck/tortoise-wow-server-V2/etc/aiplayerbot.conf /home/deck/tortoise-wow-server-V2/etc/aiplayerbot.conf.pre-sync-20260811'
```

- [ ] **Step 2: Write the conf edit script**

Write to `<scratchpad>/t3-conf.sh`:

```bash
#!/bin/bash
set -euo pipefail
CONF=/home/deck/tortoise-wow-server-V2/etc/aiplayerbot.conf

echo "=== BEFORE ==="
grep -n "^AiPlayerbot.BotLogFile" "$CONF"
grep -n "^AiPlayerbot.NonCombatStrategies" "$CONF"

sed -i 's|^AiPlayerbot.BotLogFile = bots.log$|AiPlayerbot.BotLogFile =|' "$CONF"
sed -i 's|^\(AiPlayerbot.NonCombatStrategies = .*+rpg craft\)$|\1,+quest|' "$CONF"

echo "=== AFTER ==="
grep -n "^AiPlayerbot.BotLogFile" "$CONF"
grep -n "^AiPlayerbot.NonCombatStrategies" "$CONF"

echo "=== VERIFY: exactly the two intended lines changed ==="
diff "$CONF.pre-sync-20260811" "$CONF" || true
```

- [ ] **Step 3: Run it and confirm the diff is exactly two lines**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < "/mnt/c/Users/mihov/AppData/Local/Temp/claude/D--TurtleWow/2dfcf21b-8058-4bf6-a38f-407fe762b8d5/scratchpad/t3-conf.sh" > /tmp/t3.sh && bash /tmp/t3.sh'
```

Expected AFTER lines:
```
267:AiPlayerbot.BotLogFile =
1023:AiPlayerbot.NonCombatStrategies = +grind,+loot,+custom::say,+return,+delayed roll,+tfish,+wander,+rpg craft,+quest
```
The `diff` must show exactly those two lines changed and nothing else.

- [ ] **Step 4: Mirror into the version-controlled template**

Apply the same two edits to `D:\TurtleWow\deploy\etc\aiplayerbot.conf.template` (same line
numbers, 267 and 1023) using the Edit tool, so the tracked copy does not drift from the live one.

- [ ] **Step 5: Truncate the existing log now that redirection is off**

Safe on a running server — `BotLog.cpp:35` opens with `fopen(path,"a")`.

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'ls -la /home/deck/tortoise-wow-server-V2/logs/bots.log && truncate -s 0 /home/deck/tortoise-wow-server-V2/logs/bots.log && ls -la /home/deck/tortoise-wow-server-V2/logs/bots.log'
```

- [ ] **Step 6: Commit**

```bash
git -C D:/TurtleWow add deploy/etc/aiplayerbot.conf.template
git -C D:/TurtleWow diff --cached --stat
git -C D:/TurtleWow commit -m "conf: take upstream's bot-log-off default and enable the quest strategy"
```

---

### Task 4: Scope the bypass to the commanded path only

`git merge-tree` proves the *text* merges. It cannot see this, and this one is a real defect:

Upstream's new `BotBattlegroundLimitReached()` is called from `shouldJoinBg()`. `isUseful()`
reaches `shouldJoinBg()` only inside its bgList population loop. `c06b2fb` inserts
`if (AI_VALUE(uint32, "bg type")) return true;` **before** that loop — so a set `bg type`
skips the cap entirely.

That is correct and wanted for the tournament: a forced experience should queue
deterministically. **But it currently leaks into the general bot population**, because:

```
RpgSubActions.h:258
  SET_AI_VALUE(uint32, "bg type", AI_VALUE(BattleGroundTypeId, "rpg bg type"));
  return "free bg join";                     ← dispatches to FreeBGJoinAction
```

and `class FreeBGJoinAction : public BGJoinAction` **does not override `isUseful()`** — it
overrides only `shouldJoinBg()`. So an ordinary bot that wanders up to a battlemaster gets
`bg type` set, runs the patched `BGJoinAction::isUseful()`, hits the bypass, and skips
upstream's one-instance-per-bracket cap. Which is exactly the "three Warsong instances"
behaviour the cap was written to stop.

**Decision (yours, 2026-08-11):** the forced path keeps the bypass; the general population
gets upstream's enhancement. Implement that with a virtual predicate the free-bot subclass
turns off.

**Files:**
- Modify: `src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.h`
- Modify: `src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.cpp`

**Interfaces:**
- Consumes: branch `sync/upstream-6a85c09` from Task 2.
- Produces: `virtual bool BGJoinAction::honoursCommandedBgType() const` (returns `true`),
  overridden `false` in `FreeBGJoinAction`. A third commit on the sync branch. Task 5 builds it.

- [ ] **Step 1: Confirm the leak on the rebased branch before fixing it**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git show sync/upstream-6a85c09:src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.h | grep -n "class .*JoinAction\|isUseful\|shouldJoinBg"'
```

Expected: `class FreeBGJoinAction : public BGJoinAction` appears, and `isUseful` is declared
**only** under `BGJoinAction`. That absence is the leak.

- [ ] **Step 2: Add the predicate to `BGJoinAction` in the header**

In `BattleGroundJoinAction.h`, find this line inside `class BGJoinAction`:

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

- [ ] **Step 3: Override it in `FreeBGJoinAction` in the same header**

Find this line inside `class FreeBGJoinAction` (it is the last member, just above the closing
`};`):

```cpp
    virtual bool shouldJoinBg(BattleGroundQueueTypeId queueTypeId, BattleGroundBracketId bracketId);
```

Insert immediately **after** it:

```cpp
    virtual bool honoursCommandedBgType() const override { return false; }
```

Note both classes contain an identically-worded `shouldJoinBg` declaration, so match on the
surrounding class when editing — `BGJoinAction`'s is followed by a `#ifndef MANGOSBOT_ZERO`
block, `FreeBGJoinAction`'s is followed directly by `};`.

- [ ] **Step 4: Gate the bypass in `isUseful()`**

In `BattleGroundJoinAction.cpp`, replace the guard block added by `c06b2fb` with:

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

The only behavioural change from `c06b2fb` is the added `honoursCommandedBgType() &&`.

- [ ] **Step 5: Commit**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git add src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.h src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.cpp && git diff --cached --stat && git commit -m "Hold free bots to the battleground cap, keep the commanded path deterministic"'
```

Expected: `2 files changed, 13 insertions(+), 1 deletion(-)` for the code exactly as written
above (5 header lines, 7 new comment lines, and the reworked `if`). If you reworded a comment
the counts shift — what must hold is 2 files and exactly 1 deletion.

- [ ] **Step 6: Re-verify the branch shape**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git log --oneline upstream/playerbots-integration-gh..sync/upstream-6a85c09 && git diff --stat upstream/playerbots-integration-gh..sync/upstream-6a85c09'
```

Expected: 3 commits and **4** files (the header is new to the delta), 79 insertions,
2 deletions.

---

### Task 5: Build, deploy, and validate in production

This is the expensive step — a full ~27-minute compile — and the only one that can take the
server down. The existing tooling already handles ordering, the volume check and the liveness
gate; do not hand-roll it.

**Files:**
- Runs: `D:\TurtleWow\scripts\ship-cpp-fix.sh`, `D:\TurtleWow\scripts\verify-running-commit.sh`

**Interfaces:**
- Consumes: `sync/upstream-6a85c09` (Task 4), the reconciled conf (Task 3).
- Produces: a running server built from the rebased tip, `VERDICT: MATCH`, and image tags for
  rollback.

- [ ] **Step 1: Record the rollback target before building**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'docker images tortoise-v2 --format "{{.Repository}}:{{.Tag}} {{.ID}} {{.CreatedSince}}"'
```

Note the ID currently tagged `tortoise-v2:local`. Write it down — this is what you roll back to.

- [ ] **Step 2: Confirm the ship script will push the right branch**

`ship-cpp-fix.sh:62` reads the branch from the working tree (`BRANCH=$(prov_branch)`) and
pushes `origin "$BRANCH"` at line 209. Since `sync/upstream-6a85c09` is checked out, it pushes
there — **not** to `local`. That is what we want. Confirm the checkout:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git branch --show-current'
```

Expected: `sync/upstream-6a85c09`

- [ ] **Step 3: Ship it**

This takes ~30 minutes. It refuses a dirty tree, builds stamped, restarts, asserts the world
volume is the same one, waits out the ~60 s world load plus a 120 s settle, verifies, pushes.

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < /mnt/d/TurtleWow/scripts/ship-cpp-fix.sh > /tmp/ship.sh && bash /tmp/ship.sh'
```

Expected tail:
```
volume:     tortoise-wow-v2_dbdata: intact
VERDICT:    MATCH
```

If the build fails, the script restores the previous image and exits loudly — the server comes
back on the old binary. Nothing is lost; the branch still exists on `origin`.

- [ ] **Step 4: Independently verify**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < /mnt/d/TurtleWow/scripts/verify-running-commit.sh > /tmp/verify.sh && bash /tmp/verify.sh; echo "exit=$?"'
```

Expected: `VERDICT: MATCH` and `exit=0`.

Note the `echo "exit=$?"` is *inside* the WSL script string here, so it reads the real code.

- [ ] **Step 5: Assert the world survived**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'docker compose -f /home/deck/tortoise-wow-server-V2/docker-compose.yml ps && docker volume inspect --format "{{.CreatedAt}}" tortoise-wow-v2_dbdata'
```

Expected: all three services `Up`, and a volume `CreatedAt` matching its pre-build value (the
ship script already compares this, but confirm independently). A *newer* timestamp means the
volume was recreated empty — stop and roll back immediately.

- [ ] **Step 6: In-game regression check — both paths, they are the point of Task 4**

**6a — the forced path still works.** `c06b2fb` was measured at 0/3 before the fix:

1. Log in as GM.
2. `.rndbot debug <botname> setvalueuin32 bg type,2` (2 = Warsong Gulch).
3. Confirm the bot enters the WSG queue.

Expected: the bot queues.

If it does **not**, the likely cause is that your tournament bots run the RPG strategy and so
consume `bg type` through `"free bg join"` — where Task 4 deliberately disabled the bypass.
Do not reinstate the blanket bypass; that would re-break the general population. The fix in
that case is to keep Task 4 and raise the ceiling instead, by adding to
`etc/aiplayerbot.conf`:

```
AiPlayerbot.BgBotTeamCap = 2:10
```

(`2` = Warsong Gulch, `10` = bots per team — leaves room for a 10v10 while still capping.)
Restart and retest.

**6b — the general population is now capped.** This is the enhancement you asked for, and it
is only observable ambiently:

1. Leave the server running with no commanded queueing for ~30 minutes.
2. `.bg` / check running battlegrounds, or watch for multiple WSG instances forming.

Expected: **at most one** running Warsong instance per bracket while no real player is
queuing. Before this sync, three could form. If you still see several, `FreeBGJoinAction` is
not taking the override — re-check Task 4 Step 3.

If either check fails, roll back (Step 7) and investigate before promoting.

- [ ] **Step 7: Rollback procedure, if any of the above fails**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2 && docker tag tortoise-v2:c06b2fb tortoise-v2:local && docker compose up -d'
```

Then restore the conf: `cp etc/aiplayerbot.conf.pre-sync-20260811 etc/aiplayerbot.conf` and
restart. `local` at `c06b2fb` and the tag from Task 1 are both untouched, so the git side needs
no recovery at all.

---

### Task 6: Promote and document

Only after Task 5 is green. This is the point where `local` moves.

**Files:**
- Modify: C++ repo `local` branch
- Modify: `D:\TurtleWow\CPP-SOURCE-HANDOFF.md`
- Modify: `D:\TurtleWow\docs\CPP-SOURCE-WORKFLOW.md`

**Interfaces:**
- Consumes: a validated `sync/upstream-6a85c09` from Task 5.
- Produces: `local` == `origin/local` == the synced tip; docs describing the sync procedure.

- [ ] **Step 1: Move `local` to the validated tip**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git checkout local && git reset --hard sync/upstream-6a85c09 && git log --oneline -4'
```

This rewrites `local`. It is safe now: the old tip is preserved by tag
`pre-upstream-sync-20260811` on two remotes.

- [ ] **Step 2: Force-push `local` to both remotes**

`--force-with-lease` refuses if someone else moved the branch since your last fetch.

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git push --force-with-lease origin local && git push --force-with-lease backup local'
```

- [ ] **Step 3: Verify all three remotes agree**

Write to `<scratchpad>/t6-verify.sh`:

```bash
#!/bin/bash
cd /home/deck/tortoise-wow-server-V2/src
echo "=== local branch tips ==="
git rev-parse local sync/upstream-6a85c09
echo "=== origin/local ==="; git ls-remote origin refs/heads/local
echo "=== backup/local ==="; git ls-remote backup refs/heads/local
echo "=== recovery tag still intact ==="; git ls-remote origin refs/tags/pre-upstream-sync-20260811
echo "=== the delta is still 3 files ==="
git diff --stat upstream/playerbots-integration-gh..local
echo "=== running server matches? ==="
tr -d "\r" < /mnt/d/TurtleWow/scripts/verify-running-commit.sh > /tmp/v.sh && bash /tmp/v.sh
echo "verify exit=$?"
```

Run it:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < "/mnt/c/Users/mihov/AppData/Local/Temp/claude/D--TurtleWow/2dfcf21b-8058-4bf6-a38f-407fe762b8d5/scratchpad/t6-verify.sh" > /tmp/t6.sh && bash /tmp/t6.sh'
```

Expected: `local`, `origin/local`, `backup/local` all the same SHA; the tag still
resolving to `c06b2fb`; 3 files changed; `VERDICT: MATCH`, `verify exit=0`.

- [ ] **Step 4: Delete the now-redundant sync branch**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git branch -d sync/upstream-6a85c09 && git push origin --delete sync/upstream-6a85c09'
```

`-d` (not `-D`) refuses unless the commits are reachable elsewhere — a free check that the
promotion in Step 1 actually worked.

- [ ] **Step 5: Update the handoff**

In `D:\TurtleWow\CPP-SOURCE-HANDOFF.md` §4, replace the "**Upstream sync is deferred,
deliberately.**" paragraph with:

```markdown
**Upstream sync: done 2026-08-11.** `local` now sits as 3 commits on top of upstream
`6a85c09`. The rebase was conflict-free — the 66-line local delta touches three files and
upstream's churn missed every hunk (`PlayerbotMgr.cpp` had zero upstream commits at all).
Recovery point: tag `pre-upstream-sync-20260811` = `c06b2fb`, on `origin` and `backup`.

Two things a clean merge did not cover, both handled. The live `etc/aiplayerbot.conf` had to
take upstream's new `BotLogFile` (off) and `+quest` defaults by hand, because that file is
bind-mounted and is not regenerated by a build. And `c06b2fb`'s commanded-`bg type` bypass
turned out to leak: `FreeBGJoinAction` inherits `BGJoinAction::isUseful()` without overriding
it, and the RPG battlemaster route (`RpgSubActions.h:258`) sets `bg type` before dispatching
to `"free bg join"` — so ordinary bots were skipping upstream's new
`BotBattlegroundLimitReached()` cap too. `honoursCommandedBgType()` now scopes the bypass to
the commanded path; free bots are held to one instance per bracket.
```

Also remove the `logs/bots.log` regrowth item from §4 — upstream's `90419a0` plus the conf
change in Task 3 is the permanent fix.

- [ ] **Step 6: Add the sync procedure to the workflow doc**

Append to `D:\TurtleWow\docs\CPP-SOURCE-WORKFLOW.md`:

```markdown
## Syncing with upstream

Now that this is a native fork, a sync is a rebase. The whole procedure:

```bash
cd /home/deck/tortoise-wow-server-V2/src
git fetch upstream --prune
git tag -a pre-upstream-sync-$(date +%Y%m%d) local -m "before sync"
git push origin --tags && git push backup --tags
git branch -f sync/upstream local && git checkout sync/upstream
git rebase upstream/playerbots-integration-gh
git range-diff upstream/playerbots-integration-gh pre-upstream-sync-<date> HEAD
```

`range-diff` printing `=` for every commit means the local fixes carried over unchanged; a `!`
means one was altered by the rebase and needs reading.

Two things the rebase will not do for you:

1. **Config.** `etc/aiplayerbot.conf` is bind-mounted and never regenerated. Diff it against
   `src/modules/PlayerBots/playerbot/aiplayerbot.conf.dist.in` after every sync and port new
   keys by hand. Keep `deploy/etc/aiplayerbot.conf.template` in step.
2. **Semantic overlap.** A clean merge only proves the text does not collide. Check whether
   upstream added gates in code paths the local fixes short-circuit.

Then ship it with `scripts/ship-cpp-fix.sh` from the sync branch, validate, and only then
`git reset --hard` `local` onto it and `git push --force-with-lease`.
```

- [ ] **Step 7: Commit the docs**

```bash
git -C D:/TurtleWow add CPP-SOURCE-HANDOFF.md docs/CPP-SOURCE-WORKFLOW.md
git -C D:/TurtleWow diff --cached --stat
git -C D:/TurtleWow commit -m "docs: record the upstream sync and how to do the next one"
```

Confirm `--cached --stat` shows exactly two files.

- [ ] **Step 8: Push the ops branch**

```bash
git -C D:/TurtleWow push origin proper-setup
```

PR #8 is still open against `main` and will pick these commits up.

---

## Rollback summary

| If this fails | Do this |
|---|---|
| Rebase conflicts (Task 2) | `git rebase --abort`. Nothing was moved; `local` was never touched. |
| Build fails (Task 5) | The ship script restores the previous image automatically. |
| Server misbehaves after deploy (Task 5) | `docker tag tortoise-v2:c06b2fb tortoise-v2:local && docker compose up -d`, restore `etc/aiplayerbot.conf.pre-sync-20260811`. |
| You need the pre-sync source back | `git reset --hard pre-upstream-sync-20260811` — the tag is on two remotes. |
