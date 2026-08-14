#!/usr/bin/env bash
# Switch the world between the alive world and WSG match mode.
#
# Snapshot-based, not hardcoded: `on` records the LIVE value of every lever it is
# about to change, and `off` puts back exactly those values. "How it was" is literal.
#
# `on` refuses to run when a snapshot already exists. Without that guard a second
# `on` would snapshot match-mode values and the real world state would be gone.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/lib/wsg-bots-common.sh"

SNAP="${WSG_SERVER_ROOT}/.wsg-mode-snapshot.json"
AICONF="${WSG_SERVER_ROOT}/etc/aiplayerbot.conf"
MGCONF="${WSG_SERVER_ROOT}/etc/mangosd.conf"
BAK="tw_world.battleground_template_bak_wsg"
GM_ACCOUNT="${WSG_GM_ACCOUNT:-504}"
RESTART=1

# A key that is commented out in the conf is NOT the same as a key set to empty.
# AiPlayerbot.AutoDoQuests ships commented (compiled default true); restoring it as
# "AutoDoQuests = " would silently mean something else. Absent is recorded as this
# sentinel and restored by re-commenting.
ABSENT="<absent>"

usage() { echo "usage: $0 on|off|status [--no-restart] [--profile alive-world] [--force]" >&2; exit 2; }

VERB="${1:-}"; shift || true
PROFILE=""; FORCE=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-restart) RESTART=0; shift ;;
    --restart)    RESTART=1; shift ;;
    --profile)    PROFILE="$2"; shift 2 ;;
    --force)      FORCE=1; shift ;;
    *) usage ;;
  esac
done

conf_get() { grep -E "^[[:space:]]*${2//./\\.}[[:space:]]*=" "$1" 2>/dev/null | tail -1 | sed 's/.*=[[:space:]]*//' | tr -d '\r'; }

# conf_get, but distinguishing "absent/commented" from "present but empty".
conf_get_opt() {
  if grep -qE "^[[:space:]]*${2//./\\.}[[:space:]]*=" "$1" 2>/dev/null; then
    conf_get "$1" "$2"
  else
    printf '%s' "$ABSENT"
  fi
}

# Restore a key to a snapshotted value, re-commenting it when it was absent.
conf_restore() {
  local conf="$1" key="$2" value="$3" kre="${2//./\\.}"
  if [[ "$value" == "$ABSENT" ]]; then
    sed -i "s|^[[:space:]]*${kre}[[:space:]]*=|# ${key} =|" "$conf"
  else
    wsg_ensure_conf_key "$conf" "$key" "$value" >/dev/null
  fi
}

case "$VERB" in
  status)
    if [[ -f "$SNAP" ]]; then echo "MODE: wsg-match (snapshot: $SNAP)"; else echo "MODE: alive-world (no snapshot)"; fi
    printf '%-40s %s\n' "AiPlayerbot.MinRandomBots"             "$(conf_get "$AICONF" AiPlayerbot.MinRandomBots)"
    printf '%-40s %s\n' "AiPlayerbot.MaxRandomBots"             "$(conf_get "$AICONF" AiPlayerbot.MaxRandomBots)"
    printf '%-40s %s\n' "AiPlayerbot.RandomBotAutoJoinBG"       "$(conf_get "$AICONF" AiPlayerbot.RandomBotAutoJoinBG)"
    printf '%-40s %s\n' "AiPlayerbot.DisableActivityPriorities" "$(conf_get "$AICONF" AiPlayerbot.DisableActivityPriorities)"
    printf '%-40s %s\n' "AiPlayerbot.RandomBotTimedLogout"      "$(conf_get "$AICONF" AiPlayerbot.RandomBotTimedLogout)"
    printf '%-40s %s\n' "AiPlayerbot.RandomBotNonCombatStrategies" "$(conf_get_opt "$AICONF" AiPlayerbot.RandomBotNonCombatStrategies)"
    printf '%-40s %s\n' "AiPlayerbot.AutoDoQuests"              "$(conf_get_opt "$AICONF" AiPlayerbot.AutoDoQuests)"
    printf '%-40s %s\n' "BattleGround.PrematureFinishTimer"     "$(conf_get "$MGCONF" BattleGround.PrematureFinishTimer)"
    wsg_check_db && wsg_mysql "SELECT id, min_level, min_players_per_team FROM tw_world.battleground_template ORDER BY id;"
    ;;

  on)
    if [[ -f "$SNAP" && "$FORCE" -eq 0 ]]; then
      echo "ERROR: mode is already ON — a snapshot exists at $SNAP." >&2
      echo "Re-snapshotting would capture match-mode values as 'how it was' and the real" >&2
      echo "world state would be unrecoverable. Run '$0 off' first, or --force if you are sure." >&2
      exit 1
    fi
    wsg_check_db || { echo "FATAL: DB unreachable" >&2; exit 1; }

    cat > "$SNAP" <<JSON
{
  "taken": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "MinRandomBots": "$(conf_get "$AICONF" AiPlayerbot.MinRandomBots)",
  "MaxRandomBots": "$(conf_get "$AICONF" AiPlayerbot.MaxRandomBots)",
  "RandomBotAutoJoinBG": "$(conf_get "$AICONF" AiPlayerbot.RandomBotAutoJoinBG)",
  "RandomBotJoinBG": "$(conf_get "$AICONF" AiPlayerbot.RandomBotJoinBG)",
  "DisableActivityPriorities": "$(conf_get "$AICONF" AiPlayerbot.DisableActivityPriorities)",
  "RandomBotTimedLogout": "$(conf_get "$AICONF" AiPlayerbot.RandomBotTimedLogout)",
  "RandomBotNonCombatStrategies": "$(conf_get_opt "$AICONF" AiPlayerbot.RandomBotNonCombatStrategies)",
  "AutoDoQuests": "$(conf_get_opt "$AICONF" AiPlayerbot.AutoDoQuests)",
  "PrematureFinishTimer": "$(conf_get "$MGCONF" BattleGround.PrematureFinishTimer)",
  "GmRank": "$(wsg_mysql "SELECT rank FROM tw_logon.account WHERE id=${GM_ACCOUNT};")"
}
JSON
    echo "snapshot written: $SNAP"

    # battleground_template is snapshotted as a whole table so every column can be
    # restored — the old wsg-only-mode.sh only ever restored min_level.
    wsg_mysql "DROP TABLE IF EXISTS ${BAK};"
    wsg_mysql "CREATE TABLE ${BAK} AS SELECT * FROM tw_world.battleground_template;"
    wsg_mysql "UPDATE tw_world.battleground_template SET min_level = 61 WHERE id <> 2;"
    wsg_mysql "UPDATE tw_world.battleground_template SET min_players_per_team = 10 WHERE id = 2;"
    wsg_mysql "UPDATE tw_logon.account SET rank=4 WHERE id=${GM_ACCOUNT};"

    wsg_ensure_conf_key "$AICONF" AiPlayerbot.MinRandomBots 40 >/dev/null
    wsg_ensure_conf_key "$AICONF" AiPlayerbot.MaxRandomBots 40 >/dev/null
    wsg_ensure_conf_key "$AICONF" AiPlayerbot.RandomBotJoinBG 1 >/dev/null
    wsg_ensure_conf_key "$AICONF" AiPlayerbot.RandomBotAutoJoinBG 0 >/dev/null
    wsg_ensure_conf_key "$AICONF" AiPlayerbot.DisableActivityPriorities 1 >/dev/null
    wsg_ensure_conf_key "$AICONF" AiPlayerbot.RandomBotTimedLogout 0 >/dev/null
    wsg_ensure_conf_key "$MGCONF" BattleGround.PrematureFinishTimer 0 >/dev/null

    # Settle the roster. Bots are not idle between matches — they grind, quest and
    # cross continents, and isUseful() gates on IsInCombat(), so a busy bot silently
    # cannot be queued. AiFactory applies this string AFTER adding grind/travel/rpg,
    # so a '-' prefix removes them. Measured: one priest went from 0/15 commanded
    # joins to queueing in the same second as everyone else.
    # This does not touch in-BG behaviour — AiFactory:1099 strips the same strategies
    # on BG entry anyway and adds "battleground" and "warsong" in their place.
    wsg_ensure_conf_key "$AICONF" AiPlayerbot.RandomBotNonCombatStrategies \
      "-grind,-travel,-rpg,-wander,-tfish,+custom::say" >/dev/null
    wsg_ensure_conf_key "$AICONF" AiPlayerbot.AutoDoQuests 0 >/dev/null

    echo "WSG match mode ON. Recommended before large world-DB changes: Backup-TurtleDatabase -IncludeWorld"
    if [[ "$RESTART" -eq 1 ]]; then
      docker restart tw2-mangosd
      wsg_wait_world_ready || exit 1
      if [[ "${WSG_SKIP_ROSTER:-0}" != "1" ]]; then
        bash "$SCRIPT_DIR/wsg-roster.sh" ensure
        # Park the roster in towns. rndbot rpg targets race-appropriate RPG
        # locations rather than the mob grind spots that `teleport`/`grind` pick,
        # and it puts travel on a 10-minute cooldown. WSG does not heal on entry,
        # so a bot mauled while parked in Winterspring would be ported in hurt.
        wsg_load_roster "$SCRIPT_DIR/wsg-team-roster.txt"
        wsg_console "$(printf 'rndbot rpg %s\n' "${WSG_NAMES[@]}")" 20 >/dev/null
      fi
    else
      echo "ACTION REQUIRED: docker restart tw2-mangosd   (battleground_template is read at boot)"
      echo "Then: wsg-roster.sh ensure"
    fi
    ;;

  off)
    if [[ ! -f "$SNAP" ]]; then
      if [[ "$PROFILE" != "alive-world" ]]; then
        echo "mode is already off (no snapshot at $SNAP)"
        exit 0
      fi
      echo "no snapshot — applying the documented alive-world profile"
      MinRandomBots=200; MaxRandomBots=200; RandomBotAutoJoinBG=0; RandomBotJoinBG=1
      DisableActivityPriorities=0; RandomBotTimedLogout=1; PrematureFinishTimer=300000; GmRank=3
      RandomBotNonCombatStrategies="+grind,+loot,+custom::say,+tfish,+wander,+rpg craft"
      AutoDoQuests="$ABSENT"
    else
      get() { grep -o "\"$1\": *\"[^\"]*\"" "$SNAP" | sed 's/.*: *"//; s/"$//'; }
      MinRandomBots="$(get MinRandomBots)";           MaxRandomBots="$(get MaxRandomBots)"
      RandomBotAutoJoinBG="$(get RandomBotAutoJoinBG)"; RandomBotJoinBG="$(get RandomBotJoinBG)"
      DisableActivityPriorities="$(get DisableActivityPriorities)"
      RandomBotTimedLogout="$(get RandomBotTimedLogout)"
      RandomBotNonCombatStrategies="$(get RandomBotNonCombatStrategies)"
      AutoDoQuests="$(get AutoDoQuests)"
      PrematureFinishTimer="$(get PrematureFinishTimer)"; GmRank="$(get GmRank)"
    fi
    wsg_check_db || { echo "FATAL: DB unreachable" >&2; exit 1; }

    # Log the roster out; never delete the characters.
    if [[ "${WSG_SKIP_ROSTER:-0}" != "1" ]]; then
      wsg_load_roster "$SCRIPT_DIR/wsg-team-roster.txt"
      wsg_console "$(printf 'rndbot remove %s\n' "${WSG_NAMES[@]}")" 15 >/dev/null || true
    fi

    if [[ "$(wsg_mysql "SHOW TABLES IN tw_world LIKE 'battleground_template_bak_wsg';")" == "battleground_template_bak_wsg" ]]; then
      wsg_mysql "UPDATE tw_world.battleground_template t JOIN ${BAK} b ON t.id=b.id SET t.min_level=b.min_level, t.min_players_per_team=b.min_players_per_team, t.max_players_per_team=b.max_players_per_team;"
    else
      echo "WARNING: ${BAK} missing — battleground_template left as-is" >&2
    fi

    conf_restore "$AICONF" AiPlayerbot.MinRandomBots "$MinRandomBots"
    conf_restore "$AICONF" AiPlayerbot.MaxRandomBots "$MaxRandomBots"
    conf_restore "$AICONF" AiPlayerbot.RandomBotJoinBG "$RandomBotJoinBG"
    conf_restore "$AICONF" AiPlayerbot.RandomBotAutoJoinBG "$RandomBotAutoJoinBG"
    conf_restore "$AICONF" AiPlayerbot.DisableActivityPriorities "$DisableActivityPriorities"
    conf_restore "$AICONF" AiPlayerbot.RandomBotTimedLogout "$RandomBotTimedLogout"
    conf_restore "$AICONF" AiPlayerbot.RandomBotNonCombatStrategies "$RandomBotNonCombatStrategies"
    conf_restore "$AICONF" AiPlayerbot.AutoDoQuests "$AutoDoQuests"
    conf_restore "$MGCONF" BattleGround.PrematureFinishTimer "$PrematureFinishTimer"
    wsg_mysql "UPDATE tw_logon.account SET rank=${GmRank} WHERE id=${GM_ACCOUNT};"

    # Archive rather than delete, so a mistaken `off` is still recoverable.
    [[ -f "$SNAP" ]] && mv "$SNAP" "${SNAP%.json}.$(date -u +%Y%m%dT%H%M%SZ).json"

    echo "Alive world restored. The pool climbs back to ${MaxRandomBots} over ~20 minutes."
    if [[ "$RESTART" -eq 1 ]]; then docker restart tw2-mangosd
    else echo "ACTION REQUIRED: docker restart tw2-mangosd"; fi
    ;;

  *) usage ;;
esac
