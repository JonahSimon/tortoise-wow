#!/usr/bin/env bash
# Shared provenance helpers — what commit a tree is at, what commit an image was
# built from, and whether those two agree.
#
# Sourced by ship-cpp-fix.sh and verify-running-commit.sh; not executable alone.
# Every path is an env-var override with a live default, so the tests can point
# these at stubs without touching the real server.

TW_LIVE_ROOT="${TW_LIVE_ROOT:-$HOME/tortoise-wow-server-V2}"
TW_SRC_DIR="${TW_SRC_DIR:-$TW_LIVE_ROOT/src}"
TW_IMAGE="${TW_IMAGE:-tortoise-v2}"
TW_MANGOSD="${TW_MANGOSD:-tw2-mangosd}"
TW_DB_VOLUME="${TW_DB_VOLUME:-tortoise-wow-v2_dbdata}"
TW_WORLD_PORT="${TW_WORLD_PORT:-8095}"

PROV_LABEL_REV="org.opencontainers.image.revision"
PROV_LABEL_DIRTY="com.turtle.source-dirty"
PROV_LABEL_DOCKERFILE="com.turtle.dockerfile-sha256"

prov_git() { git -C "$TW_SRC_DIR" "$@"; }

prov_head_sha()  { prov_git rev-parse HEAD; }
prov_short_sha() { prov_git rev-parse --short HEAD; }
prov_branch()    { prov_git rev-parse --abbrev-ref HEAD; }

# Untracked files count as dirty. An untracked .cpp is in the build context and
# changes the binary while `git diff` stays silent.
prov_is_dirty() {
    [[ -n "$(prov_git status --porcelain --untracked-files=all)" ]]
}

prov_dockerfile_sha() {
    sha256sum "$TW_LIVE_ROOT/Dockerfile" | cut -d' ' -f1
}

# A label off an image ref. Prints the empty string when the label is absent,
# which is how images built before provenance stamping are recognised.
prov_image_label() { # <image-ref> <label-key>
    local v
    v=$(docker image inspect "$1" --format "{{index .Config.Labels \"$2\"}}" 2>/dev/null) || return 0
    [[ "$v" == "<no value>" ]] && v=""
    printf '%s\n' "$v"
}

# The image a container actually runs, by ID — not the tag it was started with.
# :local is mutable, so the tag lies the moment anything is rebuilt.
prov_running_image_id() { # <container>
    docker inspect "$1" --format '{{.Image}}' 2>/dev/null
}

prov_container_exists() { docker inspect "$1" >/dev/null 2>&1; }
prov_volume_exists()    { docker volume inspect "$1" >/dev/null 2>&1; }

# Ready means the world port accepts a connection. This deliberately avoids
# grepping docker logs: `grep -q` closes the pipe at its first match, the
# still-writing producer dies of SIGPIPE, and under `set -o pipefail` the
# pipeline yields 141 — so an `until ... grep -q` loop never terminates.
prov_world_ready() {
    nc -z -w 3 127.0.0.1 "$TW_WORLD_PORT" >/dev/null 2>&1
}
