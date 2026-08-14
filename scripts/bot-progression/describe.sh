#!/usr/bin/env bash
# scripts/bot-progression/describe.sh SNAPSHOT.tsv
# Pure: one-line summary of a single snapshot.
# Output: rows=<N> never_played=<N> level10plus=<N> online=<N>
set -uo pipefail

SNAP="${1:?usage: describe.sh SNAPSHOT.tsv}"
[ -s "$SNAP" ] || { echo "describe: $SNAP is empty or missing" >&2; exit 1; }

awk -F'\t' '
  {
    rows++
    if ($4 + 0 == 0) never++
    if ($3 + 0 >= 10) ge10++
    if ($6 + 0 == 1) online++
  }
  END {
    printf "rows=%d never_played=%d level10plus=%d online=%d\n",
           rows, never, ge10, online
  }
' "$SNAP"
