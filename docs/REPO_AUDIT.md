# Аудит репозитория Wireless Audio 2.1

Дата: 12.08.2026
Ветка: `main`, коммит `6c48646`
Область: стандарты GitHub, архитектура, безопасность, бизнес-логика, контент целевой страницы.

---

## 1. Соответствие стандартам платформы (GitHub)

| # | Требование | Статус | Комментарий |
|---|-----------|--------|-------------|
| S1 | `README.md` | ✅ | Подробный: архитектура, быстрый старт, REST API, консоль, требования, ссылки на docs |
| S2 | Бейджи в README (CI, license, версии) | ❌ | Отсутствуют; добавить shields.io: CI status, license, PlatformIO |
| S3 | `LICENSE` | ❌ **БЛОКЕР** | Файла нет. Выбрать MIT (простая) или GPL-3.0 (сохранение свободы для firmware) и добавить |
| S4 | CI/CD `.github/workflows/ci.yml` | ✅ | host-тесты + сборка `master_s3_wifi` + сателлиты S3; кэш; фикс версии PIO (T18) |
| S5 | CI: legacy env | ⚠️ | `master_a2dp`/`satellite_left/right` не собираются в CI — осознанно (стенд C0.2), но в README это не отражено |
| S6 | CI: security hardening | ❌ | Нет `permissions: contents: read`, нет `timeout-minutes` — добавить |
| S7 | GitHub Actions активированы | ❌ **БЛОКЕР** | Задача T1: 0 запусков (Settings → Actions → enable) |
| S8 | `SECURITY.md` | ❌ | Добавить: политика раскрытия уязвимостей (private vulnerability reporting) |
| S9 | `CONTRIBUTING.md` | ❌ | Добавить: как собрать, протестировать, стиль коммитов |
| S10 | `CHANGELOG.md` | ❌ | Ведётся история в git; CHANGELOG поможет читателям релизов |
| S11 | Шаблоны issues/PR | ❌ | `.github/ISSUE_TEMPLATE/`, `PULL_REQUEST_TEMPLATE.md` |
| S12 | GitHub Pages / лендинг | ❌ | `docs/` есть, Pages не включены; нет `index.html`. Целевая страница — см. раздел 6 |
| S13 | `.gitignore` | ✅ | PIO, generated, env, тестовые бинарники, локальные инструменты |
| S14 | Чистота корня | ⚠️ | `Primer.md`, `аудит.txt`, `ТЗ.md`, `ТЗ_Веб.md` в корне. Перенести ТЗ в `docs/`, мусор удалить/заархивировать |
| S15 | Скриншоты | ⚠️ | `docs/screenshots/` пуста — добавить снимки Web UI (мастер, настройка Wi-Fi, логи) |
| S16 | Лицензии зависимостей | ⚠️ | Git-зависимости зафиксированы SHA (T11 ✅), но лицензии не документированы |
| S17 | Версии инструментов | ✅ | PIO зафиксирован `6.1.19`, Python 3.12 в CI |

### Чек-лист исправлений (стандарты)
1. [S3] Добавить `LICENSE` (рекомендация: MIT для примеров + заметка о проприетарных частях, либо GPL-3.0).
2. [S7] Активировать GitHub Actions в настройках репозитория.
3. [S6] В `ci.yml`: `permissions: contents: read` и `timeout-minutes: 30`.
4. [S2] Бейджи в README (CI, license).
5. [S8/S9/S10/S11] Создать `SECURITY.md`, `CONTRIBUTING.md`, `CHANGELOG.md`, шаблоны.
6. [S14] Перенести `ТЗ.md`/`ТЗ_Веб.md` в `docs/`; удалить `Primer.md`, `аудит.txt`.
7. [S15] Добавить скриншоты Web UI.

---

## 2. Аудит архитектуры (антипаттерны)

| # | Антипаттерн | Где | Риск | Рекомендация |
|---|------------|-----|------|--------------|
| A1 | **Монолит `web_server.h` (1594 стр.): C++ + HTML + CSS + JS в одном header** | `firmware/master/include/web_server.h` | Высокий: сложность, время компиляции, невозможно переиспользовать SPA | Разбить: HTML/CSS/JS → `PROGMEM`-массивы в отдельных `.inc`; класс → `.cpp`. Модуль `common/web/` (T16) |
| A2 | **Header-only всё в `common/`** | `firmware/common/**` | Средний: перекомпиляция, нет ODR-контроля, риск дублирования символов | Оставить для DSP (embed-friendly), но транспорт/веб перевести на `.cpp` |
| A3 | **Дублирование мастеров: `master/` (legacy) и `master_s3/`** | `firmware/master/src/main.cpp`, `firmware/master_s3/src/main.cpp` | Средний: два источника правды по Wi-Fi/web/audio | Legacy — стенд C0.2: заморозить, вынести общую логику (web, wifi) в `common/` (T15/T16), legacy оставить тонким |
| A4 | **Глобальные `static`-состояния в `main.cpp`** | оба main.cpp | Средний: нет инкапсуляции, сложно тестировать | Сгруппировать в структуры `MasterApp`/`SatelliteApp` |
| A5 | **`delay(1)` в loop + busy-wait** | `master_s3/src/main.cpp`, `satellite/src/main.cpp` | Низкий: тактовая трата (измеряется как cpu_load), нет точного тайминга | `vTaskDelay(pdMS_TO_TICKS(1))`; для аудио — таймерные прерывания/I2S DMA-коллбеки |
| A6 | **`String` (Arduino heap) в веб-обработчиках** | `web_server.h` | Средний: фрагментация heap на долгоживущем устройстве | Стек-буферы + `snprintf` (частично уже так); заменить остатки |
| A7 | **NVS как дамп структуры (`putBytes`)** | `storage.h` | Средний: добавление поля ломает размер; миграции v1→v4 растут | Приемлемо для MVP (миграции работают), но для долгосрочного — key-value схема |
| A8 | **Конфиг через препроцессор (`generated_config.h`) + дефолты в `node_config.h`** | `config/` | Средний: две системы, макросы глобальны (T13 чистили частично) | Оставить как есть для MVP; в перспективе — JSON/NVS-конфиг |
| A9 | **Долгие операции в RX-коллбеке ESP-NOW** (push в jitter/parse в Wi-Fi task) | `espnow.h`, `satellite/src/main.cpp` | Средний: противоречит документированному паттерну Espressif («не делайте длительных операций в коллбеке») | Коллбек → FreeRTOS-очередь; обработка в loop/отдельной задаче |
| A10 | **`flushTxBatch()` в hot-path обработки UDP** | `master_s3/src/main.cpp` | Низкий: esp_now_send асинхронный, но частые вызовы могут рассинхронизировать коллбеки (по докам — интервал между send) | Вынести отправку по таймеру/после приёма пакета (уже частично батчинг 117 семплов) |
| A11 | **Волшебные константы и дублирующиеся литералы** | main.cpp, web_server.h | Низкий | Вынести в `constexpr`/конфиг |

---

## 3. Аудит безопасности

| # | Уязвимость | Severity | Где | Что делать |
|---|-----------|----------|-----|------------|
| V1 | **ESP-NOW без шифрования** (`encrypt=false`, LMK/PMK не заданы) | 🔴 Высокий | `espnow.h:34,84` | ✅ (12.08.2026: `esp_now_set_pmk()` + `encrypt=true` + LMK всем пирам, включая broadcast; ключи `AUDIO_ESPNOW_PMK/LMK` в конфиге — дефолты сменить перед установкой) |
| V2 | **Хэш пароля SHA-256(пароль + фиксированная публичная соль), без итераций; пароль ≥ 4 символа** | 🟠 Средний | `auth.h`, `web_server.h:901` | ✅ (12.08.2026: PBKDF2-HMAC-SHA256, 10000 итераций, формат `pbkdf2$<n>$<hex>`; старый SHA-256-формат принимается до смены пароля. **Остаток**: per-device соль из `esp_random` и мин. пароль 8 — поле `adminPasswordHash[65]` не вмещает соль, нужна миграция NVS v5 — техдолг) |
| V3 | **`wifiPassword`/`wifiApPassword` открытым текстом в NVS** | 🟠 Средний | `storage.h:39-41` | Осознанное решение (нужен WiFi.begin). Для продакшена — включить flash encryption (esp_flash_encryption) + отметить в docs |
| V4 | **UDP-аудио без аутентификации источника** (magic 0xA210 — не защита) | 🟠 Средний | `udp_audio_packet.h`, `master_s3/src/main.cpp` | Остаток C5.7: предустановленный общий ключ/токен в пакетах ИЛИ ограничение `remoteIP()` по известному источнику/подсети; шифровать PCM не обязательно (LAN) |
| V5 | **OTA без проверки подписи** | 🟡 Низкий | `web_server.h:handleUpdateUpload` | `Update.end(false)` — только целостность. Полная защита — secure boot v2 + подпись образа; для MVP принять риск, задокументировать |
| V6 | **HTTP-заголовки: нет CSP, cookie без HttpOnly/SameSite** | 🟡 Низкий | `web_server.h` | Добавить `Content-Security-Policy` (UI — собственный SPA, ввод пользователя не рендерится как HTML — риск XSS низкий), `Cache-Control`; cookie сессии пометить `HttpOnly` (учесть, что JS читает только csrf из /api/status) |
| V7 | **`/api/wifi/save` принимает пароль по HTTP** | 🟡 Низкий | `web_server.h:443-458` | Локальная сеть; при желании — HTTPS (лимитировано для ESP32) |
| V8 | **Config export** | ✅ | `web_server.h:706-731` | Пароли и hash НЕ экспортируются — корректно |
| V9 | **CSRF** | ✅ | `web_server.h:csrfOk` | Токен X-CSRF-Token обязателен на POST — корректно |
| V10 | **Session timeout / rate limit** | ✅ | C5.2/C5.3 | Таймаут 1 ч, блокировка после 5 неудач — реализовано |
| V11 | **Доступ только из локальной подсети** | ✅ | C5.7 `clientIsLocal` | Реализовано |

---

## 4. Аудит бизнес-логики

| Область | Оценка | Замечания |
|---------|--------|-----------|
| Аудио-путь мастера (UDP → jitter → DSP → sub/local + L/R TX) | ✅ | Bатчинг 117 семплов, concealment при потерях, fade, задержки каналов — реализованы (C2.2, C3.1/C3.2, C5.x) |
| Кроссовер LR4, тембр, лимитер после volume | ✅ | B15 — порядок корректен |
| Сателлит (RX → jitter → delay → volume → I2S) | ✅ | Старт по `ready()`, повторное ожидание при истощении (C3.3) |
| **Дрейф-коррекция (C3.4)** | ⚠️ | **Не реализована**: без синхронизации тактов сателлиты со временем рассинхронизируются (накопление/истощение jitter-буфера). Критично для стерео-сцены 2.1 — приоритет следующего этапа |
| Watchdog | ✅ | C6.1: IDF 5.x API, таймаут 30 с |
| Диагностика | ✅ | heap/PSRAM, cpu_load, uptime, RSSI, MAC, логи с фильтрами |
| Host-тесты | ✅ | 5/5: jitter (вкл. B13), transport, util, udp_audio_receiver, config |
| E2E-покрытие DSP | ⚠️ | `test_pipeline` покрывает pipeline, но нет теста на новые канальные громкости/кроссовер с sub — добавить |
| Лишняя нагрузка loop | ⚠️ | `delay(1)` + блокирующий DNS/HTTP-чек вынесены в задачу — корректно (C5.x) |

---

## 5. Рекомендации по рефакторингу (приоритет)

**Приоритет 1 (безопасность, 1–2 дня):**
1. V1: включить шифрование ESP-NOW (PMK/LMK + `encrypt=true`).
2. V2: PBKDF2 + per-device соль; мин. пароль 8 символов.
3. V4: аутентификация UDP-источника (токен в заголовке пакета).

**Приоритет 2 (архитектура, 2–4 дня):**
4. A9: очередь FreeRTOS для RX ESP-NOW.
5. A1: вынести SPA из `web_server.h` в `PROGMEM`-ресурсы (T16).
6. A4: группировка состояния в `MasterApp`/`SatelliteApp`.

**Приоритет 3 (качество, по ходу):**
7. C3.4: дрейф-коррекция сателлита (timestamp → регулировка target/скорости потребления).
8. A5/A6: тайминг loop без busy-wait, убрать `String` из веб-путей.
9. E2E-тест канальных громкостей (L/R/Sub) + sub-задержки.

---

## 6. Ресурсы для целевой страницы (собранные при веб-поиске)

### 6.1 Техническая документация (материалы для разделов «Как это работает»)

| Ресурс | Ссылка | Для чего |
|--------|--------|----------|
| ESP-NOW — официальная документация Espressif (ESP32-S3, актуальная) | https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/network/esp_now.html | Транспорт: v1.0 250 Б / v2.0 1470 Б; CCMP-шифрование PMK/LMK; лимиты пиров; пример `examples/wifi/espnow` |
| Пример ESP-NOW (ESP-IDF) | https://github.com/espressif/esp-idf/tree/master/examples/wifi/espnow | Референс реализации RX/TX + шифрование |
| PCM5102A — datasheet (TI) | https://www.ti.com/lit/gpn/PCM5102A | DAC: 2.1 VRMS, 112 dB SNR, 16/24/32-bit, 8–384 кГц, встроенный PLL (без MCLK), без DC-блокировки |
| PCM5102A — страница продукта | https://www.ti.com/product/PCM5102A | Характеристики, EVM (PCM5102EVM-U), app notes SBAA346 (вспомогательные цепи), SBAA322 (активные фильтры) |
| ESP32-S3 datasheet | https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf | Технические данные S3 (2.4 GHz, I2S, PSRAM) |
| Linkwitz–Riley crossover — Википедия | https://en.wikipedia.org/wiki/Linkwitz–Riley_filter | Теория LR4 (24 дБ/окт, 360° фаза, плоская сумма); ссылки на RaneNote 119/160, статью AES 1976 |
| Linkwitz Lab (оригинал) | https://www.linkwitzlab.com/crossovers.htm | Кроссоверы, активные фильтры, статьи |
| arduino-audio-tools | https://github.com/pschatzmann/arduino-audio-tools | Уже зависимость проекта; примеры A2DP/Wi-Fi аудио |

### 6.2 Визуальные ресурсы (CC/бесплатные)

| Ресурс | Ссылка | Идея интеграции |
|--------|--------|-----------------|
| **Openverse** (поиск CC-изображений, API) | https://openverse.org | Поиск фото плат/динамиков. Найдено: ESP32-C3 NodeMCU (CC BY-SA 4.0), ESP32 (CC BY 2.0), ESP32-C3 (CC0) — примеры для блока «Железо». Фильтр: `license_type=commercial` |
| Openverse API (пример запроса) | `https://api.openverse.org/v1/images/?q=esp32&license_type=commercial` | Автоматический подбор/проверка лицензий перед публикацией |
| Unsplash (стоковые фото) | https://unsplash.com | Фото-колонки/домашнее аудио для hero-секции |
| Pexels | https://pexels.com | Бесплатные фото и видео для демо-ролика |
| SVG Repo | https://www.svgrepo.com | Иконки (Wi-Fi, динамик, ESP32) для секций |
| Iconify | https://icon-sets.iconify.design | Набор иконок для UI/лендинга |
| Storyset | https://storyset.com | Иллюстрации «wireless audio» для целевой страницы |
| unDraw | https://undraw.co | Flat-иллюстрации (open source) |

### 6.3 Аудио-датасеты и тестовые материалы

| Ресурс | Ссылка | Идея |
|--------|--------|------|
| GTZAN Genre Collection (Kaggle mirror) | https://www.kaggle.com/datasets/andradaolteanu/gtzan-dataset-music-genre-classification | Тест кроссовера/тембра на жанрах; демо-плейлист для видео |
| FMA (Free Music Archive) | https://github.com/mdeff/fma | CC-музыка для тестов DSP и контента лендинга |
| MIR-1K (вокал + аккомпанемент) | https://zenodo.org/records/3531979 | Разделение каналов, тест кроссовера |
| Audiocheck (тестовые тоны) | https://www.audiocheck.net | Синусы/щелчки/розовый шум для проверки кроссовера, каналов, задержек |
| SoX (генератор сигналов, Linux) | https://sox.sourceforge.net | `sox -n tone.wav synth sine 1000 vol 0.5` — тоны для автотестов |
| YouTube Audio Library | https://studio.youtube.com | Музыка без лицензионных сборов для демо-видео |

### 6.4 Идеи наполнения целевой страницы (GitHub Pages / README)

1. **Hero-блок**: название + 1-фразовое описание + бейджи (CI, license, PlatformIO, ESP32-S3).
2. **Блок-схема потока** — заменить ASCII на **Mermaid** (GitHub рендерит natively):
   `Smartphone → UDP PCM → Master(S3) → DSP(LR4) → L/R TX (ESP-NOW/UDP) → Satellites; Sub → I2S → PCM5102A → сабвуфер`.
3. **«Как это работает»**: секции по транспорту (ESP-NOW: 250 Б, low-latency; UDP-режим), DSP (LR4 24 дБ/окт, кроссовер 70–120 Гц), DAC (PCM5102A: 112 dB, PLL без MCLK).
4. **Галерея**: скриншоты Web UI (добавить в `docs/screenshots/`), фото сборки (Openverse/свои).
5. **Спецификация**: таблица «Параметр — значение» (SNR, диапазон, задержки 0–200 мс, каналы).
6. **Блок «Материалы»**: ссылки из 6.1 (datasheet PCM5102A, ESP-NOW docs, LR-теория).
7. **Демо-контент**: подборка FMA/GTZAN + тестовые тоны Audiocheck — ссылки в разделе «Тестирование».
8. **Раздел «Сборка и тестирование»**: команды `make -C test test`, flash-скрипты (уже в README).
9. **Pages**: включить GitHub Pages из ветки `main` (каталог `/docs`) и разместить `index.html`-лендинг (или использовать README).

---

## 7. Сводный чек-лист исправлений

### Блокеры (сделать в первую очередь)
- [x] S3 — добавить `LICENSE` (GPL-3.0, коммит 7e0a100).
- [ ] S7 — активировать GitHub Actions (Settings → Actions; workflow готов и захардкожен).
- [x] V1 — шифрование ESP-NOW (PMK/LMK + `encrypt=true`, коммит фазы 1; ключи `AUDIO_ESPNOW_PMK/LMK` в конфиге).
- [x] V2 — PBKDF2-HMAC-SHA256 (10000 итераций); **остаток**: per-device соль и мин. пароль 8 — техдолг (NVS v5).

### Приоритет 1 (безопасность/стабильность)
- [ ] V4 — аутентификация UDP-источника (токен/remoteIP).
- [x] S6 — CI: `permissions`, `timeout-minutes` (коммит 7e0a100).
- [ ] C3.4 — дрейф-коррекция сателлитов.

### Приоритет 2 (стандарты репозитория)
- [x] S2 — бейджи в README.
- [ ] S8/S9/S10/S11 — SECURITY.md, CONTRIBUTING.md, CHANGELOG.md, шаблоны.
- [x] S14 — перенос ТЗ в `docs/`, удаление мусора.
- [ ] S15 — скриншоты Web UI.

### Приоритет 3 (рефакторинг)
- [ ] A9 — очередь FreeRTOS для RX ESP-NOW.
- [ ] A1 — вынести SPA из `web_server.h`.
- [ ] A4/A5/A6 — инкапсуляция состояния, тайминг, отказ от `String`.
- [ ] A3 — заморозить legacy `master/` как стенд C0.2.
- [ ] E2E-тест канальных громкостей и sub-задержки.

### Контент целевой страницы
- [ ] Включить GitHub Pages (`/docs`) или добавить `index.html`.
- [ ] Mermaid-схема потока вместо ASCII.
- [ ] Разделы по материалам из 6.1–6.3 со ссылками и атрибуциями CC.
