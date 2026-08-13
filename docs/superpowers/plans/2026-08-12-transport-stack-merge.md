# Transport Stack Merge Plan — PRs #9–#16

**Goal:** get the eight transport/travel PRs the backlog drain opened onto
`cm-main` in an order that compiles at every step, without leaving `cm-main`
broken if a build fails.

**Decisions (2026-08-12):**

- **Three builds**, one per wave. Not per-PR (8 serial `-j2` builds is a very
  long wait) and not one at the end (a failure would leave 8 PRs as suspects).
- **Compile + server-starts gates a merge.** Gameplay is verified *after* the
  wave merges, on the integration branch. Anything found becomes a new backlog
  artifact. None of these PRs was ever runtime-tested, so gating on gameplay
  would block the whole stack on manual play sessions.
- **All work happens on `integration/transport-stack`**, cut from `cm-main`. A
  failed build never touches `cm-main`; one merge at the end.

## Why this order

Ordered by blast radius, smallest first, so the earliest builds are the least
likely to fail and a failure is cheap to attribute.

### Wave 1 — playerbots module + data only (cannot break the core build)

`#11` → `#10` → `#15` → `#12`

- `#11` and `#10` both touch `MovementActions.cpp` — rebase `#11` on `#10`.
- `#15` and `#10` both touch `TravelNode.cpp` — rebase `#15` on `#10`.
- `#12` and `#15` both touch `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md`.

→ **Build 1**, then smoke: server up, `aiplayerbot.conf` loaded, bots spawn.

### Wave 2 — small core-touching

`#9` → `#13` → `#14`

- `#9` first: `Map::GetTransports()` returning `_transports` is what makes
  `#14`'s boat path reachable at all.
- `#9` and `#12` both touch `WorldPosition.cpp` (`#12` already landed in wave 1).
- `#14` touches `MovementActions.cpp` (after `#10`/`#11`) and `Transport.h`.

→ **Build 2**, then smoke + first real functional check: a bot boards a
Menethil↔Theramore boat or an Orgrimmar↔Undercity zeppelin.

### Wave 3 — `#16` alone

1207 lines across 35 core files. Essentially all the compile risk in the stack
lives here, so it gets its own build regardless of cadence.

- Rebase over everything; resolve `Map.h`, `Map.cpp` (`#9`), `Transport.h`
  (`#14`), `Player.h` (`#13`).
- **Rename its migration.** `#15` and `#16` both ship
  `sql/database_updates/20260812120000_world.sql` — the same filename with
  different contents. `#16`'s must become a later timestamp or one silently
  wins.
- Apply the migration to the world DB before the smoke test.

→ **Build 3**, then smoke + a tram/elevator ride.

### Wave 4 — bookkeeping

Merge `loop/round2` (artifact statuses, the model-tuning baseline, the
`out-of-scope` lifecycle addition). It conflicts with `#10`, `#12` and `#13`,
each of which edited its own artifact `.md` on its branch; resolve toward the
version carrying the `**Result:**` line.

## Conflict matrix

| File | PRs |
| --- | --- |
| `sql/database_updates/20260812120000_world.sql` | **#15 + #16 — identical filename, different content** |
| `src/game/Maps/Map.cpp`, `Map.h` | #9 + #16 |
| `.../strategy/actions/MovementActions.cpp` | #10 + #11 + #14 |
| `.../playerbot/TravelNode.cpp` | #10 + #15 |
| `.../playerbot/WorldPosition.cpp` | #9 + #12 |
| `src/game/Transports/Transport.h` | #14 + #16 |
| `src/game/Objects/Player.h` | #13 + #16 |
| `docs/playerbots/BOT-TRANSPORT-INVESTIGATION.md` | #12 + #15 |

## Build invocation

No CI exists (`.github/workflows` is absent), so every build is manual. The
repo-root `Dockerfile` builds from this checkout (`COPY . /src`) with
`-DBUILD_PLAYERBOTS=ON`, `-DCMAKE_INSTALL_PREFIX=/opt/turtle`, `-j2`.

Constraints inherited from
`2026-08-11-docker-build-from-this-checkout.md` — all still binding:

- `BUILD_PLAYERBOTS=ON` defaults **OFF** and building without it yields a
  bot-free server with no warning anywhere.
- `CMAKE_INSTALL_PREFIX=/opt/turtle` is compiled in; changing it means no
  `aiplayerbot.conf` and no bots.
- Compose project name is pinned to `tortoise-wow-v2`; `tortoise-wow-v2_dbdata`
  is `external`. **Never `docker compose down -v`** — that volume is the entire
  world, 4545 characters.
- Docker VM is 4 CPUs / 8 GB. `-j2` is what fits; higher invites the OOM killer
  mid-compile.

### Environment state found 2026-08-12

- The wrapped invocation `wsl -d Ubuntu -- bash -lc '...'` was **canary-tested
  and returns correct output** (file count 186 matched a native count), despite
  the older plan's warning about mangled `$(...)`. Treat single-quoted commands
  without substitutions as safe; re-canary before trusting anything with
  `$(...)`.
- **Docker is not exposed inside the Ubuntu distro** ("command 'docker' could
  not be found… activate WSL integration in Docker Desktop settings"). The
  Windows-side CLI works (`docker compose` v5.3.1).
- Docker Desktop was **not running**; it was started as part of this plan.

Consequence: `docker build` can run from Windows — the build context is just
the repo directory and needs no WSL path semantics. The **runtime** step
(`docker compose up`, with `/mnt/d` relative bind mounts and the external
volume) is what the older plan insists must run inside WSL, so that step needs
WSL integration enabled in Docker Desktop settings — a GUI toggle no agent can
flip.

## Known-outstanding after this lands

Not blockers for merging, recorded so they are not mistaken for regressions:

- **`#16` ships every `epoch_offset` at `0`.** A bot-ridden tram car or lift is
  on the right path in the right place at the wrong *time* until each entry is
  calibrated in-game with the corrected formula (see artifact 009). Visual
  desync during the wave-3 check is expected.
- **The ~49.7-day counter wrap** shifts `#16`'s phase by `(2^32 % TotalTime)`;
  inherent to riding a wrapping 32-bit counter the client is seeded from too.
  Confirm during the same in-game pass.
- **`#12` introduces a new pending artifact**,
  `docs/backlog/010-index-go-spawn-lookup-by-entry.md`, which a future drain
  run will pick up.
- **Nothing in the stack has ever been compiled or run.** Every PR body says so.
