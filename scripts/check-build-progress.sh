#!/usr/bin/env bash
set -euo pipefail
LOG="${HOME}/tortoise-wow-server-V2/logs/mangosd-cache-fix-build.log"
echo "date=$(date -Iseconds)"
if [[ ! -f "$LOG" ]]; then
  echo "MISSING LOG: $LOG"
  exit 1
fi
stat -c "mtime=%y bytes=%s" "$LOG"
echo "--- last pct ---"
grep -oE '\[[0-9]+%\]' "$LOG" | tail -5 || true
echo "--- tail ---"
tail -n 15 "$LOG"
echo "--- procs ---"
pgrep -af 'docker build' | head -5 || true
echo "cc1plus_count=$(pgrep -c cc1plus || true)"
echo "cmake_count=$(pgrep -c cmake || true)"
