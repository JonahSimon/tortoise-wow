#!/usr/bin/env bash
# Poll until online RNDBOT count reaches a threshold (default 45).
set -uo pipefail
ROOT="${HOME}/tortoise-wow-server-V2"
PASS=$(tr -d '\r\n' < "$ROOT/.dbpass")
TARGET="${1:-45}"
MAX_ITERS="${2:-36}"

for i in $(seq 1 "$MAX_ITERS"); do
  status=$(docker inspect -f '{{.State.Status}}' tcm-mangosd 2>/dev/null || echo missing)
  online=$(docker exec -e MYSQL_PWD="$PASS" tcm-db mysql -uroot -N -B -e \
    "SELECT COUNT(*) FROM tw_char.characters c JOIN tw_logon.account a ON a.id=c.account WHERE a.username LIKE 'RNDBOT%' AND c.online=1;" \
    2>/dev/null | tr -d '\r')
  echo "$(date -u +%H:%M:%S) status=$status online=${online:-err}"
  if [[ "${online:-0}" =~ ^[0-9]+$ ]] && [ "$online" -ge "$TARGET" ]; then
    echo "BOTS_READY online=$online"
    exit 0
  fi
  if [ "$status" != "running" ] && [ "$i" -gt 3 ]; then
    echo "BLOCKED: mangosd status=$status"
    exit 2
  fi
  sleep 10
done
echo "TIMEOUT waiting for online >= $TARGET"
exit 1
