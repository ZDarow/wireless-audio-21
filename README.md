# Wireless Audio 2.1

[![CI](https://img.shields.io/github/actions/workflow/status/ZDarow/wireless-audio-21/ci.yml?branch=main&label=CI)](https://github.com/ZDarow/wireless-audio-21/actions)
[![License: GPL-3.0](https://img.shields.io/github/license/ZDarow/wireless-audio-21)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-6.1.19-orange)](https://platformio.org)
[![ESP32-S3](https://img.shields.io/badge/ESP32--S3-blue)](https://www.espressif.com/en/products/socs/esp32-s3)

Беспроводная аудиосистема 2.1 на **ESP32-S3**: мастер (сабвуфер) принимает звук
по **Wi-Fi UDP PCM** со смартфона (S3 не поддерживает A2DP), обрабатывает его
DSP-конвейером (громкость → тембр → лимитер → кроссовер), разделяет на каналы
и передаёт на два беспроводных сателлита (левый/правый) по **ESP-NOW** или
**UDP**.

```
Smartphone ──Wi-Fi UDP PCM──► ┌───────────────────────────┐
                              │  MASTER (сабвуфер)        │
                              │  DSP → splitter           │
                              │   ├─ L → TX ─────────────┼──► LEFT SATELLITE
                              │   ├─ R → TX ─────────────┼──► RIGHT SATELLITE
                              │   └─ Sub → I2S ──────────┼──► сабвуфер (локально)
                              └───────────────────────────┘
                                    serial / REST / OLED (управление)
```

## Возможности (MVP)

- Wi-Fi UDP PCM вход (смартфон → мастер, magic 0xA210, ТЗ §10)
- DSP-конвейер: громкость (0–100) + покомпонентные L/R/Sub, тембр, лимитер,
  кроссовер **Linkwitz-Riley 4** (70–120 Гц, дефолт 90 Гц)
- Задержки каналов L/R/Sub **0–200 мс** (выравнивание времени прихода)
- Транспорт: **ESP-NOW** (низкая задержка, без роутера) или **UDP** (по Wi-Fi)
- Пакетный формат: 16-байт заголовок + payload до 234 байт (лимит ESP-NOW)
- Сателлиты: приём → jitter buffer → задержка → громкость (fade) → I2S
- Serial-консоль: `status`, `volume`, `crossover`, `delay`, `pair`, `save`, `reboot`
- Web UI + REST API (мастер): громкость, кроссовер, задержки, транспорт, pair,
  Wi-Fi-настройка, OTA, логи с фильтрами, диагностика
- Режим репитера APSTA+NAPT (смартфон → AP мастера → интернет)
- NVS-хранение настроек (Preferences)
- Host-тесты чистых модулей (gcc, без железа)

## Структура проекта

```
wireless-audio-21/
├── platformio.ini              # env: master_s3_wifi, satellite_s3_left/right (+ legacy)
├── platformio.master.ini       # master_s3_wifi: изолированный core 3.x
├── config.example.env          # конфигурация → generated_config.h
├── firmware/
│   ├── common/                 # header-only, общий код
│   │   ├── config/   node_config.h, storage.h
│   │   ├── audio/    crossover.h, delay_line.h, volume_control.h,
│   │   │            pcm_pipeline.h, jitter_buffer.h, udp_audio_receiver.h,
│   │   │            i2s_output.h
│   │   ├── transport/ audio_packet.h, espnow.h, udp_transport.h,
│   │   │            udp_audio_packet.h, broadcast_ip.h
│   │   ├── ui/       display.h, encoder.h
│   │   ├── web/      auth.h, internet_check.h, logs.h, wifi_store.h
│   │   └── util/     logger.h, timing.h
│   ├── master_s3/    src/main.cpp, include/master_s3_config.h (целевой, S3)
│   ├── master/       src/main.cpp, common/web/web_server.h (общий Web UI, legacy A2DP)
│   └── satellite/    src/main.cpp
├── scripts/                    # generate_config.py, flash_master.sh, flash_satellite.sh
├── test/                       # host-тесты (make test) + stubs/
└── docs/                       # PLAN, TASKS, architecture, hardware, wiring, REPO_AUDIT
```

## Быстрый старт

### 1. Конфигурация

```bash
cp config.example.env config.env
# отредактируйте config.env: Wi-Fi, MAC сателлитов, частоты, GPIO
python3 scripts/generate_config.py config.env
```

### 2. Прошивка

```bash
# Мастер (сабвуфер)
./scripts/flash_master.sh /dev/ttyUSB0

# Сателлит (левый / правый)
./scripts/flash_satellite.sh left  /dev/ttyUSB1
./scripts/flash_satellite.sh right /dev/ttyUSB2
```

### 3. Host-тесты (без железа)

```bash
cd test && make test
```

### 4. Web UI (мастер)

При подключённом Wi-Fi мастер поднимает веб-панель управления:

```
http://<IP-мастера>/
http://audio-master.local/   # mDNS (если клиент поддерживает)
```

Адрес печатается в serial-консоль при старте (`Web UI: http://192.168.x.x`).
Доступно: громкость, mute, кроссовер, задержки каналов, транспорт, привязка
сателлитов, сохранение в NVS, перезагрузка. REST API:

```
GET  /api/status            # состояние узла (JSON)
PUT  /api/volume            # {"volume":60} | {"channel":"sub","volume":50} | {"mute":true}
PUT  /api/crossover         # {"hz":90}
PUT  /api/delay             # {"channel":"left","ms":10}
POST /api/mute              # {"mute":true}
POST /api/transport         # {"mode":"espnow"}
POST /api/pair              # {"side":"left","mac":"AA:BB:CC:DD:EE:01"}
POST /api/save              # сохранить в NVS
POST /api/system/reboot     # перезагрузка
POST /api/system/factory_reset  # сброс к заводским настройкам
GET  /api/system/config/export  # экспорт конфига (JSON)
POST /api/system/config/import  # импорт конфига (JSON)
GET  /api/wifi/status       # состояние Wi-Fi
GET  /api/wifi/scan         # скан сетей
POST /api/wifi/connect      # {"ssid","password"}
POST /api/wifi/save|forget  # управление профилями
GET  /api/wifi/profiles     # сохранённые сети
GET  /api/net/internet      # статус интернета
GET  /api/logs?level=1&module=WIFI   # логи с фильтрами
GET  /api/diagnostics       # heap/PSRAM/cpu_load/uptime
POST /api/login|logout      # аутентификация (SHA-256+соль, CSRF, rate limit)
POST /api/update            # OTA-прошивка (с прогресс-баром)
```

Web UI доступен только из локальной подсети (STA/AP); API защищён CSRF-токеном
и rate limit. Готовые примеры запросов для REST Client (VS Code):
[docs/api.http](docs/api.http).

### 5. Serial-консоль мастера

```
status                  # текущее состояние
volume 60               # громкость 0–100
volume mute / unmute    # глушение
crossover 90            # частота раздела 70–120 Гц
delay left 10           # задержка канала, мс
transport espnow|udp    # переключение транспорта
pair left AA:BB:CC:DD:EE:01   # привязка сателлита
save                    # сохранить в NVS
reboot
```

## Требования

- PlatformIO (`pipx install platformio==6.1.19`) — фиксированная версия (T18)
- Python 3.10+
- gcc (для host-тестов)

Подробности:
- [docs/PLAN.md](docs/PLAN.md) — план, требования, карта расширений
- [docs/architecture.md](docs/architecture.md) — архитектура, потоки данных, REST API
- [docs/hardware.md](docs/hardware.md) — железо, GPIO, питание
- [docs/wiring.md](docs/wiring.md) — схемы подключения
- [docs/TASKS.md](docs/TASKS.md) — файл задач (техдолг, тесты, фичи)
- [docs/REPO_AUDIT.md](docs/REPO_AUDIT.md) — аудит репозитория (стандарты, безопасность, ресурсы для лендинга)
- [docs/GIT_AUDIT.md](docs/GIT_AUDIT.md) — глубокий аудит Git (история, ветвление, CI/CD, секреты, зависимости)

## Лицензии

Проект распространяется под **GPL-3.0** (см. [LICENSE](LICENSE)): legacy-стенд
`master_a2dp` использует библиотеку `arduino-audio-tools` (GPL-3.0), что делает
сборку производной работой.

| Зависимость | Где | Лицензия |
|---|---|---|
| [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) (SHA-пин) | legacy `master_a2dp` | GPL-3.0 |
| [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) (SHA-пин) | legacy `master_a2dp` | Apache-2.0 |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | `master_s3_wifi` | MIT |
| PlatformIO / Arduino core / ESP-IDF | все env | Apache-2.0 (Espressif) |
