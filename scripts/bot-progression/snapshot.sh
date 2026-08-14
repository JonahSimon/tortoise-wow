#!/usr/bin/env bash
# scripts/bot-progression/snapshot.sh [OUT_DIR]
# Read-only. Captures one progression snapshot of every RNDBOT character.
# Columns: guid  name  level  totaltime  zone  online
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../lib/botdb.sh
source "$HERE/../lib/botdb.sh"

OUT_DIR="${1:-$HERE/../../data/progression}"
mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/$(date -u +%Y%m%dT%H%M%SZ).tsv"

# The redirect below creates/truncates $OUT before botdb_query runs, so any
# failure prior to a successful query (missing .dbpass, container down,
# docker exec/mariadb error) must not leave a stale zero-byte file behind.
trap 'rm -f "$OUT"' ERR

botdb_query "
SELECT c.guid, c.name, c.level, c.totaltime, c.zone, c.online
FROM tw_char.characters c
JOIN tw_logon.account a ON a.id = c.account
WHERE a.username LIKE 'RNDBOT%'
ORDER BY c.guid;" > "$OUT"

if [ ! -s "$OUT" ]; then
  echo "snapshot: no rows captured — is the DB up?" >&2
  rm -f "$OUT"
  exit 1
fi

trap - ERR
echo "$OUT"
