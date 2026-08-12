# Ops Toolkit → Public Fork Migration — Handoff (2026-08-11)

**One-line version:** the ops toolkit is being carried out of the private repo into
`turtle-ops/` inside the public C++ fork, behind a fail-closed secret gate. Scripts, tests,
deploy layer and the command-reference tool are migrated and committed **locally**; docs and
handoffs are not. **Nothing has been pushed.**

**Status: IN PROGRESS, safely parked.** 12 commits exist on the fork's `local` branch and none
of them have left the machine. Everything is reversible with `git reset`.

For *why* this is being done and the full task-by-task plan, read
[`docs/superpowers/plans/2026-08-11-fork-migration.md`](docs/superpowers/plans/2026-08-11-fork-migration.md).
This document is state, what's left, and the traps that cost real time.

---

## 1. Where things are

| Thing | Location |
|---|---|
| Public C++ fork | `ChrisMiho/tortoise-wow`, branch `local` — **PUBLIC** |
| Fork working tree | `/home/deck/tortoise-wow-server-V2/src` (WSL), or `D:\turtle-src` from Windows |
| The migrated toolkit | `<fork>/turtle-ops/` — 103 tracked files |
| Private ops repo (this one) | `D:\TurtleWow` = `ChrisMiho/turtle-tournament` — **PRIVATE**, branch `proper-setup` at `3bd01de` |
| Live server / stack root | `/home/deck/tortoise-wow-server-V2` |

The fork checkout sits **inside** the stack root, at `src/`. That matters for the build — see §5.

## 2. What is migrated (committed locally, not pushed)

12 commits on `local`, all on top of `c06b2fb` (the last pre-migration C++ commit):

| Area | Files | Commits |
|---|---|---|
| `turtle-ops/scripts/` | 40 — ship/verify, WSG match control, bot-progression telemetry, AH bot, the audit gate | `f487bd3`, `0b88c2b` |
| `turtle-ops/tests/` | 28 — shell suites for the above; Docker driven through a file-backed stub | `f487bd3`, `7da36de`, `03a3909` |
| `turtle-ops/deploy/` | 8 — Dockerfile, compose, `.dockerignore`, 4 redacted `*.conf.template` | `d8e04e2`, `82a854d` |
| `turtle-ops/command-reference/` | 26 — Python tool that parses this tree's C++ into command docs | `3d91c8b`, `33b574f` |
| `turtle-ops/.gitignore` | 1 — anchored `/backups/`, `/logs/`, `/data/` | `03a3909` |
| The audit gate's four fix rounds | — | `69a6e8e`, `8a16007`, `75693bb`, `bd7f442` |

Test state: the shell suites pass (the provenance group is 97 checks; the gate is 49; the
dockerignore guard is 4). `command-reference` is 68 passing, 1 skipped — the skip is a
pre-existing unshipped build artifact, not a migration effect. `python3-pytest` had to be
installed via apt; WSL's Python has neither pip nor ensurepip.

## 3. What is NOT done

**The working tree is dirty.** A Task 6 run was interrupted partway. Before doing anything else:

```bash
cd /home/deck/tortoise-wow-server-V2/src
git status --short
#  M turtle-ops/tests/docs-link-audit.sh
#  ?? turtle-ops/docs/
#  ?? turtle-ops/handoffs/
```

Decide whether to keep or discard that partial work — it was never reviewed and never committed.

Then, three tasks remain, fully specified in the plan:

| Task | What | Why it matters |
|---|---|---|
| **6** | Migrate `docs/` and the 12 root `*-HANDOFF.md` / `*-REPORT.md` into `turtle-ops/docs/` and `turtle-ops/handoffs/` | This is the "open work" payload — it's what lets an agent in the fork pick up an unfinished workstream |
| **7** | Write `turtle-ops/README.md`, the fork agent's entry point | Everything above is inert without a door into it |
| **8** | Final audit, verify nothing landed outside `turtle-ops/`, push | **Gated on an explicit owner go-ahead.** Nothing is public until this runs |

Task 6 has two exclusions that must hold: the two `friend-local-client-switcher` documents, and
`docs/interupt/` (a session transcript, not documentation).

## 4. The audit gate — read this before pushing anything

`turtle-ops/scripts/audit-public-safe.sh [PATH]` — exit **0** clean, **1** secret found.
49 checks in `turtle-ops/tests/audit-public-safe-test.sh`. It blocks private-network addresses
and hostnames, database and GM credential shapes, the `mysql -p<value>` CLI flag, private key
material, and the client switcher scripts by name.

**Run it before every push**, chained so a red gate blocks the commit:

```bash
bash turtle-ops/scripts/audit-public-safe.sh turtle-ops && git add … && git commit …
```

It took four fix rounds to become trustworthy. As originally specified it would have published
the DB password with a green `OK: public-safe` — it failed open on an unreachable path, and its
only credential pattern covered `gm_pass|admin_pass`.

**Known gaps, deliberately accepted** (all recorded in the plan's ledger):

- A secret inside a shell-expansion default — `"${DB_PASS:-actualsecret}"` — passes. It sits one
  character from the `${VAR:?}` form the gate must keep allowing, and fixing it risks re-breaking
  an exemption that took two rounds to stabilise.
- An all-numeric password of ≤6 digits, and a `-p` password of ≤7 characters, both pass. Those
  bounds exist so real config integers (`= 1`, `= 13342`) don't false-positive.
- A connection string on a 3-digit port isn't caught; the port is bounded to 4–5 digits so ANSI
  terminal colour escapes (`\033[38;2;185;95;255m`) don't false-positive.
- It requires PCRE2 ≥ 10.43 for one variable-length lookbehind. WSL's GNU grep 3.12 is fine; Git
  Bash's grep 3.0 is not. On an older PCRE2 it fails closed — unusable rather than leaky.

**The gate is defence in depth, not the primary control.** A regex only catches shapes someone
anticipated, and this content kept producing new ones — three review rounds, three new shapes.
The authoritative check is a literal search for the values you actually hold:

```bash
DBPASS=$(tr -d '\r\n' < /home/deck/tortoise-wow-server-V2/.dbpass)
cd /mnt/d/TurtleWow && git ls-files -z | xargs -0 grep -Fl -- "$DBPASS"
```

Run that from a **script file**, never inline through `wsl.exe` — see §5.

## 5. Traps that cost real time

**Shell variables and `$?` do not survive `wsl.exe -- bash -lc '...'`.** They are expanded
Windows-side and arrive empty. This produced a false security alarm in this very session: a
`git grep -F "$DBPASS"` became `git grep -F ""`, which matches every file, and reported that a
secret had been committed when none had. Write the script to a file and run the file:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < /mnt/c/<path>/x.sh > /tmp/x.sh && bash /tmp/x.sh'
```

**`turtle-ops/` must stay out of the Docker build context.** The build context is the stack root
and `Dockerfile:20` is `COPY src /src` — the layer that gates the ~27-minute compile. Without
`src/turtle-ops` in `/home/deck/tortoise-wow-server-V2/.dockerignore`, **editing a README forces
a full recompile.** That line is in place; `turtle-ops/tests/dockerignore-turtle-ops-test.sh`
asserts it. If a trivial doc change ever triggers a long build, check that first.

**`deploy/` is a mirror, not the live files.** `scripts/stage-deploy.sh` copies live → repo,
redacting the DB password. There is **no** repo → live direction. Editing
`turtle-ops/deploy/Dockerfile` changes nothing about what builds; edit
`/home/deck/tortoise-wow-server-V2/Dockerfile` and re-run `stage-deploy.sh`.

**Four migrated scripts write into the tracked tree when run** — `backup-alive-world-pre.sh`
(an unredacted `.env` into `backups/`), `wsg-kickoff.sh` and `wsg-logs-clean.sh` (match logs into
`logs/wsg-matches/`), `bot-progression/snapshot.sh` (`data/progression/`). `turtle-ops/.gitignore`
contains them. Its rules are **anchored** (`/data/`, not `data/`) on purpose: an unanchored
`data/` matches at any depth and would have silently swallowed
`turtle-ops/command-reference/data/`, which is tracked payload.

**Copying from `/mnt/d/` marks every file 755** (a DrvFs artifact), and **editing through
`\\wsl.localhost\` silently strips the executable bit.** Both bit this session. Normalize modes
before committing: `.sh` executable, everything else 644.

**`rg` is not installed in WSL.** A script that shells out to it exits 0 having checked nothing.
This is why `tests/docs-link-audit.sh` passed for so long without checking anything — it needs
converting to `grep -r`, with a missing binary made fatal. That conversion is part of Task 6 and
is among the uncommitted changes noted in §3.

**Never merge, subtree, or fetch `turtle-tournament` history into the fork.** `552ddd4`
untracked the client switcher scripts but did not rewrite history, so that history still
contains the GM password. Migration is **copy files, commit fresh** — never share refs.

## 6. Secrets and privacy — what was found and what was decided

Three review rounds turned up the live DB password in the *current tracked tree* in four places
across three files, in three shapes — including `mariadb -uroot -p<value>`, which carries no
`password` keyword and was invisible to every version of the gate until specifically hunted. All
are now redacted in the private repo (`4baecb0`).

**Owner ruling:** the only value that must never be published is the friend's IP address. The DB
and GM credentials guard a MariaDB bound to `127.0.0.1:3309` on a private game server and are
judged low-consequence.

Recorded for accuracy: the live DB password is **not** in upstream's tree or reachable history.
Upstream ships only the stock `LoginDatabaseInfo = "127.0.0.1;3306;mangos;mangos;tw_logon"`. The
*shape* is public; this value is not. That does not change the ruling — it is the owner's risk to
weigh — but rotation remains the cheap fix if it is ever revisited, because it does not depend on
having found every occurrence.

**Outstanding privacy item, not yet actioned:** several documents refer to a real third party by
first name. The gate passes them — a name is not credential-shaped. Six occurrences are already
committed to the fork in the two `command-reference/docs/superpowers/` files; more are in Task 6's
payload (`docs/playerbots/WSG-BOT-MATCH.md`, `SPAWN-AND-PARTY-STARTER.md`,
`WSG-FIRST-RUN-DEBUG-HANDOFF.md`, `docs/superpowers/plans/2026-08-10-server-lifecycle-scripts.md`).
Replacing the name with "a friend" costs nothing and loses no meaning. **Nothing is pushed, so
this is still fully reversible.** The `mmodad` account handle was deliberately left alone — it
appears in command examples where changing it would break the documentation.

## 7. Do-nots

| Don't | Why |
|---|---|
| Push before running the audit gate | It is the only automated control between a private repo and the world |
| Add `turtle-tournament` as a remote of the fork | Its history contains the GM password (§5) |
| Put anything under an upstream-owned path | `turtle-ops/` is conflict-free by construction; anything else fights every future rebase |
| Un-anchor the `.gitignore` rules | A bare `data/` swallows `command-reference/data/` (§5) |
| Weaken the gate to clear a false positive | A false block is loud and self-limiting; a false clean is silent |
| `docker compose down -v` | That volume is the entire world. It has been lost once — see [`WSG-ROSTER-RECOVERY-HANDOFF.md`](WSG-ROSTER-RECOVERY-HANDOFF.md) |
| Move the `ARG`/`LABEL` block out of the Dockerfile's runtime stage | Adds ~27 minutes to every build |

## 8. Finishing this

```bash
cd /home/deck/tortoise-wow-server-V2/src
git status --short                      # resolve the dirty tree first (§3)
git log --oneline c06b2fb..HEAD         # should show the 12 commits in §2
bash turtle-ops/scripts/audit-public-safe.sh turtle-ops   # expect: OK, rc=0
```

Then work Tasks 6, 7 and 8 from
[`docs/superpowers/plans/2026-08-11-fork-migration.md`](docs/superpowers/plans/2026-08-11-fork-migration.md).
Task 8's pre-push checks are the ones that matter:

```bash
# nothing outside turtle-ops/ (expect exactly the 3 C++ files of the local delta)
git diff --name-only upstream/playerbots-integration-gh..local | grep -v '^turtle-ops/'
# no foreign remote
git remote -v          # expect only origin, upstream, backup
```

The SDD ledger for this work, including every parked finding and the reasoning behind each
ruling, is at `.superpowers/sdd/2026-08-11-fork-migration/progress.md` (gitignored, local only).

## 9. Provenance

- Plan: [`docs/superpowers/plans/2026-08-11-fork-migration.md`](docs/superpowers/plans/2026-08-11-fork-migration.md)
- Related: [`docs/superpowers/plans/2026-08-11-upstream-sync.md`](docs/superpowers/plans/2026-08-11-upstream-sync.md)
  — the C++ rebase onto upstream's 60 newer commits, analysed and proven conflict-free, not executed
- Prior state: [`CPP-SOURCE-HANDOFF.md`](CPP-SOURCE-HANDOFF.md)
