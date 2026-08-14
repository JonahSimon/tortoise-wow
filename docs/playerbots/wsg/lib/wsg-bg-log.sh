#!/usr/bin/env bash
# Pure parsers for the server's bg.log and honor.log. Source this file; do not execute.
#
# No docker, no DB, no side effects — every function reads a stream on stdin (or a
# path argument) and writes to stdout. That is what makes the monitor testable.
#
# ALWAYS read from a byte offset captured at monitor start, never from the top of the
# file. Instance ids restart at 101 on every mangosd run, and the low-level bot pool
# runs its own concurrent WSG matches, so a whole-file grep can match a stale
# pre-restart line or another match entirely.

WSG_SERVER_ROOT="${WSG_SERVER_ROOT:-$HOME/tortoise-wow-server-V2}"

wsg_bg_log_path()    { echo "${WSG_SERVER_ROOT}/logs/bg.log"; }
wsg_honor_log_path() { echo "${WSG_SERVER_ROOT}/logs/honor.log"; }

# Current size in bytes — the anchor for every later read.
wsg_log_offset() {
  local f="$1"
  [[ -r "$f" ]] || { echo 0; return 0; }
  wc -c < "$f" | tr -d ' '
}

# Everything written after <offset>.
wsg_log_since() {
  local f="$1" off="${2:-0}"
  [[ -r "$f" ]] || return 0
  tail -c "+$((off + 1))" "$f"
}

# Turn "A,B,C" into an awk-friendly regex alternation anchored to whole names.
_wsg_roster_re() {
  local csv="$1"
  echo "^(${csv//,/|})$"
}

# stdin -> "TIMESTAMP NAME" for each roster queue tag for WSG (BG=2).
wsg_bg_tags() {
  local re; re="$(_wsg_roster_re "$1")"
  awk -v re="$re" '
    /tag BG=2/ {
      name = $3; sub(/:.*/, "", name)
      if (name ~ re) print $1 " " $2 " " name
    }'
}

# stdin -> instance id of the first roster "enters" line. Empty if none yet.
wsg_bg_instance() {
  local re; re="$(_wsg_roster_re "$1")"
  awk -v re="$re" '
    /^\[?[0-9]/ && /\[489,/ && / enters$/ {
      inst = $3; sub(/^\[489,/, "", inst); sub(/\]:$/, "", inst)
      name = $4; sub(/:.*/, "", name)
      if (name ~ re) { print inst; exit }
    }'
}

# stdin -> one name per "enters" line for <inst>.
wsg_bg_entered() {
  awk -v inst="$1" '
    $3 == "[489," inst "]:" && / enters$/ { name = $4; sub(/:.*/, "", name); print name }'
}

# stdin -> one name per "leaves" line for <inst>.
wsg_bg_left() {
  awk -v inst="$1" '
    $3 == "[489," inst "]:" && / leaves$/ { name = $4; sub(/:.*/, "", name); print name }'
}

# stdin -> "WINNER DURATION" from the [2,<inst>] destructor line. Empty if not written yet.
# Note the leading 2 is the battleground TYPE id, not the map id.
wsg_bg_result() {
  awk -v inst="$1" '
    $3 == "[2," inst "]:" {
      w = $4; sub(/^winner=/, "", w); sub(/,$/, "", w)
      d = $5; sub(/^duration=/, "", d)
      print w " " d; exit
    }'
}

# BattleGround.h:187-189 — WINNER_HORDE=0, WINNER_ALLIANCE=1, WINNER_NONE=2.
wsg_bg_winner_label() {
  case "${1:-}" in
    0) echo "HORDE" ;;
    1) echo "ALLIANCE" ;;
    2) echo "DRAW" ;;
    *) echo "UNKNOWN" ;;
  esac
}

# stdin -> count of distinct (timestamp, victim) pairs among roster type-1 awards.
# One death credits every attacker in range, so counting lines overcounts. The
# recipient is the killer; "source player" is the victim.
wsg_honor_kills() {
  local re; re="$(_wsg_roster_re "$1")"
  awk -v re="$re" '
    /\[BATTLEGROUND\]/ && /honor for type 1,/ {
      for (i = 1; i <= NF; i++) if ($i == "player" && $(i-1) == "source") victim = $(i+1)
      if (victim ~ re) seen[$1 " " $2 " " victim] = 1
    }
    END { print length(seen) }'
}

# stdin -> "TIMESTAMP TEAM" per flag capture. TEAM is A or H.
# 495 = flag capture in the level-60 bracket (396 * 1.25 BattleGround.Rate.Honor.WS).
# 247 = end-of-match win bonus — deliberately NOT a capture.
wsg_honor_captures() {
  local re; re="$(_wsg_roster_re "$1")"
  awk -v re="$re" '
    /\[BATTLEGROUND\]/ && /got 495.000000 honor for type 3,/ {
      name = $5
      if (name !~ re) next
      key = $1 " " $2
      if (key in seen) next
      seen[key] = 1
      team = (name ~ /^Wsga/) ? "A" : "H"
      print key " " team
    }'
}
