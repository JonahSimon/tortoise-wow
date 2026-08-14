#!/usr/bin/env bash
# One run = one match. Preflight, queue the whole roster in a single console
# attach, confirm they queued, then hand off to the monitor.
#
# It never starts a second match. When this exits, the match is over and you run
# it again for the next one.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/lib/wsg-bots-common.sh"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/lib/wsg-bg-log.sh"

ROSTER="$SCRIPT_DIR/wsg-team-roster.txt"
RUN_ROOT="${WSG_RUN_ROOT:-$SCRIPT_DIR/../logs/wsg-matches}"
QUEUE_DEADLINE=120     # seconds to get all 20 tagged
RETRY_EVERY="${WSG_RETRY_EVERY:-15}"
# How long each console attach stays open. wsg_console sleeps this long before
# sending the detach keys, so it is a floor on every attach; only the tests turn it
# down, and they stub docker anyway.
CONSOLE_WAIT="${WSG_CONSOLE_WAIT:-15}"
MIN_HEALTH="${WSG_MIN_HEALTH:-1200}"   # warn below this; refuse only at 0 (dead)
MONITOR=1
WAIT_POP=1
MONITOR_ONCE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-monitor) MONITOR=0; shift ;;
    --no-wait)    WAIT_POP=0; shift ;;
    --monitor-once) MONITOR_ONCE=1; shift ;;
    --roster)     ROSTER="$2"; shift 2 ;;
    --queue-deadline) QUEUE_DEADLINE="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

wsg_load_roster "$ROSTER"

# --- preflight ---------------------------------------------------------------
wsg_check_db || { echo "FATAL: DB unreachable via tcm-db" >&2; exit 1; }

online=0
while read -r _ on _; do [[ "$on" == "1" ]] && online=$((online+1)); done < <(wsg_team_status)
if [[ "$online" -lt 20 ]]; then
  echo "FATAL: roster is ${online}/20 online. Run: wsg-mode.sh on   (or wsg-roster.sh ensure)" >&2
  exit 1
fi

a="$(wsg_count_on_wsg A)"; h="$(wsg_count_on_wsg H)"
if [[ $((a + h)) -gt 0 ]]; then
  echo "FATAL: a match is already live — ${a} Alliance / ${h} Horde on map 489." >&2
  echo "Matches are hard-capped at 20 minutes. Wait for it to end, then re-run." >&2
  exit 1
fi

# Health gate. WSG does NOT heal on entry — the only reset is in
# BattleGround::RemovePlayerAtLeave, i.e. on the way out, and vanilla has no
# Preparation spell (see BattleGroundBR.cpp). There is also no safe force-heal:
# Refresh() would do it but is disabled by DisableRandomLevels=1, which must stay on
# or it re-rolls the roster's gear. So a dead bot is a hard stop; merely hurt is a
# warning, because characters.health is up to 60s stale (PlayerSave.Interval=60000)
# and the food strategy tops bots up behind the closed WSG gates.
roster_health() {
  local csv="" n
  for n in "${WSG_NAMES[@]}"; do csv+="${csv:+,}'$n'"; done
  wsg_mysql "SELECT name, health, level FROM tw_char.characters WHERE name IN ($csv);"
}

dead=(); hurt=()
while read -r name hp _; do
  [[ -z "${name:-}" ]] && continue
  if [[ "${hp:-0}" -eq 0 ]]; then dead+=("$name")
  elif [[ "$hp" -lt "$MIN_HEALTH" ]]; then hurt+=("${name}(${hp})")
  fi
done < <(roster_health)

if [[ "${#dead[@]}" -gt 0 ]]; then
  echo "FATAL: ${#dead[@]} roster bot(s) are dead: ${dead[*]}" >&2
  echo "WSG does not heal on entry, so they would be ported in as corpses." >&2
  echo "Run: wsg-roster.sh ensure   (or rndbot revive <name>) and re-run." >&2
  exit 1
fi
[[ "${#hurt[@]}" -gt 0 ]] && \
  echo "WARNING: low health (<${MIN_HEALTH}): ${hurt[*]} — they eat behind the gates, but watch them."

floor="$(wsg_mysql "SELECT min_players_per_team FROM tw_world.battleground_template WHERE id=2;")"
[[ "$floor" == "10" ]] || echo "WARNING: WSG floor is ${floor}, not 10 — the match may pop before all 20 are in."

open_bgs="$(wsg_mysql "SELECT COUNT(*) FROM tw_world.battleground_template WHERE id <> 2 AND min_level <= 60;")"
[[ "$open_bgs" == "0" ]] || echo "WARNING: ${open_bgs} non-WSG battlegrounds joinable — run wsg-mode.sh on."

# --- artifacts + anchors -----------------------------------------------------
STAMP="$(date -u +%Y-%m-%dT%H-%M-%SZ)"
RUN_DIR="${RUN_ROOT}/${STAMP}"
mkdir -p "$RUN_DIR"
exec > >(tee -a "$RUN_DIR/kickoff.log") 2>&1

WSG_BG_OFFSET="$(wsg_log_offset "$(wsg_bg_log_path)")"
WSG_HONOR_OFFSET="$(wsg_log_offset "$(wsg_honor_log_path)")"
echo "run       ${STAMP}"
echo "artifacts ${RUN_DIR}"
echo "anchors   bg=${WSG_BG_OFFSET} honor=${WSG_HONOR_OFFSET}"

ROSTER_CSV="$(IFS=,; echo "${WSG_NAMES[*]}")"

# --- kickoff: ONE attach carrying all 40 lines -------------------------------
echo "kickoff: queueing ${#WSG_NAMES[@]} bots in one console attach"
{
  echo "CONSOLE_BATCH_START ${STAMP}"
  wsg_bgjoin_lines "${WSG_NAMES[@]}"
} | { batch="$(cat)"; wsg_console "$batch" "$CONSOLE_WAIT"; } >> "$RUN_DIR/console-raw.txt" 2>&1

# --- confirm -----------------------------------------------------------------
deadline=$((SECONDS + QUEUE_DEADLINE))
declare -A tagged=()
POPPED_INST=""
while (( SECONDS < deadline )); do
  while read -r _ _ name; do [[ -n "$name" ]] && tagged["$name"]=1; done \
    < <(wsg_log_since "$(wsg_bg_log_path)" "$WSG_BG_OFFSET" | wsg_bg_tags "$ROSTER_CSV")
  echo "$(date -u +%H:%M:%S) queued=${#tagged[@]}/20"
  [[ "${#tagged[@]}" -ge 20 ]] && break

  # Counting tags alone cannot confirm a roster that was already partly queued.
  # BGJoinAction only writes "tag BG=2" on a fresh JoinQueue, so a bot left in the
  # queue by an earlier run stays silent forever and 20/20 is unreachable — while the
  # queue is in fact full and the match pops on schedule. `bg leave` cannot clear them
  # either: BGLeaveAction feeds a CMSG_BATTLEFIELD_PORT-shaped packet to
  # HandleLeaveBattlefieldOpcode whenever the event has a source, which console
  # commands always do. So treat the pop as what it is — proof the roster is in.
  POPPED_INST="$(wsg_log_since "$(wsg_bg_log_path)" "$WSG_BG_OFFSET" | wsg_bg_instance "$ROSTER_CSV")"
  if [[ -n "$POPPED_INST" ]]; then
    echo "  pop detected (instance ${POPPED_INST}) at ${#tagged[@]}/20 tagged — bots already queued do not re-tag"
    break
  fi

  missing=()
  for n in "${WSG_NAMES[@]}"; do [[ -z "${tagged[$n]+x}" ]] && missing+=("$n"); done
  echo "  retrying ${#missing[@]}: ${missing[*]}"
  wsg_console "$(wsg_bgjoin_lines "${missing[@]}")" "$CONSOLE_WAIT" >> "$RUN_DIR/console-raw.txt" 2>&1
  sleep "$RETRY_EVERY"
done

if [[ "${#tagged[@]}" -lt 20 && -z "$POPPED_INST" ]]; then
  echo "TIMEOUT: only ${#tagged[@]}/20 queued after ${QUEUE_DEADLINE}s"
  for n in "${WSG_NAMES[@]}"; do [[ -z "${tagged[$n]+x}" ]] && echo "  never queued: $n"; done
  echo "A bot that never queues is usually in combat — isUseful() gates on IsInCombat()."
  echo "wsg-mode.sh on parks the roster and strips the wander strategies; check it ran."
  wsg_console "$(wsg_bgtype_reset_lines "${WSG_NAMES[@]}")" "$CONSOLE_WAIT" >/dev/null 2>&1 || true
  exit 1
fi
[[ -n "$POPPED_INST" ]] || echo "QUEUED 20/20"

[[ "$WAIT_POP" -eq 0 ]] && exit 0
[[ "$MONITOR" -eq 0 ]] && exit 0

# --- wait for the pop --------------------------------------------------------
INST=""
pop_deadline=$((SECONDS + 120))
while (( SECONDS < pop_deadline )); do
  INST="$(wsg_log_since "$(wsg_bg_log_path)" "$WSG_BG_OFFSET" | wsg_bg_instance "$ROSTER_CSV")"
  [[ -n "$INST" ]] && break
  sleep 2
done
if [[ -z "$INST" ]]; then
  echo "TIMEOUT: 20/20 queued but no pop within 120s. Check the WSG floor and the funnel."
  exit 1
fi
T0="$(date -u +%s)"
echo "POP instance=${INST} at $(date -u +%H:%M:%SZ) — ends by $(date -u -d '+20 minutes' +%H:%M:%SZ)"

# --- monitor -----------------------------------------------------------------
# The winner= line comes from BattleGround::~BattleGround(), so it lags the true end
# by 1-122s depending on how long stragglers take to auto-remove. The reliable end
# marker is the first "leaves" line for this instance — 1-2s after the real end.
score_a=0; score_h=0; kills=0; last_event=""
while :; do
  bgs="$(wsg_log_since "$(wsg_bg_log_path)" "$WSG_BG_OFFSET")"
  hon="$(wsg_log_since "$(wsg_honor_log_path)" "$WSG_HONOR_OFFSET")"

  in_bg=$(( $(wsg_bg_entered "$INST" <<< "$bgs" | sort -u | wc -l) \
          - $(wsg_bg_left    "$INST" <<< "$bgs" | sort -u | wc -l) ))
  kills="$(wsg_honor_kills "$ROSTER_CSV" <<< "$hon")"
  caps="$(wsg_honor_captures "$ROSTER_CSV" <<< "$hon")"
  score_a="$(awk '$NF=="A"' <<< "$caps" | grep -c . || true)"
  score_h="$(awk '$NF=="H"' <<< "$caps" | grep -c . || true)"
  [[ -n "$caps" ]] && last_event="$(tail -1 <<< "$caps")"

  printf '\rT+%02d:%02d  in_bg=%-2s  score A%s-%sH  kills=%-3s %s' \
    $(( ($(date -u +%s) - T0) / 60 )) $(( ($(date -u +%s) - T0) % 60 )) \
    "$in_bg" "$score_a" "$score_h" "$kills" "$last_event"

  left="$(wsg_bg_left "$INST" <<< "$bgs" | sort -u | wc -l)"
  [[ "$left" -ge 3 ]] && { echo; echo "MATCH ENDED at $(date -u +%H:%M:%SZ)"; break; }
  [[ "${MONITOR_ONCE:-0}" -eq 1 ]] && { echo; break; }
  sleep 2
done

# --- result ------------------------------------------------------------------
# Wait up to 130s for the destructor line; report without it rather than
# falling back to a whole-file grep, which could match a recycled instance id.
result=""; res_deadline=$((SECONDS + 130))
while (( SECONDS < res_deadline )); do
  result="$(wsg_log_since "$(wsg_bg_log_path)" "$WSG_BG_OFFSET" | wsg_bg_result "$INST")"
  [[ -n "$result" ]] && break
  [[ "${MONITOR_ONCE:-0}" -eq 1 ]] && break
  sleep 5
done
winner_n="${result%% *}"; duration="${result#* }"
winner="$(wsg_bg_winner_label "${winner_n:-}")"

# --- artifacts ---------------------------------------------------------------
wsg_log_since "$(wsg_bg_log_path)"    "$WSG_BG_OFFSET"    > "$RUN_DIR/bg-slice.log"
wsg_log_since "$(wsg_honor_log_path)" "$WSG_HONOR_OFFSET" > "$RUN_DIR/honor-slice.log"

{
  echo "WSG match ${STAMP}"
  echo "instance   ${INST}"
  echo "winner     ${winner}${duration:+ (raw winner=${winner_n}, duration=${duration})}"
  echo "score      Alliance ${score_a} - ${score_h} Horde"
  echo "kills      ${kills}"
  echo
  echo "Run wsg-kickoff.sh again to start the next match."
} | tee "$RUN_DIR/summary.txt"

cat > "$RUN_DIR/summary.json" <<JSON
{
  "run": "${STAMP}",
  "instance": ${INST},
  "winner": "${winner}",
  "winner_raw": "${winner_n:-null}",
  "duration": "${duration:-null}",
  "score_alliance": ${score_a},
  "score_horde": ${score_h},
  "kills": ${kills}
}
JSON
