#!/usr/bin/env bash
# Remove per-match artifact directories written by wsg-kickoff.sh.
#
# Dry-run by default: nothing is deleted without --yes.
#
# This script can only ever touch $WSG_RUN_ROOT. It has no code path reaching the
# server's own logs — that boundary is deliberate and is asserted by the tests,
# because this is the script most likely to be run carelessly. The server's own bot
# log has been seen at 11.7 GB; nothing here may go near it. The tests assert that
# even its filename never appears in this file, so keep it that way.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN_ROOT="${WSG_RUN_ROOT:-$SCRIPT_DIR/../logs/wsg-matches}"

KEEP=""; OLDER=""; ALL=0; YES=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --keep-last)  KEEP="$2"; shift 2 ;;
    --older-than) OLDER="$2"; shift 2 ;;
    --all)        ALL=1; shift ;;
    --yes)        YES=1; shift ;;
    *) echo "usage: $0 [--keep-last N | --older-than 7d | --all] [--yes]" >&2; exit 2 ;;
  esac
done

[[ -d "$RUN_ROOT" ]] || { echo "no run directory at $RUN_ROOT — nothing to do"; exit 0; }
mapfile -t dirs < <(find "$RUN_ROOT" -mindepth 1 -maxdepth 1 -type d | sort)
[[ "${#dirs[@]}" -gt 0 ]] || { echo "no match artifacts in $RUN_ROOT"; exit 0; }

victims=()
if [[ "$ALL" -eq 1 ]]; then
  victims=("${dirs[@]}")
elif [[ -n "$KEEP" ]]; then
  # Run directories are ISO-8601 stamped, so lexical sort is chronological.
  # `if`, not `&&`: under set -e a false (( )) would abort the script when
  # --keep-last is larger than the number of runs.
  drop=$(( ${#dirs[@]} - KEEP ))
  if (( drop > 0 )); then victims=("${dirs[@]:0:drop}"); fi
elif [[ -n "$OLDER" ]]; then
  mapfile -t victims < <(find "$RUN_ROOT" -mindepth 1 -maxdepth 1 -type d -mtime "+${OLDER%d}")
else
  echo "DRY RUN — no selector given. ${#dirs[@]} match artifacts in $RUN_ROOT:"
  printf '  %s\n' "${dirs[@]##*/}"
  echo "Pass --keep-last N, --older-than 7d, or --all (add --yes to actually delete)."
  exit 0
fi

if [[ "${#victims[@]}" -eq 0 ]]; then echo "nothing matches — ${#dirs[@]} kept"; exit 0; fi

if [[ "$YES" -eq 0 ]]; then
  echo "DRY RUN — would remove ${#victims[@]} of ${#dirs[@]}:"
  printf '  %s\n' "${victims[@]##*/}"
  echo "Re-run with --yes to delete."
  exit 0
fi

for d in "${victims[@]}"; do
  case "$d" in
    "$RUN_ROOT"/*) rm -rf "$d"; echo "removed ${d##*/}" ;;
    *) echo "REFUSING: $d is outside $RUN_ROOT" >&2; exit 1 ;;
  esac
done
echo "removed ${#victims[@]}; $(( ${#dirs[@]} - ${#victims[@]} )) kept"
