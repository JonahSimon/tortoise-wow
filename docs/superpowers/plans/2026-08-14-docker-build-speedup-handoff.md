# Handoff: cutting Docker build time by raising the engine's own resource ceiling

Written 2026-08-14, after applying this to `tortoise-wow`. Portable — written for
an agent picking this up in a **different** repo with no memory of this one.
Read the whole thing before touching anything; the propagation step (§4) is
where the actual win lives, and it's easy to stop one step short of it.

## What happened here, in one paragraph

`tortoise-wow`'s C++ rebuild (`scripts/rebuild.sh` → `docker build`) took ~40
minutes. The Dockerfile's own comments already explained why: `-j2`, sized to
fit inside a Docker Desktop WSL2 VM capped at 4 CPUs/8GB in `.wslconfig`, on a
host with 24 CPUs/32GB available. Raising `.wslconfig` to 16 CPUs/24GB and the
build's `BUILD_JOBS` default to 10 (bounded by ~2GB per translation unit, not
by CPU count) cut the same build to **9m20s** — a 4.3x improvement — verified
with a real timed rebuild, not estimated. The harder part wasn't the resource
bump; it was finding that the repo's own build-automation workflow had the old
`-j2` ceiling **hardcoded into an agent prompt**, which would have silently
overridden the new default and eaten the entire win for the one consumer that
needed it most (§4 below is why that happened and how to catch it elsewhere).

## The method

### 1. Find where the ceiling actually lives — it is almost never the Dockerfile

A `docker build` (or `docker compose build`) runs inside whatever engine
`docker` is actually talking to. On most laptops that engine is a VM, not the
host, and the VM's own resource cap — not any Docker-level setting — is what's
throttling the build. Per-container `deploy.resources.limits` in a compose
file *constrain* a container below what the engine has; they cannot grant it
more than the engine's own ceiling. Figure out which case you're in before
changing anything:

| Docker install | Where the real ceiling lives | How to raise it |
|---|---|---|
| Docker Desktop, **WSL2 backend** (default on Windows) | The WSL2 VM's own allocation | Edit `%UserProfile%\.wslconfig` (`[wsl2]` section: `memory=`, `processors=`), then `wsl --shutdown` to apply |
| Docker Desktop, **Hyper-V backend** (older Windows setups) | The Docker Desktop VM | Docker Desktop → Settings → Resources → Advanced (CPUs / Memory sliders), then Docker Desktop restarts the VM itself |
| Docker Desktop on **macOS** | The Docker Desktop VM (LinuxKit) | Docker Desktop → Settings → Resources (CPUs / Memory sliders); no config file, the UI is authoritative |
| **Native Linux** Docker Engine (no VM — `dockerd` runs directly on the host) | The host itself | Nothing to raise at the engine layer; the build already sees full host resources. Tuning here means the build's own `-j`/parallelism flag and any cgroup limits in `/etc/docker/daemon.json`, not a VM ceiling |
| **CI runner** (GitHub Actions, GitLab CI, etc.) | The runner's own spec | Upgrade the runner tier/size, or move to a self-hosted runner sized for the build |

Confirm which engine you're actually hitting before editing anything:
`docker info` reports the *engine's* CPU/memory, which is the number that
matters — not the host's. Compare it against the host's own total (Task
Manager / `nproc` + `free -h` on the host / OS resource monitor) to see the
gap.

### 2. Size the new ceiling — this is a host-wide tradeoff, not a build decision

Whatever you raise the engine's allocation to is resource taken away from the
host while a build runs. If builds run in the background while the user is
also using the machine (an IDE, a browser, the thing this repo *is* — a live
game server, in `tortoise-wow`'s case), maxing out the engine's allocation can
make the host sluggish exactly when the user is also trying to work. **This is
not something to decide unilaterally** — ask, with concrete numbers: host
total, current engine allocation, and 2-3 named tradeoff points (e.g.
conservative / balanced / aggressive, each with what's left for the host).
`tortoise-wow`'s host had 24 CPUs/32GB; the user chose 16 CPUs/24GB, leaving
8 CPUs/8GB for the host.

### 3. Apply, restart the engine, and verify with commands — not assumption

Editing the config file does nothing until the engine restarts:

```bash
# WSL2 backend, from the Windows side (not from inside WSL — this kills the
# WSL instance you'd be running it from):
wsl --shutdown

# Then from inside a fresh WSL shell:
nproc                                              # expect the new CPU count
free -h                                            # expect the new memory total
docker info | grep -i -E "cpus|total memory"       # confirm Docker Desktop's VM picked it up
```

Docker Desktop reports the VM's resources, not the host's — `docker info`
showing the new numbers is the actual confirmation, not the config file
having new text in it.

### 4. Raise the build's own parallelism to match — bounded by memory, not CPU count

The engine having more CPUs doesn't speed anything up until the build is told
to use them. Find the project's parallelism knob — `make -j`, `cmake --build
-j`, `cargo build -j`, a `--parallel` flag, whatever the build system uses —
and raise it. **The bound is memory per unit of work, not CPU count.** A
build with 16 CPUs available can still OOM at `-j16` if each compiled unit
needs 2GB and the engine only has 24GB: `16 × 2GB = 32GB` exceeds the ceiling
before CPU parallelism is even the limiting factor. Compute the safe value as
`floor(engine_memory / peak_memory_per_unit)`, leaving headroom for the OS and
the Docker daemon itself inside the VM — don't just set it to the CPU count.
If you don't know the peak memory per unit, either look for a comment near the
existing flag (this repo's Dockerfile documented "~1-2GB per translation
unit" from prior empirical observation) or start conservative and raise it
across successive timed builds, watching for OOM (`dmesg | grep -i "killed
process"` after each attempt).

### 5. Measure with a real build, not a guess

Run the actual build, timed, and let it finish:

```bash
time ./build-or-rebuild-script   # or whatever the project's build entrypoint is
```

Confirm no OOM occurred (`dmesg | grep -i "killed process"`) and that the
build's own success checks passed — a build that finishes faster but produces
a broken artifact is not a win. Record the real wall-clock number; don't
estimate from CPU count, because compile graphs rarely parallelize linearly
(this repo's build went 40min → 9m20s on a 5x CPU increase — not 5x, because
early configure/link stages and the dependency graph's critical path don't
parallelize).

### 6. Propagate — this is the step that's easy to skip and the one that matters most

**Search the entire repo, not just the Dockerfile and build script, for the
old resource ceiling baked into other text.** The old numbers don't just live
in the build config — they end up copy-pasted into:

- CI config (`.github/workflows/*.yml`, `.gitlab-ci.yml`, etc.) — job resource
  requests, matrix parallelism, explicit `--build-arg`/`-j` flags
- Any agent/automation workflow scripts that shell out to `docker build`
  directly instead of calling the project's build script — these are the
  highest-risk spot, because an explicit flag in a prompt or script silently
  **overrides** a config-file default; raising the default doesn't help if
  something downstream still passes the old value explicitly
- Docs that state a build-time budget ("build takes ~40 minutes") — stale
  budgets cause automation or humans to time out or bail early on a build
  that's actually already finished, or to under-provision a task queued
  against the old number
- Backlog/ticket/issue text describing a future task's build-time cost or
  citing the old CPU/memory figures as a constraint

Grep broadly and read every hit — don't trust a single-file mental model of
where "the config" lives:

```bash
grep -rn "j[0-9]\|BUILD_JOBS\|-jN\|CPUs\|[0-9]\+ minutes\?\|[0-9]\+GB\|OOM" \
  --include="*.md" --include="*.yml" --include="*.yaml" --include="*.js" \
  --include="*.sh" --include="*.json" . 2>/dev/null
```

(Adjust the pattern to the project's actual flag names and file types.) In
`tortoise-wow`, this exact search surfaced a workflow script
(`.claude/workflows/backlog-batch.js`) whose Build phase told its build agent,
in prose, to pass `-j2` and cited "the Docker VM here is 4 CPUs/8GB" as the
reason — even though the Dockerfile's own default had already been raised.
Left alone, every future automated build would have silently re-capped itself
back to the old speed, because an explicit flag beats a config default every
time. Update every hit found this way, re-verify the specific ones that drive
automation (not just docs), and commit them in the same change — a resource
bump that isn't propagated to the thing that actually runs builds in a loop is
a bump that didn't happen where it counted.

### 7. Document the new baseline where the old one was written down

Whatever doc told a human "expect a build to take ~40 minutes" needs the new
number, plus a pointer to what actually controls it (the config file, not a
Docker setting) so the next person who needs to raise it again knows where to
look instead of re-deriving this whole investigation.

## Pitfalls actually hit, or worth watching for

- **`.wslconfig` only applies on VM restart.** Editing it does nothing until
  `wsl --shutdown` (from the Windows side — running it from inside the WSL
  shell you're using kills your own shell first) followed by reopening WSL.
- **CPU count is not the binding constraint — memory per compile unit is.**
  Don't raise parallelism to match CPU count without checking the memory math;
  that's exactly how a build OOMs partway through, often 20+ minutes in.
- **Build scripts that guard against the wrong shell.** If the project's build
  script refuses to run from certain shells (e.g. `tortoise-wow`'s
  `rebuild.sh` fails closed on Git Bash because MSYS path-rewriting silently
  corrupted its verification checks once — five false FAILs after a real
  40-minute compile succeeded), respect that guard. It exists because someone
  got burned; don't route around it to save a step.
- **A raised default doesn't help if something downstream passes the old value
  explicitly** — this is worth repeating, because it's the failure mode that's
  invisible until you go looking: `docker build` with an explicit
  `--build-arg BUILD_JOBS=2` ignores a Dockerfile `ARG BUILD_JOBS=10` default
  completely, and nothing errors or warns. §6 exists because of this.
- **Sizing the engine's allocation is the user's call, not yours** — it trades
  against host responsiveness while a build runs in the background, which is
  exactly the automation use case that motivates doing this in the first
  place. Ask with concrete numbers rather than picking a value.

## Checklist for the receiving agent

- [ ] Identify the Docker backend (§1's table) and confirm with `docker info`,
      not assumption
- [ ] Get the host's real total CPU/memory and the engine's current allocation
- [ ] Ask the user to pick a new allocation with named tradeoff points (§2)
- [ ] Edit the config, restart the engine, verify with `nproc`/`free -h`/
      `docker info` (§3)
- [ ] Find the build's parallelism flag and compute a memory-bounded safe
      value, not a CPU-count-bounded one (§4)
- [ ] Run a real timed build, confirm no OOM, confirm the build's own success
      checks passed (§5)
- [ ] Grep the **whole repo** — CI config, automation/agent scripts, docs,
      backlog/ticket text — for the old ceiling hardcoded anywhere downstream
      of the config default, and fix every hit (§6)
- [ ] Update the doc that states the build-time budget, pointing at what
      actually controls it (§7)
- [ ] Commit the config-adjacent changes and the propagation fixes together
      (or in clearly linked commits) so a reviewer sees the whole story, not
      just the config bump
