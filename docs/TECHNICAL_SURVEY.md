# Технический обзор внешних решений (Wireless Audio 2.1)

> Версия: 1.0 (22.11.2026)
> Источник: глубокий веб-анализ открытых материалов, статей, репозиториев и документации Espressif.
> Назначение: сравнить текущий подход проекта с актуальными открытыми решениями по критериям
> производительности, безопасности, масштабируемости и сложности внедрения.

---

## 1. Транспорт аудио (Wi-Fi / ESP-NOW / UDP)

### 1.1 Wi-Fi UDP PCM (текущий подход проекта)

| Решение | Источник | Особенности |
|---|---|---|
| **Текущий проект** | `firmware/common/transport/udp_audio_packet.h` | Магия `0xA210/0xA211`, 18-байт заголовок, HMAC-SHA256 (опц.), sequence, timestamp_samples, 48 кГц/16/2, payload ≤ 1200 Б |

**Почему Wi-Fi UDP:** ESP32-S3 не поддерживает Classic Bluetooth (только BLE) — подтверждено официальной документацией Espressif ([esp-idf#16232](https://github.com/espressif/esp-idf/issues/16232), [arduino-esp32#8675](https://github.com/espressif/arduino-esp32/issues/8675)). A2DP Sink/Source возможен только на ESP32 (оригинал) и ESP32-S31 (dual-mode).

### 1.2 RTP/UDP (альтернатива кастомному UDP)

| Решение | Источник | Плюсы | Минусы | Применимость |
|---|---|---|---|---|
| **ESP32 RTP Audio Streamer** | briansivieri.com (2026-04-21) | RTP-инкапсуляция (PT=11 L16), VLC автоопределение формата, 2 КБ ring buffer как rate limiter, DHCP gateway автоопределение | Требует RTP-стека, только unicast UDP | Рассмотреть для замены кастомного заголовка `0xA210` — стандарт RTP упростит интеграцию с плеерами (VLC, ffplay) |

### 1.3 ESP-NOW (мастер → сателлиты)

| Решение | Источник | Плюсы | Минусы | Применимость |
|---|---|---|---|---|
| **arduino-audio-tools (ESPNowStream)** | pschatzmann/arduino-audio-tools | ESP-NOW как Arduino Stream, `use_send_ack` для надёжности, broadcast без ACK, буферы `buffer_size/buffer_count`, шифрование PMK/LMK, измерено 33–37 КБ/сек | 250 Б лимит (v1.0), нет ACK для broadcast, clock drift между сателлитами | **Оставить** для мастер→сателлит, но добавить timestamp-синхронизацию |
| **ESP-NOW Audio Source/Sink (IMA ADPCM)** | WillyBilly06/esp-now-audio-source | IMA ADPCM (~12:1 сжатие), 48 кГц/24-bit/стерео, 96 сэмплов/пакет (~2 мс), 115 Б пакет, микросекундный timestamp для clock sync, APLL для низкого джиттера | Требует кодека, только ESP32 (не S3) | Рассмотреть если нужна экономия трафика между сателлитами |
| **ESP-NOW low latency blog** | pschatzmann.ch (2022) | `esp_wifi_set_ps(WIFI_PS_NONE)` отключает modem sleep, стабильные 32 КБ/сек | Без кодека недостаточно для Hi-Fi | Применимо — отключить modem sleep для стабильности TX |

**Критические факты ESP-NOW:**
- v1.0: до 250 Б на пакет; v2.0 (IDF 5.4+): до 1470 Б ([ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html))
- CCMP-шифрование (PMK/LMK) снижает payload с 250 до 242 Б и добавляет 2–4 мс задержки ([ElectricalFlux](https://electricalflux.com/mcu-projects/esp-now-walkie-talkie-payload-math-rf-gotchas))
- Реальный опыт вещания на 60 устройств через ESP-NOW с Opus-кодеком и избыточными кадрами ([Hapbeat статья](https://zenn.dev/hapbeat/articles/espnow-audio-broadcast))

### 1.4 Сравнение транспортов

| Критерий | Wi-Fi UDP (текущий) | RTP/UDP | ESP-NOW | IMA ADPCM + ESP-NOW |
|---|---|---|---|---|
| Задержка | Низкая | Низкая | Очень низкая | Низкая (+ декодирование) |
| Надёжность | Средняя (UDP) | Средняя | Высокая (MAC-уровень) | Высокая |
| Аутентификация | HMAC (опц.) | Нет | CCMP (PMK/LMK) | CCMP |
| Масштабируемость | Unicast/broadcast | Unicast | Broadcast (250 Б) | Broadcast |
| Сложность внедрения | Низкая (уже есть) | Средняя | Низкая (уже есть) | Средняя (кодек) |

---

## 2. DSP / Кроссовер

### 2.1 Linkwitz-Riley 4-го порядка (LR4)

| Решение | Источник | Плюсы | Минусы | Применимость |
|---|---|---|---|---|
| **Текущий проект** | `firmware/common/audio/crossover.h` | LR4, биквады Direct Form I, header-only, хост-тесты | Нет адаптивной подстройки коэффициентов | **Оставить** |
| **SOF Project Crossover** | thesofproject.github.io (2026-10-27) | 64-бит аккумулятор, Q2.30 фиксированная точка, Bilinear Transform с pre-warping, Octave скрипты для генерации коэффициентов | Сложно портировать на Arduino без изменений | Идеи для улучшения точности |
| **dsp-c-filters** | dimtass/dsp-c-filters | C библиотека: `so_linkwitz_riley_lpf/hpf`, биквады, портируемость на MCU | C, не C++ | Альтернатива для переносимости |
| **DSP-Cpp-filters** | dimtass/DSP-Cpp-filters | C++ биквады, Linkwitz-Riley 2-го порядка, MIT лицензия | Только 2-й порядок (LR2), не LR4 | Рассмотреть для расширения |
| **NAudio LinkwitzRileyCrossover** | naudio/NAudio | C# реализация LR4, сумма НЧ+ВЧ даёт flat magnitude (all-pass) | C#, не встроенные | Справочная реализация |
| **Faust filters.lib** | grame-cncm/faustlibraries | `crossover2LR4/3LR4/8LR4`, функциональный подход | DSL, не C++ | Справочная реализация |

**Теория LR4 (подтверждена внешними источниками):**
- LR4 = два каскада одинаковых фильтров Баттерворта 2-го порядка (Q=0.7071)
- Сумма НЧ+ВЧ даёт ровно 0 дБ на частоте раздела, фаза 360° (в фазе)
- Крутизна 24 дБ/октаву, нулевоеmagnitude peaking ([RaneNote 107/160](https://www.ranecommercial.com/legacy/note160.html), [Linkwitz Lab](https://linkwitzlab.com/filters.htm))

---

## 3. Безопасность OTA / Secure Boot

### 3.1 Secure Boot v2 (ESP-IDF)

| Решение | Источник | Плюсы | Минусы | Применимость |
|---|---|---|---|---|
| **ESP-IDF Secure Boot v2** | docs.espressif.com | RSA-3072 + RSA-PSS, один ключ в eFuse (ESP32 v3.0+), подпись образа на хосте, верификация при загрузке и OTA, rollback на неверный образ | Требует программирования eFuse (необратимо), сложная настройка | **Критично** для продакшена, но для личного открытого проекта можно начать с более простого варианта |
| **ESP HTTPS OTA** | docs.espressif.com | HTTPS транспорт, упрощённая абстракция ESP-IDF | Только целостность, нет подписи | Базовый минимум |
| **App Rollback / Anti-rollback** | docs.espressif.com | A/B слоты, `esp_ota_mark_app_valid_cancel_rollback()`, анти-rollback через eFuse security version | Требует Secure Boot | Дополнение к Secure Boot v2 |

### 3.2 SimpleOTA (Ed25519) — более простая альтернатива

| Решение | Источник | Плюсы | Минусы | Применимость |
|---|---|---|---|---|
| **SimpleOTA** | simpleota.com (2026-07-13) | Ed25519 подпись на устройстве, Monocypher 3.1.3, SHA-256 + подпись перед пометкой bootable, легче Secure Boot v2 | Требует интеграции с текущим веб-сервером | **Рекомендуется** для личного открытого проекта как первый шаг |

### 3.3 Сравнение подходов к OTA

| Критерий | Текущий (Update.end) | SimpleOTA Ed25519 | Secure Boot v2 |
|---|---|---|---|
| Подпись образа | Нет | Да (Ed25519) | Да (RSA-3072) |
| Верификация на устройстве | Нет | Да | Да |
| Rollback | Нет | Нет | Да (A/B) |
| eFuse программирование | Нет | Нет | Да (необратимо) |
| Сложность внедрения | Низкая | Средняя | Высокая |
| Риск кирпича | Низкий | Низкий | Высокий (еFuse) |

---

## 4. Веб-сервер / REST API / Аутентификация

### 4.1 Текущий подход проекта

`firmware/common/web/web_server.h` — монолит (~976 строк реализации + 430 строк SPA HTML), WebServer ESP32, REST API, auth (SHA-256+соль, CSRF, rate limit), Wi-Fi-профили, OTA, логи, диагностика.

### 4.2 Внешние решения

| Решение | Источник | Плюсы | Минусы | Применимость |
|---|---|---|---|---|
| **esp_http_server (IDF)** | docs.espressif.com | Легковесный HTTP сервер на FreeRTOS, сессии, WebSocket pre-handshake callback для auth, RESTful пример | Требует ESP-IDF (не Arduino core) | Рассмотреть при миграции на IDF |
| **esp32_https_server** | fhessel/esp32_https_server | HTTPS для Arduino core, middleware (auth, logging), Basic Auth, WebSocket, self-signed сертификаты | Требует сертификаты, больше памяти | Рассмотреть для HTTPS вместо HTTP |
| **ESP32 REST API tutorial** | solderhub.com (2026-10-28) | ArduinoJson, API key заголовки, CORS, mDNS (`esp32-api.local`), JSON ответы, статус-коды | Статья, не библиотека | Рекомендации по дизайну API |
| **ESP-IDF REST server example** | iot-da.github.io | `/api/v1/system/info`, `/api/v1/temp/raw`, `/api/v1/light/brightness`, JSON, curl примеры | ESP-IDF, не Arduino | Справочная реализация |

### 4.3 Рекомендации по веб-интерфейсу (из GUI_UX_REPORT.md)

1. Оставить веб-интерфейс на устройстве как основной интерфейс
2. Добавить SSE endpoint для потокового статуса сателлитов (вместо опроса каждые 2 с)
3. Добавить preset-менеджмент — сохранение/загрузка конфигураций DSP в NVS + экспорт/импорт JSON
4. Добавить dirty indicator на ползунках — визуальная обратная связь о несохранённых изменениях
5. Десктопное приложение — только при необходимости: Tauri 2 (малый размер, Rust бэкенд, веб-фронтенд)

---

## 5. Open-source мультирум / аудио-проекты (аналоги)

| Решение | Источник | Ключевые особенности | Лицензия |
|---|---|---|---|
| **SonicStream** | chagoguila/SonicStream (2026-08-17) | UDP Multicast + TCP, SBC кодек, 5-параметрический EQ, soft-knee limiter, Web Dashboard, AP+STA режимы, часы синхронизации | MIT |
| **ESP32-C5 Wi-Fi 6 Multichannel** | Glider95/Wireless_audio_ESP32 | 4 ingest узла → aggregator → multicast группы, 1 мс фреймы, 24-bit PCM, Wi-Fi 6 5 ГГц | — |
| **ESP32 AirPlay 2** | Kristian8606/airplay-esp32 | AirPlay 2 приёмник, ALAC 48 кГц/24-bit, HomeKit, 2×24-параметрический EQ, OTA по Wi-Fi | — |
| **ESP32 Multi-room speakers** | svxa/esp32-multiroom-speakers | ESP32-S3, USB PD, TAS5827 усилитель, микрофоны для voice assistant | — |
| **Home Assistant Audio Speaker** | HouseWaves/undotcom | ESPHome + SendSpin / Squeezelite / Snapcast, мультирум, TTS | — |

---

## 6. I2S / PCM5102A

| Решение | Источник | Ключевые особенности |
|---|---|---|
| **ESP-IDF I2S (S3)** | docs.espressif.com | Два контроллера, DMA, стандартный/TDM/PDM режимы, коллбеки DMA (без сложной логики в ISR) |
| **PCM5102A + ESP32-S3** | king2552/esp32-speaker, lingflux.com | GPIO 12/13/14 (BCK/WS/DIN), DMA_DESC_NUM 16–24, DMA_FRAME_NUM 512–1024, 33–100Ω резисторы на линиях, 10μF+0.1μF конденсаторы питания |
| **ESP32-S3 USB Speaker (UAC2)** | king2552/esp32-speaker | USB Audio Class 2.0, TinyUSB, I2S DMA ring buffer, 48 кГц фиксировано |

**Рекомендации по железу (PCM5102A):**
- GPIO: BCK=4, WS=5, DATA=6 (уже исправлено в проекте, B17)
- DMA: 16 deskriptоров × 512 фреймов (рекомендация lingflux.com)
- 33–100Ω последовательные резисторы на BCK, WS, DIN
- 10μF + 0.1μF конденсаторы питания PCM5102A
- PCM5102A: FLT=GND, DEMP=GND, XSMT=3.3V, FMT=GND (I2S формат)

---

## 7. CI/CD для встроенной прошивки

| Решение | Источник | Плюсы | Минусы | Применимость |
|---|---|---|---|---|
| **PlatformIO GitHub Actions** | docs.platformio.org | `pio run` / `pio ci`, кэш `~/.platformio`, Python 3.14, артефакты прошивки | Нужен PlatformIO на ранере | **Базовый минимум** (уже в проекте) |
| **Firmware CI/CD pipeline** | chrisnortonjr.com (2026-07) | Фаза 1: reproducible builds, GitHub Actions, Python тесты; Фаза 2: self-hosted runner + реальный ESP32 + serial валидация | Требует физического железа на ранере | Рекомендуется для HIL |
| **Unit Testing PlatformIO** | piolabs.com | Unity фреймворк, `test_native` (хост) vs `test_embedded` (железо), PlatformIO Remote для удалённых плат | Настройка Remote требует токена | Рекомендуется |
| **CI/CD Embedded Firmware** | fss.cc (2026-04-24) | Unity + Ceedling, HIL тестирование, partition table изменения, stack overflow детекция | Сложность настройки | Для продакшена |

**Текущее состояние CI проекта:** workflow написан (`.github/workflows/ci.yml`), но **0 runs** — Actions не активированы в настройках репозитория (T1).

---

## 8. Сравнение по критериям (итоговая матрица)

| Категория | Решение | Производительность | Безопасность | Масштабируемость | Сложность внедрения |
|---|---|---|---|---|---|
| Транспорт смартфон→мастер | Wi-Fi UDP (текущий) | Высокая | Средняя (HMAC опц.) | Средняя | Низкая (уже есть) |
| Транспорт мастер→сателлиты | ESP-NOW | Очень высокая | Высокая (CCMP) | Высокая (broadcast) | Низкая (уже есть) |
| DSP / кроссовер | Текущий (crossover.h) | Высокая | — | Средняя | Низкая (уже есть) |
| OTA | Текущий (Update.end) | — | Низкая (нет подписи) | — | Низкая |
| OTA | SimpleOTA Ed25519 | — | Высокая | — | Средняя |
| OTA | Secure Boot v2 | — | Очень высокая | — | Высокая (eFuse) |
| Веб-сервер | Текущий (монолит) | Средняя | Средняя (CSRF, rate limit) | Низкая | Низкая (уже есть) |
| Веб-сервер | esp32_https_server | Средняя | Высокая (HTTPS) | Средняя | Средняя |
| CI/CD | Текущий (workflow_dispatch) | — | — | — | Низкая (нужно активировать) |
| CI/CD | Self-hosted runner + HIL | — | — | Высокая | Высокая |

---

## 9. Выводы обзора

1. **Архитектура проекта верная** — Wi-Fi UDP вместо A2DP для ESP32-S3 — единственно правильный выбор (подтверждено Espressif).
2. **ESP-NOW для мастер→сателлиты** — оптимальный выбор (низкая задержка, broadcast, нет Wi-Fi стека).
3. **Критические пробелы:** V4 (UDP без аутентификации источника), V5 (OTA без подписи), S-1 (дефолтный пароль AP), C3.4 (дрейф-коррекция не интегрирована).
4. **Рекомендуемые внешние решения для интеграции:** SimpleOTA (Ed25519) для OTA, SOF Project идеи для точности кроссовера, RTP/UDP как альтернатива кастомному заголовку, SSE для статуса сателлитов.
5. **CI/CD:** необходимо активировать GitHub Actions и рассмотреть self-hosted runner для HIL тестирования.
