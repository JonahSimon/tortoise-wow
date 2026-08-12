# Ops Scripts Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the private ops repo's `scripts/` and `tests/` trees — already copied into this
checkout as `OldScripts/` and `oldTests/` — into `local`'s `scripts/` and `tests/`, safely, with
every private-repo-specific path fixed and the one leaked secret excluded permanently.

**Architecture:** `fork-migration/2026-08-11-fork-migration.md` planned this same move for a
different environment (a WSL checkout nested inside a stack root, landing everything under a new
`turtle-ops/` namespace to guarantee no upstream conflict). That environment doesn't apply here.
This checkout is `D:\CodingProjects\tortoise-wow\tortoise-wow` (Windows), upstream owns nothing at
`scripts/` or `tests/` at the root already (verified below), and `scripts/rebuild.sh` already
established `scripts/` as the fork-owned convention. So this plan lands the migrated files
directly in `scripts/` and `tests/` at repo root — no new namespace — and ports only the
audit-gate concept from the old plan, not its directory layout.

**Tech Stack:** git (Windows), bash test suites (run through WSL, where Docker lives, same as
`scripts/rebuild.sh`). `scripts/lib/` does not exist in `local` yet; `OldScripts/lib/` is new
content this plan adds.

## Global Constraints

- **`ChrisMiho/tortoise-wow` is PUBLIC.** Nothing secret may land in it, in any commit, ever.
- **`oldTests/TurtleClient.Common.Tests.ps1` is excluded from this migration regardless of
  content — not just because of the secret below.** The private repo's owner ruled the whole
  client-switcher workstream (the tooling that points a local client at a friend's server) stays
  off the public fork. Redacting the file does not change that; it stays excluded.
- **The file also carried a real secret, now redacted at the source.** Verified 2026-08-11: line
  34 was `$script:FriendServer | Should -Be "<REDACTED-FRIEND-SERVER-IP>"` — a real CGNAT
  (100.64.0.0/10) address belonging to a third party's home server, the exact value the owner
  ruled must never be published. **The literal is deliberately not reproduced in this document.**
  This plan lives in the public fork, so quoting the value here to describe the leak would publish
  it just as surely as the file would have. Redacted in place (both the main checkout's
  `oldTests/` and the `ops-migration` worktree's copy) to
  `<REDACTED-FRIEND-SERVER-IP>`, so the plaintext value no longer exists in either raw dump — pure
  hygiene, since the file was never going to be copied into `tests/` either way. The file was
  untracked in the main checkout before this edit (`git status` confirmed `?? OldScripts/` /
  `?? oldTests/`, and `git log --all -- oldTests/ OldScripts` returned nothing) — nothing had
  leaked to git or any remote. Do not `git add` this file. Do not copy it into `tests/`.
- **No other credential material was found**, but that is a pattern-match result, not a proof.
  `grep -rliE` across all of `OldScripts`/`oldTests` for GM/admin password assignments,
  `-p<value>` MySQL flags, private key headers, and CGNAT/`.ts.net`/`tailscale` references
  turned up only the one file above (plus harmless false positives: `--profile`, `--reprint-sec`,
  doc-comment mentions of "password" in argument-parsing code). The DB password itself lives in
  `.dbpass` on the live server and was not available to check literally in this session — Task 1's
  gate is defence in depth, not proof of absence.
- **This plan covers `scripts/` and `tests/` only.** `deploy/`, `command-reference/`, `docs/`,
  and the 12 root `*-HANDOFF.md` files from `fork-migration.md`'s Tasks 4–6 were not handed over
  in this session and are out of scope here.
- **`local` is the branch, not `cm-main`.** `docs/superpowers/plans/2026-08-11-branch-topology-cleanup.md`
  Task 5 (rename to `cm-main`, set default branch) has not run yet as of this plan. Target
  whichever branch is checked out when this plan executes; do not assume the rename happened.
- **Run all git for this repo from Windows.** Run test suites needing Docker from WSL, same
  path-translation rules as the other plans in this repo:
  `wsl.exe -d Ubuntu -u deck -- bash -lc '<command>'`, and never put a `$VAR` or `$?` inside the
  quoted string if the caller needs its value — write scripts to a file and run the file.
- Repo root (Windows): `D:\CodingProjects\tortoise-wow\tortoise-wow`. Same tree from WSL:
  `/mnt/d/CodingProjects/tortoise-wow/tortoise-wow`.
- This plan executes inside the git worktree at `.worktrees/ops-migration` (branch
  `ops-migration`, branched from `local` at `99078f0`), not the main checkout. All commit and
  push commands below target that worktree.

## File Structure

| Path | From | Task |
|---|---|---|
| `scripts/**` (new: 33 files) | `OldScripts/**` (in the worktree, copied from the main checkout's untracked dump) | 2 |
| `tests/**` (new: 20 files, `TurtleClient.Common.Tests.ps1` excluded) | `oldTests/**` minus the switcher test | 2 |
| `scripts/audit-public-safe.sh` | **new** — the secret gate, adapted from `fork-migration/2026-08-11-fork-migration.md` Task 1 | 1 |
| `tests/audit-public-safe-test.sh` | **new** | 1 |

Existing `scripts/rebuild.sh` is untouched and unrelated to this migration — it builds the C++
server; the migrated scripts operate the running stack (WSG match control, bot progression,
provenance/shipping tooling for the older single-service deploy).

---

### Task 1: The secret-audit gate

Build this before copying anything else in. Adapted from `fork-migration/2026-08-11-fork-migration.md`'s
`audit-public-safe.sh` — same detection logic (CGNAT range, `.ts.net`/tailscale mentions, GM
credential assignments, private key material, the switcher scripts by name), scanning this
repo's `scripts/`/`tests/` instead of a `turtle-ops/` subtree.

**Files:**
- Create: `scripts/audit-public-safe.sh`
- Create: `tests/audit-public-safe-test.sh`

**Interfaces:**
- Produces: `audit-public-safe.sh [PATH]` — scans `PATH` (default: repo root). Exit **0** clean,
  exit **1** secret found. Task 4 runs it before every commit.

- [ ] **Step 1: Write the failing test**

Create `tests/audit-public-safe-test.sh`:

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
check "bare CGNAT addr, no prose"    1 'echo "100.71.4.203" > a.md'
check "ts.net hostname"              1 'echo "host: deck.tail1a2b.ts.net" > a.md'
check "literal tailscale mention"    1 'echo "use the tailscale ip" > a.md'
check "unredacted DB pass marker"    0 'echo "LoginDatabaseInfo = \"db;3306;mangos;__DB_PASS__;tw_logon\"" > a.conf'
check "GM password env leak"         1 'echo "GMPASS=hunter2" > a.sh'
check "private key block"            1 'printf -- "-----BEGIN RSA PRIVATE KEY-----\n" > id.pem'
check "switcher script by name"      1 'echo x > Setup-TurtleClient.ps1'
check "switcher test by name"        1 'echo x > TurtleClient.Common.Tests.ps1'
check "nested dir is scanned"        1 'mkdir -p deep/er && echo "100.72.0.1" > deep/er/a.md'
check "non-CGNAT 100.x is allowed"   0 'echo "100.5.0.1 is a public address" > a.md'

# The gate's own source contains every pattern it blocks. If it does not exclude
# itself it flags itself, and no commit can ever pass.
if bash "$AUDIT" "$(dirname "$AUDIT")" >/dev/null 2>&1; then
    PASS=$((PASS+1)); echo "ok   - gate does not flag its own source"
else
    FAIL=$((FAIL+1)); echo "FAIL - gate flags its own source (add --exclude for itself)"
fi

echo ""
echo "passed: $PASS  failed: $FAIL"
[ "$FAIL" = 0 ]
```

The `non-CGNAT 100.x` case matters: `100.5.0.1` is ordinary public address space. Blocking all of
`100.*` would false-positive forever; only `100.64.0.0/10` is the private range.

Every address in these fixtures is **synthetic**. An earlier draft pinned one case to the literal
value found in this session, which was a mistake: this plan is published to the public fork, so a
fixture asserting "the gate catches *this exact IP*" leaks the IP to everyone who reads the test.
Nothing is lost by using synthetic values, because the gate matches the CGNAT **range**, not a
literal — it cannot catch `100.64.12.9` and miss any other in-range address. The two in-range
cases differ by shape instead (embedded in prose vs. bare on its own line), which is the property
actually worth testing.

- [ ] **Step 2: Run it to verify it fails**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration && bash tests/audit-public-safe-test.sh'
```

Expected: FAIL — `audit-public-safe.sh` does not exist yet.

- [ ] **Step 3: Write the gate**

Create `scripts/audit-public-safe.sh`:

```bash
#!/bin/bash
# Fail-closed audit: refuse to let anything secret reach the PUBLIC fork.
#
# Exit 0 = clean, 1 = something found. Run before every commit that touches
# migrated ops content.
#
# grep, not rg: rg is not guaranteed present in every shell this runs from, and a
# missing binary would make this exit 0 without checking anything.
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
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration && chmod +x scripts/audit-public-safe.sh && bash tests/audit-public-safe-test.sh'
```

Expected: `passed: 13  failed: 0`.

- [ ] **Step 5: Run the gate against the current worktree before copying anything else in**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration && bash scripts/audit-public-safe.sh .'
```

Expected: `OK: . is public-safe` — at this point only the gate itself is new, and it excludes
itself.

- [ ] **Step 6: Commit**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration add scripts/audit-public-safe.sh tests/audit-public-safe-test.sh
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration diff --cached --stat
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration commit -m "Add the fail-closed public-safety audit gate for migrated ops content"
```

---

### Task 2: Copy the scripts and tests in, excluding the leaked file

**Files:**
- Create: `scripts/**` (from `OldScripts/**`, 38 files incl. `lib/`, `bot-progression/`)
- Create: `tests/**` (from `oldTests/**`, 27 files minus `TurtleClient.Common.Tests.ps1`, incl.
  `fixtures/`)

**Interfaces:**
- Consumes: the gate from Task 1.
- Produces: a `scripts/` and `tests/` tree the audit gate has cleared. Task 3 fixes internal
  paths; Task 4 runs the suite.

- [ ] **Step 1: Copy, excluding the leaked file**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc '
set -euo pipefail
cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration
cp -r OldScripts/. scripts/
cp -r oldTests/. tests/

# Confirmed secret: line 34 assigns $script:FriendServer a real third-party
# home-server IP in the CGNAT range. Never let this reach git. The literal is
# not repeated here on purpose -- this plan is published.
rm -f tests/TurtleClient.Common.Tests.ps1

find scripts tests -type f -name "*.sh" -exec sed -i "s/\r$//" {} +
find scripts tests -type f -name "*.sh" -exec chmod +x {} +

echo "=== counts ==="
echo "scripts: $(find scripts -type f | wc -l)"
echo "tests:   $(find tests -type f | wc -l)"
echo "=== leaked file must be absent ==="
ls tests/TurtleClient.Common.Tests.ps1 2>/dev/null && echo "FAIL: still present" || echo "OK: absent"
'
```

Expected: `scripts: 38`, `tests: 26`, `OK: absent`.

- [ ] **Step 2: Run the audit gate against the freshly copied content**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration && bash scripts/audit-public-safe.sh .'
```

Expected: `OK: . is public-safe`. If this reports a hit, **stop** — it means something this
session's pattern search missed. Do not proceed to committing; read the finding, redact or
exclude, and re-run.

- [ ] **Step 3: Delete the now-redundant raw dumps from the worktree**

`OldScripts/`/`oldTests/` were the untracked staging copies; `scripts/`/`tests/` now hold the
same content, minus the excluded file. Keeping both around risks someone `git add -A`-ing the raw
dump later and reintroducing the leaked file.

```bash
rm -rf /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration/OldScripts
rm -rf /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration/oldTests
```

Leave the main checkout's `OldScripts/`/`oldTests/` alone for now — Task 5 addresses those.

- [ ] **Step 4: Commit**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration add scripts tests
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration diff --cached --stat
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration commit -m "Migrate the operational scripts and their test suites"
```

The commit must show 64 files added (38 + 26), and must **not** list `TurtleClient.Common.Tests.ps1`.
Check `git diff --cached --stat` before running the commit, not after.

---

### Task 3: Fix the paths that assumed the private repo

Eight files reference the old private-repo location (`D:\TurtleWow` / `/mnt/d/TurtleWow`).
Everything else already resolves relative to `$HOME` or the script's own directory (verified in
`scripts/lib/provenance.sh`, which is env-var-overridable and needs no change) and needs no fix.

**Files:**
- Modify: `scripts/patch-createbot-cache-dbg.sh:3` (comment only)
- Modify: `scripts/backup-alive-world-pre.sh:8,34`
- Modify: `scripts/ship-cpp-fix.sh:4` (comment only)
- Modify: `scripts/stage-deploy.sh:14`
- Modify: `scripts/verify-running-commit.sh:9` (comment only)
- Modify: `scripts/overnight-createbot-run.sh:6,21,25,46`
- Modify: `tests/playerbot-verify.sh:5-6` (comment only)
- Modify: `tests/createbot-cache-probe.sh:26`

**Interfaces:**
- Consumes: the migrated tree from Task 2.
- Produces: a `scripts/`/`tests/` tree with no remaining reference to the private repo's path.
  Task 4 verifies this with a grep, then runs the suite.

- [ ] **Step 1: Confirm the full list before editing**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration && grep -rn "TurtleWow\|/mnt/d\b" scripts tests'
```

Expected: exactly the 10 lines listed in the Files section above (across 8 files). If this finds
more, add them to this task before continuing — do not fix a subset and call it done.

- [ ] **Step 2: Fix `scripts/patch-createbot-cache-dbg.sh`**

Line 3 currently reads:

```
# NEVER edit D:\TurtleWow\extracted.
```

Replace with:

```
# NEVER edit scripts/../extracted (this repo's extracted/, not a build output).
```

If `extracted/` does not exist anywhere in this repo (check with
`find /mnt/d/CodingProjects/tortoise-wow/tortoise-wow -maxdepth 1 -name extracted` from WSL),
read the rest of the script first to confirm what path this comment actually warns about, and
word the replacement to name that path instead of guessing.

- [ ] **Step 3: Fix `scripts/backup-alive-world-pre.sh`**

Line 8:
```
WIN="/mnt/d/TurtleWow/backups/pre-alive-world-${STAMP}"
```
becomes:
```
WIN="/mnt/d/CodingProjects/tortoise-wow/tortoise-wow/backups/pre-alive-world-${STAMP}"
```

Line 34:
```
echo "WIN_BACKUP=D:/TurtleWow/backups/pre-alive-world-${STAMP}"
```
becomes:
```
echo "WIN_BACKUP=D:/CodingProjects/tortoise-wow/tortoise-wow/backups/pre-alive-world-${STAMP}"
```

`backups/` does not exist yet at repo root — check `.gitignore` for a `/backups/` entry (the old
plan anchored this exact rule so a bare `backups/` at any depth wouldn't swallow something else);
if absent, add `/backups/` to `.gitignore` in this step, since this script writes an unredacted
`.env` there and it must never be tracked.

- [ ] **Step 4: Fix `scripts/ship-cpp-fix.sh`**

Line 4:
```
# Run from WSL:  /mnt/d/TurtleWow/scripts/ship-cpp-fix.sh [flags]
```
becomes:
```
# Run from WSL:  /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/scripts/ship-cpp-fix.sh [flags]
```

- [ ] **Step 5: Fix `scripts/stage-deploy.sh`**

Line 14:
```
DEST="${TW_DEPLOY_DIR:-/mnt/d/TurtleWow/deploy}"
```
becomes:
```
DEST="${TW_DEPLOY_DIR:-/mnt/d/CodingProjects/tortoise-wow/tortoise-wow/deploy}"
```

Note there is no `deploy/` directory in this repo yet — `fork-migration/2026-08-11-fork-migration.md`
Task 4 covers migrating it and is out of scope here. This script will fail at runtime until that
directory exists; that is expected and does not block this task. The default is still worth
fixing now so it points at the right *eventual* location instead of a repo that may not exist
much longer.

- [ ] **Step 6: Fix `scripts/verify-running-commit.sh`**

Line 9:
```
# Run from WSL:  /mnt/d/TurtleWow/scripts/verify-running-commit.sh
```
becomes:
```
# Run from WSL:  /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/scripts/verify-running-commit.sh
```

- [ ] **Step 7: Fix `scripts/overnight-createbot-run.sh`**

Four lines, all `/mnt/d/TurtleWow/` → `/mnt/d/CodingProjects/tortoise-wow/tortoise-wow/`:

Line 6:
```
STATUS="/mnt/d/TurtleWow/docs/superpowers/plans/2026-08-09-createbot-overnight-status.md"
```
becomes:
```
STATUS="/mnt/d/CodingProjects/tortoise-wow/tortoise-wow/docs/superpowers/plans/2026-08-09-createbot-overnight-status.md"
```

This status file does not exist in this repo (it was never handed over). Leave the path
pointing at where it would live under `docs/superpowers/plans/` — creating the file is not part
of this task.

Line 21:
```
bash /mnt/d/TurtleWow/scripts/patch-createbot-cache-dbg.sh | tee -a "$STATUS"
```
becomes:
```
bash /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/scripts/patch-createbot-cache-dbg.sh | tee -a "$STATUS"
```

Line 25:
```
if bash /mnt/d/TurtleWow/scripts/rebuild-mangosd-lowj.sh; then
```
becomes:
```
if bash /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/scripts/rebuild-mangosd-lowj.sh; then
```

Line 46:
```
bash /mnt/d/TurtleWow/tests/createbot-cache-probe.sh | tee "$PROBE_OUT" | tee -a "$STATUS"
```
becomes:
```
bash /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/tests/createbot-cache-probe.sh | tee "$PROBE_OUT" | tee -a "$STATUS"
```

- [ ] **Step 8: Fix `tests/playerbot-verify.sh`**

Lines 5-6:
```
# Run from Windows:  MSYS_NO_PATHCONV=1 wsl -d Ubuntu -u deck -- bash /mnt/d/TurtleWow/tests/playerbot-verify.sh [player-name]
# Run inside WSL:    bash /mnt/d/TurtleWow/tests/playerbot-verify.sh [player-name]
```
become:
```
# Run from Windows:  MSYS_NO_PATHCONV=1 wsl -d Ubuntu -u deck -- bash /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/tests/playerbot-verify.sh [player-name]
# Run inside WSL:    bash /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/tests/playerbot-verify.sh [player-name]
```

- [ ] **Step 9: Fix `tests/createbot-cache-probe.sh`**

Line 26:
```
bash /mnt/d/TurtleWow/tests/playerbot-verify.sh Usagi | sed -n '1,80p'
```
becomes:
```
bash /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/tests/playerbot-verify.sh Usagi | sed -n '1,80p'
```

- [ ] **Step 10: Verify no reference to the old repo remains**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration && grep -rn "TurtleWow" scripts tests; echo "rc=$?"'
```

Expected: `rc=1` (grep found nothing).

- [ ] **Step 11: Re-run the audit gate, then commit**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration && bash scripts/audit-public-safe.sh . && git add scripts tests .gitignore && git diff --cached --stat && git commit -m "Point migrated scripts at this repo instead of the private one"'
```

The commit must not run if the audit exits 1 — the `&&` chain enforces that. If `.gitignore` was
not touched in Step 3, `git add .gitignore` is a harmless no-op addition of zero changes; drop it
from the command if `git status` shows it unmodified.

---

### Task 4: Run the migrated test suite

**Files:**
- No file changes — this task only runs what Tasks 1–3 produced.

**Interfaces:**
- Consumes: the path-fixed tree from Task 3.
- Produces: a pass/fail record. A failing suite here does not block committing (Task 3 already
  committed) but must be resolved, or explicitly triaged and recorded, before this branch merges
  back to `local`.

- [ ] **Step 1: Run every test script and record pass/fail**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc '
cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration
for t in tests/*-test.sh tests/audit-public-safe-test.sh; do
  echo "--- $t"
  bash "$t" >/tmp/out.$$ 2>&1 && echo "PASS" || { echo "FAIL rc=$?"; tail -20 /tmp/out.$$; }
  rm -f /tmp/out.$$
done
'
```

- [ ] **Step 2: Triage failures**

Expect some suites to fail here that did not fail in the old repo — they may depend on a running
Docker stack (`tw2-db`, `tw2-mangosd`) that this checkout has not started, or on `.dbpass` /
`etc/` files that live at `$HOME/tortoise-wow-server-V2` on the WSL side and are independent of
this migration. For each failure:

- If the failure is a missing live dependency (no `.dbpass`, no running containers) — expected,
  not a migration defect. Note it and move on.
- If the failure is a path error, a "file not found" for something under `scripts/` or `tests/`,
  or a assertion about repo-relative content — that is a Task 3 miss. Fix it, re-run Step 1 for
  that one file, and fold the fix into a new commit before proceeding.
- `tests/docs-link-audit.sh` may pass vacuously if it shells out to `rg`, which is not guaranteed
  present. Check with
  `wsl.exe -d Ubuntu -u deck -- bash -lc 'command -v rg'`. If absent, this is a pre-existing
  condition inherited from the source repo (documented in `fork-migration/2026-08-11-fork-migration.md`
  §6/Task 6 Step 4) — record it, do not treat it as this plan's defect to fix. Converting it to
  `grep -r` is optional follow-up, not required for this migration.

- [ ] **Step 3: Commit any path fixes found in triage**

Only if Step 2 found something:

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration add -A scripts tests
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration diff --cached --stat
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration commit -m "Fix test failures found after the migration"
```

---

### Task 5: Clean up the raw dumps in the main checkout

The main checkout still has untracked `OldScripts/`/`oldTests/` sitting at repo root — the
originals this plan copied from. They are redundant now that `scripts/`/`tests/` hold the same
content (minus the excluded file), and leaving them around is exactly the kind of loose end that
gets `git add -A`'d by accident later, reintroducing the leaked IP.

**Files:**
- Delete: `OldScripts/` (main checkout, untracked)
- Delete: `oldTests/` (main checkout, untracked)

**Interfaces:**
- Consumes: a green Task 4, so nothing is deleted before its content is confirmed captured.

- [ ] **Step 1: Confirm they are still untracked before deleting**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow status --short -- OldScripts oldTests
```

Expected: `?? OldScripts/` and `?? oldTests/`. If either shows a tracked (`A`/`M`) entry instead,
**stop** — something committed them in the main checkout outside this plan; investigate before
deleting.

- [ ] **Step 2: Delete**

```bash
rm -rf "D:/CodingProjects/tortoise-wow/tortoise-wow/OldScripts"
rm -rf "D:/CodingProjects/tortoise-wow/tortoise-wow/oldTests"
```

- [ ] **Step 3: Confirm the main checkout is clean**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow status --short
```

Expected: only `?? image.png` (pre-existing, unrelated).

---

### Task 6: Commit this plan and merge the worktree branch back

**Files:**
- Create: `docs/superpowers/plans/2026-08-11-ops-scripts-migration.md` (this file — already
  present in the worktree from the start of execution; commit it now that the work it describes
  is done)

**Interfaces:**
- Consumes: Tasks 1–5, all committed on `ops-migration`.
- Produces: `local` (or `cm-main`, whichever this checkout is on by the time this runs) updated
  with the full migration, worktree removed.

- [ ] **Step 1: Commit the plan document itself**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration add docs/superpowers/plans/2026-08-11-ops-scripts-migration.md
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration diff --cached --stat
git -C D:/CodingProjects/tortoise-wow/tortoise-wow/.worktrees/ops-migration commit -m "Document the ops scripts migration plan"
```

- [ ] **Step 2: Merge into the target branch**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow checkout local
git -C D:/CodingProjects/tortoise-wow/tortoise-wow merge ops-migration -m "Merge ops-migration: bring the ops scripts and tests into the fork"
```

If `local` has been renamed to `cm-main` by the time this runs (branch-topology-cleanup.md
Task 5), substitute `cm-main` for `local` in every command in this task.

- [ ] **Step 3: Run the audit gate one final time against the merged tree**

```bash
wsl.exe -d Ubuntu -u deck -- bash -lc 'cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow && bash scripts/audit-public-safe.sh .; echo "rc=$?"'
```

Expected: `rc=0`. This is the last check before anything reaches a remote.

- [ ] **Step 4: Push**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow push origin local
```

(Or `cm-main`, per Step 2's note.) This is a normal, non-forced push — nothing upstream of this
merge was rewritten.

- [ ] **Step 5: Remove the worktree**

```bash
git -C D:/CodingProjects/tortoise-wow/tortoise-wow worktree remove .worktrees/ops-migration
git -C D:/CodingProjects/tortoise-wow/tortoise-wow branch -d ops-migration
```

`-d` (not `-D`) refuses unless `ops-migration` is fully merged — a free check that Step 2 worked.

---

## Rollback summary

| If this fails | Do this |
|---|---|
| Audit gate finds something in Task 2 or 3 | Do not commit. Read the finding, redact or exclude the file, re-run the gate. |
| A test fails in Task 4 for a reason that isn't a live-dependency gap | Fix in the worktree, re-commit there — `local`/`cm-main` is untouched until Task 6. |
| The merge in Task 6 conflicts | `git merge --abort`. Nothing on `local` moved; investigate why upstream of this worktree changed. |
| You want the pre-migration state back | Nothing on `local`/`cm-main` was rewritten — `git reset --hard <pre-merge-sha>` if the merge commit itself needs undoing, or just don't merge `ops-migration` at all. |
