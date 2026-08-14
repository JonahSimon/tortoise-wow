#!/usr/bin/env bash
# Roster lifecycle: rebuild the 20 WSG bots from scripts/wsg-team-roster.txt.
#
# That file is the source of truth and it is in git, which is what makes the roster
# reproducible without a database backup. See WSG-ROSTER-RECOVERY-HANDOFF.md.
#
# Two hazards drive the design:
#   1. Digits are rejected at character LOAD, not creation, and "Bot is now online"
#      prints before login is attempted — so a bad name looks like success. Names are
#      validated at the FILE level before any create is issued.
#   2. LoginFreeBots never removes a failed login from its queue, so a character that
#      cannot load is retried every world tick forever — 10,558 at_login writes in 39
#      minutes, measured. Creates go out in batches of 5 and are verified within
#      seconds; a broken row is deleted immediately, never retried.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/lib/wsg-bots-common.sh"

ROSTER="$SCRIPT_DIR/wsg-team-roster.txt"
BATCH=5
SETTLE="${WSG_SETTLE:-10}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --roster) ROSTER="$2"; shift 2 ;;
    ensure|verify|create|repair|add) VERB="$1"; shift ;;
    *) echo "usage: $0 [--roster FILE] ensure|verify|create|repair|add" >&2; exit 2 ;;
  esac
done
[[ -n "${VERB:-}" ]] || { echo "usage: $0 [--roster FILE] ensure|verify|create|repair|add" >&2; exit 2; }

wsg_load_roster "$ROSTER"

# --- file-level preflight ----------------------------------------------------
preflight() {
  local bad=0 n
  [[ "${#WSG_NAMES[@]}" -gt 0 ]] || { echo "FATAL: roster is empty: $ROSTER" >&2; return 1; }
  for n in "${WSG_NAMES[@]}"; do
    if [[ ! "$n" =~ ^[A-Za-z]+$ ]]; then
      echo "FATAL: '$n' is not alphabetic-only. Digits are rejected at character load," >&2
      echo "       not creation, so the bot would appear to log in and then vanish." >&2
      bad=1
    fi
    [[ "${#n}" -le 12 ]] || { echo "FATAL: '$n' exceeds 12 characters" >&2; bad=1; }
  done
  [[ "$(printf '%s\n' "${WSG_NAMES[@]}" | sort | uniq -d | grep -c . || true)" -eq 0 ]] \
    || { echo "FATAL: duplicate names in $ROSTER" >&2; bad=1; }
  return "$bad"
}

# name -> "online level at_login", empty when the character does not exist
db_rows() {
  local csv="" n
  for n in "${WSG_NAMES[@]}"; do csv+="${csv:+,}'$n'"; done
  wsg_mysql "SELECT name, online, level, at_login FROM tw_char.characters WHERE name IN ($csv);"
}

do_verify() {
  local bad=0 n
  declare -A on=() lvl=() al=()
  while read -r name online level at_login; do
    [[ -z "${name:-}" ]] && continue
    on["$name"]="$online"; lvl["$name"]="$level"; al["$name"]="$at_login"
  done < <(db_rows)
  for n in "${WSG_NAMES[@]}"; do
    if [[ -z "${on[$n]+x}" ]]; then echo "MISSING  $n"; bad=1
    elif [[ "${al[$n]}" != "0" ]]; then echo "BROKEN   $n (at_login=${al[$n]}) — delete it; it will never load"; bad=1
    elif [[ "${lvl[$n]}" != "60" ]]; then echo "WRONGLVL $n (level=${lvl[$n]})"; bad=1
    elif [[ "${on[$n]}" != "1" ]]; then echo "OFFLINE  $n"; bad=1
    else echo "ok       $n"
    fi
  done
  return "$bad"
}

do_repair() {
  while read -r name _ _ at_login; do
    [[ -z "${name:-}" ]] && continue
    if [[ "$at_login" != "0" ]]; then
      echo "deleting broken row: $name (at_login=$at_login)"
      wsg_mysql "DELETE FROM tw_char.characters WHERE name='$name';"
    fi
  done < <(db_rows)
}

# Log the roster into the world. Creating a character does not put it in world, and
# bots never come back on their own after a mangosd restart — and `wsg-mode.sh on`
# always restarts. Without this, `ensure` polled for five minutes and then reported all
# 20 OFFLINE, because nothing in the roster lifecycle ever issued a login.
#
# `rndbot add` (HandleBotAddLogin) is idempotent for a bot already in world, so this is
# safe to run unconditionally. One attach for all 20 — forty attaches is forty chances
# to EOF the console and shut the world down. Measured: 0 -> 20 online in under 10s.
do_add() {
  local offline=() n
  declare -A on=()
  while read -r name online _ _; do
    [[ -n "${name:-}" ]] && on["$name"]="$online"
  done < <(db_rows)
  for n in "${WSG_NAMES[@]}"; do
    [[ "${on[$n]:-0}" != "1" ]] && offline+=("$n")
  done
  [[ "${#offline[@]}" -eq 0 ]] && { echo "all ${#WSG_NAMES[@]} already in world"; return 0; }
  echo "logging in ${#offline[@]} bot(s)"
  wsg_console "$(printf 'rndbot add %s\n' "${offline[@]}")" 15 >/dev/null
}

do_create() {
  preflight || return 1
  declare -A exists=()
  while read -r name _ _ _; do [[ -n "${name:-}" ]] && exists["$name"]=1; done < <(db_rows)

  local missing=() i
  for i in "${!WSG_NAMES[@]}"; do
    [[ -z "${exists[${WSG_NAMES[$i]}]+x}" ]] && missing+=("$i")
  done
  [[ "${#missing[@]}" -eq 0 ]] && { echo "nothing to create"; return 0; }
  echo "creating ${#missing[@]} bots in batches of ${BATCH}"

  local batch=() idx
  for idx in "${missing[@]}"; do
    # Console commands take no leading dot; wsg_create_line emits the in-game form.
    batch+=("$(wsg_create_line "${WSG_NAMES[$idx]}" "${WSG_CLASS[$idx]}" "${WSG_RACE[$idx]}" "${WSG_ROLE[$idx]}" | sed 's/^\.//')")
    if [[ "${#batch[@]}" -ge "$BATCH" ]]; then
      wsg_console "$(printf '%s\n' "${batch[@]}")" 12 >/dev/null
      sleep "$SETTLE"; do_repair; batch=()
    fi
  done
  if [[ "${#batch[@]}" -gt 0 ]]; then
    wsg_console "$(printf '%s\n' "${batch[@]}")" 12 >/dev/null
    sleep "$SETTLE"; do_repair
  fi
}

case "$VERB" in
  verify) do_verify ;;
  repair) do_repair ;;
  create) do_create ;;
  add)    do_add ;;
  ensure)
    preflight || exit 1
    do_repair
    do_create
    do_add
    for _ in $(seq 1 30); do do_verify >/dev/null 2>&1 && break; sleep 10; done
    do_verify
    ;;
esac
