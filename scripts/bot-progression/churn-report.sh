#!/usr/bin/env bash
# scripts/bot-progression/churn-report.sh SNAP1 SNAP2 [SNAP3...]
# Pure: reports how widely the online cast rotates across snapshots.
# Emits "CHURN: <ratio>" where ratio = distinct-ever-online / max-online-at-once.
set -uo pipefail

[ "$#" -ge 2 ] || { echo "usage: churn-report.sh SNAP1 SNAP2 [SNAP3...]" >&2; exit 2; }

awk -F'\t' '
  FNR == 1 { file_idx++ }
  $6 == 1 { ever[$1] = 1; online[file_idx]++ }
  END {
    distinct = 0
    for (g in ever) distinct++
    maxonline = 0
    for (i = 1; i <= file_idx; i++) if (online[i] > maxonline) maxonline = online[i]
    ratio = (maxonline > 0) ? distinct / maxonline : 0

    printf "# Bot pool churn\n\n"
    printf "| Metric | Value |\n|---|---|\n"
    printf "| Snapshots compared | %d |\n", file_idx
    printf "| Distinct bots ever online | %d |\n", distinct
    printf "| Max online in any snapshot | %d |\n", maxonline
    printf "| Churn ratio | %.3f |\n\n", ratio
    printf "CHURN: %.3f\n", ratio
  }
' "$@"
