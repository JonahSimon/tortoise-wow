#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/lib/wsg-bots-common.sh"

SOFT_ALLY=8
SOFT_HORDE=8
ONLINE_TIMEOUT_SEC=600
FILL_TIMEOUT_SEC=1800
REPRINT_SEC=60
SKIP_CONF=0
NO_FUNNEL=0
ROSTER="$SCRIPT_DIR/wsg-team-roster.txt"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --soft-ally) SOFT_ALLY="$2"; shift 2 ;;
    --soft-horde) SOFT_HORDE="$2"; shift 2 ;;
    --online-timeout-sec) ONLINE_TIMEOUT_SEC="$2"; shift 2 ;;
    --fill-timeout-sec) FILL_TIMEOUT_SEC="$2"; shift 2 ;;
    --reprint-sec) REPRINT_SEC="$2"; shift 2 ;;
    --skip-conf) SKIP_CONF=1; shift ;;
    --no-funnel) NO_FUNNEL=1; shift ;;
    *) echo "Unknown arg: $1"; exit 2 ;;
  esac
done

wsg_load_roster "$ROSTER"
if ! wsg_check_db; then
  echo "FATAL: DB unreachable via tcm-db (docker stack up? ${WSG_SERVER_ROOT}/.dbpass present?)" >&2
  exit 1
fi
# Funnel preflight: WSG must be the only joinable BG for level-60 bots,
# otherwise bots spread uniformly across 5 queues and WSG never fills.
non_wsg_open="$(wsg_mysql "SELECT COUNT(*) FROM tw_world.battleground_template WHERE id <> 2 AND min_level <= 60;")"
if [[ "$non_wsg_open" != "0" ]]; then
  if [[ "$NO_FUNNEL" -eq 1 ]]; then
    echo "WARNING: ${non_wsg_open} non-WSG battlegrounds joinable (MinLvl <= 60) — bots will spread across queues and WSG may never fill."
  else
    echo "FATAL: ${non_wsg_open} non-WSG battlegrounds are joinable (MinLvl <= 60)." >&2
    echo "Run:  bash $SCRIPT_DIR/wsg-only-mode.sh on   then:  docker restart tcm-mangosd" >&2
    echo "(or pass --no-funnel to ignore)" >&2
    exit 1
  fi
fi
CONF="${WSG_SERVER_ROOT}/etc/aiplayerbot.conf"

if [[ "$SKIP_CONF" -eq 0 ]]; then
  result="$(wsg_ensure_match_conf "$CONF")"
  echo "Match conf: $result ($CONF)"
  if [[ "$result" == "CHANGED" ]]; then
    echo "ACTION REQUIRED in-game (GM): .rndbot reload"
  fi
fi

echo "=== Team status ==="
wsg_team_status

missing=()
offline=()
while read -r name online map; do
  if [[ "$map" == "-1" ]]; then missing+=("$name")
  elif [[ "$online" != "1" ]]; then offline+=("$name")
  fi
done < <(wsg_team_status)

if [[ "${#missing[@]}" -gt 0 ]]; then
  echo "Missing ${#missing[@]} bots — create these names only:"
  # Print create lines only for missing names
  for i in "${!WSG_NAMES[@]}"; do
    for m in "${missing[@]}"; do
      if [[ "${WSG_NAMES[$i]}" == "$m" ]]; then
        wsg_create_line "${WSG_NAMES[$i]}" "${WSG_CLASS[$i]}" "${WSG_RACE[$i]}" "${WSG_ROLE[$i]}"
      fi
    done
  done | { echo "===== PASTE IN-GAME (GM) — CREATE MISSING ====="; cat; echo "===== END CREATE ====="; }
fi

if [[ "${#offline[@]}" -gt 0 ]]; then
  wsg_print_add_block "${offline[@]}"
fi

echo "Waiting up to ${ONLINE_TIMEOUT_SEC}s for team bots online..."
deadline=$((SECONDS + ONLINE_TIMEOUT_SEC))
last_print=$SECONDS
nap=$REPRINT_SEC; (( nap > 5 )) && nap=5   # poll at least as fast as the re-print cadence
while (( SECONDS < deadline )); do
  online_n=0
  while read -r name online map; do
    [[ "$online" == "1" ]] && online_n=$((online_n+1))
  done < <(wsg_team_status)
  echo "$(date -u +%H:%M:%S) online_team=${online_n}/20"
  if [[ "$online_n" -ge 20 ]]; then break; fi

  # Re-print create/add blocks for still-missing/offline bots every REPRINT_SEC (default 60)s
  missing=()
  offline=()
  while read -r name online map; do
    if [[ "$map" == "-1" ]]; then missing+=("$name")
    elif [[ "$online" != "1" ]]; then offline+=("$name"); fi
  done < <(wsg_team_status)
  if (( SECONDS - last_print >= REPRINT_SEC )); then
    last_print=$SECONDS
    if [[ "${#missing[@]}" -gt 0 ]]; then
      for i in "${!WSG_NAMES[@]}"; do
        for m in "${missing[@]}"; do
          if [[ "${WSG_NAMES[$i]}" == "$m" ]]; then
            wsg_create_line "${WSG_NAMES[$i]}" "${WSG_CLASS[$i]}" "${WSG_RACE[$i]}" "${WSG_ROLE[$i]}"
          fi
        done
      done | { echo "===== PASTE IN-GAME (GM) — CREATE MISSING ====="; cat; echo "===== END CREATE ====="; }
    fi
    if [[ "${#offline[@]}" -gt 0 ]]; then
      wsg_print_add_block "${offline[@]}"
    fi
  fi
  sleep "$nap"
done

online_n=0
while read -r name online map; do
  [[ "$online" == "1" ]] && online_n=$((online_n+1))
done < <(wsg_team_status)
if [[ "$online_n" -lt 20 ]]; then
  echo "TIMEOUT: only ${online_n}/20 team bots online"
  echo "HINT: paste create/add blocks above; confirm cache-fixed mangosd; .rndbot list"
  exit 1
fi

echo "All 20 online. Ensure AutoJoin is live (.rndbot reload if conf CHANGED)."
echo "Optional while testing fills: .bgtest"
echo "Waiting for WSG map 489 (soft ${SOFT_ALLY}/${SOFT_HORDE})..."
deadline=$((SECONDS + FILL_TIMEOUT_SEC))

while (( SECONDS < deadline )); do
  a="$(wsg_count_on_wsg A)"
  h="$(wsg_count_on_wsg H)"
  echo "$(date -u +%H:%M:%S) wsg_ally=${a} wsg_horde=${h}"
  if [[ "$a" -ge "$SOFT_ALLY" && "$h" -ge "$SOFT_HORDE" ]]; then
    bot="$(wsg_pick_spectate_bot)"
    echo "WSG_READY ally=$a horde=$h spectate_target=$bot"
    wsg_print_spectate_block "$bot"
    if [[ "$a" -lt 10 || "$h" -lt 10 ]]; then
      echo "WARNING: soft threshold met but not full 10/10 (healers sometimes skip queue)."
    fi
    exit 0
  fi
  sleep 10
done

echo "TIMEOUT waiting for WSG fill"
echo "=== Final team status (name online map) ==="
wsg_team_status
echo "HINT: funnel is ON — bots cannot seed other BGs. If bots sit on maps != 489, confirm tcm-mangosd was restarted AFTER 'wsg-only-mode.sh on' (template loads at startup only; the DB preflight cannot see a missed restart). Testing fallback: .bgtest"
exit 1
