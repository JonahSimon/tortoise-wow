#!/usr/bin/env bash
# Task 3 staged ramp helper — run as deck in WSL
set -euo pipefail

ROOT=/home/deck/tortoise-wow-server-V2
TARGET="${1:?usage: task3-ramp-step.sh <min/max> [wait-only|apply|gates|wait]}"
MODE="${2:-apply}"  # apply | wait | gates | verify-dials
PASS=$(cat "$ROOT/.dbpass")
cd "$ROOT"

online_count() {
  docker exec -e MYSQL_PWD="$PASS" tw2-db mysql -uroot -N -B -e \
    "SELECT COUNT(*) FROM tw_char.characters c JOIN tw_logon.account a ON a.id=c.account WHERE a.username LIKE 'RNDBOT%' AND c.online=1;"
}

show_gates() {
  echo "=== WSL free ==="
  free -h
  echo "=== docker stats ==="
  docker stats --no-stream --format 'table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}'
  echo "=== compose ps ==="
  docker compose ps
  echo "=== online RNDBOT ==="
  online_count
  echo "=== dials ==="
  grep -E '^AiPlayerbot\.(Min|Max)RandomBots|^AiPlayerbot\.DisableActivityPriorities|^AiPlayerbot\.botActiveAlone|^AiPlayerbot\.ForceActiveWhenNearPlayer|^AiPlayerbot\.RandomBotTeleportNearPlayer' \
    etc/aiplayerbot.conf || true
}

case "$MODE" in
  gates)
    show_gates
    ;;
  wait)
    THRESH="${3:-90}"
    TIMEOUT_SEC="${4:-900}"
    echo "Waiting for online RNDBOT >= $THRESH (timeout ${TIMEOUT_SEC}s)..."
    start=$(date +%s)
    while true; do
      n=$(online_count)
      now=$(date +%s)
      elapsed=$((now - start))
      echo "$(date +%H:%M:%S) online=$n elapsed=${elapsed}s"
      if [ "$n" -ge "$THRESH" ]; then
        echo "REACHED online=$n"
        show_gates
        exit 0
      fi
      if [ "$elapsed" -ge "$TIMEOUT_SEC" ]; then
        echo "TIMEOUT online=$n (wanted >= $THRESH)"
        show_gates
        exit 2
      fi
      sleep 30
    done
    ;;
  apply)
    echo "Setting Min/MaxRandomBots = $TARGET"
    sed -i "s/^AiPlayerbot.MinRandomBots = .*/AiPlayerbot.MinRandomBots = ${TARGET}/" etc/aiplayerbot.conf
    sed -i "s/^AiPlayerbot.MaxRandomBots = .*/AiPlayerbot.MaxRandomBots = ${TARGET}/" etc/aiplayerbot.conf
    grep -E '^AiPlayerbot\.(Min|Max)RandomBots|^AiPlayerbot\.DisableActivityPriorities|^AiPlayerbot\.botActiveAlone|^AiPlayerbot\.ForceActiveWhenNearPlayer|^AiPlayerbot\.RandomBotTeleportNearPlayer' \
      etc/aiplayerbot.conf
    echo "Restarting mangosd..."
    docker compose restart mangosd
    sleep 5
    docker compose ps
    ;;
  *)
    echo "unknown mode: $MODE" >&2
    exit 1
    ;;
esac
