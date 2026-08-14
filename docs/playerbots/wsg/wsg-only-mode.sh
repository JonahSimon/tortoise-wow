#!/usr/bin/env bash
# DEPRECATED — superseded by wsg-mode.sh, which owns all ten world levers and
# restores every battleground_template column rather than only min_level.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
echo "NOTE: wsg-only-mode.sh is deprecated — delegating to wsg-mode.sh $*" >&2
exec bash "$SCRIPT_DIR/wsg-mode.sh" "$@" --no-restart
