---
status: pending
risk: medium
area: playerbots/memory
depends-on:
---

# No attribution for bot memory — mangosd burns ~4.67 GiB at 200 bots with no idea where it goes

**Problem:** The stack cannot hold a dense world. `docs/alive-world/STATUS.md`
records mangosd at **~4.67 GiB RSS with ~201 online bots**, on an 8 GB WSL that
was already using ~1.1 GiB of swap, and slow character logins were traced to
that swapping rather than to bot count. The upstream fork's owner reports
**2000 bots in 1 GB** on their Turtle server, so the gap is roughly two orders
of magnitude per bot — but their optimizations have not been shared, and it is
unknown whether the win is configuration, allocation-level code work, or an
architectural difference in how per-bot AI state is held.

Nothing in this repo attributes that 4.67 GiB. There is no per-bot marginal cost
figure and no separation between the bot-free server footprint (maps, mmaps,
DBC/world caches, InnoDB) and what each additional bot actually costs. Without
that split, "optimize memory" has no target and no way to tell a real win from
noise — the current numbers are a single data point taken by hand at one bot
count.

This artifact produces the measurement and the analysis. It ships no functional
change to the server.

**Suspected cause / area:** whole-system, but the likely concentrations are
per-bot AI state in `src/modules/PlayerBots/playerbot/strategy/`
(`AiObjectContext`, `NamedObjectContext.h`, `Engine`, `Action`/`Value` object
graphs — every bot may hold its own full instantiation of objects that could be
shared or lazily built), `PlayerbotAI.{h,cpp}`, `PlayerbotFactory.cpp` and
`RandomPlayerbotMgr.cpp` (per-bot inventory/talent construction), `TravelMgr` /
`TravelNode` / `WorldPosition` (the travel graph and any per-bot copies of it),
`RandomItemMgr.cpp` and `PlayerbotTextMgr.cpp` (large static tables), plus core
`Player`/`WorldSession` cost that a bot pays for even without a client socket.
`PlayerbotLLMInterface.cpp` and the `bots.log` trace path
(`BotLog.cpp`, see `docs/playerbots/BOTS-LOG-GROWTH-HANDOFF.md`) are also worth
pricing since both are optional.

The module already contains an unused instrument:
`src/modules/PlayerBots/playerbot/MemoryMonitor.h` implements a per-class
object census (`Add`/`Rem`/`LogCount`) gated behind a commented-out
`#define MEMORY_MONITOR` at line 3. Enabling it in an instrumented build is the
intended way to attribute object counts by class rather than guessing.

## Existing tooling — restored, do not rewrite

A set of harness scripts was recovered from an older stack tree and landed
under `scripts/` on 2026-08-14. **Extend these; do not write parallel
implementations.** They work, but they were written against a different host
layout and carry the specific defects listed below.

| Script | Role here | Known defect to repair |
|---|---|---|
| `scripts/task3-ramp-step.sh` | The ramp itself: `apply` sets Min/MaxRandomBots and restarts mangosd, `gates` dumps `free -h` + `docker stats` + online count, `wait` blocks until a threshold. | `ROOT=/home/deck/tortoise-wow-server-V2` is hardcoded — make it an env override. Output is human-readable tables, not parseable rows; it needs a CSV mode before any curve can be fitted. The name is a leftover from an old task list and should be renamed. |
| `scripts/wait-rndbots-online.sh` | Plateau detection — a sample taken mid-login is not a plateau. Handles container status better than `task3`'s `wait`. | Duplicates `task3-ramp-step.sh wait`. Collapse into one implementation. |
| `scripts/lib/botdb.sh` | Read-only queries against `tcm-db`. Env-overridable already. | None known. |
| `scripts/lib/provenance.sh`, `scripts/verify-running-commit.sh` | Proves the running image was built from the commit under test. **Before/after memory numbers are meaningless without this.** | Two mismatches against the current `Dockerfile`/`rebuild.sh` — see below. Both must be fixed before the script is trusted. |
| `scripts/ai-dev-profile.sh` | Stops mangosd + realmd, keeps `tcm-db` up. This is how the **bot-free intercept** gets measured, and how RAM is freed for a build. | Paths only. |
| `scripts/backup-alive-world-pre.sh` | Snapshots `aiplayerbot.conf` and friends before the ramp mutates them. | Writes to `/mnt/d/TurtleWow/backups`, which may not exist on this host. |
| `scripts/check-build-progress.sh` | Progress on a multi-hour unattended build. | Hardcoded to one historical build's log path. |
| `scripts/bot-progression/{snapshot,report,churn-report,describe}.sh` | **The capability-regression instrument** — levels gained per logged-in hour, pool churn ratio, level bands, with pass/fail thresholds. | None known. |

`verify-running-commit.sh` is currently guaranteed to report **DRIFT** against
any image `scripts/rebuild.sh` produces, for two independent reasons. Both are
comparison bugs in the recovered script, not problems with the build:

1. `lib/provenance.sh`'s `prov_head_sha()` returns the **full** SHA, while
   `rebuild.sh` stamps `org.opencontainers.image.revision` from
   `git rev-parse --short HEAD`. The equality test can never pass.
2. `prov_is_dirty()` treats the label as the string `"true"`, but `rebuild.sh`
   stamps `com.turtle.source-dirty` from `git status --porcelain | wc -l` — a
   count (`0`, `3`, …), never `true`. It also disagrees on definition:
   `provenance.sh` counts untracked files as dirty, `rebuild.sh` passes
   `--untracked-files=no`.

The label **keys** themselves are correct — `Dockerfile:81-83` stamps exactly
the three `provenance.sh` reads — so this is a small repair, not a redesign.

**Acceptance criteria:**
- A new `docs/playerbots/BOT-MEMORY-INVESTIGATION.md` exists, in the same voice
  and structure as `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md`.
- The two `verify-running-commit.sh` defects above are fixed, and the script
  reports `MATCH` against a freshly built image. Every measurement recorded in
  the doc names the image revision it was taken against.
- `scripts/task3-ramp-step.sh` is repaired per the table (env-overridable root,
  CSV output, sane name) and its `wait` mode is merged with
  `wait-rndbots-online.sh`. `docs/alive-world/README.md`'s reference to it is
  updated to the new name if it is renamed. That doc's other reference,
  `tests/playerbot-verify.sh`, is still missing from this repo — either restore
  it or delete the reference.
- A **baseline ramp** is captured across at least three bot counts — 50, 200,
  400 at minimum, continuing upward while stop gates hold — each held long
  enough for RSS to plateau. Stop gates are the ones already used in
  `docs/alive-world/STATUS.md`: host free ≥ 4 GB, WSL available ≥ 800 MB, no
  Docker OOM or container restarts, client still playable. The ramp stops at
  the first gate trip and the doc records which gate tripped at what count.
- The **bot-free intercept** is measured, not inferred: mangosd up with
  `MaxRandomBots = 0` (or the stack under `ai-dev-profile.sh off` with bots
  disabled), RSS recorded once the world has finished loading.
- From intercept plus ramp the doc reports a **linear fit** giving the per-bot
  marginal cost in MB, and restates the 2000-in-1-GB goal as a concrete budget
  — `intercept + 2000 x slope <= 1024 MB` — with the current numbers plugged in
  so the size of the gap is stated as a number, not an adjective.
- An **object census** from an instrumented build with `MEMORY_MONITOR` enabled
  (`MemoryMonitor.h:3`) at a bot count that fits comfortably, attributing live
  object counts by class, plus per-object sizes for the top classes (`sizeof`
  from the headers, noting where the real cost is in owned heap allocations
  rather than the object itself). The doc states what fraction of the measured
  per-bot cost this census actually accounts for, and says so plainly if a
  large share is unattributed.
- A **capability baseline** is captured with `scripts/bot-progression/`:
  snapshots either side of a run long enough for `report.sh` to produce a
  meaningful levels-per-logged-in-hour figure, plus a churn ratio. This is the
  reference any later optimization is measured against.
- A **prioritized optimization plan**, each item carrying: estimated MB/bot
  saved with the reasoning behind the estimate, blast radius, risk (low/medium/
  high), and whether it is configuration-only or a code change. Items are
  ordered by saving-per-unit-risk and split into two clearly separated groups:
  **config-only wins** applicable to a running server today, and **code
  changes** that need their own artifacts. Each code item is written
  specifically enough that `/backlog-scope` can turn it into a standalone
  artifact without re-deriving the analysis — name the files and the mechanism,
  not just the subsystem.
- Every plan item is labelled **capability-neutral** or **capability-affecting**
  against the constraint below, with what the bot would lose spelled out for the
  latter, and states how `bot-progression/report.sh` would detect the
  regression. A capability-affecting item is not automatically excluded, but it
  must be ranked separately so it is never applied by accident.
- The doc ends with an explicit **"what we did not determine"** section — the
  measurements attempted and failed, the numbers that are estimates rather than
  observations, and what the upstream fork owner would need to share to close
  the remaining unknowns.
- `MaxRandomBots` in the stack's `aiplayerbot.conf` is restored to `200` and the
  stack is left running and healthy at the end of the run, whatever happened
  during the ramp.

**Notes:**

*Capability constraint — the point of the whole exercise.* Memory savings must
not cost the bots their ability to play the game like a real player would.
Rewriting a system to remove waste while preserving behaviour is explicitly in
scope, including large rewrites. Trimming what bots are allowed to think about
or do — fewer strategies, coarser decisions, shallower travel graphs, longer
update intervals that make bots visibly sluggish — is not a win and must not be
proposed as one without being labelled capability-affecting. Making bots
*smarter* is a separate goal that later work will pursue, so the plan should
prefer optimizations that leave headroom for that rather than spend it.

*Build path.* `scripts/rebuild.sh` is the only supported build: it builds from
**this** checkout to `tortoise-cm:candidate`, runs five acceptance checks, and
moves the `:local` tag only if all pass. It must run **from WSL, not Git Bash**
(it fails closed on `MSYSTEM`, because MSYS path rewriting once produced five
false FAILs after a successful 40-minute compile). Budget ~40 minutes.
`docker compose build` is a silent no-op — `docker-compose.yml` pins
`image: tortoise-cm:local` with no `build:` key. The instrumented
`MEMORY_MONITOR` build therefore costs a full cold compile, and cannot run
while the server is up: the heavy playerbot translation units want 1.5-2.5 GiB
per `g++` and the build OOMs above `-j2`, so bring the stack down with
`ai-dev-profile.sh on` first, or use `BUILD_JOBS=1`.

*Source tree.* This checkout (`/mnt/c/Coding/tortoise-wow/tortoise-wow` from
WSL) is what `rebuild.sh` builds. A second, **diverged** checkout exists at
`/home/deck/tortoise-wow-server-V2/src` sharing ancestor `c06b2fb` — see
`docs/DOCKER.md` "Where the source of truth is". Do not build from it. That
directory is still live for everything else, though: `.env` bind-mounts its
`etc/`, `data/` and `logs/` into the containers and its `.dbpass` is the
database password, so the recovered scripts pointing at
`$HOME/tortoise-wow-server-V2` for **configs and logs** are correct as long as
`$HOME` resolves to `/home/deck`. Only their *source* and *build* paths are
wrong.

*Environment.* The drain has full control of the stack for this run: it may
stop/start the compose stack, edit `Min/MaxRandomBots`, restart mangosd
repeatedly, and — per `docs/alive-world/CONTINUE-LATER.md` — raise WSL from
8 GB to 12 GB in `C:\Users\mihov\.wslconfig` and restart WSL to get further up
the ramp. Run `scripts/backup-alive-world-pre.sh` before the first config
change. A WSL restart kills everything else in WSL, so it must be recorded in
the doc when it happens, and figures either side of it must never be compared
as if they came from the same configuration: the doc reports which ramp points
were taken at 8 GB and which at 12 GB. Never `docker compose down -v` — that
volume is the entire world and has been lost once already.

*Feasibility.* This needs a working stack per `docs/DOCKER.md` and enough bot
accounts for the ramp — `RandomBotAccountCount` is 500 per
`docs/playerbots/PLAYERBOT-AI-HANDOFF.md`, which may cap the achievable bot
count before memory does; if it does, that is a finding to record, not a
failure. If the stack cannot be brought up at all, this artifact is **blocked**,
not failed — but the static half (code analysis, object sizes, script repairs,
plan) should still be delivered, with the doc stating clearly that no
measurement was taken and every number in it is an estimate.

*Scope.* The 1000-, 2000- and 3000-bot runs the original idea asks for are
deliberately **out of scope here** — they cannot fit before the optimizations
land. They belong in a follow-up artifact scoped after the plan's items have
been implemented, at which point the same repaired ramp script re-runs and the
before/after comparison is apples to apples. Note that requirement in the doc so
the CSV format is designed to make the later comparison possible.

*Source.* `docs/memory-efficiency/idea.md`.
