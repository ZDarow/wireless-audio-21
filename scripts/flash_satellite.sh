#!/usr/bin/env bash
# flash_satellite.sh — сборка и прошивка сателлита (left/right).
# Использование: ./scripts/flash_satellite.sh [left|right] [PORT]
set -euo pipefail

cd "$(dirname "$0")/.."

SIDE="${1:-left}"
PORT="${2:-/dev/ttyUSB0}"

case "${SIDE}" in
  left)  ENV="satellite_left" ;;
  right) ENV="satellite_right" ;;
  *) echo "ОШИБКА: сторона должна быть left или right" >&2; exit 1 ;;
esac

echo "==> Генерация конфига"
python3 scripts/generate_config.py config.env

echo "==> Сборка сателлита (${SIDE})"
pio run -e "${ENV}"

echo "==> Прошивка на ${PORT}"
pio run -e "${ENV}" -t upload --upload-port "${PORT}"

echo "==> Монитор"
pio device monitor -p "${PORT}" -b 115200