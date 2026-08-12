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
- **Сборка прошивки** (PlatformIO, целевые S3 env + legacy-стенд):
  ```
  pio run -c platformio.master.ini            # master_s3_wifi (pioarduino, core 3.x)
  pio run -e satellite_s3_left
  pio run -e satellite_s3_right
  ```
  Legacy env (`master_a2dp`, `satellite_left/right`, espressif32@6.9.0 core
  2.0.17) — отладочный стенд (C0.2), в CI не собираются; оставлять рабочими.
  `master_s3_wifi` собирается на pioarduino-платформе (Arduino core 3.3.11 /
  IDF 5.5.5) с ИЗОЛИРОВАННЫМ core-каталогом (`.pio-core-master/` через
  `platformio.master.ini`): его пакет `framework-arduinoespressif32` совпадает по
  имени с фреймворком официальной платформы `espressif32@6.9.0` (core 2.0.17),
  которой нужны сателлиты — в общем каталоге они перезаписывают друг друга.
  Сборку мастера и сателлитов нельзя смешивать в одном core-каталоге.
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
  `master_s3` и legacy `master` (`firmware/master/include/web_server.h`), включает
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
  `REPO_AUDIT.md` — аудит репозитория).
- Обновлять `docs/TASKS.md` при закрытии/добавлении задач; держать
  `AGENTS.md`/`README.md`/`architecture.md` синхронизированными с кодом
  (несколько агентов работают с репозиторием параллельно).

## Тестирование

- Чистые DSP/transport/util-модули покрываются host-тестами в `test/`.
- Ожидается: после изменений в `firmware/common` host-тесты зелёные.
- Железо-зависимое (I2S, NVS, ESP-NOW, Wi-Fi) покрывается только ручной проверкой.

## Известные проблемы и техдолг

- Конфликт пинов в `config.example.h`: `AUDIO_I2S_DATA_OUT=22` и `AUDIO_OLED_SCL=22`
  (актуально при реализации OLED-меню, см. TASKS.md B2).
- CI (`.github/workflows/ci.yml`) требует активации GitHub Actions; для ручного
  запуска добавлен `workflow_dispatch` (см. TASKS.md T1).
- Актуальный список задач/техдолга — `docs/TASKS.md`. На 12.08.2026 закрыты:
  C2.2 (канальные громкости), C3.1/C3.2 (TX аудио), C3.3/C3.5 (сателлит),
  C5.1–C5.7 (веб-безопасность), C6.1–C6.7 (надёжность), B13–B15,
  T11–T13/T18/T20–T22, F18/F21 (репитер APSTA+NAPT — проверен на железе).
  Остаются: T1 (CI), B6/B7/B11/B12/B16 (legacy `master_a2dp`), B10 (IPv6 —
  осознанное ограничение), T15 (ITransport), T16 (перенос `web_server.h` в
  `common/web`), T17 (`console.h`), T19 (сателлиты на core 3.x), F12/F14
  (OLED, синхронизация), C3.4 (дрейф-коррекция), C4.x, C5.7 (MAC-фильтр/
  проверка источника UDP-аудио), ручная проверка звука на железе (§18).
- Открытые уязвимости (см. `docs/REPO_AUDIT.md`): ESP-NOW без шифрования,
  UDP-аудио без аутентификации источника, SHA-256 без итераций — приоритет
  следующей итерации.
