# Docker Build From This Checkout — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make this checkout able to compile its own C++ into a Docker image and launch the stack from this directory, reusing the existing world database and extracted client data rather than rebuilding them.

**Architecture:** Add a repo-root `Dockerfile`, `.dockerignore`, `docker-compose.yml` and `.env.example`, adapted from the proven deploy layer that currently lives outside this repo. The build context becomes the repo root (`COPY . /src`) instead of a parent stack directory (`COPY src /src`). Runtime state — extracted client data, tuned configs, logs, and the `tortoise-wow-v2_dbdata` volume — stays exactly where it is in the WSL stack directory and is referenced through `.env` variables, so nothing multi-gigabyte is duplicated and the existing world is preserved.

**Tech Stack:** Debian trixie, GCC 14.2, CMake 3.31, ACE 8.0.2, Boost 1.83, MariaDB 10.6, Docker Compose v2, WSL2 Ubuntu.

## Global Constraints

- **`CMAKE_INSTALL_PREFIX=/opt/turtle`** is compiled into the binary (`SYSCONFDIR` is baked at build time). Never change it, or the server starts, logs one line about not opening `aiplayerbot.conf`, and runs with no bots.
- **`-DBUILD_PLAYERBOTS=ON`** — defaults `OFF`, and building without it produces a bot-free server with **no warning anywhere**.
- **`-DALLOW_TURTLE_ADDONS=ON`** — defaults ON and must stay ON, or the client crashes with *"interface corrupt"* on entering the world.
- **`-DUSE_EXTRACTORS=ON`** — defaults `OFF`; without it there is no `mapextractor`/`vmapextractor`/`MoveMapGen`.
- **`-DCMAKE_BUILD_TYPE=Release`** — a Debug build exceeds half a gigabyte and runs slower.
- **Compose project name is pinned to `tortoise-wow-v2`.** Compose otherwise derives it from the directory basename, which would point the stack at a new, empty database volume.
- **`tortoise-wow-v2_dbdata` is declared `external`. NEVER run `docker compose down -v`.** That volume is the entire world — 4545 characters and all progression.
- **Ports:** auth `3724`, world `8095` (not the stock 8090), db `127.0.0.1:3309`. `WorldServerPort` in `mangosd.conf`, the published port, and the `tw_logon.realmlist.port` column must all read `8095`.
- **Docker parallelism defaults to `-j2`.** The Docker VM has 4 CPUs and 8 GB RAM; higher parallelism risks the OOM killer mid-compile.
- **All `docker` and `docker compose` commands run inside WSL Ubuntu**, never from Windows PowerShell, so relative bind-mount paths resolve consistently.
- **Crossing into WSL: two separate silent-corruption traps.** Both return plausible wrong output instead of failing, which is what makes them dangerous. Every command block below assumes an interactive prompt:

  ```bash
  wsl -d Ubuntu
  cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow
  ```

  **Trap 1 — shell expansion is eaten.** `wsl -d Ubuntu -- bash -lc '...'` from Git Bash loses `$(...)` *and* plain `$var`. Verified twice: on 2026-08-11 `ls /test | wc -l` returned `0` for a directory holding 11 entries and a password read came back empty with `Access denied`; on 2026-08-12 a `for d in etc data logs` loop reported all three directories `MISSING` when all three existed and were populated — `$d` had expanded to nothing, so it was stat-ing the parent.

  **Trap 2 — Git Bash rewrites Unix-looking paths (MSYS path conversion).** Any absolute `/...` argument gets the Git install root prepended. Verified 2026-08-12: `wsl ... bash /mnt/c/.../up.sh` became `C:/Program Files/Git/mnt/c/.../up.sh`, and `docker run --entrypoint /opt/turtle/bin/mangosd` became `C:/Program Files/Git/opt/turtle/bin/mangosd`. This one is not WSL-specific — it hits any command run from Git Bash, `docker` included.

  **The recipe that works, and makes this automatable (verified 2026-08-12 — used to bring the stack up and to query the world DB):** put the commands in a **script file** and invoke it from **PowerShell**, not Git Bash:

  ```powershell
  wsl -d Ubuntu -- bash /mnt/c/path/to/script.sh
  ```

  PowerShell does no path rewriting, and a script file has no expansion crossing the boundary at all — variables expand inside WSL where they belong. An interactive shell is therefore *not* required. Two riders: `MSYS_NO_PATHCONV=1` fixes Trap 2 for a Git Bash one-liner but does **nothing** for Trap 1; and literal commands with no expansion do survive Git Bash → WSL intact (canary: a file count of 186 matched a native count), which is exactly why a canary without variables gives false confidence.

- **`docker compose` writes progress to stderr.** Called from PowerShell, each line comes back wrapped as a `NativeCommandError` even when the command succeeds with exit 0. Judge success from `docker compose ps` / container status, not from the presence of those records.
- **Mutating `git` operations for this repository run from Windows** (it is a Windows-owned checkout): commits, `reset`, `config`. Read-only queries from WSL are fine and are used deliberately in Tasks 2 and 3 — `git -c safe.directory='*' rev-parse --short HEAD` is verified working against `/mnt/d` (git 2.53.0). Only the *WSL* tree at `/home/deck/tortoise-wow-server-V2/src` is subject to the stricter "git only inside WSL" rule.

## Starting State (verified 2026-08-11)

| Fact | Value |
|---|---|
| Known-good image, rollback anchor | `tortoise-v2:c06b2fb` (= `tortoise-v2:local`, id `9893d93cbecf`) |
| This checkout's C++ vs that image | **identical** — the 3 commits since `c06b2fb` touch only `.gitignore` and two `.md` files |
| Working tree line endings | **CRLF** (`core.autocrlf=true`); the WSL build tree is **LF** |
| Runtime state directory | `/home/deck/tortoise-wow-server-V2/{data,etc,logs}` |
| World DB volume | `tortoise-wow-v2_dbdata` (exists, populated) |
| Stack status | running; started via `docker compose up -d` in the WSL stack dir |
| Docker VM resources | 4 CPUs, 8 GB RAM |
| Bind mounts from `/mnt/d` | **verified working**, read and write — a container saw all 186 files in `sql/base` and could create a file. The `./sql` mount in Task 3 is safe. |

---

### Task 1: Normalize the working tree to LF

The WSL tree that produces the proven image is LF. This checkout is CRLF, so a build from here would compile different bytes. Git stores LF in the object database regardless, so flipping the checkout setting and re-materialising the working tree changes **no committed content** — `git status` stays clean and no commit is produced by this task.

**Files:**
- Modify: `.git/config` (via `git config`, not by hand)
- Test: none — verification is a byte comparison against the WSL tree

**Interfaces:**
- Consumes: nothing
- Produces: an LF working tree, which every later task's `docker build` depends on

- [ ] **Step 1: Confirm the tree is clean before touching it**

`git reset --hard` in step 4 discards tracked modifications. Verify there are none to lose.

Run from Git Bash:
```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
git status --porcelain
```

Expected: only untracked entries, e.g. `?? docs/migration/` and `?? docs/superpowers/plans/2026-08-11-docker-build-from-this-checkout.md`. **If any line starts with `M `, ` M`, `A `, or `D `, stop** and resolve those changes first — another agent may be mid-edit.

- [ ] **Step 2: Write the failing check**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
file src/framework/GameSystem/Grid.h
```

Expected right now: `C++ source, ASCII text, with CRLF line terminators` — this is the failure the task fixes.

- [ ] **Step 3: Switch the checkout to LF**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
git config core.autocrlf false
git config core.eol lf
```

- [ ] **Step 4: Re-materialise the working tree**

Dropping the cached index entries and hard-resetting re-checks-out every tracked file under the new setting. Untracked files (`docs/migration/`, `.worktrees/`) are not touched by `reset --hard`.

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
git rm --cached -r . -q
git reset --hard
```

- [ ] **Step 5: Run the check to verify it passes**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
file src/framework/GameSystem/Grid.h
git status --porcelain
```

Expected: `C++ source, ASCII text` with **no** "CRLF" clause, and `git status --porcelain` showing only the same untracked entries as Step 1. A clean status is the proof that no committed content changed.

- [ ] **Step 6: Verify against the tree that built the proven image**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
diff src/framework/GameSystem/Grid.h \
     '//wsl.localhost/Ubuntu/home/deck/tortoise-wow-server-V2/src/src/framework/GameSystem/Grid.h' \
  && echo "MATCHES the proven build tree"
```

Expected: `MATCHES the proven build tree`.

- [ ] **Step 7: No commit**

This task intentionally produces no commit. `git config` writes to `.git/config`, which is not tracked. Confirm with `git log --oneline -1` that HEAD is unchanged.

---

### Task 2: Add the build definition and produce an image

**Files:**
- Create: `.dockerignore`
- Create: `Dockerfile`
- Test: verification commands in Steps 5–7 (binary presence, linker resolution, playerbots-compiled-in)

**Interfaces:**
- Consumes: the LF working tree from Task 1
- Produces: image `tortoise-v2:candidate`, containing `/opt/turtle/bin/{mangosd,realmd}`, `/opt/turtle/etc/*.conf.dist` (including `aiplayerbot.conf.dist`), and `/opt/turtle/extractors/`. Task 3's compose consumes the image tag; Task 4 retags it to `tortoise-v2:local`.

- [ ] **Step 1: Write the failing check**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
ls Dockerfile
```

Expected: `ls: cannot access 'Dockerfile': No such file or directory` — there is currently no way to build this tree into an image, which is the whole gap.

- [ ] **Step 2: Create `.dockerignore`**

Create `.dockerignore` at the repo root:

```
# The build context is the repo root. Everything listed here is either useless to
# the compiler or actively harmful in a layer.

# 139 MB of history with no business in the build context. Excluding it also keeps
# cmake/revision.h.cmake on its no-git fallback, so the compiled revision string
# does not shift underneath us between builds of the same commit. This is why
# `Release:` in the mangosd log reads 1970-01-01 — expected, not a bug.
.git
.github
.gitignore

# 131 MB of world content. Nothing in the build reads it: cmake/migrations.cmake
# globs sql/migrations/*.sql, that directory does not exist, and no CMakeLists
# includes migrations.cmake at all. The server reads sql/ from a bind mount at
# runtime, not from the image.
sql/base/

# Docs, plans and agent scratch space. Without these, every doc edit invalidates
# the COPY layer and forces a full ~30-minute recompile.
docs/
.claude/
.worktrees/
*.md

# Local build output, if anyone has configured in-tree.
build/

# Secrets. Must never enter a layer.
.env
```

- [ ] **Step 3: Create `Dockerfile`**

Create `Dockerfile` at the repo root:

```dockerfile
# Tortoise-WoW 1.18.1 (build 7272) — build + runtime image, built from this repo.
#
# Debian trixie because INSTALL-LINUX.md pins GCC 14.2 / CMake 3.31 / ACE 8.0.2 /
# Boost 1.83, and trixie is where those line up. ACE must be 7.x or newer: the
# tree is C++17, which removed dynamic exception specifications, and ACE 6.x still
# uses them — its headers bury WorldSocketMgr.cpp in errors.
#
# Two stages: the builder keeps the ~4 GB of objects and the extractor binaries,
# the runtime carries only what mangosd/realmd need.

# ---------------------------------------------------------------- build stage
FROM debian:trixie AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake git \
      libace-dev libboost-all-dev \
      default-libmysqlclient-dev libssl-dev zlib1g-dev libbz2-dev \
 && rm -rf /var/lib/apt/lists/*

# Build context is the repo root, so the repo lands at /src directly.
COPY . /src

# 4 CPUs / 8 GB in the Docker VM. -j2 is what fits: each heavy translation unit
# in this tree peaks around 1-2 GB, and -j4 invites the OOM killer partway
# through a ~30-minute compile. Raise via --build-arg BUILD_JOBS=3 if the VM
# has been given more memory.
ARG BUILD_JOBS=2

# CMAKE_INSTALL_PREFIX is COMPILED IN (SYSCONFDIR is baked at build time), so the
# prefix chosen here is where the binaries look for their configs forever. Moving
# an install after the fact produces "AI Playerbot is Disabled. Unable to open
# configuration file".
#
#   BUILD_PLAYERBOTS    defaults OFF -> no bots, and NO warning.
#   USE_EXTRACTORS      defaults OFF -> no mapextractor/vmapextractor/MoveMapGen.
#   ALLOW_TURTLE_ADDONS defaults ON and must STAY on, or the client crashes with
#                       "interface corrupt" the moment you enter the world.
RUN cmake -B /build -S /src \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/opt/turtle \
      -DBUILD_PLAYERBOTS=ON \
      -DUSE_EXTRACTORS=ON \
      -DALLOW_TURTLE_ADDONS=ON \
 && cmake --build /build -j"${BUILD_JOBS}" \
 && cmake --install /build

# The extractors install next to the server binaries in some trees and stay in
# the build dir in others; collect whichever exist at one predictable path.
RUN mkdir -p /opt/turtle/extractors \
 && for t in mapextractor vmapextractor vmap_assembler MoveMapGen movemapgen; do \
        f=$(find /build /opt/turtle -maxdepth 4 -type f -name "$t" -perm -u+x 2>/dev/null | head -1); \
        [ -n "$f" ] && cp "$f" /opt/turtle/extractors/ || true; \
    done \
 && ls -la /opt/turtle/extractors

# -------------------------------------------------------------- runtime stage
FROM debian:trixie-slim AS runtime

# Exactly what `ldd mangosd` reports in the build stage, not a guess:
#   libACE-8.0.2 libboost_filesystem libboost_thread libcrypto libssl
#   libmariadb libz libzstd (+ libc/libm/libstdc++/libgcc from the base)
# Shipping libboost-system instead of libboost-thread makes mangosd die instantly
# with "error while loading shared libraries: libboost_thread.so.1.83.0", while
# realmd — which links fewer libraries — starts fine and makes it look like a
# config problem.
RUN apt-get update && apt-get install -y --no-install-recommends \
      libace-8.0.2 libboost-filesystem1.83.0 libboost-thread1.83.0 \
      libmariadb3 libssl3 zlib1g libzstd1 libbz2-1.0 \
      ca-certificates tini netcat-openbsd \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/turtle /opt/turtle

# Provenance. These sit in the runtime stage on purpose: an ARG here cannot
# invalidate the ~30-minute compile layer above, so stamping costs nothing.
# .git is excluded from the build context, so the binary cannot learn its own
# commit — it has to arrive as a build arg.
ARG GIT_SHA=unknown
ARG GIT_DIRTY=unknown
ARG DOCKERFILE_SHA=unknown
LABEL org.opencontainers.image.revision="$GIT_SHA"
LABEL com.turtle.source-dirty="$GIT_DIRTY"
LABEL com.turtle.dockerfile-sha256="$DOCKERFILE_SHA"

# Data (dbc/maps/vmaps/mmaps) and the generated configs are bind-mounted in, so
# the image stays reusable across re-extractions and config edits.
WORKDIR /opt/turtle/bin
ENTRYPOINT ["/usr/bin/tini", "-g", "--"]
```

- [ ] **Step 4: Build the image**

Built to `:candidate`, **not** `:local`. `tortoise-v2:local` is the tag the running stack uses; overwriting it before verification would leave no known-good pointer. (`tortoise-v2:c06b2fb` remains the rollback anchor either way.)

At the WSL prompt, in the repo directory. Expect **~30 minutes**.

```bash
SHA=$(git -c safe.directory='*' rev-parse --short HEAD)
DIRTY=$(git -c safe.directory='*' status --porcelain --untracked-files=no | wc -l)
DFSHA=$(sha256sum Dockerfile | cut -c1-12)
echo "building $SHA (dirty=$DIRTY) dockerfile=$DFSHA"

docker build \
  -t tortoise-v2:candidate \
  --build-arg GIT_SHA="$SHA" \
  --build-arg GIT_DIRTY="$DIRTY" \
  --build-arg DOCKERFILE_SHA="$DFSHA" \
  --build-arg BUILD_JOBS=2 \
  .
```

The `echo` is not decoration: if `$SHA` prints empty, the substitution was mangled
and the image would be stamped `unknown`. Stop and fix the shell before building
for half an hour.

Expected final line: `naming to docker.io/library/tortoise-v2:candidate`.

If the build dies with `c++: fatal error: Killed signal terminated program cc1plus`, that is the OOM killer — rerun with `--build-arg BUILD_JOBS=1`.

- [ ] **Step 5: Verify the binaries exist and their libraries resolve**

The `libboost_thread` class of mistake shows up here, not at compile time.

```bash
docker run --rm tortoise-v2:candidate ls /opt/turtle/bin
echo "--- unresolved libraries (must print none twice) ---"
docker run --rm tortoise-v2:candidate sh -c 'ldd /opt/turtle/bin/mangosd | grep "not found" || echo none'
docker run --rm tortoise-v2:candidate sh -c 'ldd /opt/turtle/bin/realmd  | grep "not found" || echo none'
```

Expected: `ls` lists `mangosd` and `realmd`; both `ldd` checks print `none`.

- [ ] **Step 6: Verify playerbots were actually compiled in**

This is the silent failure the constraints warn about — a bot-free build produces no warning at all. The PlayerBots module installs its own config template, so its presence is the proof.

```bash
docker run --rm tortoise-v2:candidate ls /opt/turtle/etc | grep -i aiplayerbot \
  && echo "PLAYERBOTS COMPILED IN" \
  || echo "FAIL: built without -DBUILD_PLAYERBOTS=ON"
```

Expected: `aiplayerbot.conf.dist` listed, then `PLAYERBOTS COMPILED IN`.

- [ ] **Step 7: Verify the extractors were collected**

```bash
docker run --rm tortoise-v2:candidate ls /opt/turtle/extractors
```

Expected: a non-empty listing including `mapextractor` and `vmapextractor`.

- [ ] **Step 8: Commit**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
git add Dockerfile .dockerignore
git commit -m "Add a repo-root Docker build so the server image can be built from this checkout"
```

---

### Task 3: Add the compose stack and launch from this directory

**Files:**
- Create: `docker-compose.yml`
- Create: `.env.example`
- Modify: `.gitignore` (append `.env`)
- Test: verification in Steps 6–8 (containers healthy, realm row correct, bots online)

**Interfaces:**
- Consumes: `tortoise-v2:candidate` from Task 2, via the `TW_IMAGE` variable
- Produces: a launchable stack in this directory; Task 4 documents it

- [ ] **Step 1: Write the failing check**

```bash
docker compose config --services
```

Expected now: `no configuration file provided: not found`.

- [ ] **Step 2: Create `docker-compose.yml`**

Create `docker-compose.yml` at the repo root:

```yaml
# Tortoise-WoW 1.18.1 — launched from the source checkout.
#
# Runtime state (extracted client data, tuned configs, logs, and the world
# database volume) is NOT duplicated here. It stays in the WSL stack directory
# and is bind-mounted in through the paths in .env, so this file can be run from
# the repo without copying several gigabytes of mmaps or re-importing the world.

# PINNED. Compose otherwise derives the project name from the directory basename,
# which would point the stack at a new, empty database volume.
name: tortoise-wow-v2

services:
  db:
    # 10.6, NOT 11.8. sql/create_databases.sql is a MariaDB 10.6 dump using
    # utf8mb4_general_ci; 11.8 defaults new databases to utf8mb4_uca1400_ai_ci and
    # joins between the two raise "Illegal mix of collations". Matching the dump's
    # own version avoids the problem rather than repairing it afterwards.
    image: mariadb:10.6
    container_name: tw2-db
    restart: "no"
    environment:
      MARIADB_ROOT_PASSWORD: "${DB_PASS:?set DB_PASS in .env}"
      MARIADB_USER: mangos
      MARIADB_PASSWORD: "${DB_PASS}"
      MARIADB_DATABASE: tw_world
    volumes:
      - dbdata:/var/lib/mysql
    ports:
      # Host-side access only, for mysql/Navicat; the servers reach it over the
      # compose network as "db".
      - "127.0.0.1:3309:3306"
    command:
      - --sql-mode=NO_ENGINE_SUBSTITUTION
      - --max_allowed_packet=256M      # sql/base has single files over 100 MB
      - --innodb-buffer-pool-size=512M
    healthcheck:
      test: ["CMD", "healthcheck.sh", "--connect", "--innodb_initialized"]
      interval: 5s
      timeout: 5s
      retries: 60
    networks: [tw2-net]

  realmd:
    image: "${TW_IMAGE:-tortoise-v2:local}"
    container_name: tw2-realmd
    restart: "no"
    depends_on:
      db: { condition: service_healthy }
    ports:
      - "3724:3724"
    volumes:
      - "${TW_ETC:?set TW_ETC in .env}:/opt/turtle/etc"
      - "${TW_LOGS:?set TW_LOGS in .env}:/opt/turtle/logs"
    working_dir: /opt/turtle/bin
    command: ["./realmd", "-c", "/opt/turtle/etc/realmd.conf"]
    networks: [tw2-net]

  mangosd:
    image: "${TW_IMAGE:-tortoise-v2:local}"
    container_name: tw2-mangosd
    restart: "no"
    depends_on:
      db: { condition: service_healthy }
    ports:
      # 8095, NOT the stock 8090. MUST match WorldServerPort in mangosd.conf AND
      # the tw_logon.realmlist row, or the client logs in and then hangs before
      # character selection: it was handed a port nobody is listening on.
      - "8095:8095"
    volumes:
      - "${TW_ETC:?set TW_ETC in .env}:/opt/turtle/etc"
      - "${TW_DATA:?set TW_DATA in .env}:/opt/turtle/data"
      # NOT :ro — the DB auto-updater creates sql/unused/ on boot and a read-only
      # mount crashes mangosd with "cannot create directory ... Read-only".
      - ./sql:/opt/turtle/sql
      - "${TW_LOGS:?set TW_LOGS in .env}:/opt/turtle/logs"
    working_dir: /opt/turtle/bin
    command: ["./mangosd", "-c", "/opt/turtle/etc/mangosd.conf"]
    # mangosd needs a console on stdin or it exits immediately with no error.
    stdin_open: true
    tty: true
    networks: [tw2-net]

volumes:
  # EXTERNAL on purpose. This volume is the entire world — every character and all
  # progression. Declaring it external means compose will never create a blank one
  # by accident, and it is why `docker compose down -v` must never be run here.
  dbdata:
    external: true
    name: tortoise-wow-v2_dbdata

networks:
  tw2-net:
```

- [ ] **Step 3: Create `.env.example`**

Create `.env.example` at the repo root:

```bash
# Copy to .env and fill in DB_PASS. .env is gitignored — never commit the password.
#
#   cp .env.example .env
#   # then paste the password from /home/deck/tortoise-wow-server-V2/.dbpass
DB_PASS=

# Runtime state. These point at the existing stack directory so the world, the
# extracted client data (dbc/maps/vmaps/mmaps, several GB) and the tuned configs
# are reused rather than duplicated. Paths are as seen from inside WSL.
TW_ETC=/home/deck/tortoise-wow-server-V2/etc
TW_DATA=/home/deck/tortoise-wow-server-V2/data
TW_LOGS=/home/deck/tortoise-wow-server-V2/logs

# Image the stack runs. Set to tortoise-v2:candidate to test a fresh build before
# promoting it to :local.
TW_IMAGE=tortoise-v2:local
```

- [ ] **Step 4: Ignore `.env`**

Append to `.gitignore`:

```
# Local Docker environment — contains the database password.
.env
```

- [ ] **Step 5: Create the real `.env` and point it at the candidate image**

```bash
cp .env.example .env
PASS=$(tr -d '\r\n' < /home/deck/tortoise-wow-server-V2/.dbpass)
echo "password length: ${#PASS}"
sed -i "s|^DB_PASS=.*|DB_PASS=${PASS}|" .env
sed -i "s|^TW_IMAGE=.*|TW_IMAGE=tortoise-v2:candidate|" .env
grep -c . .env
```

Expected: `password length: 19`, then a line count. **If the length is 0 the
substitution was mangled** — you are not in a real WSL shell; go back and open one.
Do not print `.env` itself, it holds the password.

- [ ] **Step 6: Run the check to verify compose now resolves**

```bash
docker compose config --services
```

Expected: `db`, `mangosd`, `realmd` — the failure from Step 1 is gone.

- [ ] **Step 7: Stop the currently running stack, then launch from here**

Both compose files use project name `tortoise-wow-v2`, so this replaces the running stack rather than starting a second one. **`down` without `-v`.**

```bash
(cd /home/deck/tortoise-wow-server-V2 && docker compose down)
docker compose up -d
```

Expected: `Container tw2-db Healthy`, then `tw2-realmd Started` and `tw2-mangosd Started`.

- [ ] **Step 8: Verify the world actually serves**

A bound port only proves `docker-proxy` answered. Check the population instead. Allow ~2 minutes after `up -d` for the world load and bot login.

```bash
P=$(tr -d '\r\n' < /home/deck/tortoise-wow-server-V2/.dbpass)
docker ps --format '{{.Names}} | {{.Status}}'
docker exec -i tw2-db mysql -uroot --password="$P" -N -e \
  "SELECT CONCAT(name,'  port=',port,'  realmflags=',realmflags) FROM tw_logon.realmlist;
   SELECT CONCAT('characters online: ',COUNT(*)) FROM tw_char.characters WHERE online=1;" 2>/dev/null
```

Expected: three containers `Up` with `tw2-db` `(healthy)`; realm row reading `port=8095  realmflags=0`; and `characters online:` climbing above 0 as bots log in.

If `characters online: 0` persists past five minutes, check `docker logs tw2-mangosd | tail -40` before assuming failure — bot login is gradual.

- [ ] **Step 9: Promote the candidate image**

Only once Step 8 passes. This makes the freshly built image the default the stack runs.

```bash
SHA=$(git -c safe.directory='*' rev-parse --short HEAD)
docker tag tortoise-v2:candidate tortoise-v2:local
docker tag tortoise-v2:candidate "tortoise-v2:${SHA}"
sed -i "s|^TW_IMAGE=.*|TW_IMAGE=tortoise-v2:local|" .env
docker images --filter reference=tortoise-v2 --format '{{.Tag}}'
```

Expected: tag list including `local`, `candidate`, the new short SHA, and the preserved `c06b2fb` rollback anchor.

- [ ] **Step 10: Commit**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
git add docker-compose.yml .env.example .gitignore
git commit -m "Add a compose stack so the server can be launched from this checkout"
```

---

### Task 4: Write the runbook

Without this, the next session repeats the discovery work that produced this plan — including the ~30 minutes spent finding out the image already existed.

**Files:**
- Create: `docs/DOCKER.md`
- Test: none — the verification is that a reader can start, verify and roll back without reading this plan

**Interfaces:**
- Consumes: the files created in Tasks 2 and 3
- Produces: nothing consumed by other tasks

- [ ] **Step 1: Create `docs/DOCKER.md`**

````markdown
# Building and running the server in Docker

Everything below runs **inside WSL Ubuntu**. Docker Desktop's engine is shared,
but relative bind-mount paths only resolve correctly from the WSL side.

    cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow

## First-time setup

```bash
cp .env.example .env
# paste the password from /home/deck/tortoise-wow-server-V2/.dbpass into DB_PASS
```

`.env` also points `TW_DATA`, `TW_ETC` and `TW_LOGS` at the existing stack
directory. That is deliberate: the extracted client data is several gigabytes and
the configs are tuned, so both are reused rather than duplicated.

## Start / stop

```bash
docker compose up -d
docker compose down          # NEVER -v — see below
```

## Rebuild after a C++ change

Roughly 30 minutes. Build to `:candidate` first so `:local` keeps pointing at a
server that works.

```bash
SHA=$(git -c safe.directory='*' rev-parse --short HEAD)
docker build -t tortoise-v2:candidate \
  --build-arg GIT_SHA="$SHA" \
  --build-arg DOCKERFILE_SHA="$(sha256sum Dockerfile | cut -c1-12)" \
  --build-arg BUILD_JOBS=2 .

# verify, then promote
docker tag tortoise-v2:candidate tortoise-v2:local
docker tag tortoise-v2:candidate "tortoise-v2:$SHA"
docker compose up -d
```

## Verify it actually works

A bound port only proves `docker-proxy` answered. Check the population:

```bash
P=$(tr -d '\r\n' < /home/deck/tortoise-wow-server-V2/.dbpass)
docker exec -i tw2-db mysql -uroot --password="$P" -N -e \
  "SELECT CONCAT(name,'  port=',port,'  realmflags=',realmflags) FROM tw_logon.realmlist;
   SELECT CONCAT('characters online: ',COUNT(*)) FROM tw_char.characters WHERE online=1;"
```

`port=8095  realmflags=0` and a rising online count mean the realm is reachable.
`realmflags=2` means offline; `port` disagreeing with `WorldServerPort` in
`mangosd.conf` makes the client hang after login, before character select.

## Rollback

Every build is tagged with its commit, so the previous server is still on disk:

```bash
docker images --filter reference=tortoise-v2
docker tag tortoise-v2:c06b2fb tortoise-v2:local
docker compose up -d
```

## Things that will cost you an afternoon

| | |
|---|---|
| **`docker compose down -v`** | Destroys `tortoise-wow-v2_dbdata` — every character and all progression. The volume is declared `external` so compose cannot recreate it silently, but `-v` still removes it. Never run it. |
| Line endings | This checkout must stay LF (`git config core.autocrlf false`). A CRLF tree compiles, but produces different bytes than the tree the proven image came from. |
| `BUILD_PLAYERBOTS` | Defaults `OFF`. A build without it yields a bot-free server with no warning. Check: `docker run --rm tortoise-v2:local ls /opt/turtle/etc \| grep aiplayerbot`. |
| `CMAKE_INSTALL_PREFIX` | Compiled in. It must stay `/opt/turtle` or the server logs one line about `aiplayerbot.conf` and runs with no bots. |
| Ports 3724 / 8095 | Shared with the older V1 stack. They cannot run together. |
| `Release: 1970-01-01` in the log | Expected. `.git` is excluded from the build context, so the revision falls back; the real commit is on the image's `org.opencontainers.image.revision` label. |

## Where the source of truth is

This checkout is **not** the only tree of this repo on the machine. A second,
diverged checkout lives at `/home/deck/tortoise-wow-server-V2/src` and shares
ancestor `c06b2fb`. Before building, confirm which tree you mean to ship —
building the wrong one silently produces a server without the change you made.
````

- [ ] **Step 2: Verify the runbook's commands are real**

Read `docs/DOCKER.md` back and confirm every path, tag and flag matches what Tasks 2 and 3 actually created — in particular that `TW_IMAGE`, `TW_DATA`, `TW_ETC`, `TW_LOGS` are spelled identically to `.env.example`, and that `tortoise-v2:c06b2fb` still appears in `docker images --filter reference=tortoise-v2`.

- [ ] **Step 3: Commit**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
git add docs/DOCKER.md
git commit -m "Document building and running the server in Docker"
```

---

### Task 5: Add a rebuild script

Added after the plan was written, at the user's request. Deliberately **build-only**:
it never stops, starts or restarts the stack. A fuller stop → build → up → verify
cycle already exists as `D:\TurtleWow\scripts\ship-cpp-fix.sh`, and duplicating it
here would collide with the deployment-layer migration in flight in the other tree.

The one idea worth borrowing from that script is stated but not implemented here,
because this script never restarts anything: a missing named volume is silently
recreated **empty** by `docker compose up`, so an existence check taken after a
restart passes straight over a wipe. Any future script that does restart the stack
must fingerprint the volume before and compare after — not merely check it exists.

**Files:**
- Create: `scripts/rebuild.sh`
- Modify: `docs/DOCKER.md` (replace the hand-pasted rebuild commands with the script)
- Test: Steps 4–5 (refuses to promote on a failed check; promotes on a clean build)

**Interfaces:**
- Consumes: `Dockerfile` from Task 2; the `tortoise-v2:candidate` → `:local` promotion flow from Task 3 Step 9; `docs/DOCKER.md` from Task 4
- Produces: `scripts/rebuild.sh`, taking `BUILD_JOBS` from the environment (default `2`)

- [ ] **Step 1: Write the failing check**

```bash
ls scripts/rebuild.sh
```

Expected: `No such file or directory` — rebuilding currently means pasting four
separate commands and remembering not to promote a broken image.

- [ ] **Step 2: Create `scripts/rebuild.sh`**

```bash
#!/usr/bin/env bash
# Rebuild the server image from this checkout, verify it, then promote it.
#
# Run from WSL:   ./scripts/rebuild.sh
#                 BUILD_JOBS=1 ./scripts/rebuild.sh     # if the VM OOMs
#
# Builds to tortoise-v2:candidate, runs three acceptance checks, and moves the
# :local tag ONLY if all three pass. A failed check leaves :local pointing at
# whatever was working before, so a bad build cannot take the server with it.
#
# This script deliberately does NOT stop, start or restart the stack. Apply a new
# image with `docker compose up -d` when you are ready for the downtime.
# NEVER `docker compose down -v` — that volume is the entire world.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

JOBS="${BUILD_JOBS:-2}"

command -v docker >/dev/null 2>&1 \
  || { echo "FATAL: docker not on PATH. Run this from WSL, not Windows." >&2; exit 1; }
docker info >/dev/null 2>&1 \
  || { echo "FATAL: docker is not responding. Is Docker Desktop running?" >&2; exit 1; }

# .git is excluded from the build context, so the binary cannot learn its own
# commit — it has to arrive as a build arg. An empty SHA stamps the image
# "unknown", which is worth catching now rather than after a ~30 minute compile.
SHA=$(git -c safe.directory='*' rev-parse --short HEAD)
DIRTY=$(git -c safe.directory='*' status --porcelain --untracked-files=no | wc -l)
DFSHA=$(sha256sum Dockerfile | cut -c1-12)
[ -n "$SHA" ] || { echo "FATAL: could not read the commit SHA; refusing to build." >&2; exit 1; }

echo "==> building $SHA (dirty=$DIRTY) dockerfile=$DFSHA jobs=$JOBS"
docker build \
  -t tortoise-v2:candidate \
  --build-arg GIT_SHA="$SHA" \
  --build-arg GIT_DIRTY="$DIRTY" \
  --build-arg DOCKERFILE_SHA="$DFSHA" \
  --build-arg BUILD_JOBS="$JOBS" \
  .

echo "==> verifying tortoise-v2:candidate"
fail=0

# Missing runtime libraries do not surface at compile time. Shipping
# libboost-system instead of libboost-thread once killed mangosd instantly while
# realmd started fine, which read as a config problem rather than a link problem.
#
# Two distinct failures need two distinct checks. `ldd` on a binary that is not
# there writes "No such file or directory" to STDERR, so a grep reading only
# stdout matches nothing, `|| true` swallows the exit code, and the binary reads
# as healthy — which is exactly how a build that silently produced no mangosd
# would sail through and get promoted onto the tag a live server runs from.
# Verified empirically: deleting only /opt/turtle/bin/mangosd from a known-good
# image produced "ok: mangosd links cleanly". So test existence first, and fold
# stderr into the grep so neither failure can pass quietly.
for b in mangosd realmd; do
  if ! docker run --rm tortoise-v2:candidate test -x "/opt/turtle/bin/$b"; then
    echo "  FAIL: $b is missing from the image"; fail=1; continue
  fi
  missing=$(docker run --rm tortoise-v2:candidate \
              sh -c "ldd /opt/turtle/bin/$b 2>&1 | grep 'not found' || true")
  if [ -n "$missing" ]; then
    echo "  FAIL: $b has unresolved libraries:"; echo "$missing"; fail=1
  else
    echo "  ok: $b exists and links cleanly"
  fi
done

# BUILD_PLAYERBOTS defaults OFF, and a bot-free build warns about nothing at all.
# The module installs its own config template, so its presence is the proof.
if docker run --rm tortoise-v2:candidate ls /opt/turtle/etc | grep -qi aiplayerbot; then
  echo "  ok: playerbots compiled in"
else
  echo "  FAIL: built without -DBUILD_PLAYERBOTS=ON"; fail=1
fi

if [ -n "$(docker run --rm tortoise-v2:candidate ls /opt/turtle/extractors)" ]; then
  echo "  ok: extractors present"
else
  echo "  FAIL: no extractors collected"; fail=1
fi

if [ "$fail" -ne 0 ]; then
  echo "==> verification FAILED. tortoise-v2:local left untouched." >&2
  exit 1
fi

docker tag tortoise-v2:candidate tortoise-v2:local
docker tag tortoise-v2:candidate "tortoise-v2:$SHA"
echo "==> promoted to tortoise-v2:local and tortoise-v2:$SHA"
echo "    apply it when ready:  docker compose up -d"
```

- [ ] **Step 3: Make it executable and commit the mode**

```bash
chmod +x scripts/rebuild.sh
git -c safe.directory='*' update-index --chmod=+x scripts/rebuild.sh 2>/dev/null || true
```

- [ ] **Step 4: Verify it refuses to promote a failing image**

This is the script's whole reason to exist, so test the failure path, not just the
happy one. Tag a deliberately wrong image as the candidate — `mariadb:10.6` has no
`/opt/turtle` at all — and confirm `:local` does not move.

```bash
BEFORE=$(docker images --no-trunc --format '{{.ID}}' tortoise-v2:local)
docker tag mariadb:10.6 tortoise-v2:candidate

# Run only the verify-and-promote half; skipping the build keeps this test cheap.
sed -n '/^echo "==> verifying/,$p' scripts/rebuild.sh > /tmp/verify-only.sh
bash /tmp/verify-only.sh; echo "exit=$?"

AFTER=$(docker images --no-trunc --format '{{.ID}}' tortoise-v2:local)
[ "$BEFORE" = "$AFTER" ] && echo "PASS: :local did not move" || echo "FAIL: :local moved"
```

Expected: `FAIL:` lines for both binaries and for playerbots, a non-zero `exit=`,
and `PASS: :local did not move`.

- [ ] **Step 4b: Regression-test the missing-binary case specifically**

The `mariadb` stand-in above is missing *everything*, so it cannot prove the binary
checks work — playerbots and extractors would fail it regardless. Test the case that
actually slipped through: an otherwise-correct image with one binary deleted.

```bash
docker build -t scratch-verify:nomangosd - <<'EOF'
FROM tortoise-v2:local
RUN rm -f /opt/turtle/bin/mangosd
EOF

docker tag scratch-verify:nomangosd tortoise-v2:candidate
bash /tmp/verify-only.sh; echo "exit=$?"

docker rmi scratch-verify:nomangosd
```

Expected: `FAIL: mangosd is missing from the image`, `ok: realmd exists and links
cleanly`, and a non-zero `exit=`. Before the fix this printed `ok: mangosd links
cleanly` and exited 0 — that is the regression this step exists to catch.

- [ ] **Step 5: Restore the real candidate image**

The test above overwrote the `:candidate` tag. Point it back at the real build.

```bash
SHA=$(git -c safe.directory='*' rev-parse --short HEAD)
docker tag "tortoise-v2:${SHA}" tortoise-v2:candidate
docker images --filter reference=tortoise-v2 --format '{{.Tag}} {{.ID}}'
```

Expected: `candidate`, `local` and the short SHA all showing the same image id.

- [ ] **Step 6: Point the runbook at the script**

In `docs/DOCKER.md`, replace the body of the "Rebuild after a C++ change" section
with:

````markdown
Roughly 30 minutes. `scripts/rebuild.sh` builds to `tortoise-v2:candidate`, runs
the three acceptance checks, and moves the `:local` tag only if all three pass —
so a broken build cannot take the running server down with it.

```bash
./scripts/rebuild.sh
BUILD_JOBS=1 ./scripts/rebuild.sh    # if the Docker VM OOMs mid-compile
```

It does not restart anything. Apply the new image when you are ready:

```bash
docker compose up -d
```

For the fuller stop → build → up → verify → push cycle, including a world-volume
fingerprint taken before the restart and compared after, see
`D:\TurtleWow\scripts\ship-cpp-fix.sh`.
````

- [ ] **Step 7: Commit**

```bash
cd /d/CodingProjects/tortoise-wow/tortoise-wow
git add scripts/rebuild.sh docs/DOCKER.md
git commit -m "Add a rebuild script that only promotes an image once it verifies"
```

---

## Open question for the user, not for the implementer

This plan deliberately does **not** reconcile this checkout with the diverged WSL
tree at `/home/deck/tortoise-wow-server-V2/src` (shared ancestor `c06b2fb`; that
tree is ahead by ten `turtle-ops` commits which include a `turtle-ops/deploy/`
copy of the same Dockerfile and compose file, plus ~30 uncommitted modifications).

Reconciling them is a separate decision with a live collision risk, because
another agent is working in that tree right now. Until it happens, two trees of
this repo can each produce an image, and only the one you build from carries your
change. The runbook's closing section says so explicitly.
