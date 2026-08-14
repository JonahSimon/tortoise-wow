#!/usr/bin/env bash
# Is the running server built from the committed source?
#
# Read-only, runs in seconds. Exit code is the contract:
#   0  MATCH    the running image was built from HEAD
#   1  DRIFT    it was built from something else
#   2  UNKNOWN  nothing is running, or the image has no provenance labels
#
# Run from WSL:  /mnt/d/TurtleWow/scripts/verify-running-commit.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/provenance.sh
. "$HERE/lib/provenance.sh"

[[ -d "$TW_SRC_DIR/.git" ]] || { echo "FATAL: no git repo at $TW_SRC_DIR" >&2; exit 2; }

HEAD_SHA=$(prov_head_sha)
DF_SHA=$(prov_dockerfile_sha)

echo "source:      $TW_SRC_DIR"
echo "branch:      $(prov_branch)"
echo "HEAD:        $HEAD_SHA"
if prov_is_dirty; then
    echo "tree:        DIRTY (uncommitted or untracked changes)"
else
    echo "tree:        clean"
fi
echo

if ! prov_container_exists "$TW_MANGOSD"; then
    echo "container:   $TW_MANGOSD does not exist"
    echo
    echo "VERDICT: UNKNOWN — nothing is running to compare against."
    exit 2
fi

# `docker inspect` succeeds on Exited containers too, and every compose
# service here is restart:"no", so a crashed mangosd is left Exited rather
# than removed. Existence is not liveness — this script answers "is the
# *running* server built from HEAD", and there is no running server to ask.
mangosd_running() { [[ "$(docker inspect -f '{{.State.Running}}' "$TW_MANGOSD" 2>/dev/null)" == "true" ]]; }
if ! mangosd_running; then
    echo "container:   $TW_MANGOSD exists but is NOT running (exited)"
    echo
    echo "VERDICT: UNKNOWN — the container is there but the server is down."
    exit 2
fi

IMAGE_ID=$(prov_running_image_id "$TW_MANGOSD")
IMG_REV=$(prov_image_label "$IMAGE_ID" "$PROV_LABEL_REV")
IMG_DIRTY=$(prov_image_label "$IMAGE_ID" "$PROV_LABEL_DIRTY")
IMG_DF=$(prov_image_label "$IMAGE_ID" "$PROV_LABEL_DOCKERFILE")

echo "container:   $TW_MANGOSD"
echo "image:       $IMAGE_ID"

if [[ -z "$IMG_REV" ]]; then
    echo "revision:    (none)"
    echo
    echo "VERDICT: UNKNOWN — this image carries no provenance labels, so it was"
    echo "         built before SHA stamping existed. That is not drift. Ship once"
    echo "         with ship-cpp-fix.sh and the answer becomes definite."
    exit 2
fi

echo "revision:    $IMG_REV"
echo "built dirty: ${IMG_DIRTY:-unknown}"

rc=0

if [[ "$IMG_REV" != "$HEAD_SHA" ]]; then
    echo
    echo "VERDICT: DRIFT — the running server was built from"
    echo "           $IMG_REV"
    echo "         but HEAD is"
    echo "           $HEAD_SHA"
    echo
    echo "  Ship HEAD:  scripts/ship-cpp-fix.sh"
    echo "  Roll back:  docker tag $TW_IMAGE:${IMG_REV:0:7} $TW_IMAGE:local && docker compose up -d"
    rc=1
fi

if [[ -n "$IMG_DF" && "$IMG_DF" != "$DF_SHA" ]]; then
    echo
    echo "WARNING: the Dockerfile changed since this image was built."
    echo "         image: $IMG_DF"
    echo "         now:   $DF_SHA"
    echo "         The same commit can still produce a different binary."
    rc=1
fi

if [[ "$IMG_DIRTY" == "true" ]]; then
    echo
    echo "WARNING: this image was built from a dirty tree, so its revision label"
    echo "         does not fully describe the binary."
fi

if (( rc == 0 )); then
    echo
    echo "VERDICT: MATCH — the running server is built from HEAD."
fi
exit $rc
