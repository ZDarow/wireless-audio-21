# CHANGELOG — Wireless Audio 2.1

Все заметные изменения в проекте документируются в этом файле.

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/).
Проект придерживается [семантического версионирования](https://semver.org/lang/ru/).

## [v0.1.0] — 23.11.2026

### Добавлено
- ESP32-S3 мастер (сабвуфер) с Wi-Fi UDP приёмом аудио, DSP-конвейером (tone → limiter → volume → LR4 кроссовер), ESP-NOW/UDP передачей на сателлиты
- Сателлиты L/R с приёмом аудио, jitter buffer, VolumeControl, I2S выход
- Web UI + REST API с авторизацией (SHA-256+соль, CSRF, rate limit), Wi-Fi профили, OTA, логи, диагностика
- UDP-аутентификация источника (HMAC-SHA256, магия 0xA211, ключ AUDIO_UDP_AUTH_KEY)
- Forced password change при первом запуске (S-1)
- Подпись OTA-образа Ed25519 (V5, заголовок X-OTA-Signature)
- SSE endpoint /api/events для статуса сателлитов в реальном времени
- Декомпозиция web_server.cpp на модули (web_auth, web_api, web_ota, web_sse)
- LICENSE (GPL-3.0)
- .clang-tiny, шаг clang-format --dry-run в CI
- dependabot.yml, защита ветки main (1 approving review)

### Исправлено
- V4: UDP-аудио без аутентификации источника — добавлен ключ AUDIO_UDP_AUTH_KEY
- V5: OTA без подписи образа — добавлена проверка Ed25519
- S-1: Дефолтный пароль AP — forced password change при первом запуске
- AP-1: Заменён String на char[] в веб-путях (предотвращение фрагментации heap)
- C2: Добавлен файл LICENSE (GPL-3.0)

### Удалено
- config.example.h, docs/аудит.txt, firmware/idf_component.yml, firmware/master_s3/include/master_s3_config.h
- Тестовые бинарники test/*.bin и *.bin.d

### Изменено
- kDefaultWifiChannel перенесён из master_s3_config.h в node_config.h
- g_udpBuf → g_udpAudioBuffer (4 вхождения)
- .gitignore: добавлен .pio-core-master/
- ci.yml: добавлены permissions, timeout-minutes, SHA-пины actions, шаг clang-format --dry-run

## [v0.0.1] — 10.08.2026

### Добавлено
- Начальная версия проекта Wireless Audio 2.1
- ESP32-S3 env (master_s3_wifi, satellite_s3_left/right)
- Host-тесты (make test)
- Генератор конфига (config.env → generated_config.h)
- CI workflow (ci.yml)

[Unreleased]: https://github.com/ZDarow/wireless-audio-21/compare/v0.1.0...HEAD
[v0.1.0]: https://github.com/ZDarow/wireless-audio-21/releases/tag/v0.1.0
[v0.0.1]: https://github.com/ZDarow/wireless-audio-21/releases/tag/v0.0.1
