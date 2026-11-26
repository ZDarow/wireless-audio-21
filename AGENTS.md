# AGENTS.md — руководство для агентов и разработчиков

Беспроводная аудиосистема 2.1 на ESP32: мастер (сабвуфер) принимает звук по A2DP,
обрабатывает DSP-конвейером и передаёт на два беспроводных сателлита (ESP-NOW/UDP).

## Команды

- **Host-тесты** (без железа, чистые модули):
  ```
  cd test && make test          # Linux/macOS
  mingw32-make -C test test     # Windows (MinGW), если доступен sh
  ```
  Если make/sh недоступны — компилировать вручную:
  ```
  g++ -std=c++17 -Wall -Wextra -O2 -I. -I../firmware/common/audio \
      -I../firmware/common/transport -I../firmware/common/util \
      -I../firmware/common/config audio_filter_test.cpp -o audio_filter_test_bin.exe -lm
  ```
- **Сборка прошивки** (ESP-IDF v5.3.1, три проекта — мастер + два сателлита):
  ```
  source /home/mi/esp/esp-idf-v5.3.1/export.sh && idf.py build
  # в каждую из: firmware/master_s3_idf, firmware/satellite_s3_idf_left,
  # firmware/satellite_s3_idf_right
  idf.py -p /dev/ttyACM0 flash   # после erase_flash при смене прошивки
  ```
  Legacy env (`master_a2dp`, `satellite_left/right`, espressif32@6.9.0 core
  2.0.17) — отладочный стенд (C0.2), в CI не собираются; оставлять рабочими.
  `master_a2dp` собирается: `pio run -e master_a2dp`.
  ESP32-S3 мастера и сателлитов перенесены на чистый IDF (см. раздел
  «ESP-IDF миграция» ниже); `platformio.master.ini` и env `master_s3_wifi` /
  `satellite_s3_left/right` удалены из platformio.ini.
  PlatformIO может быть не установлен на машине разработчика — проверять код на
  хосте через host-тесты, сборку оставлять CI.
- **Генерация конфига**:
  ```
  python3 scripts/generate_config.py config.env
  ```
  Результат — `firmware/common/generated/generated_config.h` (gitignored).

## Архитектура (кратко)

- Общий код — **header-only** в `firmware/common` (config/audio/transport/web/ui/util),
  подключается через `build_flags -I` в `platformio.ini`.
- Роли (master/satellite) изолируются `build_src_filter` по env, не препроцессором.
- **Целевая платформа — ESP32-S3** (мастер = сабвуфер). S3 не поддерживает A2DP,
  поэтому источник аудио — Wi-Fi UDP PCM со смартфона (`firmware/master_s3/`).
- Поток мастера (реализовано, C3.1/C3.2): Wi-Fi UDP → jitter buffer → DSP (tone →
  limiter → volume → LR4 crossover) → left/right → `DelayLine` → канальная
  громкость (C2.2) → батч 117 семплов → ESP-NOW/UDP; sub → `DelayLine` →
  канальная громкость → I2S.
- Поток сателлита: RX → `parsePacket` → `JitterBuffer` → `DelayLine` →
  `VolumeControl` (громкость + fade, C3.5) → I2S.
- Пакет мастер→сателлит: 16-байт заголовок `AudioPacketHeader` + payload ≤ 234 байт.
  Пакет смартфон→мастер — отдельный (magic 0xA210, см. ТЗ §10).
- Управление: serial-консоль, Web UI + REST API — `web_server.h` **общий** для
  `master_s3` и legacy `master` (`firmware/common/web/web_server.h`), включает
  auth (SHA-256+соль, CSRF, rate limit), Wi-Fi-профили, OTA, логи, диагностику.
- Режим Wi-Fi мастера: `AP_DIRECT` (AP без интернета), `STA` (клиент домашней сети)
  или `APSTA` — репитер: смартфон → AP мастера → NAT → домашняя сеть (интернет).
- **`master_s3_wifi` собирается на pioarduino-платформе (Arduino core 3.3.11 /
  IDF 5.5.5, тег `55.03.311`)**: только там lwip собран с
  `CONFIG_LWIP_IP_FORWARD`/`CONFIG_LWIP_IPV4_NAPT` и работает
  `esp_netif_napt_enable()`. Остальные env остаются на `espressif32@6.9.0`
  (core 2.0.17, NAPT недоступен).

## Соглашения

- C++17, header-only модули без зависимостей от Arduino-железа (для host-тестов).
- Конфигурация — макросы из `generated_config.h` (генерируется), дефолты в
  `node_config.h`; правки конфига — через `config.env`, не вручную.
- Вся документация по проекту — в `docs/` (`PLAN.md` — план/требования,
  `TASKS.md` — задачи/техдолг, `architecture.md`, `hardware.md`, `wiring.md`,
  `REPO_AUDIT.md` — аудит репозитория, `GIT_AUDIT.md` — аудит Git-процессов).
- Обновлять `docs/TASKS.md` при закрытии/добавлении задач; держать
  `AGENTS.md`/`README.md`/`architecture.md` синхронизированными с кодом
  (несколько агентов работают с репозиторием параллельно).

## Тестирование

- Чистые DSP/transport/util-модули покрываются host-тестами в `test/`.
- Ожидается: после изменений в `firmware/common` host-тесты зелёные.
- Железо-зависимое (I2S, NVS, ESP-NOW, Wi-Fi) покрывается только ручной проверкой.

## Известные проблемы и техдолг

- Конфликт пипов (исправлен B17/B2): `AUDIO_I2S_DATA_OUT=22` и `AUDIO_OLED_SCL=22`
  были в `config.example.h` (удалено при реорганизации — единый источник —
  `config.env` + `node_config.h`). Текущие валидные пины: I2S 5/4/6, OLED SCL 17.
- CI (`.github/workflows/ci.yml`) требует активации GitHub Actions; для ручного
  запуска добавлен `workflow_dispatch` (см. TASKS.md T1).
- Актуальный список задач/техдолга — `docs/TASKS.md`. На 12.08.2026 закрыты:
  C2.2 (канальные громкости), C3.1/C3.2 (TX аудио), C3.3/C3.5 (сателлит),
  C5.1–C5.7 (веб-безопасность), C6.1–C6.7 (надёжность), B13–B15,
  T11–T13/T18/T20–T22, F18/F21 (репитер APSTA+NAPT — проверен на железе).
  Остаются: T1 (CI), B6/B7/B11/B12/B16 (legacy `master_a2dp`), B10 (IPv6 —
  осознанное ограничение), T15 (ITransport), T16 (перенос `web_server.h` в
  `common/web`), T17 (`console.h`), F12/F14
  (OLED, синхронизация), C4.x, C5.7 (MAC-фильтр/проверка источника UDP-аудио),
  ручная проверка звука на железе (§18).
- Открытые уязвимости (см. `docs/REPO_AUDIT.md`): закрыты V1 (ESP-NOW
  unicast шифрование PMK/LMK, ключи `AUDIO_ESPNOW_PMK/LMK`; broadcast
  heartbeat/discovery остаётся открытым по докам ESP-IDF) и V2 (PBKDF2-HMAC-SHA256,
  10000 итераций; остаток — per-device соль в NVS v5). Остаётся: V4 — UDP-аудио
  без аутентификации источника (C5.7), V5 — OTA без подписи, S-1 — дефолтный
  AP-пароль (в прошивке предупреждение, сменить в config.env/Web UI).

## ESP-IDF миграция (`firmware/master_s3_idf/`)

Параллельно ведётся миграция мастера и сателлитов с Arduino/PlatformIO на чистый ESP-IDF v5.3.1
для устранения циклических перезагрузок `TG1WDT_SYS_RST` (watchdog).

- **Проекты**: `firmware/master_s3_idf/` (мастер), `firmware/satellite_s3_idf_left/`, `firmware/satellite_s3_idf_right/` (CMake + `idf.py`)
- **Платформа**: ESP-IDF v5.3.1, уже установлена на `/home/mi/esp/esp-idf-v5.3.1/`
- **Сборка**: `source /home/mi/esp/esp-idf-v5.3.1/export.sh && idf.py build` в каждом проекте
- **Прошивка**: `idf.py -p /dev/ttyACM0 flash` (после `erase_flash` при смене прошивки)
- **Ветка Git**: `feature/esp32-idf-migration` (запущена в origin)
- **Статус**: устройство стабильно 24+ сек без WDT перезагрузок (vs циклические ребуты на Arduino core)

### Компоненты ESP-IDF проекта (общие, в `firmware/master_s3_idf/components/`, подключаются через `EXTRA_COMPONENT_DIRS`)

| Компонент | Назначение |
|-----------|-----------|
| `dsp_pipeline` | обёртка над `firmware/common/audio/` (pcm_pipeline, volume_control, crossover, delay_line, jitter_buffer, drift_correction) |
| `espnow_transport` | ESP-NOW unicast/broadcast (магия 0xA210/0xA211/0xA212), AudioPacketHeader 16 байт |
| `udp_audio` | UDP порт 5004, PCM приём, HMAC-SHA256 аутентификация через mbedTLS |
| `web_server` | HTTP REST API + SSE, cJSON, эндпоинты /api/*, /api/system/* |
| `i2s_output` | esp_driver_i2s Master TX моно 16-bit для PCM5102A (BCK=4, WS=5, DATA=6) |
| `nvs_config` | NVS хранение NodeConfig (C API nvs_flash/nvs.h), версия схемы kConfigVersion=1 |
| `dns_captive` | UDP DNS сервер на порту 53, отвечает softAP IP на все запросы (captive portal) |
| `captive_portal` | HTML страница настройки Wi-Fi на `/captive` (GET/POST), сканирование сетей, DHCP/static toggle |
| `mdns_raw` | mDNS отвечатель на сыром UDP (порт 5353, multicast 224.0.0.251), доступ по `.local` |
| `ota_update` | OTA через esp_https_ota, эндпоинт POST /api/system/ota |

### Интеграция в main/main.cpp (мастер)

- NVS конфиг загружается при старте (дефолты если не найден), сохраняется при изменении через Web UI
- Wi-Fi AP (softAP), Captive DNS, ESP-NOW, I2S, UDP audio, Web server — все инициализируются в `app_main()`
- Основной цикл 100 Гц с `vTaskDelayUntil()`, `esp_task_wdt_reset()` не требуется (стабильность без WDT)
- mDNS через сырой UDP (порт 5353) — доступ по `http://audio-master.local`

### Интеграция в main/main.cpp (сателлиты)

- NVS конфиг загружается при старте, сторона (left/right) из макроса `AUDIO_SATELLITE_SIDE`
- Wi-Fi STA на канале ESP-NOW, ESP-NOW RX (heartbeat + аудио пакеты мастера), JitterBuffer → DelayLine → VolumeControl → I2S
- Heartbeat (discovery-response) мастере каждые 2 сек, UART-консоль (команды `status`, `delay <ms>`)
- Основной цикл 100 Гц

### Этапы миграции (завершены)

| Этап | Описание | Статус |
|------|----------|--------|
| Этап 1 | Скелет проекта, CMake, main.cpp, DSP конвейер | ✅ |
| Этап 2 | ESP-NOW транспорт, UDP аудио, Web server, I2S вывод | ✅ |
| Этап 3 | NVS конфигурация (компонент nvs_config) | ✅ |
| Этап 4 | Captive DNS портал, OTA обновление, Wi-Fi API (/api/wifi) | ✅ |
| Этап 5 | mDNS через сырой UDP (порт 5353), captive portal, Wi-Fi переконфигурация без перезагрузки | ✅ |
| Этап 6 | Сателлиты на ESP-IDF (left + right, EXTRA_COMPONENT_DIRS) | ✅ |
| Этап 7 | Веб-интерфейс для агентов: `/api/agent/health`, `/api/agent/ping`, `/api/agent/diagnostics` (с расширенным статусом сателлитов) | ✅ |

