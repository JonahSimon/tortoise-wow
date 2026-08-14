#!/usr/bin/env bash
# scripts/bot-progression/report.sh BEFORE.tsv AFTER.tsv
# Pure: compares two snapshots, writes a markdown progression report to stdout.
# Exit 0 = progression meets thresholds, 1 = below.
set -uo pipefail

BEFORE="${1:?usage: report.sh BEFORE.tsv AFTER.tsv}"
AFTER="${2:?usage: report.sh BEFORE.tsv AFTER.tsv}"

MIN_LEVELUPS="${MIN_LEVELUPS:-1}"
MIN_LEVELS_PER_HOUR="${MIN_LEVELS_PER_HOUR:-0.10}"

awk -F'\t' -v min_levelups="$MIN_LEVELUPS" -v min_rate="$MIN_LEVELS_PER_HOUR" '
  FNR == NR { blevel[$1] = $3; btime[$1] = $4; next }
  {
    guid = $1; lvl = $3 + 0; tt = $4 + 0
    if (guid in blevel) {
      seen++
      dl = lvl - blevel[guid]
      dt = tt  - btime[guid]
      if (dl > 0) { levelups += dl; movers++ }
      if (dt > 0) played += dt
    }
    if (lvl >= 10) ge10++
    band[int(lvl / 10) * 10]++
  }
  END {
    hours = played / 3600.0
    rate  = (hours > 0) ? levelups / hours : 0

    printf "# Bot progression report\n\n"
    printf "| Metric | Value |\n|---|---|\n"
    printf "| Bots compared | %d |\n", seen
    printf "| Bots that gained a level | %d |\n", movers
    printf "| Total levels gained | %d |\n", levelups
    printf "| Logged-in hours accrued | %.1f |\n", hours
    printf "| Levels per logged-in hour | %.3f |\n", rate
    printf "| Bots at level 10+ | %d |\n", ge10

    printf "\n## Level bands (after)\n\n| Band | Bots |\n|---|---|\n"
    for (b = 0; b <= 60; b += 10)
      if (band[b]) printf "| %d-%d | %d |\n", b, b + 9, band[b]
    printf "\n"

    fail = 0
    if (levelups < min_levelups) {
      printf "FAIL: levels gained %d < %d\n", levelups, min_levelups; fail = 1
    }
    if (rate < min_rate) {
      printf "FAIL: rate %.3f < %.3f levels per logged-in hour\n", rate, min_rate; fail = 1
    }
    if (!fail) printf "PASS: progression within thresholds\n"
    exit fail
  }
' "$BEFORE" "$AFTER"
