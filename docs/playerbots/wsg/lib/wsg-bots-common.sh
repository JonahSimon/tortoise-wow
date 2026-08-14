#!/usr/bin/env bash
# Shared helpers for WSG bot match scripts. Source this file; do not execute.

WSG_NAMES=()
WSG_CLASS=()
WSG_RACE=()
WSG_ROLE=()
WSG_FACTION=()

wsg_load_roster() {
  local file="$1"
  WSG_NAMES=(); WSG_CLASS=(); WSG_RACE=(); WSG_ROLE=(); WSG_FACTION=()
  local name class race role faction
  while IFS='|' read -r name class race role faction || [[ -n "${name:-}" ]]; do
    # Strip CR so CRLF checkouts (Windows) still match faction/name compares.
    name="${name%$'\r'}"; class="${class%$'\r'}"; race="${race%$'\r'}"
    role="${role%$'\r'}"; faction="${faction%$'\r'}"
    [[ -z "${name:-}" || "$name" =~ ^# ]] && continue
    WSG_NAMES+=("$name")
    WSG_CLASS+=("$class")
    WSG_RACE+=("$race")
    WSG_ROLE+=("$role")
    WSG_FACTION+=("$faction")
  done < "$file"
}

wsg_create_line() {
  local name="$1" class="$2" race="$3" role="$4"
  printf '.rndbot create name=%s class=%s race=%s level=60 role=%s gear=blue login=1 group=\n' \
    "$name" "$class" "$race" "$role"
}

WSG_SERVER_ROOT="${WSG_SERVER_ROOT:-$HOME/tortoise-wow-server-V2}"

# Ensures one conf key has the wanted value. Prints CHANGED|UNCHANGED.
# A commented-out key (e.g. "# AiPlayerbot.Foo = 1") is treated as absent and
# the active line is appended at the end.
wsg_ensure_conf_key() {
  local conf="$1" key="$2" value="$3"
  local kre="${key//./\\.}"
  if grep -qE "^[[:space:]]*${kre}[[:space:]]*=[[:space:]]*${value}[[:space:]]*$" "$conf"; then
    echo "UNCHANGED"
    return 0
  fi
  if grep -qE "^[[:space:]]*${kre}[[:space:]]*=" "$conf"; then
    sed -i "s|^[[:space:]]*${kre}[[:space:]]*=.*|${key} = ${value}|" "$conf"
  else
    printf '\n%s = %s\n' "$key" "$value" >> "$conf"
  fi
  echo "CHANGED"
}

# Match-session profile: AutoJoin on, noLag gate bypassed, roster bots never
# timed out. Prints CHANGED if any key changed (caller tells the GM to
# `.rndbot reload`); always exits 0.
wsg_ensure_match_conf() {
  local conf="$1" r changed=0
  # 0, not 1: with the isUseful patch the kickoff script queues bots explicitly, so
  # ambient auto-join would only start matches we did not ask for. This is also the
  # alive-world default, so match mode no longer departs from it.
  r="$(wsg_ensure_conf_key "$conf" "AiPlayerbot.RandomBotAutoJoinBG" 0)"
  [[ "$r" == "CHANGED" ]] && changed=1
  r="$(wsg_ensure_conf_key "$conf" "AiPlayerbot.DisableActivityPriorities" 1)"
  [[ "$r" == "CHANGED" ]] && changed=1
  r="$(wsg_ensure_conf_key "$conf" "AiPlayerbot.RandomBotTimedLogout" 0)"
  [[ "$r" == "CHANGED" ]] && changed=1
  [[ "$changed" -eq 1 ]] && echo "CHANGED" || echo "UNCHANGED"
}

wsg_db_pass() {
  # Preferred: .dbpass in the server checkout. Fallback: ask tcm-db itself —
  # the checkout may live elsewhere (or not exist) on this machine, and every
  # caller only ever talks to the tcm-db container anyway.
  if [[ -r "${WSG_SERVER_ROOT}/.dbpass" ]]; then
    tr -d '\r\n' < "${WSG_SERVER_ROOT}/.dbpass"
  else
    docker exec tcm-db printenv MARIADB_ROOT_PASSWORD 2>/dev/null | tr -d '\r\n'
  fi
}

wsg_mysql() {
  local sql="$1"
  docker exec -e MYSQL_PWD="$(wsg_db_pass)" tcm-db mysql -uroot -N -B -e "$sql" 2>/dev/null | tr -d '\r'
}

# Sends lines to the tcm-mangosd console and detaches cleanly.
# NEVER let docker attach's stdin reach EOF without detaching first:
# mangosd treats console EOF as "shut down the world" and the compose
# service is restart:"no" (recovery: docker start tcm-mangosd).
# The pty wrapper (`script`) is required because the container runs with
# tty=true, and plain piped `docker attach` refuses non-tty stdin.
# Console commands take NO leading dot and run as console security.
# Usage: wsg_console "server info" [wait_seconds]
#        wsg_console $'rndbot reload\nserver info' 8
#
# ALWAYS RETURNS 0. Detaching with the detach-keys makes `docker attach` exit
# non-zero, so under `set -o pipefail` this pipeline reports failure on a completely
# successful send. That silently aborted a kickoff whose commands had in fact worked
# — the match popped and the script exited 1 anyway. The exit status here carries no
# information: rndbot replies go to a null player session and vanish, so a failed
# command is indistinguishable from a successful one either way. Confirm from bg.log
# or the DB, never from this return value.
wsg_console() {
  local cmds="$1" wait_s="${2:-4}"
  { printf '%s\n' "$cmds"; sleep "$wait_s"; printf '\x10\x11'; sleep 1; } \
    | timeout $((wait_s + 20)) script -qec 'docker attach --detach-keys "ctrl-p,ctrl-q" tcm-mangosd' /dev/null \
    | tr -d '\r' || true
  return 0
}

# --- Deterministic BG queue commands -----------------------------------------
#
# BGJoinAction::Execute() reads AI_VALUE(uint32, "bg type"); when non-zero it skips
# bgList entirely and calls JoinQueue(<that value>). BATTLEGROUND_QUEUE_WS = 2
# (BattleGround.h:149). DebugAction's setvalueuin32 splits its arguments on COMMAS,
# not spaces, so the value name "bg type" survives intact.
#
# Emit all of these into ONE wsg_console attach. Forty attaches is forty chances to
# EOF the console, which shuts the world down.

WSG_QUEUE_WS=2

# Two lines per bot, interleaved so each set is immediately followed by its join.
wsg_bgjoin_lines() {
  local n
  for n in "$@"; do
    printf 'rndbot debug %s setvalueuin32 bg type,%s\n' "$n" "$WSG_QUEUE_WS"
    printf 'rndbot do %s bg join\n' "$n"
  done
}

# One argument to setvalueuin32 means RESET_AI_VALUE — clears the value.
wsg_bgtype_reset_lines() {
  local n
  for n in "$@"; do
    printf 'rndbot debug %s setvalueuin32 bg type\n' "$n"
  done
}

# DO NOT add a "values" read-back helper here. `rndbot debug <bot> values bg` CRASHES
# THE WORLD: HandleBotDebug dispatches with the bot itself as the reply target when
# there is no master (always, from console), and TellPlayerNoFacing then pushes a
# multi-line chat payload through that bot's socketless WorldSession, overflowing the
# ByteBuffer -- mangosd aborts with SIGABRT. Observed 2026-08-10; see
# docs/evidence/WSG-Debug/2026-08-10-command-surface-validation.md
#
# Confirm queueing from bg.log ("tag BG=2") instead. Every rndbot console reply is
# discarded anyway, so a failed command is indistinguishable from a successful one.

# Returns 0 iff the DB answers a trivial query (docker stack + .dbpass sane).
wsg_check_db() {
  [[ "$(wsg_mysql "SELECT 1;")" == "1" ]]
}

# Blocks until mangosd is far enough into boot to be driving random bots.
# Returns 0 when ready, 1 on timeout. Usage: wsg_wait_world_ready [seconds] [marker]
#
# The probe MUST run with pipefail OFF. `grep -q` exits at its first match and closes
# the pipe, so `docker logs` — which still has a huge stream to write — dies of
# SIGPIPE and reports 141. Under `set -o pipefail` the whole pipeline then reports
# 141, never 0, so an `until ...; do sleep 5; done` around it can NEVER terminate.
# That silently hung `wsg-mode.sh on` forever, which is why the on path had never
# completed live. Same trap as wsg_console's exit status: a successful command
# reporting failure because of how the pipeline ended.
#
# Bounded on purpose. The original loop was unbounded, so the failure looked like a
# slow boot rather than a bug.
wsg_wait_world_ready() {
  local timeout_s="${1:-600}" marker="${2:-ai_playerbot_random_bots}"
  local deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    if (
      set +o pipefail
      docker logs tcm-mangosd --since "$(date -u -d '-5 minutes' +%Y-%m-%dT%H:%M:%SZ)" 2>&1 \
        | grep -qa "$marker"
    ); then
      echo "world ready (${marker} seen in the last 5 minutes)"
      return 0
    fi
    sleep 5
  done
  echo "ERROR: mangosd did not log ${marker} within ${timeout_s}s of restarting." >&2
  echo "Check: docker ps, then docker logs tcm-mangosd --tail 50" >&2
  return 1
}

# Prints: name online map   (MISSING bots: name 0 -1)
wsg_team_status() {
  local names_csv=""
  local n
  for n in "${WSG_NAMES[@]}"; do
    if [[ -z "$names_csv" ]]; then names_csv="'$n'"; else names_csv+=",'$n'"; fi
  done
  local rows
  rows="$(wsg_mysql "SELECT name, online, map FROM tw_char.characters WHERE name IN ($names_csv);")"
  declare -A online_map=()
  declare -A map_map=()
  while read -r name online map; do
    [[ -z "${name:-}" ]] && continue
    online_map["$name"]="$online"
    map_map["$name"]="$map"
  done <<< "$rows"
  for n in "${WSG_NAMES[@]}"; do
    if [[ -z "${online_map[$n]+x}" ]]; then
      printf '%s 0 -1\n' "$n"
    else
      printf '%s %s %s\n' "$n" "${online_map[$n]}" "${map_map[$n]}"
    fi
  done
}

wsg_print_create_block() {
  local i
  echo "===== PASTE IN-GAME (GM) — CREATE MISSING TEAM BOTS ====="
  for i in "${!WSG_NAMES[@]}"; do
    wsg_create_line "${WSG_NAMES[$i]}" "${WSG_CLASS[$i]}" "${WSG_RACE[$i]}" "${WSG_ROLE[$i]}"
  done
  echo "===== END CREATE BLOCK (trailing group= is required) ====="
}

wsg_print_add_block() {
  local names=("$@")
  local n
  echo "===== PASTE IN-GAME (GM) — LOGIN OFFLINE TEAM BOTS ====="
  for n in "${names[@]}"; do
    echo ".rndbot add $n"
  done
  echo "===== END ADD BLOCK ====="
}

wsg_print_spectate_block() {
  local bot="$1"
  cat <<EOF
===== PASTE IN-GAME (GM) — SPECTATE =====
.gm on
.gm visible off
.appear $bot
.hover 1
.god on
===== END SPECTATE (optional: .modify speed 2 / .bg status) =====
EOF
}

# Counts roster bots with online=1 and map=489 for faction A|H
wsg_count_on_wsg() {
  local faction="$1"
  local count=0
  while read -r name online map; do
    local i fac=""
    for i in "${!WSG_NAMES[@]}"; do
      if [[ "${WSG_NAMES[$i]}" == "$name" ]]; then fac="${WSG_FACTION[$i]}"; break; fi
    done
    if [[ "$fac" == "$faction" && "$online" == "1" && "$map" == "489" ]]; then
      count=$((count+1))
    fi
  done < <(wsg_team_status)
  echo "$count"
}

wsg_pick_spectate_bot() {
  while read -r name online map; do
    if [[ "$online" == "1" && "$map" == "489" ]]; then
      echo "$name"
      return 0
    fi
  done < <(wsg_team_status)
  return 1
}
