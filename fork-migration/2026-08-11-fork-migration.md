# Fork Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every enhancement, script, test, config and open idea from the private
`turtle-tournament` repo into the public `ChrisMiho/tortoise-wow` fork, so an agent working
inside that fork can do all the work that previously required the private repo — without
publishing a single credential.

**Architecture:** Everything lands in one new top-level directory, `turtle-ops/`, on the
fork's `local` branch. Upstream has no such directory, so it is structurally impossible for
it to ever cause a rebase conflict — the C++ delta stays at 3 files and future upstream syncs
stay as cheap as the one analysed in
[`2026-08-11-upstream-sync.md`](2026-08-11-upstream-sync.md). Files are copied at their
**current working-tree state and committed fresh**. The private repo's *history* is never
grafted, because that history contains GM credentials.

**Tech Stack:** git, bash (run from WSL against the WSL checkout), Python 3 + pytest (for
`command-reference/`), Docker Compose.

---

## Why history must not be grafted

`552ddd4` untracked the client switcher scripts, but **did not rewrite history** — the
`.gitignore` says so explicitly:

> The files are still on disk — untracking does not delete them. Note that the
> versions committed before 2026-08-10 remain in this repo's history.

Those files (`NoxFiles/Setup-TurtleClient.ps1`, `TurtleWOWClient/TurtleWoW/*`, 15 files,
1,394 deleted lines) carry the GM account credentials and Tailscale tailnet addresses in
plaintext. Any `git merge`, `git subtree`, `git filter-repo --path`, or remote-add-and-fetch
that brings `turtle-tournament` history into the public fork **publishes those commits**.

**Therefore: copy files, never refs.** `cp` the working tree, `git add`, commit fresh in the
fork. No shared history, no `git remote add`, no subtree.

## What is already safe, and what is not

The private repo's secret hygiene is better than the handoff suggests. Verified 2026-08-11:

| Mechanism | Status |
|---|---|
| DB password | Lives in `.dbpass`, which is gitignored. Scripts read it at runtime (`scripts/lib/wsg-bots-common.sh:70`). Never embedded. |
| `deploy/etc/*.template` | Redacted to `__DB_PASS__` by `scripts/stage-deploy.sh`, which **fails closed** — it greps for the password after redacting and aborts if found (`stage-deploy.sh:35-38`). |
| `adhoc/Install-*.ps1` | Generates or reuses passwords at runtime. No literals. |
| `claude-settings-kimi-k3.json` | Contains the placeholder `YOUR_MOONSHOT_API_KEY`. No real key. |

**The actual exposure is six files in the current tree that carry Tailscale addresses**, plus
one hardcoded bot credential:

| File | Problem | Disposition |
|---|---|---|
| `docs/superpowers/plans/2026-08-09-friend-local-client-switcher.md` | Tailnet addresses; this *is* the friend-server switcher work | **EXCLUDE** — owner's explicit instruction |
| `docs/superpowers/specs/2026-08-09-friend-local-client-switcher-design.md` | Same | **EXCLUDE** |
| `tests/TurtleClient.Common.Tests.ps1` | Tests for the switcher | **EXCLUDE** |
| `WINDOWS-SETUP-HANDOFF.md` | Tailnet addresses, **the live DB root/mangos password in two different shapes** (a `password :` line in "Reference values", and a `mariadb -uroot -p<value>` command), **and a GM account credential pair** (`Accounts : <user> / <pass>`) | **REDACT ALL** then migrate (Task 2) |
| `docs/superpowers/plans/2026-08-09-markdown-cleanup.md` | The live DB password again, as `mariadb -uroot -p<value>` | **REDACT** then migrate (Task 2) |
| `docs/superpowers/plans/2026-08-10-server-lifecycle-scripts.md` | Tailnet addresses | **REDACT** then migrate (Task 2) |
| `docs/superpowers/specs/2026-08-10-server-lifecycle-scripts-design.md` | Tailnet addresses | **REDACT** then migrate (Task 2) |
| `scripts/fix-ahbot-password.sh` | Sets the AHBOT account password to the literal `AHBOT` | Migrate as-is; note in README. It is an internal auction-house bot account on a LAN server, not a user credential. |

Also excluded as noise rather than risk: `docs/interupt/logs.txt` (a 447-line Claude Code
session transcript), `claude-settings-kimi-k3.json` (harness config, not server work),
`turtle-command-data.txt` (already gitignored), `TurtleV2/`, `*.dmlpack`, `*.torrent`.

## Global Constraints

- **The fork `ChrisMiho/tortoise-wow` is PUBLIC.** Nothing secret may land in it, in any
  commit, ever. Task 1's audit gate runs before every push.
- **Never add `turtle-tournament` as a remote of the fork, and never merge/subtree/fetch its
  history.** Copy working-tree files only.
- **Never run git from WSL against a Windows worktree, or Windows git against the WSL tree.**
  The fork checkout is at `/home/deck/tortoise-wow-server-V2/src` (WSL) — use WSL git there.
  The private repo is `D:\TurtleWow` (Windows) — use Windows git there.
- **Do not use `wsl.exe -- bash -lc '<inline script>'` for anything with variables or `$?`.**
  They arrive empty — this has produced three wrong conclusions in one session. Write the
  script to a file and run the file. Where a step below says "write to `<scratchpad>/x.sh`",
  that means this session's scratchpad directory, and "run it" means exactly:

  ```bash
  wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < "/mnt/c/Users/mihov/AppData/Local/Temp/claude/D--TurtleWow/2dfcf21b-8058-4bf6-a38f-407fe762b8d5/scratchpad/x.sh" > /tmp/x.sh && bash /tmp/x.sh'
  ```
- **All new content goes under `turtle-ops/`.** Never add a file to an upstream-owned path —
  that is what keeps rebases free.
- **`rg` is not installed in WSL.** Use `grep -r`. A script that shells out to `rg` silently
  no-ops and exits 0 (this is why `tests/docs-link-audit.sh` passes without checking anything).
- **Check `git diff --cached --stat` before every commit**, or use `git commit -- <paths>`.
- Do not delete anything from `D:\TurtleWow` in this plan. Migration is additive; retiring the
  private repo is a separate decision.

## File Structure

Target layout inside the fork (all new, all under one directory):

| Path in fork | From | Task |
|---|---|---|
| `turtle-ops/scripts/` | `D:\TurtleWow\scripts\` (33 files incl. `lib/`, `bot-progression/`) | 3 |
| `turtle-ops/tests/` | `D:\TurtleWow\tests\` (22 files incl. `fixtures/`) minus the PowerShell switcher test | 3 |
| `turtle-ops/deploy/` | `D:\TurtleWow\deploy\` (8 files) | 4 |
| `turtle-ops/command-reference/` | `D:\TurtleWow\command-reference\` (21 files) | 5 |
| `turtle-ops/docs/` | `D:\TurtleWow\docs\` minus the two switcher documents | 6 |
| `turtle-ops/handoffs/` | The 12 root `*-HANDOFF.md` / `*-REPORT.md` files | 6 |
| `turtle-ops/scripts/audit-public-safe.sh` | **new** — the secret gate | 1 |
| `/home/deck/tortoise-wow-server-V2/.dockerignore` | **live file, edited** — must exclude `src/turtle-ops` or every doc edit costs a 27-min rebuild | 1B |
| `turtle-ops/README.md` | **new** — the fork agent's entry point | 7 |

Two paths deliberately **unchanged**: the fork checkout stays at
`/home/deck/tortoise-wow-server-V2/src`, and the stack root stays where it is. The scripts
resolve both from `$HOME` at `scripts/lib/provenance.sh:9-10`
(`TW_LIVE_ROOT="${TW_LIVE_ROOT:-$HOME/tortoise-wow-server-V2}"`), not from their own location,
so relocating `scripts/` does not break them — provided `lib/` moves with them, which Task 3
does by copying the directory wholesale.

---

### Task 1: The secret-audit gate

Build this **first**, before a single file is copied. It is the one thing standing between a
private repo and a public push. Modelled on the fail-closed pattern already proven in
`scripts/stage-deploy.sh:33-38`.

**Files:**
- Create: `turtle-ops/scripts/audit-public-safe.sh` (in the fork checkout)
- Create: `turtle-ops/tests/audit-public-safe-test.sh`

**Interfaces:**
- Produces: `audit-public-safe.sh [PATH]` — scans `PATH` (default: the `turtle-ops/` tree).
  Exit **0** = clean, exit **1** = secret found. Every later task runs it before committing.

- [ ] **Step 1: Write the failing test**

Create `turtle-ops/tests/audit-public-safe-test.sh`:

```bash
#!/bin/bash
# Test the public-safety audit gate. Each case builds a throwaway tree and asserts
# the script's exit code — the gate must fail closed on every known secret shape.
set -uo pipefail
AUDIT="$(dirname "$0")/../scripts/audit-public-safe.sh"
PASS=0; FAIL=0

check() { # name expected_rc setup_fn
    local name="$1" want="$2" body="$3"
    local d; d=$(mktemp -d)
    ( cd "$d" && eval "$body" )
    bash "$AUDIT" "$d" >/dev/null 2>&1
    local got=$?
    if [ "$got" = "$want" ]; then PASS=$((PASS+1)); echo "ok   - $name"
    else FAIL=$((FAIL+1)); echo "FAIL - $name (want rc=$want got rc=$got)"; fi
    rm -rf "$d"
}

check "clean tree passes"            0 'echo "just some docs" > a.md'
check "tailnet 100.x address"        1 'echo "server at 100.64.12.9" > a.md'
check "ts.net hostname"              1 'echo "host: deck.tail1a2b.ts.net" > a.md'
check "literal tailscale mention"    1 'echo "use the tailscale ip" > a.md'
check "unredacted DB pass marker"    0 'echo "LoginDatabaseInfo = \"db;3306;mangos;__DB_PASS__;tw_logon\"" > a.conf'
check "GM password env leak"         1 'echo "GMPASS=hunter2" > a.sh'
check "private key block"            1 'printf -- "-----BEGIN RSA PRIVATE KEY-----\n" > id.pem'
check "switcher script by name"      1 'echo x > Setup-TurtleClient.ps1'
check "nested dir is scanned"        1 'mkdir -p deep/er && echo "100.72.0.1" > deep/er/a.md'
check "non-CGNAT 100.x is allowed"   0 'echo "100.5.0.1 is a public address" > a.md'

# The gate's own source contains every pattern it blocks. If it does not exclude
# itself it flags itself, and no commit can ever pass.
echo "ok   - (self-exclusion checked below)"
if bash "$AUDIT" "$(dirname "$AUDIT")" >/dev/null 2>&1; then
    PASS=$((PASS+1)); echo "ok   - gate does not flag its own source"
else
    FAIL=$((FAIL+1)); echo "FAIL - gate flags its own source (add --exclude for itself)"
fi

echo ""
echo "passed: $PASS  failed: $FAIL"
[ "$FAIL" = 0 ]
```

The `non-CGNAT 100.x` case matters: `100.5.0.1` is ordinary public address space. Blocking all
of `100.*` would produce false positives forever; only `100.64.0.0/10` is the private range.

Note the `__DB_PASS__` case expects **0** — the redaction placeholder is the *safe* state and
must not trip the gate.

- [ ] **Step 2: Run it to verify it fails**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && bash turtle-ops/tests/audit-public-safe-test.sh'
```

Expected: FAIL — `audit-public-safe.sh` does not exist yet.

- [ ] **Step 3: Write the gate**

Create `turtle-ops/scripts/audit-public-safe.sh`:

```bash
#!/bin/bash
# Fail-closed audit: refuse to let anything secret reach the PUBLIC fork.
#
# Exit 0 = clean, 1 = something found. Run before every push.
#
# grep, not rg: rg is not installed in WSL and a missing binary would make this
# exit 0 without checking anything — the exact failure mode that makes
# tests/docs-link-audit.sh useless.
set -uo pipefail
TARGET="${1:-$(dirname "$0")/..}"
FOUND=0

# This script and its test necessarily contain the patterns they block. Excluding
# them by name is what stops the gate flagging itself and blocking every commit.
SELF=(--exclude=audit-public-safe.sh --exclude=audit-public-safe-test.sh)

report() { echo "BLOCKED: $1"; shift; printf '  %s\n' "$@"; FOUND=1; }

# CGNAT range 100.64.0.0/10 — the second octet is 64-127.
hits=$(grep -rlnE "${SELF[@]}" '\b100\.(6[4-9]|[7-9][0-9]|1[0-1][0-9]|12[0-7])\.[0-9]{1,3}\.[0-9]{1,3}\b' "$TARGET" 2>/dev/null)
[ -n "$hits" ] && report "private network IP address" $hits

hits=$(grep -rlniE "${SELF[@]}" '\.ts\.net|tailscale|tailnet' "$TARGET" 2>/dev/null)
[ -n "$hits" ] && report "private network reference" $hits

# Credential assignments with a literal value. __DB_PASS__ is the redaction
# placeholder and is explicitly allowed.
hits=$(grep -rlniE "${SELF[@]}" '(gm_?pass|gmpassword|admin_?pass)[[:space:]]*=[[:space:]]*[^_[:space:]]' "$TARGET" 2>/dev/null)
[ -n "$hits" ] && report "GM credential assignment" $hits

hits=$(grep -rln "${SELF[@]}" -- '-----BEGIN .*PRIVATE KEY-----' "$TARGET" 2>/dev/null)
[ -n "$hits" ] && report "private key material" $hits

# The switcher scripts, by name, wherever they turn up.
hits=$(find "$TARGET" -type f \( -name 'Setup-TurtleClient.ps1' -o -name 'TurtleClient.Common*' \
       -o -name 'Restart-TurtleServer.ps1' -o -name 'Stop-TurtleServer.ps1' \) 2>/dev/null)
[ -n "$hits" ] && report "client switcher script" $hits

if [ "$FOUND" = 0 ]; then
    echo "OK: $TARGET is public-safe"
fi
exit "$FOUND"
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && chmod +x turtle-ops/scripts/audit-public-safe.sh && bash turtle-ops/tests/audit-public-safe-test.sh'
```

Expected: `passed: 11  failed: 0`

- [ ] **Step 5: Commit**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git checkout local && git add turtle-ops/scripts/audit-public-safe.sh turtle-ops/tests/audit-public-safe-test.sh && git diff --cached --stat && git commit -m "turtle-ops: add the fail-closed public-safety audit gate"'
```

---

### Task 1B: Keep the build cache intact

**This must land before any bulk content is copied.** The migration puts `turtle-ops/` inside
`src/`, and `src/` is what the Docker build copies. Without this task, editing a README costs
27 minutes.

The mechanism, verified 2026-08-11:

- `ship-cpp-fix.sh:94` does `cd "$TW_LIVE_ROOT"`, then line 105 runs `docker build` — so the
  **build context is the stack root**, `/home/deck/tortoise-wow-server-V2`.
- `Dockerfile:20` is `COPY src /src`. Every layer after it — including the ~27-minute cmake
  compile — is cached against that layer's content hash.
- The live `.dockerignore` excludes `src/.git` and `src/.github` for exactly this reason
  (its own comment says the 138 MB history "has no business in the build context"). It does
  **not** exclude `src/turtle-ops`, which does not exist yet.

So every doc, script, test and handoff added under `turtle-ops/` becomes a cache-busting
input to the C++ compile. One line fixes it.

Note also that `docker-compose.yml` has **no `build:` section** — compose only runs the
prebuilt `tortoise-v2:local` image. `ship-cpp-fix.sh` is the only thing that builds.

**Files:**
- Modify: `/home/deck/tortoise-wow-server-V2/.dockerignore` (the live file — the one that acts)
- Modify: `D:\TurtleWow\deploy\.dockerignore` (the private-repo mirror)
- Create: `turtle-ops/tests/dockerignore-turtle-ops-test.sh`

**Interfaces:**
- Consumes: nothing. Run it immediately after Task 1.
- Produces: a build context that ignores `turtle-ops/`. Task 5 copies the corrected
  `.dockerignore` into `turtle-ops/deploy/`.

- [ ] **Step 1: Prove the problem before fixing it**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2 && mkdir -p src/turtle-ops && echo probe > src/turtle-ops/probe.txt && docker build --no-cache -f - . <<EOF
FROM busybox
COPY src /src
RUN if [ -e /src/turtle-ops/probe.txt ]; then echo "LEAKED: turtle-ops is in the build context"; exit 1; else echo "OK: excluded"; fi
EOF'
```

This builds a busybox image in seconds — it does not touch the server or the running image.

Expected **before** the fix: `LEAKED: turtle-ops is in the build context`, and the build exits
non-zero.

Two flags matter here and both were wrong in an earlier draft of this plan. **Do not add
`-q`** — it suppresses `RUN` output, so neither sentence would ever print. And the `RUN` must
`exit 1` on the leak rather than using `cmd && echo … || echo …`, which always exits 0 and so
reports success either way. `--no-cache` stops a cached layer from answering for a context
that has since changed.

- [ ] **Step 2: Write the regression test**

Create `turtle-ops/tests/dockerignore-turtle-ops-test.sh`:

```bash
#!/bin/bash
# turtle-ops/ must never enter the Docker build context. If it does, every doc
# edit invalidates `COPY src /src` and forces a full ~27-minute recompile.
set -uo pipefail
ROOT="${TW_LIVE_ROOT:-$HOME/tortoise-wow-server-V2}"
DI="$ROOT/.dockerignore"
PASS=0; FAIL=0

if [ -f "$DI" ]; then
    PASS=$((PASS+1)); echo "ok   - .dockerignore exists at $DI"
else
    FAIL=$((FAIL+1)); echo "FAIL - no .dockerignore at $DI"
fi

if grep -qE '^\s*src/turtle-ops/?\s*$' "$DI" 2>/dev/null; then
    PASS=$((PASS+1)); echo "ok   - src/turtle-ops is excluded from the build context"
else
    FAIL=$((FAIL+1)); echo "FAIL - src/turtle-ops NOT excluded; a doc edit will cost a full rebuild"
fi

# The exclusions that were already load-bearing before this migration.
for pat in 'src/.git' 'src/.github'; do
    if grep -qF "$pat" "$DI" 2>/dev/null; then
        PASS=$((PASS+1)); echo "ok   - $pat still excluded"
    else
        FAIL=$((FAIL+1)); echo "FAIL - $pat exclusion was lost"
    fi
done

echo ""
echo "passed: $PASS  failed: $FAIL"
[ "$FAIL" = 0 ]
```

- [ ] **Step 3: Run it to verify it fails**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'bash /home/deck/tortoise-wow-server-V2/src/turtle-ops/tests/dockerignore-turtle-ops-test.sh'
```

Expected: `FAIL - src/turtle-ops NOT excluded`, `passed: 3  failed: 1`.

- [ ] **Step 4: Add the exclusion to the live `.dockerignore`**

Append to `/home/deck/tortoise-wow-server-V2/.dockerignore`, directly below the `src/.github`
line so it sits with the other `src/` exclusions:

```
# The operations toolkit lives inside the repo but is not part of the server build.
# Without this line every doc, script or test edit changes the `COPY src /src` layer
# and forces a full ~27-minute recompile.
src/turtle-ops
```

- [ ] **Step 5: Verify the fix, both ways**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'bash /home/deck/tortoise-wow-server-V2/src/turtle-ops/tests/dockerignore-turtle-ops-test.sh'
```

Expected: `passed: 4  failed: 0`.

Then re-run the Step 1 probe:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2 && docker build --no-cache -f - . <<EOF
FROM busybox
COPY src /src
RUN if [ -e /src/turtle-ops/probe.txt ]; then echo "LEAKED: turtle-ops is in the build context"; exit 1; else echo "OK: excluded"; fi
EOF'
```

Expected now: `OK: excluded`, build exits 0. Then clean up the probe:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'rm -f /home/deck/tortoise-wow-server-V2/src/turtle-ops/probe.txt'
```

- [ ] **Step 6: Mirror into the private repo and commit both**

Apply the same three-line addition to `D:\TurtleWow\deploy\.dockerignore` with the Edit tool,
then:

```bash
git -C D:/TurtleWow add deploy/.dockerignore
git -C D:/TurtleWow diff --cached --stat
git -C D:/TurtleWow commit -m "docker: keep turtle-ops out of the build context"
```

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git add turtle-ops/tests/dockerignore-turtle-ops-test.sh && git diff --cached --stat && git commit -m "turtle-ops: assert the build context excludes the ops toolkit"'
```

---

### Task 2: Redact the three secret-bearing documents

These carry real work worth publishing, spoiled by embedded addresses — and, in one case, by a
live credential. Redact into a publishable form, in the **private** repo, so the redacted
version is what Task 6 copies.

**The live DB password is in more places, and more shapes, than planning assumed.** Three
separate rounds of Task 1 review each turned up another one. What is known as of round 3:

| File | Shape |
|---|---|
| `WINDOWS-SETUP-HANDOFF.md` | `DB root/mangos password : <value>` in the "Reference values" block |
| `WINDOWS-SETUP-HANDOFF.md` | `docker exec tw2-db mariadb -uroot -p<value>` — no `password` keyword at all |
| `WINDOWS-SETUP-HANDOFF.md` | `Accounts : <user> / <pass>   (GM, rank 3)` — a GM credential pair |
| `docs/superpowers/plans/2026-08-09-markdown-cleanup.md` | `mariadb -uroot -p<value>` again |

None of these was in the original plan. The `-p<value>` form is the instructive one: it
carries no `password` token, so no keyword-based scan finds it, and it was only discovered by
grepping for the literal value from `.dbpass`.

**Treat the pattern-matching gate as defence in depth, not as the primary control here.** The
authoritative check is a literal search for the actual secret values, which Step 1b below
does. The gate catches shapes you did not think to look for; the literal search catches the
value you know.

**Owner ruling, 2026-08-11:** the only value that genuinely must not be published is the
friend's IP address. The DB and GM credentials guard a MariaDB bound to `127.0.0.1:3309` and
the compose network on a private game server, and are judged low-consequence.

Recorded for accuracy, since it was checked: the live DB password is **not** in upstream's
tree and **not** in any reachable upstream history. Upstream ships only the stock default
`LoginDatabaseInfo = "127.0.0.1;3306;mangos;mangos;tw_logon"`. So the shape is public; this
specific value is not. That does not change the ruling — it is the owner's risk to weigh — but
the redactions below are kept anyway, because they cost a handful of edits.

**Priority order for this task:** the network addresses are the hard requirement and block the
migration. The credential redactions are hygiene and do not.

**Files:**
- Modify: `D:\TurtleWow\WINDOWS-SETUP-HANDOFF.md`
- Modify: `D:\TurtleWow\docs\superpowers\plans\2026-08-10-server-lifecycle-scripts.md`
- Modify: `D:\TurtleWow\docs\superpowers\specs\2026-08-10-server-lifecycle-scripts-design.md`
- Modify: `D:\TurtleWow\docs\superpowers\plans\2026-08-09-markdown-cleanup.md`

**Interfaces:**
- Consumes: the gate from Task 1 (used here to verify the redaction worked).
- Produces: three documents that pass `audit-public-safe.sh`. Task 6 copies them.

- [ ] **Step 1: Find every occurrence**

```bash
grep -nE '\b100\.(6[4-9]|[7-9][0-9]|1[0-1][0-9]|12[0-7])\.[0-9]{1,3}\.[0-9]{1,3}\b|\.ts\.net|[Tt]ailscale|[Tt]ailnet|[Pp]assword|[Pp]asswd' WINDOWS-SETUP-HANDOFF.md docs/superpowers/plans/2026-08-10-server-lifecycle-scripts.md docs/superpowers/specs/2026-08-10-server-lifecycle-scripts-design.md
```

Read every hit. Some will be bare mentions of the word "Tailscale" (fine to keep in principle,
but the gate blocks them, so they must go too), some will be actual addresses, and at least one
is a live credential.

Cross-check with the gate itself, which is the authority on what will block a push:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'mkdir -p /tmp/pre-redact && cp /mnt/d/TurtleWow/WINDOWS-SETUP-HANDOFF.md /mnt/d/TurtleWow/docs/superpowers/plans/2026-08-10-server-lifecycle-scripts.md /mnt/d/TurtleWow/docs/superpowers/specs/2026-08-10-server-lifecycle-scripts-design.md /tmp/pre-redact/ && bash /home/deck/tortoise-wow-server-V2/src/turtle-ops/scripts/audit-public-safe.sh /tmp/pre-redact; echo "rc=$?"'
```

Expected **before** redaction: `rc=1`, listing the files it can see.

- [ ] **Step 1b: Search for the literal secret values — this is the authoritative check**

The gate finds shapes; this finds the value. Read the real secrets from the server and grep
the whole tracked tree for them, so nothing depends on a regex having anticipated the form:

Write to `<scratchpad>/t2-literal.sh`:

```bash
#!/bin/bash
# Find every occurrence of the ACTUAL secret values in the tracked tree.
# Reads them from the server rather than hardcoding, so this script is itself safe.
set -uo pipefail
DBPASS=$(tr -d '\r\n' < /home/deck/tortoise-wow-server-V2/.dbpass)
[ -n "$DBPASS" ] || { echo "FATAL: .dbpass empty"; exit 1; }

cd /mnt/d/TurtleWow || exit 9
echo "=== tracked files containing the live DB password ==="
git ls-files -z | xargs -0 grep -Fl -- "$DBPASS" 2>/dev/null || echo "(none)"
echo ""
echo "=== occurrences per file (line numbers, value masked) ==="
git ls-files -z | xargs -0 grep -Fn -- "$DBPASS" 2>/dev/null \
  | sed "s/${DBPASS}/<REDACTED>/g" || true
```

Run it. Every file it lists must be redacted before Task 6 copies it — whether or not this
plan named it. If it reports a file not in this task's Files list, add it and say so in the
report.

Repeat for any other known credential (the GM account pair in
`WINDOWS-SETUP-HANDOFF.md`).

- [ ] **Step 2: Redact with the Edit tool**

Replace, using these exact substitutions so the documents stay readable:

| Find | Replace with |
|---|---|
| any `100.x.y.z` tailnet address | `<SERVER-LAN-IP>` |
| any `*.ts.net` hostname | `<SERVER-HOSTNAME>` |
| "Tailscale" / "tailnet" as the transport | "the private network" |

Where a sentence only makes sense with the concrete value, rewrite the sentence rather than
leaving a dangling placeholder. Add one line near the top of each redacted file:

```markdown
> Network addresses in this document are redacted. The concrete values live in the private
> ops repo, not here.
```

- [ ] **Step 3: Verify the redaction with the gate**

Copy the three files to a scratch dir and run the audit against it:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'mkdir -p /tmp/redact-check && cp /mnt/d/TurtleWow/WINDOWS-SETUP-HANDOFF.md /mnt/d/TurtleWow/docs/superpowers/plans/2026-08-10-server-lifecycle-scripts.md /mnt/d/TurtleWow/docs/superpowers/specs/2026-08-10-server-lifecycle-scripts-design.md /tmp/redact-check/ && bash /home/deck/tortoise-wow-server-V2/src/turtle-ops/scripts/audit-public-safe.sh /tmp/redact-check'
```

Expected: `OK: /tmp/redact-check is public-safe`. If it reports a hit, fix and re-run — do not
proceed while the gate is red.

- [ ] **Step 4: Commit in the private repo**

```bash
git -C D:/TurtleWow add WINDOWS-SETUP-HANDOFF.md docs/superpowers/plans/2026-08-10-server-lifecycle-scripts.md docs/superpowers/specs/2026-08-10-server-lifecycle-scripts-design.md
git -C D:/TurtleWow diff --cached --stat
git -C D:/TurtleWow commit -m "docs: redact network addresses ahead of the public fork migration"
```

---

### Task 3: Migrate `scripts/` and `tests/`

The largest and most path-sensitive move: 33 scripts and 22 test files. They currently assume
a repo root of `D:\TurtleWow` and a server root of `/home/deck/tortoise-wow-server-V2`. In the
fork, the repo root becomes `<fork>/turtle-ops/` while the server root is unchanged — and the
fork checkout now sits *inside* the server root.

**Files:**
- Create: `turtle-ops/scripts/**` (from `D:\TurtleWow\scripts\`, all 33)
- Create: `turtle-ops/tests/**` (from `D:\TurtleWow\tests\`, 22 minus `TurtleClient.Common.Tests.ps1`)

**Interfaces:**
- Consumes: `audit-public-safe.sh` from Task 1.
- Produces: a runnable `turtle-ops/tests/` suite. Later tasks reference
  `turtle-ops/scripts/lib/provenance.sh` and `turtle-ops/scripts/ship-cpp-fix.sh`.

- [ ] **Step 1: Copy, excluding the switcher test**

Write to `<scratchpad>/t3-copy.sh`:

```bash
#!/bin/bash
set -euo pipefail
SRC=/mnt/d/TurtleWow
DST=/home/deck/tortoise-wow-server-V2/src/turtle-ops

mkdir -p "$DST/scripts" "$DST/tests"
cp -r "$SRC/scripts/." "$DST/scripts/"
cp -r "$SRC/tests/."   "$DST/tests/"

# Excluded by owner instruction: the friend-server client switcher.
rm -f "$DST/tests/TurtleClient.Common.Tests.ps1"

# CRLF makes every .sh unrunnable under WSL.
find "$DST/scripts" "$DST/tests" -type f -name '*.sh' -exec sed -i 's/\r$//' {} +
find "$DST/scripts" "$DST/tests" -type f -name '*.sh' -exec chmod +x {} +

echo "=== counts ==="
echo "scripts: $(find "$DST/scripts" -type f | wc -l)"
echo "tests:   $(find "$DST/tests" -type f | wc -l)"
echo "=== switcher test must be absent ==="
ls "$DST/tests/TurtleClient.Common.Tests.ps1" 2>/dev/null && echo "FAIL: still present" || echo "OK: absent"
```

Run it. Expected: `scripts: 33`, `tests: 21`, `OK: absent`.

- [ ] **Step 2: Find every hardcoded path that assumed the private repo**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src/turtle-ops && grep -rn "TurtleWow\|/mnt/d\|D:/\|D:\\\\" scripts tests | grep -v "tortoise-wow-server" | head -40'
```

Every hit is a path that must become repo-relative. The established pattern in these scripts
is `ROOT="$(cd "$(dirname "$0")/.." && pwd)"` — use it rather than inventing a new one.

- [ ] **Step 3: Fix the paths with the Edit tool**

For each hit from Step 2, replace the absolute private-repo path with a `$ROOT`-relative one.
Do **not** change references to `/home/deck/tortoise-wow-server-V2` — the server root is
genuinely unchanged and correct.

- [ ] **Step 4: Run the migrated test suite**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src/turtle-ops && for t in tests/*-test.sh; do echo "--- $t"; bash "$t" >/dev/null 2>&1 && echo "PASS" || echo "FAIL rc=$?"; done'
```

Expected: the provenance suites (`provenance-lib-test.sh`, `dockerfile-provenance-test.sh`,
`verify-running-commit-test.sh`, `ship-cpp-fix-test.sh` — 97 checks between them) and the WSG
suites all PASS. Any FAIL is a path fix missed in Step 3 — fix it and re-run.

Note `docs-link-audit.sh` will pass vacuously; it shells out to `rg`, which WSL does not have.
Task 6 Step 4 addresses that.

- [ ] **Step 5: Run the audit gate, then commit**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && bash turtle-ops/scripts/audit-public-safe.sh turtle-ops && git add turtle-ops/scripts turtle-ops/tests && git diff --cached --stat && git commit -m "turtle-ops: migrate the operational scripts and their test suites"'
```

The commit must not run if the audit exits 1 — `&&` chaining enforces that.

---

### Task 4: Migrate `deploy/`

**Files:**
- Create: `turtle-ops/deploy/**` (9 files: `Dockerfile`, `docker-compose.yml`, `.dockerignore`,
  `README.md`, `etc/{ahbot,aiplayerbot,mangosd,realmd}.conf.template`)

**Interfaces:**
- Consumes: the gate from Task 1, and the corrected `.dockerignore` from Task 1B — copy it
  **after** that task, so the mirror carries the `src/turtle-ops` exclusion.
- Produces: `turtle-ops/deploy/` — the reference deployment. Task 7's README points at it.

- [ ] **Step 1: Copy**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'mkdir -p /home/deck/tortoise-wow-server-V2/src/turtle-ops/deploy && cp -r /mnt/d/TurtleWow/deploy/. /home/deck/tortoise-wow-server-V2/src/turtle-ops/deploy/ && find /home/deck/tortoise-wow-server-V2/src/turtle-ops/deploy -type f | sort'
```

Expected: 9 files including the four `.template` configs.

- [ ] **Step 1b: Confirm the copied `.dockerignore` carries the Task 1B exclusion**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'grep -n "src/turtle-ops" /home/deck/tortoise-wow-server-V2/src/turtle-ops/deploy/.dockerignore || echo "MISSING - re-copy after Task 1B"'
```

Expected: the exclusion line. If missing, Task 1B has not run — do it first.

- [ ] **Step 2: Confirm the redaction placeholder survived the copy**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'grep -rc "__DB_PASS__" /home/deck/tortoise-wow-server-V2/src/turtle-ops/deploy/etc/ | grep -v ":0$"'
```

Expected: at least `realmd.conf.template:1`. If any template contains a real password instead
of the placeholder, **stop** — re-run `stage-deploy.sh` in the private repo first.

- [ ] **Step 3: Update `deploy/README.md` for its new home**

The copied README says "The C++ source is not in this repo." Inside the fork that is now
false. Replace that paragraph with:

```markdown
**Everything in this directory is a one-way snapshot. Editing it changes nothing.**

The files that actually build and run the server live at the stack root, outside this
repository:

| Operative file | This directory's copy |
|---|---|
| `/home/deck/tortoise-wow-server-V2/Dockerfile` | `Dockerfile` |
| `/home/deck/tortoise-wow-server-V2/docker-compose.yml` | `docker-compose.yml` |
| `/home/deck/tortoise-wow-server-V2/.dockerignore` | `.dockerignore` |
| `/home/deck/tortoise-wow-server-V2/etc/*.conf` | `etc/*.conf.template` |

[`../scripts/stage-deploy.sh`](../scripts/stage-deploy.sh) copies live → here, redacting the
DB password to `__DB_PASS__` and failing closed if the redaction does not take. **There is no
repo → live direction.** To change what builds, edit the stack-root file and re-run
`stage-deploy.sh` to refresh the snapshot.

This directory does sit alongside the C++ source it describes — the Dockerfile's build
context is the stack root, and it compiles the tree this repository is checked out into at
`src/`.
```

This replaces the copied README's "The C++ source is not in this repo" paragraph, which is true
in the private repo and false here.

**Do not leave the live-vs-mirror statement in only one place while a differently-scoped section
says something narrower.** The copied README has a "Direction of travel" section that talks
about *configs* being snapshots; widen it to cover the Dockerfile and compose file too, or fold
it into the block above. A reader looking for "can I edit the Dockerfile here" will consult that
section, and if it only mentions configs they will conclude the Dockerfile is different.

- [ ] **Step 4: Audit and commit**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && bash turtle-ops/scripts/audit-public-safe.sh turtle-ops && git add turtle-ops/deploy && git diff --cached --stat && git commit -m "turtle-ops: migrate the deployment layer with redacted configs"'
```

---

### Task 5: Migrate `command-reference/`

A Python tool that parses the C++ source to generate command documentation. It belongs in the
fork more than anywhere else — it reads the very tree it now ships with, so its source paths
get *shorter*, not longer.

**Files:**
- Create: `turtle-ops/command-reference/**` (21 files: `tools/`, `tests/`, `data/`,
  `COMMANDS.md`, `BOT-COMMANDS.md`, `README.md`, `pytest.ini`, `requirements.txt`)

**Interfaces:**
- Consumes: the gate from Task 1.
- Produces: a pytest suite runnable from inside the fork.

- [ ] **Step 1: Copy**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'mkdir -p /home/deck/tortoise-wow-server-V2/src/turtle-ops/command-reference && cp -r /mnt/d/TurtleWow/command-reference/. /home/deck/tortoise-wow-server-V2/src/turtle-ops/command-reference/ && find /home/deck/tortoise-wow-server-V2/src/turtle-ops/command-reference -type f -not -path "*/__pycache__/*" | wc -l'
```

Expected: 21.

- [ ] **Step 2: Repoint its source path at the local tree**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src/turtle-ops/command-reference && grep -rn "tortoise-wow-server-V2\|/mnt/d\|TurtleWow\|src/game\|src/modules" tools/sources.py tools/extract_source.py 2>/dev/null | head -20'
```

Whatever absolute or cross-repo path it uses to reach the C++ must become relative to the
repository root — from `turtle-ops/command-reference/`, that is `../../`. Edit `tools/sources.py`
accordingly.

- [ ] **Step 3: Run its test suite**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src/turtle-ops/command-reference && (python3 -m pytest -q 2>&1 || echo "PYTEST_RC=$?") | tail -20'
```

Expected: all tests pass. If `pytest` is not installed in WSL, record that in the report and
note it in Task 7's README as a prerequisite (`pip install -r requirements.txt`) rather than
treating it as a migration failure — the code moved correctly either way.

- [ ] **Step 4: Audit and commit**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && bash turtle-ops/scripts/audit-public-safe.sh turtle-ops && git add turtle-ops/command-reference && git diff --cached --stat && git commit -m "turtle-ops: migrate the command reference generator"'
```

---

### Task 6: Migrate the docs and the open-work handoffs

This is the "ideas still to be worked on" payload — the handoffs are what let a fork agent pick
up an unfinished workstream.

**Files:**
- Create: `turtle-ops/docs/**` — all of `D:\TurtleWow\docs\` **except** the two client-switcher
  documents and `docs/interupt/`
- Create: `turtle-ops/handoffs/**` — the 12 root-level `*-HANDOFF.md` / `*-REPORT.md` files

**Interfaces:**
- Consumes: the redacted documents from Task 2, the gate from Task 1.
- Produces: `turtle-ops/docs/`, `turtle-ops/handoffs/`. Task 7's README indexes them.

- [ ] **Step 1: Copy with exclusions**

Write to `<scratchpad>/t6-copy.sh`:

```bash
#!/bin/bash
set -euo pipefail
SRC=/mnt/d/TurtleWow
DST=/home/deck/tortoise-wow-server-V2/src/turtle-ops

mkdir -p "$DST/docs" "$DST/handoffs"
cp -r "$SRC/docs/." "$DST/docs/"

# Owner instruction: the friend-server switcher does not go public.
rm -f "$DST/docs/superpowers/plans/2026-08-09-friend-local-client-switcher.md"
rm -f "$DST/docs/superpowers/specs/2026-08-09-friend-local-client-switcher-design.md"
# A Claude Code session transcript, not project documentation.
rm -rf "$DST/docs/interupt"

for f in BOT-PROGRESSION-HANDOFF.md BOTS-LOG-GROWTH-HANDOFF.md CPP-SOURCE-HANDOFF.md \
         PLAYERBOT-AI-HANDOFF.md PLAYERBOT-COMMAND-FIX-HANDOFF.md WINDOWS-SETUP-HANDOFF.md \
         WSG-BOT-QUEUE-CONTROL-HANDOFF.md WSG-DETERMINISTIC-KICKOFF-HANDOFF.md \
         WSG-FIRST-RUN-DEBUG-HANDOFF.md WSG-MATCH-FIXES-HANDOFF.md WSG-MATCH-REPORT.md \
         WSG-ROSTER-RECOVERY-HANDOFF.md; do
    cp "$SRC/$f" "$DST/handoffs/$f"
done

echo "=== excluded files must be absent ==="
for f in "$DST/docs/superpowers/plans/2026-08-09-friend-local-client-switcher.md" \
         "$DST/docs/superpowers/specs/2026-08-09-friend-local-client-switcher-design.md" \
         "$DST/docs/interupt"; do
    [ -e "$f" ] && { echo "FAIL: $f present"; exit 1; } || echo "OK absent: $(basename "$f")"
done
echo "docs:     $(find "$DST/docs" -type f | wc -l)"
echo "handoffs: $(find "$DST/handoffs" -type f | wc -l)"
```

Run it. Expected: three `OK absent` lines and `handoffs: 12`.

- [ ] **Step 2: Fix the cross-references the move broke**

Handoffs reference sibling files by their old root-relative paths (e.g.
`` [`docs/CPP-SOURCE-WORKFLOW.md`](docs/CPP-SOURCE-WORKFLOW.md) ``). From
`turtle-ops/handoffs/` that path is now `../docs/CPP-SOURCE-WORKFLOW.md`.

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src/turtle-ops && grep -rno "](docs/[^)]*)\|](scripts/[^)]*)\|](deploy/[^)]*)\|](tests/[^)]*)" handoffs/ | head -40'
```

Rewrite each with the Edit tool to be correct from `handoffs/` (prefix `../`).

- [ ] **Step 3: Add the migration note to `CPP-SOURCE-HANDOFF.md`**

Its §1 table says the ops repo is `D:\TurtleWow` and its do-nots say "Put anything secret in
the C++ repo". Both still hold, but the reader is now *inside* the C++ repo. Add at the top of
the copied `turtle-ops/handoffs/CPP-SOURCE-HANDOFF.md`:

```markdown
> **You are reading this inside the public fork.** The operational tooling it describes now
> lives beside you under `turtle-ops/`. The private ops repo still exists and still holds the
> client switcher scripts and the unredacted configs — nothing secret was carried across.
> Before pushing anything here, run `turtle-ops/scripts/audit-public-safe.sh`.
```

- [ ] **Step 4: Fix the link auditor so it actually checks something**

`tests/docs-link-audit.sh` shells out to `rg`, which WSL lacks, so it exits 0 without checking.
In `turtle-ops/tests/docs-link-audit.sh`, replace the `rg` invocation with `grep -r`, and make a
missing binary fatal rather than silent:

```bash
command -v grep >/dev/null || { echo "FATAL: grep not found — refusing to pass vacuously"; exit 2; }
```

Then run it and fix any dead links it now genuinely finds:

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src/turtle-ops && bash tests/docs-link-audit.sh; echo "rc=$?"'
```

Expected: `rc=0` with real checking, or a list of genuinely dead links to fix.

- [ ] **Step 5: Audit and commit**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && bash turtle-ops/scripts/audit-public-safe.sh turtle-ops && git add turtle-ops/docs turtle-ops/handoffs turtle-ops/tests && git diff --cached --stat && git commit -m "turtle-ops: migrate documentation and the open-work handoffs"'
```

---

### Task 7: Write the fork agent's entry point

Everything above is inert without a door into it. This is the file an agent reads first.

**Files:**
- Create: `turtle-ops/README.md`
- Modify: the fork's root `README.md` — add one pointer line

**Interfaces:**
- Consumes: everything from Tasks 1–6.
- Produces: the briefing document that is this plan's actual deliverable.

- [ ] **Step 1: Write `turtle-ops/README.md`**

```markdown
# turtle-ops

Everything needed to build, run, change and verify this server, carried over from the private
ops repo on 2026-08-11. Upstream (`Shyalya/tortoise-wow`) has no directory of this name, so
nothing here can ever conflict with an upstream rebase.

## Read this first if you are an agent working here

**This repository is PUBLIC.** Before every push:

    turtle-ops/scripts/audit-public-safe.sh

Exit 0 means clean. It blocks private-network addresses and hostnames, GM credential
assignments, private key material, and the client switcher scripts by name. It is fail-closed
and it is not optional.

Write documentation here in those general terms. The gate matches on the literal names of the
things it blocks, so spelling them out in prose trips it — the gate excludes only itself and
its own test.

Two things are deliberately **not** here, and must not be added:

1. The client switcher scripts that repoint a local client at a friend's server. They carry
   GM credentials and private network addresses.
2. Unredacted server configs. `deploy/etc/*.conf.template` carry `__DB_PASS__`; the real
   password lives in `.dbpass` on the server, gitignored, read at runtime.

The private repo's *history* was never merged here — files were copied and committed fresh —
because that history still contains credentials.

## Layout

| Directory | What |
|---|---|
| `scripts/` | Server operation: shipping a C++ fix, WSG match control, bot progression telemetry, AH bot setup |
| `tests/` | Shell suites for the above. Docker is driven through a file-backed stub, so the safety guards are provable in seconds without touching the server |
| `deploy/` | Dockerfile, compose file, redacted config templates |
| `command-reference/` | Python tool that parses this tree's C++ into command documentation |
| `docs/` | Workflows, designs, plans, evidence |
| `handoffs/` | State of each workstream — **start here for open work** |

## How this repository relates to the running server

This repository is checked out **inside** the stack directory, not beside it:

    /home/deck/tortoise-wow-server-V2/        <- stack root; the Docker build context
    ├── Dockerfile                            <- LIVE. Builds the tree below.
    ├── docker-compose.yml                    <- LIVE. No `build:` section; runs a prebuilt image.
    ├── .dockerignore                         <- LIVE. Excludes src/.git and src/turtle-ops.
    ├── .dbpass  .env                         <- secrets; gitignored, never in a layer
    ├── etc/  data/  logs/                    <- bind-mounted at runtime
    └── src/                                  <- THIS REPOSITORY
        ├── src/  sql/  cmake/  …             <- the C++ server (upstream's tree)
        └── turtle-ops/                       <- this toolkit

**The files under `deploy/` are a mirror, not the live ones.** `scripts/stage-deploy.sh`
copies live → repo, redacting the DB password on the way. There is no repo → live direction:
editing `deploy/Dockerfile` changes nothing about what builds. To change the build, edit
`/home/deck/tortoise-wow-server-V2/Dockerfile`, then re-run `stage-deploy.sh` to bring the
redacted copy back here.

## Building

    turtle-ops/scripts/ship-cpp-fix.sh

That is the whole loop: refuse a dirty tree → build with the commit stamped into the image →
restart → assert the world volume is the same one → verify → push. A full compile is
**~27 minutes**. Read-only verdict at any time:

    turtle-ops/scripts/verify-running-commit.sh   # exit 0 MATCH, 1 DRIFT, 2 UNKNOWN

Full workflow, including rollback: [`docs/CPP-SOURCE-WORKFLOW.md`](docs/CPP-SOURCE-WORKFLOW.md).

**`turtle-ops/` is excluded from the build context on purpose.** `Dockerfile` does
`COPY src /src`, and that layer gates the 27-minute compile — so without the `src/turtle-ops`
line in `.dockerignore`, editing this README would force a full recompile. If a trivial doc
change ever triggers a long build, that exclusion is the first thing to check:

    turtle-ops/tests/dockerignore-turtle-ops-test.sh

Two build settings that are not defaults and matter: `BUILD_PLAYERBOTS` must be ON (it
defaults OFF and gives no warning — bots are the entire point here), and the Dockerfile uses
`cmake --build -j2`, **not** `-j$(nproc)`, because the Deck runs out of memory above that.

## Open work

| Workstream | Start at | State |
|---|---|---|
| **Upstream sync** | [`docs/superpowers/plans/2026-08-11-upstream-sync.md`](docs/superpowers/plans/2026-08-11-upstream-sync.md) | Analysed, not executed. `local` is 2 commits on a base 60 behind upstream. The rebase is proven conflict-free; the plan also fixes a config drift and a `FreeBGJoinAction` cap leak |
| **WSG 10v10** | [`handoffs/WSG-MATCH-FIXES-HANDOFF.md`](handoffs/WSG-MATCH-FIXES-HANDOFF.md), [`handoffs/WSG-DETERMINISTIC-KICKOFF-HANDOFF.md`](handoffs/WSG-DETERMINISTIC-KICKOFF-HANDOFF.md) | Forced-match control |
| **Bot progression** | [`handoffs/BOT-PROGRESSION-HANDOFF.md`](handoffs/BOT-PROGRESSION-HANDOFF.md) | Bots stall at ~14 |
| **Bot log growth** | [`handoffs/BOTS-LOG-GROWTH-HANDOFF.md`](handoffs/BOTS-LOG-GROWTH-HANDOFF.md) | Upstream's `90419a0` ships the fix; the live conf overrides it |
| **Playerbot AI** | [`handoffs/PLAYERBOT-AI-HANDOFF.md`](handoffs/PLAYERBOT-AI-HANDOFF.md) | |

## Traps that have cost real time

- **`docker compose down -v` destroys the world.** It has been lost once.
- **A published port answers before the process does.** `docker-proxy` binds on container
  create, so `nc -z` succeeds with nothing listening. Readiness must assert `.State.Running`
  and re-assert after a settle — mangosd's world load is ~60s.
- **`docker compose up -d` recreates a missing named volume, empty.** Checking that the volume
  *exists* proves nothing; compare `docker volume inspect --format '{{.CreatedAt}}'`.
- **`rg` is not installed in WSL.** A script that shells out to it exits 0 having checked
  nothing.
- **Shell variables and `$?` do not survive `wsl.exe -- bash -lc '...'`.** Write a script file
  and run it.
- **The `ARG`/`LABEL` block must stay in the Dockerfile's runtime stage.** In the build stage
  it invalidates the ~27-minute compile layer on every commit.
```

- [ ] **Step 2: Add the pointer to the fork's root README**

Insert near the top of the fork's root `README.md`, after the project description:

```markdown
> **Fork note:** this fork carries local server fixes plus an operations toolkit under
> [`turtle-ops/`](turtle-ops/README.md) — build, deploy, test and match-control scripts, and
> the state of each open workstream.
```

Keep the edit to that one block; the rest of the README is upstream's and should stay
rebase-clean.

- [ ] **Step 3: Verify every link in the new README resolves**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'tr -d "\r" < /mnt/c/Users/mihov/AppData/Local/Temp/claude/D--TurtleWow/2dfcf21b-8058-4bf6-a38f-407fe762b8d5/scratchpad/t7-links.sh > /tmp/t7.sh && bash /tmp/t7.sh'
```

with `<scratchpad>/t7-links.sh`:

```bash
#!/bin/bash
cd /home/deck/tortoise-wow-server-V2/src/turtle-ops || exit 9
rc=0
grep -o '](\([^)h][^)]*\))' README.md | sed 's/](\(.*\))/\1/' | while read -r link; do
    target="${link%%#*}"
    [ -z "$target" ] && continue
    if [ -e "$target" ]; then echo "ok   $target"; else echo "DEAD $target"; rc=1; fi
done
exit $rc
```

Expected: every line `ok`. Any `DEAD` is a broken path — fix before committing.

- [ ] **Step 4: Audit and commit**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && bash turtle-ops/scripts/audit-public-safe.sh turtle-ops && git add turtle-ops/README.md README.md && git diff --cached --stat && git commit -m "turtle-ops: add the entry point for agents working in this fork"'
```

---

### Task 8: Final audit and push

**Files:**
- No new files — this is the gate and the publication.

**Interfaces:**
- Consumes: every commit from Tasks 1–7.

- [ ] **Step 1: Audit the entire staged tree, not just `turtle-ops/`**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && bash turtle-ops/scripts/audit-public-safe.sh . ; echo "audit rc=$?"'
```

Expected: `audit rc=0`. Scanning `.` rather than `turtle-ops` catches anything that landed
outside the intended directory.

- [ ] **Step 2: Confirm nothing landed in an upstream-owned path**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git diff --name-only upstream/playerbots-integration-gh..local | grep -v "^turtle-ops/" | grep -v "^README.md$"'
```

Expected: exactly three files — `src/game/ObjectMgr.cpp`,
`src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp`,
`src/modules/PlayerBots/playerbot/strategy/actions/BattleGroundJoinAction.cpp`. Anything else
means content leaked into a path upstream owns, which will conflict on the next rebase.

- [ ] **Step 3: Confirm no foreign history was grafted**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git remote -v && echo "--- commits on local not from upstream ---" && git log --oneline upstream/playerbots-integration-gh..local | wc -l'
```

Expected: remotes are exactly `origin`, `upstream`, `backup` — **no `turtle-tournament`
remote**. The commit count is 2 (the original C++ fixes) plus one per migration task.

- [ ] **Step 4: Push**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git push origin local'
```

This is a fast-forward — nothing is rewritten, so no `--force` and no risk to `c06b2fb`.

- [ ] **Step 5: Verify what the public now sees**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /home/deck/tortoise-wow-server-V2/src && git ls-remote origin refs/heads/local && git log --oneline -1 origin/local'
```

Then open `https://github.com/ChrisMiho/tortoise-wow/tree/local/turtle-ops` and confirm the
directory renders with the README.

- [ ] **Step 6: Commit this plan in the private repo**

```bash
git -C D:/TurtleWow add docs/superpowers/plans/2026-08-11-fork-migration.md
git -C D:/TurtleWow diff --cached --stat
git -C D:/TurtleWow commit -m "docs: plan for migrating the ops toolkit into the public fork"
git -C D:/TurtleWow push origin proper-setup
```

---

## Follow-up the owner must decide (not in scope here)

**Rotating the DB and GM credentials — optional, owner-deferred 2026-08-11.** The owner's
assessment is that these guard a LAN-only database on a private game server and are
low-consequence; the only value that must never be published is the friend's IP address, which
the audit gate's network scans cover and which this plan excludes at the file level.

Kept here as a note rather than a recommendation, because two facts are worth having on
record if the assessment is ever revisited:

- The live DB password is not in upstream's tree or history. Upstream ships only the stock
  `mangos;mangos` default, so the shape is public but this value is not.
- Task 1's review rounds found it in four places across three files, in three shapes — one of
  which (`mariadb -uroot -p<value>`) carries no `password` keyword and was invisible to every
  version of the gate until specifically hunted. A regex only catches shapes someone
  anticipated. If these credentials ever do become consequential, rotation is the cheap fix,
  because it does not depend on having found every occurrence.

The GM password in `552ddd4`'s history is unchanged by any of this: that repo stays private,
and history rewriting would be the only full fix.

**Decide what the private repo is for now.** After this migration it holds three things the
fork does not: the client switcher scripts, the unredacted configs, and the tailnet addresses.
Everything else is duplicated. Options are to thin it to just those, or leave it as the
private mirror. Do not delete anything until the fork has been verified in use.
