#!/usr/bin/env bash
# flash_master.sh — сборка и прошивка мастер-узла (сабвуфер).
# Использование: ./scripts/flash_master.sh [PORT]
# Целевой env — master_s3_wifi (ESP32-S3, Wi-Fi UDP источник, C6.4).
# Сборка идёт через platformio.master.ini (изолированный core 3.x).
set -euo pipefail

cd "$(dirname "$0")/.."

PORT="${1:-/dev/ttyUSB0}"

echo "==> Генерация конфига"
python3 scripts/generate_config.py config.env

echo "==> Сборка мастера (master_s3_wifi)"
pio run -c platformio.master.ini -e master_s3_wifi

echo "==> Прошивка на ${PORT}"
pio run -c platformio.master.ini -e master_s3_wifi -t upload --upload-port "${PORT}"

echo "==> Монитор"
pio device monitor -p "${PORT}" -b 115200