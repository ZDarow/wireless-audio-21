#!/usr/bin/env bash
# flash_master.sh — сборка и прошивка мастер-узла (сабвуфер).
# Использование: ./scripts/flash_master.sh [PORT]
set -euo pipefail

cd "$(dirname "$0")/.."

PORT="${1:-/dev/ttyUSB0}"

echo "==> Генерация конфига"
python3 scripts/generate_config.py config.env

echo "==> Сборка мастера"
pio run -e master_a2dp

echo "==> Прошивка на ${PORT}"
pio run -e master_a2dp -t upload --upload-port "${PORT}"

echo "==> Монитор"
pio device monitor -p "${PORT}" -b 115200