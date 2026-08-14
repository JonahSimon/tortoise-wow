#!/usr/bin/env bash
# ai-dev profile: run the TurtleWoW stack at minimal footprint while AI work
# only needs the database.
#
#   on     stop tw2-mangosd + tw2-realmd (the heavy pair), keep tw2-db
#   off    start the full stack back up (db -> realmd -> mangosd)
#   status show tw2-* container states
#
# Plain docker stop/start only — never `compose down`, so all state survives.
set -euo pipefail

usage() { echo "usage: $0 on|off|status" >&2; exit 2; }
[[ $# -eq 1 ]] || usage

case "$1" in
  on)
    docker stop -t 60 tw2-mangosd
    docker stop tw2-realmd
    echo "ai-dev ON: mangosd + realmd stopped, tw2-db kept (idle MySQL ~120MB, ~0% CPU)."
    echo "All AI scripts/tests keep working. For maximum headroom also: docker stop tw2-db"
    ;;
  off)
    docker start tw2-db tw2-realmd tw2-mangosd
    echo "ai-dev OFF: full stack starting. mangosd takes ~a minute to load the world; bots re-add over time."
    ;;
  status)
    docker ps -a --format '{{.Names}}\t{{.Status}}' | grep '^tw2-' || echo "no tw2-* containers found"
    ;;
  *)
    usage
    ;;
esac
