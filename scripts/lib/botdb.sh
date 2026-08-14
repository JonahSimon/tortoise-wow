#!/usr/bin/env bash
# scripts/lib/botdb.sh — shared read-only access to the live tw2-db container.
# Source this file; do not execute it.
#
#   source "$(dirname "${BASH_SOURCE[0]}")/../lib/botdb.sh"
#   botdb_query "SELECT COUNT(*) FROM tw_char.characters;"

BOTDB_SERVER_DIR="${BOTDB_SERVER_DIR:-$HOME/tortoise-wow-server-V2}"
BOTDB_CONTAINER="${BOTDB_CONTAINER:-tw2-db}"

botdb_require() {
  if [ ! -f "$BOTDB_SERVER_DIR/.dbpass" ]; then
    echo "botdb: missing $BOTDB_SERVER_DIR/.dbpass" >&2
    return 1
  fi
  if ! docker ps --format '{{.Names}}' | grep -qx "$BOTDB_CONTAINER"; then
    echo "botdb: container $BOTDB_CONTAINER is not running" >&2
    return 1
  fi
}

# botdb_query <sql> — tab-separated, header-less rows on stdout.
botdb_query() {
  botdb_require || return 1
  docker exec "$BOTDB_CONTAINER" mariadb -uroot \
    -p"$(cat "$BOTDB_SERVER_DIR/.dbpass")" -N -B -e "$1" 2>/dev/null
}
