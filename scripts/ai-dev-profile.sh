#!/usr/bin/env bash
# ai-dev profile: run the TurtleWoW stack at minimal footprint while AI work
# only needs the database.
#
#   on     stop tcm-mangosd + tcm-realmd (the heavy pair), keep tcm-db
#   off    start the full stack back up (db -> realmd -> mangosd)
#   status show tcm-* container states
#
# Plain docker stop/start only — never `compose down`, so all state survives.
set -euo pipefail

usage() { echo "usage: $0 on|off|status" >&2; exit 2; }
[[ $# -eq 1 ]] || usage

case "$1" in
  on)
    docker stop -t 60 tcm-mangosd
    docker stop tcm-realmd
    echo "ai-dev ON: mangosd + realmd stopped, tcm-db kept (idle MySQL ~120MB, ~0% CPU)."
    echo "All AI scripts/tests keep working. For maximum headroom also: docker stop tcm-db"
    ;;
  off)
    docker start tcm-db tcm-realmd tcm-mangosd
    echo "ai-dev OFF: full stack starting. mangosd takes ~a minute to load the world; bots re-add over time."
    ;;
  status)
    docker ps -a --format '{{.Names}}\t{{.Status}}' | grep '^tcm-' || echo "no tcm-* containers found"
    ;;
  *)
    usage
    ;;
esac
