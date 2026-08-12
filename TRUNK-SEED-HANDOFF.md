# Trunk Seed — Recovery Audit & Documentation Gaps

**Written:** 2026-08-11 · **For:** the agent standing up the new trunk
**Status:** audit complete and verified; documentation gaps listed but **not yet closed**

This document has two jobs: (1) tell you what actually exists and where, so you don't
re-derive it, and (2) hand you a ranked list of source material that still needs reading
before the trunk's docs can claim to be complete.

Everything in §1–§5 was verified directly this session. §6 is explicitly unread.

---

## 1. Headline: nothing was lost

The "untracked previous project" is a **fully version-controlled git repo** at
`D:\TurtleWow` — `ChrisMiho/turtle-tournament`, **private**, all branches intact, live
remote. The `OldScripts/` `oldTests/` `OldHandoffs/` dumps at this repo's root are a flat
copy of its `proper-setup` branch.

Verified:

| Dump | Finding |
|---|---|
| `OldScripts/` (38 files) | **Byte-identical** to `proper-setup:scripts/` (`diff -rq` clean) |
| `oldTests/` (27 files) | Filename-identical to `proper-setup:tests/`, no diff either direction |
| `OldHandoffs/` (81 files) | All markdown / `docs/` / `command-reference/` present in git; git is a **superset** |

**The three dump folders are redundant and can be deleted** — but read §2 first, because
they are *not* a complete capture of the old repo.

Only six dump-only files exist, none of them wanted:

- `backups/pre-alive-world-20260809-110537/` — gitignored runtime backup. **Contains the
  live DB root password in plaintext in `.env`.** Never `git add` this directory.
- `Restart/Stop-TurtleServer.ps1` + `.bat`, `TurtleClient.Common.ps1` — the `NoxFiles/`
  client-switcher family. Already ruled EXCLUDE (a third party's private server; carries
  tailnet addresses + GM credentials). In old history at `NoxFiles/` and
  `TurtleWOWClient/TurtleWoW/`.
- `turtle-command-data.txt` — gitignored generated dump.

## 2. ⚠️ The dumps missed two branch-exclusive workstreams

The old repo's value is split across **divergent branches**. `proper-setup` is 24 commits
ahead of `main`; `main` is 9 commits behind `origin/main`. The dump captured only
`proper-setup`, so this content exists in **no dump**:

| Content | Lives on | Why it matters to a new trunk |
|---|---|---|
| `docs/server-identity/README.md`, `scripts/collect-setup-evidence.sh`, `tests/setup-evidence-test.sh`, + `docs/superpowers/plans/2026-08-10-server-identity-and-setup-comparison.md` | **`origin/main` only** | A read-only evidence collector answering *"which playerbot implementation is actually running, and how does our setup diverge from stock?"* — the first question a fresh trunk asks. **Unrecovered and unread.** |
| `deploy/` — Dockerfile, `docker-compose.yml`, `.dockerignore`, `etc/*.conf.template` (8 files) | **`proper-setup` only** | The deployment layer. Absent from `main` and `origin/main`. |

**Implication:** any migration must read from the *union* of `proper-setup` and
`origin/main`, not from the dumps and not from a single branch.

## 3. Environment state (verified this session)

- **The world survived.** Docker volume `tortoise-wow-v2_dbdata` exists, created
  `2026-08-09T08:56:03Z` — the original. So the 20-bot WSG roster, ~4,541 RNDBOT
  characters, ~1,909 AH listings and all progression data are intact.
- **No containers exist** (`docker ps -a` empty). The stack is torn down, not running.
- **Images survive**, including rollback targets:

  | Tag | ID | Age |
  |---|---|---|
  | `local` / `candidate` / `17bb757` | `ca6f8a68ef99` | built from **this fork**, minutes ago |
  | `c06b2fb` | `9893d93cbecf` | ~22h |
  | `previous` | `4db3412224fd` | last pre-stamping build |
  | `pristine` | `7918a9e7c510` | untouched upstream build |

- Compose pins the mutable `tortoise-v2:local`, so **rollback is a re-tag**, not a rebuild:
  `docker tag tortoise-v2:previous tortoise-v2:local && docker compose up -d`.

**Open question for the owner:** does the new trunk reuse this volume, or start clean?
That single decision determines whether the roster and 4,541 characters are an asset or
irrelevant — and it gates most of §5.

## 4. The C++ fixes are already here — verified in source, not just claimed

All three are ancestors of this fork's `local` branch, confirmed present in the working
tree (not merely in commit messages):

| Commit | What |
|---|---|
| `2a45788` | Register a freshly created bot in the player cache, and group at any level |
| `c06b2fb` | Honour an explicitly-set bg type in `BGJoinAction::isUseful()` |
| `0f5a71b` | Hold free bots to the BG cap, keep the commanded path deterministic |

The guard is live at `src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.cpp:652`.

**Do not re-extract, re-apply, or hand-retype these.** Prior sessions burned time on this.

### Why they matter (the nuance a new trunk needs)

`Engine::ExecuteAction` gates **every** commanded action on `isUseful()`. `BGJoinAction::Execute()`
already honoured an explicitly-set `"bg type"` AI value and skipped the candidate list —
but `isUseful()` never read it, and re-rolled a **hardcoded 20% tank/healer gate** over
the decision, returning `ACTION_RESULT_USELESS` without ever calling `Execute()`. So the
command silently never ran. The fix is a guard placed *after* the free-slot check and
*before* the roll, so every real safety gate (in-BG, deserter, free slot, player master,
**in combat**) still applies.

Measured: commanded joins **0/3 → 8/8**; full roster **20/20 in 16 seconds**.

Second nuance: `bgList` is populated **only inside `isUseful()`**, so invoking the action
directly without `isUseful()` having just run hits an `empty()` guard and no-ops silently.
The `"bg type"` value is the escape hatch that bypasses `bgList` entirely.

## 5. Workstream inventory — honest status

| Workstream | State | Where |
|---|---|---|
| **WSG spawn → queue → auto-start** | ✅ **Proven live.** 20/20 queued in 16s, instant pop, true 10v10, full match reported end-to-end, mode cycle round-tripped. 10 test suites green. | `proper-setup:scripts/wsg-{kickoff,mode,roster,logs-clean}.sh` + `lib/` |
| **C++ provenance / ship pipeline** | ✅ Done, verified in production. 97 checks. | `scripts/{ship-cpp-fix,verify-running-commit}.sh`, `lib/provenance.sh` |
| **Bot-progression telemetry** | 🟡 Code complete + 20 tests green; **Tasks 3–8 blocked** on the server being in WSG-experiment mode | `scripts/bot-progression/*`, `scripts/lib/botdb.sh` |
| **Guide-driven leveling routes** | 📄 **Design only — zero code.** Approved spec + 757-line plan. No `scripts/bot-routes/` exists anywhere. | `docs/superpowers/{specs,plans}/2026-08-09-guide-driven-bot-leveling-routes*` |
| **Server identity / stock-vs-local** | ❓ Exists on `origin/main`, **never read** | see §2 |

### Leveling: the finding that reframes the whole problem

The observed symptom was *"level ~8 bots lingering in starting zones"*, and the design
treats it as a **routing** problem. But the bot-progression investigation found the
dominant constraint is an **activity budget**, not a route:

- `botActiveAlone` scales what fraction of *solitary* bots run full AI, rotating ~1%/min.
  At `25`, **three quarters of the population is dormant** while still accruing
  `characters.totaltime`.
- So `totaltime` measures **logged-in** time, not **active** time. Measured: ~29 logged-in
  hours × ~25% ≈ 7 active hours → level 12–14. Roughly **2 levels per active hour.**
  *The leveling engine is fine; the budget is thin.*
- `DisableActivityPriorities = 1` makes every bot always-active and **ignores
  `botActiveAlone` entirely** (`PlayerbotAI.cpp:6087` short-circuits).

Live population at the time: 4,541 RNDBOT characters, **3,499 never logged in**, 0 guilds,
professions stuck at skill 1–2, and the only level 60s were the WSG roster — *created* at
60, none organically levelled.

**Nuance:** routes and budget are complementary, not alternatives. Route data genuinely
determines world distribution (because `DisableRandomLevels=1`, `startingLevel=1`,
`XPRate=3` mean bots really do level), but no route fixes a population that is 75% asleep.
A new trunk should treat activity budget as the first lever and routes as the second.

**Route levers are all SQL/config — no C++:**

| Table / key | Controls |
|---|---|
| `tw_world.ai_playerbot_zone_level(id, level)` | Travel gate. `botLevel < areaLevel → invalid`; plus a grind-only floor `areaLevel <= max(level*0.4, level-12) → invalid for Grind` |
| `tw_char.ai_playerbot_tele_cache` | Per-level teleport drop points |
| `tw_world.ai_playerbot_rpg_races` | RPG camp placement per race/level |
| `RandomBotTeleportTeleportMin/MaxInterval` | Relocation frequency (plan stretches 1–7d → 3–14d) |

Guide sources are Guidelime addons: **Sage** (Alliance 1–60), **Bustea** (Horde 1–60),
manually downloaded, gitignored; everything generated from them is committed.
`EnableRandomTeleports=0` is **not** a stuck-only option — it kills all relocation
including near-player seeding.

## 6. 📋 Documentation gaps — what still needs reading

**None of the following has been read.** Ranked by what a fresh trunk would otherwise
rediscover the hard way. All paths are in the private repo unless noted.

| # | Source | Lines | Why it matters |
|---|---|---|---|
| 1 | `PLAYERBOT-AI-HANDOFF.md` | 630 | Largest unread doc. "AI engine / enhance notes" — by title, the most relevant material to the leveling ambition |
| 2 | `docs/playerbots/WSG-BOT-MATCH.md` | — | **The operator runbook.** Every other doc defers to it for the actual procedure. Read *about*, never read |
| 3 | `docs/alive-world/{README,STATUS,CONTINUE-LATER}.md` | — | The "alive-world hard rules" cited as binding constraints by three separate plans |
| 4 | `docs/server-identity/README.md` (**`origin/main` only**) | — | Unrecovered *and* unread. See §2 |
| 5 | `BOTS-LOG-GROWTH-HANDOFF.md` | 338 | An unresolved decision the trunk inherits — see §7 |
| 6 | `WSG-FIRST-RUN-DEBUG-HANDOFF.md` / `WSG-MATCH-FIXES-HANDOFF.md` | 204 / 115 | Outcome docs + restore sequences |
| 7 | `WINDOWS-SETUP-HANDOFF.md` | — | Server restore / host topology |
| 8 | `docs/evidence/WSG-Debug/*` (3 docs + screenshot) | — | Live validation records behind the measured claims in §4 |
| 9 | `command-reference/` | ~20 files | Standalone Python tool parsing C++ chat tables → command docs. Plausibly belongs in this fork, since the C++ is here |
| 10 | `deploy/` (**`proper-setup` only**) | 8 | Deployment layer — Dockerfile, compose, conf templates |
| 11 | Leveling plan body (`2026-08-09-guide-driven-bot-leveling-routes.md`) | 757 | Only headers + the spec were read. The 9 tasks themselves are unread |
| 12 | `fork-migration/` (this repo) | 3 docs | Written for a WSL-nested environment; read as history, not instructions |

**Recommendation:** close **1–5** before the trunk docs claim completeness. Their absence
would make the documentation *misleading*, not merely thin. Items 6–12 are fine as
annotated pointers into the private repo.

## 7. Open decisions the trunk inherits

These need an owner, not investigation:

1. **Reuse the DB volume or start clean?** (§3) Gates everything downstream.
2. **`bots.log` growth.** Regrows to gigabytes — observed at 11.7 GB, and back to 32 MB
   within 90 minutes of being truncated. `truncate -s 0` is safe on a running server
   (`BotLog.cpp:35` opens with `fopen(path,"a")`). The permanent fix has a stated tradeoff
   documented in gap #5. **Never `cat` this file.**
3. **Upstream sync stance.** Upstream was 60 commits ahead and rewrote
   `BattleGroundJoinAction.cpp` (81 lines) — the file carrying the bgtype fix. Prior
   stated intent: *track local diffs from where the codebase started, don't pull upstream.*
4. **Do ops scripts belong in a public repo at all?** The scripts audit clean, but the old
   ruling was that automation stays private and only C++ goes public.
5. **Roster gear determinism.** `gear=blue` rolls a *random* rare kit, so a rebuilt roster
   is equivalent-tier, not identical.

## 8. Hard-won traps — do not rediscover these

Consolidated from the docs that *were* read. This is the highest-value section here.

**Will destroy things:**

- **`rndbot debug <bot> values …` CRASHES THE WORLD.** SIGABRT, container `Exited (134)`.
  `HandleBotDebug` dispatches with `master ? master : bot`, so from console the reply
  target is the bot itself, and a multi-line payload through its socketless `WorldSession`
  overflows the ByteBuffer. `setvalueuin32` is safe — it has no output path.
- **EOF on the mangosd console shuts the world down.** Only reach it via `wsg_console`,
  which detaches with `ctrl-p,ctrl-q`. Never Ctrl-C the attach stream.
- **Never `docker compose down -v`** — that volume is the entire world. It has been lost once.
- **`docker compose up -d` recreates a missing named volume, empty.** So asserting the
  volume *exists* proves nothing; compare `docker volume inspect --format '{{.CreatedAt}}'`.

**Silent-failure traps:**

- **Bot names must be alphabetic.** Digits are rejected at character *load*, not creation,
  and `"Bot is now online"` prints *before* login is attempted. A digit-named bot looks
  created, then vanishes. Cost a full debugging session.
- **A failed create is a runaway, not inert.** `LoginFreeBots` never removes a failed login
  from its queue → retried every world tick forever. Measured **10,558 `at_login` writes in
  39 minutes across two bots.** Create in small batches, verify in DB within ~10s, and
  **delete any row with `at_login != 0` immediately.**
- **Every `rndbot` console reply is discarded.** A failed command looks exactly like a
  successful one. **Verify from `bg.log` or the DB — never console output or exit codes.**
- **`grep -q` + a big stream + `set -o pipefail` = an unkillable wait.** `grep -q` exits at
  first match and closes the pipe; `docker logs` dies of SIGPIPE and reports 141, which
  `pipefail` propagates, so an `until` loop can *never* terminate. Read as slowness for a
  whole session. Note `bash -c '...'` starts a shell **without** `pipefail` and will
  cheerfully tell you the loop is fine.
- **`docker attach` exits non-zero when you detach**, so under `pipefail` a fully
  successful send reports failure. Same shape as above.
- **A published port answers before the process does.** `docker-proxy` binds the host port
  at container *creation*, so `nc -z` succeeds with nothing listening. Readiness must also
  assert `.State.Running`, and re-assert after a settle — mangosd's world load is ~60s.
- **After any mangosd restart the roster does not return.** `characters.online` reads a
  stale `1`, so commands silently no-op against bots not actually in world. Re-add all 20
  explicitly.
- **`rndbot do` cannot take arguments** — it resolves the *entire* param string as an
  action name. `bg join` works because that is literally a registered name. Strategy
  changes must go through config.
- **`bg leave` cannot dequeue from console.** `rndbot do` always supplies a source, so
  `BGLeaveAction::Execute` always takes the wrong branch and feeds a PORT-shaped body
  (5 bytes) to the LEAVE handler, which expects 8.

**Environment traps:**

- **Shell variables do not survive `wsl.exe -- bash -lc '...'`.** `$VAR`, `$1`, `$?` and
  even `\$` arrive **empty** — so an inline `awk '{print $3}'` becomes `{print }` and dies.
  `$(...)` substitution *does* work. **Write a script to a file and run it by path.**
  Pipe output through `tr -d '\0'`.
- **Never run git from inside WSL against a Windows worktree.** A worktree's `.git` file
  holds `gitdir: D:/...`, which WSL git rewrites to `/mnt/d/...`, breaking Windows git.
- **CRLF.** `.sh` files checked out CRLF are reported *clean* by git but cannot execute in
  WSL (`set: pipefail: invalid option name`). If a script fails bizarrely, check for `\r`.
- **`docker logs --since` needs a `Z`-suffixed timestamp**, else it's read as local time,
  lands in the future, and returns nothing.
- **`rg` is not installed in WSL** — link/reference audits that use it silently no-op and
  exit 0. Do not trust a green `docs-link-audit.sh`.
- **`git commit` commits the whole staged index.** A commit intended to carry four doc
  files once carried ~3,400 lines of a concurrent session's work. Check
  `git diff --cached --stat` first.
- Console commands take **no leading dot**; in-game chat keeps it.

## 9. Never commit

This fork (`ChrisMiho/tortoise-wow`) is **PUBLIC**. `ChrisMiho/turtle-tournament` is
private and **must stay private** — its *history* still contains a GM password and tailnet
addresses (`552ddd4` untracked them but did not rewrite history).

Blocked from ever reaching a commit here:

1. `OldHandoffs/backups/**` — `.env` holds the live DB root password in plaintext.
2. The `NoxFiles/` client-switcher family (script, plan, spec) — a third party's private
   server. Owner ruled EXCLUDE; that ruling carries forward.
3. `docs/interupt/logs.txt` — a raw session transcript. Noise, not documentation.

An `audit-public-safe.sh` gate is designed (catches CGNAT `100.64.0.0/10`, `.ts.net`, the
switcher by name) but **does not catch a bare `DB_PASS=<value>` line**. A literal search
for the actual secret value is still required before anything from `backups/` could be
considered — and nothing there needs to move: the password reaches scripts at runtime via
`.dbpass`, which is the pattern every migrated script already follows correctly.

Two files in that backup dir (`aiplayerbot.conf`, `docker-compose.yml`) matched the sweep
but were checked and are safe — a doc comment and a `${DB_PASS:?...}` expansion. The rest
of `backups/` was **not** read in full.

## 10. Suggested first moves

1. **Get the owner's answer on §7.1** (volume reuse) — it gates the rest.
2. **Read gaps 1–5** (§6) and fold them into the trunk docs.
3. **Pull the two branch-exclusive workstreams** (§2) out of `proper-setup` and
   `origin/main` before anyone deletes or archives the private repo.
4. **Delete `OldScripts/` `oldTests/` `OldHandoffs/`** from this repo root once 2–3 are
   done — verified redundant, and `OldHandoffs/backups/` is an active secret-leak risk
   sitting in the working tree of a public repo.
5. Reconcile the private repo (`proper-setup` 24 ahead, `main` 9 behind `origin/main`) so
   the archive is coherent, *or* consciously decide to leave it divergent.

---

## Reference

| What | Where |
|---|---|
| This fork (public) | `D:\CodingProjects\tortoise-wow\tortoise-wow`, branch `local` |
| Private archive | `D:\TurtleWow` = `ChrisMiho/turtle-tournament` — branches `proper-setup` (tip), `main`, `origin/main` |
| Upstream | `Shyalya/tortoise-wow` |
| Live stack root (WSL) | `/home/deck/tortoise-wow-server-V2`, C++ under nested `src/src` |
| Existing migration plan (unexecuted) | `.worktrees/ops-migration/docs/superpowers/plans/2026-08-11-ops-scripts-migration.md` — 6 tasks, scripts/tests only, predates the §2 finding |
| Prior handoff | `NEXT-SESSION-HANDOFF.md` — superseded by this document on the "nothing is lost" question |
