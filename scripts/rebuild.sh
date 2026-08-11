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
