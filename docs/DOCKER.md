# Building and running the server in Docker

Everything below runs **inside WSL Ubuntu**. Docker Desktop's engine is shared,
but relative bind-mount paths only resolve correctly from the WSL side.

    cd /mnt/d/CodingProjects/tortoise-wow/tortoise-wow

## First-time setup

```bash
cp .env.example .env
# paste the password from /home/deck/tortoise-wow-server-V2/.dbpass into DB_PASS
```

`.env` holds the database password in plaintext. It is excluded from commits via
`.git/info/exclude` in this checkout — but that is **local only**. The matching
`.gitignore` line exists in the working tree and is **not yet committed**, so a
fresh clone of this repo does not inherit the protection. Commit it when the
concurrent `fork-migration` work in `.gitignore` has landed.

`.env` also points `TW_DATA`, `TW_ETC` and `TW_LOGS` at the existing stack
directory. That is deliberate: the extracted client data is several gigabytes and
the configs are tuned, so both are reused rather than duplicated.

`DB_PASS` reaches the database container as an environment variable, so anyone
who can run `docker inspect tw2-db` can read the root password in plaintext. The
verify command below also puts it on a command line inside the container, where
it is visible to `ps`. Both are normal for this kind of stack — but treat docker
access as equivalent to database access.

## Start / stop

```bash
docker compose up -d
docker compose down          # NEVER -v — see below
```

## Rebuild after a C++ change

Roughly 40 minutes. `scripts/rebuild.sh` builds to `tortoise-v2:candidate`, runs
its acceptance checks, and moves the `:local` tag ONLY if every one passes — so a
broken build cannot take the running server down with it.

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

## Verify it actually works

A bound port only proves `docker-proxy` answered. Check the population:

```bash
P=$(tr -d '\r\n' < /home/deck/tortoise-wow-server-V2/.dbpass)
docker exec -i tw2-db mysql -uroot --password="$P" -N -e \
  "SELECT CONCAT(name,'  port=',port,'  realmflags=',realmflags) FROM tw_logon.realmlist;
   SELECT CONCAT('characters online: ',COUNT(*)) FROM tw_char.characters WHERE online=1;" \
  2>/dev/null
```

`port=8095  realmflags=0` and a rising online count mean the realm is reachable.
`realmflags=2` means offline; `port` disagreeing with `WorldServerPort` in
`mangosd.conf` makes the client hang after login, before character select.

## Rollback

Every build is tagged with its commit, so the previous server is still on disk:

```bash
docker images --filter reference=tortoise-v2

# Point TW_IMAGE at the anchor explicitly. Retagging :local is NOT enough — if
# .env has TW_IMAGE=tortoise-v2:candidate (which .env.example invites for testing
# a fresh build), compose resolves :candidate, sees no change, prints "Running",
# and relaunches the very image you are rolling back from.
sed -i 's|^TW_IMAGE=.*|TW_IMAGE=tortoise-v2:c06b2fb|' .env
docker compose up -d
```

Set `TW_IMAGE` back to `tortoise-v2:local` once you have rebuilt a good image.

## Things that will cost you an afternoon

| | |
|---|---|
| **`docker compose down -v`** | Destroys `tortoise-wow-v2_dbdata` — every character and all progression. The volume is declared `external` so compose cannot recreate it silently, but `-v` still removes it. Never run it. |
| Line endings | This checkout must stay LF (`git config core.autocrlf false`). A CRLF tree compiles, but produces different bytes than the tree the proven image came from. |
| `BUILD_PLAYERBOTS` | Defaults `OFF`. A build without it yields a bot-free server with no warning. Check: `docker run --rm tortoise-v2:local ls /opt/turtle/etc \| grep aiplayerbot`. |
| **A rebuild that produces no binary** | `scripts/rebuild.sh` checks that `mangosd`/`realmd` exist before checking that they link — `ldd` on a missing file writes to stderr, so a naive `ldd \| grep 'not found'` reports a missing binary as healthy. Do not "simplify" the `test -x` check or the `2>&1` out of that loop. |
| `CMAKE_INSTALL_PREFIX` | Compiled in. It must stay `/opt/turtle` or the server logs one line about `aiplayerbot.conf` and runs with no bots. |
| Ports 3724 / 8095 | Shared with the older V1 stack. They cannot run together. |
| `Release: 1970-01-01` in the log | Expected. `.git` is excluded from the build context, so the revision falls back; the real commit is on the image's `org.opencontainers.image.revision` label. |

## Where the source of truth is

This checkout is **not** the only tree of this repo on the machine. A second,
diverged checkout lives at `/home/deck/tortoise-wow-server-V2/src` and shares
ancestor `c06b2fb`. Before building, confirm which tree you mean to ship —
building the wrong one silently produces a server without the change you made.
