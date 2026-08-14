#!/usr/bin/env bash
# Backup live Turtle WoW V2 configs before alive-world population changes.
set -euo pipefail

ROOT="${HOME}/tortoise-wow-server-V2"
STAMP="$(date +%Y%m%d-%H%M%S)"
DEST="${ROOT}/backups/pre-alive-world-${STAMP}"
WIN="/mnt/d/TurtleWow/backups/pre-alive-world-${STAMP}"

mkdir -p "$DEST" "$WIN"

cp -a "${ROOT}/etc/aiplayerbot.conf" "$DEST/"
cp -a "${ROOT}/etc/aiplayerbot.conf.orig1000" "$DEST/" 2>/dev/null || true
cp -a "${ROOT}/etc/ahbot.conf" "$DEST/"
cp -a "${ROOT}/etc/mangosd.conf" "$DEST/"
cp -a "${ROOT}/etc/realmd.conf" "$DEST/"
cp -a "${ROOT}/docker-compose.yml" "$DEST/"
cp -a "${ROOT}/.env" "$DEST/" 2>/dev/null || true

{
  echo "stamp=${STAMP}"
  echo "source=${ROOT}"
  echo "host_time=$(date -Iseconds)"
  echo "---"
  grep -nE '^AiPlayerbot\.(Min|Max)RandomBots|^AiPlayerbot\.DisableActivityPriorities|^AiPlayerbot\.botActiveAlone|^AiPlayerbot\.ForceActiveWhenNearPlayer|^AiPlayerbot\.RandomBotTeleportNearPlayer|^AhBot\.(Enabled|GUID)|^LFT\.BotFill\.DelaySeconds|^PlayerHardLimit' \
    "${DEST}/aiplayerbot.conf" "${DEST}/ahbot.conf" "${DEST}/mangosd.conf" 2>/dev/null || true
  echo "---"
  ls -la "$DEST"
} | tee "${DEST}/MANIFEST.txt"

cp -a "${DEST}/." "$WIN/"

echo "WSL_BACKUP=${DEST}"
echo "WIN_BACKUP=D:/TurtleWow/backups/pre-alive-world-${STAMP}"
