# Docker Build Resource Increase Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut the ~40 minute C++ rebuild (`scripts/rebuild.sh`) down by raising the WSL2 VM's CPU/memory ceiling and the build's parallelism to match, so automated backlog-drain / batch-implement cycles spend less wall-clock time blocked on a single compile.

**Architecture:** Docker Desktop on this machine runs on the WSL2 backend, so there is no such thing as a per-build "Docker profile" for CPU/memory — the actual ceiling is the WSL2 VM's own resource allocation, set in `C:\Users\mihov\.wslconfig` and applied on VM restart (`wsl --shutdown`). `Dockerfile:23-26` already documents this relationship: `-j2` is sized to fit inside a `4 CPU / 8GB` VM, and its own comment says to raise `BUILD_JOBS` once the VM has more memory. This plan raises the VM to 16 CPU / 24GB (out of the host's 24 CPU / ~32GB), then raises `BUILD_JOBS`'s default in `scripts/rebuild.sh` from 2 to 10 (bounded by the ~2GB-per-translation-unit ceiling the Dockerfile already documents, not by the 16 CPUs available), and verifies with a real timed rebuild.

**Tech Stack:** Docker Desktop (WSL2 backend), WSL2 (`.wslconfig`), bash (`scripts/rebuild.sh`), CMake/g++ build inside the Debian trixie build stage.

**Spec:** None — this is a direct infrastructure change scoped from a live conversation, not a written spec. The user chose "Aggressive — 16 CPU / 24GB" (leaving 8 CPU / 8GB for the Windows host) from `C:\Users\mihov\.wslconfig`'s current `4 CPU / 8GB`, given a 24 CPU / ~32GB host.

## Global Constraints

- Host has 24 logical CPUs and 31,848 MB (~31GB) physical RAM (confirmed via `wmic ComputerSystem get TotalPhysicalMemory`).
- Current `.wslconfig` (`C:\Users\mihov\.wslconfig`): `memory=8GB`, `processors=4`.
- Target `.wslconfig`: `memory=24GB`, `processors=16` — leaves 8 CPU / 8GB for Windows host use during a background build.
- Each heavy translation unit in this tree peaks around 1-2GB (`Dockerfile:23-26`, empirically observed). `BUILD_JOBS=10` at worst case (10 × 2GB = 20GB) leaves ~4GB headroom inside the 24GB VM for the OS, Docker daemon, and lighter TUs compiling concurrently — do not raise `BUILD_JOBS` past what memory allows just because 16 CPUs are available; CPU is not the binding constraint here, memory is.
- `scripts/rebuild.sh` must be run from WSL, never Git Bash (`scripts/rebuild.sh:27-28` already enforces this and must keep enforcing it).
- Never touch `docker-compose.yml`'s `dbdata` volume or run `docker compose down -v` — out of scope for this change, but any verification step that touches the running stack must respect it.
- This plan changes host-level (`.wslconfig`) and repo-level (`scripts/rebuild.sh`, `Dockerfile` comment, `docs/DOCKER.md`) state; it does not touch application code, so no unit tests apply. "Tests" here are verification commands (`nproc`, `free -h`, a timed rebuild) run in WSL.

---

### Task 1: Raise the WSL2 VM's CPU/memory ceiling

**Files:**
- Modify: `C:\Users\mihov\.wslconfig` (outside the repo — host-level config)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: a WSL2 VM with 16 processors / 24GB memory available, which Task 2 and Task 3 depend on before raising `BUILD_JOBS` or timing a rebuild.

- [ ] **Step 1: Read the current `.wslconfig`**

```bash
cat /c/Users/mihov/.wslconfig
```

Expected output (current state):
```
[wsl2]
memory=8GB
processors=4
```

- [ ] **Step 2: Edit `.wslconfig` to the new ceiling**

Replace the file contents with:

```
[wsl2]
memory=24GB
processors=16
```

- [ ] **Step 3: Shut down WSL so the new limits take effect**

WSL2 only reads `.wslconfig` on VM start, not live. From PowerShell or the Windows side (not from inside WSL, since this kills the WSL instance you'd be running it from):

```
wsl --shutdown
```

This also stops Docker Desktop's backend VM. Docker Desktop will restart it automatically on next use, or you can reopen Docker Desktop manually.

- [ ] **Step 4: Verify the new allocation from inside WSL**

Reopen a WSL Ubuntu terminal (this restarts the VM under the new `.wslconfig`), then:

```bash
nproc
free -h
```

Expected: `nproc` reports `16`, `free -h` shows a total near `24Gi` (WSL2 typically reports slightly under the configured value due to kernel reservation — anything in the 23-24Gi range confirms the change took effect).

- [ ] **Step 5: Verify Docker Desktop picked up the new VM**

```bash
docker info | grep -i -E "cpus|total memory"
```

Expected: `CPUs: 16` and a memory figure near 24GB (Docker reports the VM's resources, not the host's).

No commit for this step — `.wslconfig` lives outside the repo, at `C:\Users\mihov\.wslconfig`.

---

### Task 2: Raise `BUILD_JOBS`'s default and update the sizing comments

**Files:**
- Modify: `scripts/rebuild.sh:19`
- Modify: `Dockerfile:23-26`

**Interfaces:**
- Consumes: the 16 CPU / 24GB VM from Task 1 (this task's new default assumes that ceiling; it would OOM under the old 4 CPU / 8GB VM).
- Produces: a `scripts/rebuild.sh` whose default build now uses 10-way parallelism, and a `Dockerfile` comment that documents *why* 10 rather than 16 — this is what Task 3's timed rebuild measures, and what a future reader tunes from if the VM's resources change again.

- [ ] **Step 1: Update the default in `scripts/rebuild.sh`**

Current (`scripts/rebuild.sh:19`):
```bash
JOBS="${BUILD_JOBS:-2}"
```

New:
```bash
JOBS="${BUILD_JOBS:-10}"
```

- [ ] **Step 2: Update the usage comment at the top of `scripts/rebuild.sh`**

Current (`scripts/rebuild.sh:4-5`):
```bash
# Run from WSL:   ./scripts/rebuild.sh
#                 BUILD_JOBS=1 ./scripts/rebuild.sh     # if the VM OOMs
```

New:
```bash
# Run from WSL:   ./scripts/rebuild.sh
#                 BUILD_JOBS=4 ./scripts/rebuild.sh     # if the VM OOMs
```

(`BUILD_JOBS=1` is needlessly conservative as a documented fallback now that the default is 10; `4` is a more useful first thing to try before dropping all the way to serial.)

- [ ] **Step 3: Update the sizing comment in the `Dockerfile`**

Current (`Dockerfile:23-26`):
```dockerfile
# 4 CPUs / 8 GB in the Docker VM. -j2 is what fits: each heavy translation unit
# in this tree peaks around 1-2 GB, and -j4 invites the OOM killer partway
# through a ~30-minute compile. Raise via --build-arg BUILD_JOBS=3 if the VM
# has been given more memory.
ARG BUILD_JOBS=2
```

New:
```dockerfile
# 16 CPUs / 24 GB in the Docker VM (see docs/DOCKER.md and C:\Users\<user>\.wslconfig
# on Windows hosts running the WSL2 backend). -j10 is what fits: each heavy
# translation unit in this tree peaks around 1-2 GB, so 10 concurrent jobs can
# reach ~20 GB worst case, leaving headroom for the OS and Docker daemon inside
# the VM. CPU is not the binding constraint here — memory is. Lower via
# --build-arg BUILD_JOBS=N (or the scripts/rebuild.sh BUILD_JOBS env var) if the
# VM's memory allocation is ever reduced, or if the VM OOMs mid-compile.
ARG BUILD_JOBS=10
```

- [ ] **Step 4: Commit**

```bash
git add scripts/rebuild.sh Dockerfile
git commit -m "build: raise default BUILD_JOBS to 10 to match a 16 CPU/24GB WSL2 VM"
```

---

### Task 3: Verify with a timed rebuild

**Files:**
- None modified — this task only runs verification commands.

**Interfaces:**
- Consumes: the 16 CPU / 24GB VM from Task 1 and the `BUILD_JOBS=10` default from Task 2.
- Produces: a measured before/after wall-clock number that Task 4 records in `docs/DOCKER.md`.

- [ ] **Step 1: Run a full timed rebuild from WSL**

```bash
cd /mnt/c/Coding/tortoise-wow/tortoise-wow
time ./scripts/rebuild.sh
```

Expected: the script builds `tortoise-cm:candidate`, runs its five acceptance checks (mangosd/realmd exist, link cleanly, execute; playerbots compiled in; extractors present), and on success promotes `:local` and prints `==> promoted to tortoise-cm:local`. Capture the `real` time reported by `time`.

- [ ] **Step 2: Confirm no OOM occurred mid-build**

If the build fails partway with a killed compiler process (not one of the script's own `FAIL:` lines), that is the OOM killer, not a real compile error. Check:

```bash
dmesg | grep -i "killed process" | tail -5
```

Expected: no output. If the OOM killer fired, drop `BUILD_JOBS` to 8 in `scripts/rebuild.sh:19`, re-run, and use that as the new default instead of 10.

- [ ] **Step 3: Confirm the acceptance checks passed and record the time**

Expected script output includes, for each of `mangosd`/`realmd`:
```
  ok: mangosd exists and links cleanly
  ok: realmd exists and links cleanly
```
plus `ok: playerbots compiled in`, `ok: mapextractor present`, `ok: vmapextractor present`, and finally `==> promoted to tortoise-cm:local`.

Note the `real` time from Step 1 — this is the number Task 4 records in `docs/DOCKER.md` against the previous "~40 minutes" baseline.

No commit for this task — it only runs and observes, it does not change tracked files.

---

### Task 4: Document the new baseline in `docs/DOCKER.md`

**Files:**
- Modify: `docs/DOCKER.md:45-53` (the "Rebuild after a C++ change" section)

**Interfaces:**
- Consumes: the measured rebuild time from Task 3, Step 3.
- Produces: nothing consumed by a later task — this is the last task in the plan.

- [ ] **Step 1: Update the rebuild section**

Current (`docs/DOCKER.md:45-53`):
```markdown
## Rebuild after a C++ change

Roughly 40 minutes. `scripts/rebuild.sh` builds to `tortoise-cm:candidate`, runs
its acceptance checks, and moves the `:local` tag ONLY if every one passes — so a
broken build cannot take the running server down with it.

```bash
./scripts/rebuild.sh
BUILD_JOBS=1 ./scripts/rebuild.sh    # if the Docker VM OOMs mid-compile
```
```

New (replace `<measured minutes>` with the Task 3 result before committing):
```markdown
## Rebuild after a C++ change

Roughly <measured minutes> minutes with the WSL2 VM at 16 CPU / 24GB
(`C:\Users\mihov\.wslconfig`) and `BUILD_JOBS=10` (the `Dockerfile` default —
see `Dockerfile:23-33`). Previously ~40 minutes at the VM's original 4 CPU / 8GB
allocation with `BUILD_JOBS=2`. `scripts/rebuild.sh` builds to
`tortoise-cm:candidate`, runs its acceptance checks, and moves the `:local` tag
ONLY if every one passes — so a broken build cannot take the running server
down with it.

```bash
./scripts/rebuild.sh
BUILD_JOBS=4 ./scripts/rebuild.sh    # if the Docker VM OOMs mid-compile
```

If the VM's own resource ceiling ever needs raising again — this is what
actually controls build parallelism, not any per-container Docker setting —
edit `C:\Users\mihov\.wslconfig`, then `wsl --shutdown` from PowerShell (not
from inside WSL) to apply it, and reopen a WSL terminal to pick up the new
limits. `docker info | grep -i -E "cpus|total memory"` confirms what Docker
Desktop is actually running with.
```

- [ ] **Step 2: Commit**

```bash
git add docs/DOCKER.md
git commit -m "docs(DOCKER.md): record new rebuild baseline after WSL2 VM resource increase"
```
