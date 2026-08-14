# Changelog

Формат — [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/).
Версии — по [SemVer](https://semver.org/lang/ru/).

## [0.2.2] — 2026-08-14 (адаптация под ESP32-S3-DevKitC-1 N8R2)

### Железо / платформа
- Целевая плата: `esp32-s3-devkitc1-n8r2` (8MB flash, без PSRAM) для всех S3 env.
- I2S пины: BCK=GPIO14, WS=GPIO13, DIN=GPIO12 (переопределены через build_flags).
- PSRAM fallback: jitter buffer и delay lines используют heap при отсутствии PSRAM.
- I2S hardening: `ESP.restart()` при ошибках инициализации (предотвращает
  невалидное состояние), `g_i2sReady` guard в legacy master.

### Конфигурация
- `generate_config.py`: добавлены `#ifndef`/`#endif` guards вокруг каждого
  макроса — позволяет переопределять значения через `build_flags` без редактирования
  `generated_config.h`.

### Зависимости / CI
- Зафиксирован `ArduinoJson` на `7.4.3`.
- Добавлен `clang-format --dry-run` check в CI.

### Известные ограничения
- Per-device соль PBKDF2 — техдолг (NVS v5).
- UDP-аудио без аутентификации источника (V4/C5.7), OTA без подписи (V5).

## [0.2.1] — 2026-08-13 (фаза 1-2 аудита безопасности + синхронизация)

### Безопасность
- ESP-NOW шифрование: PMK/LMK + `encrypt=true` (V1; ключи `AUDIO_ESPNOW_PMK/LMK`).
- PBKDF2-HMAC-SHA256 (10000 итераций) вместо SHA-256 для пароля Web UI (V2);
  старый формат принимается до смены пароля.
- Предупреждение о заводском пароле AP: баннер Web UI + serial (S-1).

### Рефакторинг / процессы
- T16: перенос `web_server.h` из `master/include` → `common/web/` (общий модуль).
- CI hardening: permissions, timeout-minutes, concurrency, SHA-пины actions.
- LICENSE (GPL-3.0), dependabot, .yamllint, .editorconfig, .gitattributes.
- Шаблоны issue/PR, SECURITY.md, CONTRIBUTING.md, CHANGELOG.md.

### Тесты
- Новый host-тест `auth_test` (SHA-256, HMAC-SHA256 RFC 4231, PBKDF2 RFC 7914).
- C3.4: дрейф-коррекция сателлита (`drift_correction.h`) + host-тест.

### Рефакторинг / стабильность
- A9: FreeRTOS-очередь для RX ESP-NOW на сателлите (коллбек → пул 8 буферов → drain в loop).
- T16: `web_server.h` перенесён в `common/web/` (общий модуль для S3 и legacy).
- CI: добавлен `clang-format --dry-run` check (`.clang-format` конфиг, без массового реформата).

### Зависимости
- Зафиксирован `ArduinoJson` на `7.4.3` (раньше `^7.0.0`).

### Известные ограничения
- Per-device соль PBKDF2 — техдолг (NVS v5).
- UDP-аудио без аутентификации источника (V4/C5.7), OTA без подписи (V5).

## [0.2.0] — 2026-08-12

- Канальные громкости (C2.2), TX-аудио батчами на сателлиты (C3.1/C3.2),
  сателлит: jitter/DelayLine/VolumeControl (C3.3/C3.5).
- Веб-безопасность (C5.1–C5.7), надёжность (C6.1–C6.7).
- Репитер APSTA+NAPT на мастере S3 (F18/F21).

## [0.1.0] — 2026-08-10

- Каркас прошивки: Wi-Fi, Web UI + REST, DSP (tone/limiter/volume/LR4),
  ESP-NOW/UDP транспорт, конфигурация через `config.env`.
