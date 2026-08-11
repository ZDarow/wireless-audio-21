# Wireless Audio 2.1 — Файл задач

> Список задач по итогам проверки проекта (10.08.2026) и план дальнейших
> итераций. Живой документ: статусы обновляются по мере выполнения.
> Формат задач: `T<номер>` — техдолг/правки, `F<номер>` — функциональность
> (совпадает с нумерацией требований в `docs/PLAN.md`), `B<номер>` — баги по
> итогам ревизии кода.

---

## 1. Срочно / блокеры

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| T1 | **CI не запускается**: на GitHub 0 runs при активном workflow и включённых Actions. Вероятно, Actions не активированы в настройках репозитория (Settings → Actions → Enable). В `ci.yml` добавлен `workflow_dispatch` | `.github/workflows/ci.yml` | Высокий | ⬜ (workflow_dispatch добавлен) |

---

## 2. Техдолг и мелкие правки

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| T2 | Docstring `generate_config.py` упоминает флаг `--out`, фактический интерфейс — позиционный аргумент. Привести справку/docstring в соответствие с реальным CLI | `scripts/generate_config.py` | Низкий | ✅ |
| T3 | Расхождение со спецификацией §6.4: REST использует POST вместо PUT для volume/crossover/delay. Решить: привести к PUT или зафиксировать POST в документации | `firmware/master/include/web_server.h` | Низкий | ✅ (приведено к PUT) |
| T4 | `/api/status` отдаёт `left_online`/`right_online` (bool), спецификация §6.4 ожидает `satellites: {left, right}` со значениями «online»/«offline». Привести формат ответа к спецификации (обратная совместимость не нужна — MVP) | `firmware/master/include/web_server.h` | Низкий | ✅ |
| T5 | UDP-транспорт: мастер шлёт broadcast, спецификация §5.6 предполагает UDP unicast по MAC/IP сателлитов. Нужен discovery (сателлиты отвечают на broadcast-запрос, мастер запоминает IP) или явная настройка IP в конфиге | `firmware/common/transport/udp_transport.h`, `firmware/master/src/main.cpp` | Средний | ✅ (discovery: request/response, unicast после запоминания IP) |
| T6 | `config.example.h` — ручная конфигурация; проверить, что она не расходится с `generated_config.h` (макросы дублируются в двух источниках) | `config.example.h`, `firmware/common/generated/generated_config.h` | Низкий | ✅ (23 макроса совпадают, обновлён комментарий) |

---

## 2.1 Баг-лист по итогам ревизии кода (10.08.2026)

| ID | Проблема | Место | Приоритет | Статус |
|---|---|---|---|---|
| B1 | `g_leftOnline`/`g_rightOnline` нигде не выставляются в true: статус сателлитов в `/api/status` и serial всегда «offline». Нет ESP-NOW sent-callback и UDP-сердцебиений | `firmware/master/src/main.cpp:47` | Высокий | ✅ (ESP-NOW sent-callback по MAC + UDP discovery каждые 3 с + timeout 5 с) |
| B2 | Конфликт пинов в `config.example.h`: `AUDIO_I2S_DATA_OUT=22` и `AUDIO_OLED_SCL=22` (актуально при реализации F12) | `config.example.h` | Средний | ✅ (OLED SCL → 18 — валидный пин S3, 23 невалиден; см. B17) |
| B3 | `UdpTransport::broadcast()` считает broadcast-IP как `~localIP` + `bc[3]=255` — корректен только для /24-подсети | `firmware/common/transport/udp_transport.h:53` | Низкий | ✅ (broadcast по маске подсети, `broadcast_ip.h`) |
| B4 | `handleVolume` без поля `volume` в JSON молча ставит громкость 0 вместо ошибки | `firmware/master/include/web_server.h:528` | Низкий | ✅ (ошибка `missing volume|mute`) |
| B5 | `generate_config.py:133` повторно вызывает `expand(env)` при выводе счётчика макросов (косметика) | `scripts/generate_config.py` | Низкий | ✅ (результат переиспользуется) |
| B6 | Риск нестабильности A2DP + Wi-Fi STA coexistence на ESP32 (нужна проверка на железе) | мастер | Риск | ⬜ (актуально только для legacy `master_a2dp` — см. C0.2) |
| B7 | Partition `huge_app.csv` без OTA — осознанное MVP-решение, блокирует будущий OTA (legacy `master_a2dp`; у `master_s3_wifi` — `default_16MB.csv` с OTA) | `platformio.ini:30` | Замечание | ⬜ (см. C0.2) |
| B8 | Имя `udp.h` в case-insensitive ФС (Windows) конфликтует с системным `Udp.h` из Arduino (включается `WiFiUdp.h`) — сборка падала. Файл переименован в `udp_transport.h` | `firmware/common/transport/udp_transport.h` | Высокий | ✅ (переименование + сборка 3 env SUCCESS) |
| B9 | **APSTA-репитер (core 3.x): мастер не отвечает на AP-интерфейсе** — клиент за AP получает IP (DHCP), интернет через NAPT работает (ping 8.8.8.8, HTTP/HTTPS 200), но unicast на AP-IP не обрабатывается: ping/ARP `192.168.4.1` с клиента — 100% loss, Web UI недоступен по `192.168.4.1` (доступен только через STA-IP `192.168.1.129`). Диагностика `net` с мастера: `ping ap_gw` FAIL при живом AP-интерфейсе. Похоже на ограничение APSTA в Arduino core 3.x / IDF 5.5 (AP netif не принимает пакеты на свой IP, если активен STA) | `firmware/master_s3/src/main.cpp`, Wi-Fi APSTA | Средний | ✅ (dual WebServer: отдельный экземпляр на `softAPIP():80` для AP-клиентов + на `localIP():80` для STA; captive portal `DNSServer "*"→softAPIP()` + probe-роуты `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, `/connecttest.txt`, `/fwlink`; скан сетей выполняется ДО старта AP и кешируется (`scanWifiBeforeAp`), т.к. `WiFi.scanNetworks()` при активном Soft-AP отключает радио и роняет соединение телефона в момент POST с кредами. Остаётся ручная проверка на железе: Web UI по `192.168.4.1` и автооткрытие страницы настройки) |
| B10 | **IPv6 не форвардится через NAPT** — DNS возвращает AAAA-записи, клиенты за AP мастера пытаются идти по IPv6 и «зависают» (curl без `-4` = FAIL, HTTP 000), хотя IPv4 полностью работает. Нужно: не отдавать IPv6-маршрут/RA на AP (проверить, что отключено) или документировать требование IPv4 | мастер (APSTA) | Низкий | ⬜ (осознанное ограничение: NAPT в lwIP — только IPv4, IPv6-форвардинг не реализован; требование «клиент использует IPv4» зафиксировано в `docs/PLAN.md`; проверено: IPv4-путь работает полностью) |

---

## 3. Пробелы в тестах

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| T7 | Тест переполнения и дефицита jitter buffer (overwrite старых семплов, pop из пустого буфера) | `test/audio_filter_test.cpp` | Средний | ✅ |
| T8 | Host-тест генератора конфига: `generate_config.py` → проверить все 23 макроса, включая MAC-адреса и строки с кавычками | `test/` (новый `config_gen_test.py`) | Средний | ✅ |
| T9 | Тест `ConfigStorage` (NVS) — только на железе; добавить round-trip тест в firmware-тестах или документировать ручную проверку | `firmware/common/config/storage.h` | Низкий | ✅ (инструкция ручной проверки в storage.h) |
| T10 | Тест UDP broadcast-адресации и `sendTo` (чистая логика выделения broadcast-IP) | `test/transport_packet_test.cpp` | Низкий | ✅ (`broadcast_ip.h` + тесты /8, /16, /24, /32) |

---

## 4. Функциональность (следующие итерации, из PLAN.md)

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| F12 | OLED-меню (SSD1306, U8g2) + энкодер (KY-040): громкость, кроссовер, задержки, статус сателлитов | `firmware/common/ui/display.h`, `encoder.h` | Средний | ⬜ |
| F13 | Wi-Fi UDP источник (мастер принимает поток по UDP вместо A2DP). ESP32-S3 не поддерживает A2DP — Wi-Fi UDP PCM (смартфон → мастер) становится единственным источником | `firmware/master_s3/src/main.cpp` | Средний | ⬜ |
| F14 | Синхронизация воспроизведения по `timestampMs` в пакете (компенсация дрейфа часов) | `firmware/common/transport/audio_packet.h`, `firmware/satellite/src/main.cpp` | Средний | ⬜ |
| F15 | Защита от щелчков при включении/остановке: fade-in/out на мастере и сателлитах | `firmware/common/audio/volume_control.h` | Средний | ⬜ |
| F16 | mDNS: доступ к Web UI по `http://audio-master.local` | `firmware/master_s3/src/main.cpp` | Низкий | ✅ |
| F17 | Документация: `docs/architecture.md`, `docs/hardware.md`, `docs/wiring.md` | `docs/` | Низкий | ✅ |
| F18 | ESP32-S3: покомпонентные громкости (master/left/right/sub) + fade-in/out, разное время старта каналов (ТЗ §8.2) | `firmware/common/audio/volume_control.h` | Средний | ⬜ |
| F19 | ESP32-S3: синхронизация часов сателлитов от мастера по timestamp (PTP-подобная, ТЗ §12) | сателлиты | Средний | ⬜ |
| F20 | ESP32-S3: режим STA + автонастройка Wi-Fi (blink beacon, ТЗ §6.2), UDP/HTTP-аудио (RTP-совместимый, ТЗ §10) | `firmware/master_s3/src/main.cpp` | Низкий | ✅ (настройка Wi-Fi через Web UI: `/api/wifi/scan` + `/api/wifi`, при неудачном STA — AP настройки + `http://192.168.4.1`) |
| F21 | Wi-Fi репитер на мастере (APSTA+NAPT): смартфон → AP мастера → NAT → домашняя сеть (интернет). Требует Arduino core 3.x (pioarduino 55.03.311, lwip с `CONFIG_LWIP_IPV4_NAPT=y`); ограничение: один радиомодуль → канал AP следует за каналом домашней сети | `platformio.ini`, `firmware/master_s3/src/main.cpp`, `firmware/common/config/node_config.h` | Средний | ✅ |

---

## 5. Статус на момент создания (10.08.2026)

- Проект собран: 3 env PlatformIO — SUCCESS (master 58.4% flash, сателлиты 24.2%).
- Host-тесты: 3 файла, все зелёные (`make test`).
- Генератор конфига: 23 макроса, работает.
- Git: `main` синхронизирован с `origin/main`, артефакты не коммитятся.
- Открытых блокеров нет; единственный срочный пункт — активация CI (T1).

## 6. Выполнено (обновление 10.08.2026)

- **T2–T7** — техдолг закрыт: docstring генератора, REST PUT, формат
  `/api/status` с `satellites`, UDP discovery (unicast), синхронизация
  `config.example.h`, тесты jitter buffer (коммит `a93e202`).
- **F17** — документация: `docs/architecture.md`, `docs/hardware.md`,
  `docs/wiring.md`, ссылки из README.
- **Ревизия кода** — host-тесты проверены на машине разработчика (g++ 14.2,
  все 3 бинаря зелёные), добавлен `AGENTS.md` и баг-лист B1–B7.
- **B8 + сборка** — PlatformIO Core 6.1.19 установлен, сгенерирован конфиг
  (23 макроса), `udp.h` → `udp_transport.h` (коллизия с системным `Udp.h`),
  `pio run` для всех 3 env — SUCCESS (master 58.4% flash, сателлиты 24.2%);
  в `.gitignore` добавлены `.vscode/c_cpp_properties.json`, `.vscode/launch.json`.
- **B1** — статус сателлитов: `EspNowSentCallback` теперь передаёт MAC пира,
  мастер помечает канал online по успешной доставке; в UDP-режиме discovery
  идёт каждые 3 с и обновляет online; timeout 5 с — offline.
- **Этап 1 (адаптация ESP32-S3)** — выполнено:
  - `platformio.ini`: новые env `master_s3_wifi`, `satellite_s3_left`,
    `satellite_s3_right` (board `esp32-s3-devkitc-1`, 16MB
    `default_16MB.csv`, `memory_type=qio_opi`, PSRAM `-DBOARD_HAS_PSRAM`).
  - Конфиг: `AUDIO_WIFI_MODE` (AP_DIRECT/STA), `AUDIO_UDP_PORT` (5004),
    `WifiMode` в `NodeConfig`, `AUDIO_SAMPLE_RATE` → 48000; генератор теперь
    выдаёт 25 макросов; `config.example.env`/`config.example.h` обновлены.
  - `firmware/master_s3/` — загрузка S3, стартовая диагностика §14.2
    (chip/flash/PSRAM/IP/пины/MAC/транспорт/кроссовер/задержки), Wi-Fi
    AP_DIRECT (канал 6) или STA, UDP-listener на 5004, serial-консоль
    (`status`, `save`).
  - CI: `ci.yml` собирает новые S3 env, добавлен `workflow_dispatch`.
  - Сборка: 3 S3 env — SUCCESS (master 10.7% flash, сателлиты 22.6%);
    host-тесты зелёные.
- Осталось: T1 (активация Actions), F12–F16, F18–F20,
  Этап 2 (Wi-Fi приём PCM: `udp_audio_receiver`, jitter buffer, clock recovery).

## 7. Выполнено (обновление 10.08.2026, вечер)

- **F21 — Wi-Fi репитер (APSTA+NAPT)** — реализовано:
  - `platformio.ini`: env `master_s3_wifi` переведён на pioarduino-платформу
    (Arduino core 3.3.11 / IDF 5.5.5, тег `55.03.311`), где lwip собран с
    `CONFIG_LWIP_IP_FORWARD=y` и `CONFIG_LWIP_IPV4_NAPT=y` — NAPT работает
    из коробки. Официальные PlatformIO 6.x/7.x (core 2.0.17) NAPT не содержат
    (проверено по sdkconfig и символам `liblwip.a`). Удалена зависимость
    `martin-ger/esp32_nat_router` (это IDF-прошивка, не Arduino-библиотека).
  - Конфиг: добавлены AP-креды мастера `AUDIO_WIFI_AP_SSID`/`AUDIO_WIFI_AP_PASSWORD`
    (ТЗ §6.3: `Audio21-Master`/`audio21master`); `AUDIO_WIFI_SSID`/`PASSWORD` —
    домашняя сеть (uplink). `NodeConfig` + генератор (+2 строковых ключа, всего 27).
  - `main.cpp`: режим `ApSta` — `WIFI_AP_STA`, SoftAP (`Audio21-Master`, канал 6),
    STA-подключение к домашней сети, затем `esp_netif_napt_enable()` (core 3.x,
    guard `#if ESP_ARDUINO_VERSION_MAJOR >= 3`); при недоступной домашней сети
    AP остаётся висеть без интернета. Команда `status` выводит `wifi_ap_ip`.
  - Сборка: `master_s3_wifi` на core 3.3.11 — SUCCESS (RAM 14.2%, Flash 13.5%);
    сателлиты (core 2.0.17) — SUCCESS.
  - **Изоляция core-каталога**: пакет pioarduino `framework-arduinoespressif32`
    (core 3.x) совпадает по имени с фреймворком официальной платформы
    `espressif32@6.9.0` (core 2.0.17) для сателлитов — в общем `~/.platformio`
    сборки перезаписывали друг друга (мастер падал с `FRAMEWORK_DIR=None`).
    Решено: `platformio.master.ini` с `[platformio] core_dir = .pio-core-master`
    (gitignored) + `extra_configs = platformio.ini`; CI и локальная сборка
    мастера идут через `pio run -c platformio.master.ini`.
  - Проверка на железе (COM10): прошито, boot-лог подтверждает
    `wifi mode: apsta_repeater`, AP `Audio21-Master` (канал 6, 192.168.4.1),
    STA-подключение к домашней сети `Cudy` (192.168.10.42),
    `NAPT enabled — AP clients routed to upstream`.
    Остаётся финальный ручной тест: смартфон → AP мастера → интернет.

## 8. Выполнено (обновление 11.08.2026)

- **F20 — ручная настройка Wi-Fi со смартфона через Web UI** — реализовано:
  - `web_server.h`: добавлены эндпоинты `GET /api/wifi/scan` (список найденных
    сетей: SSID/RSSI/enc) и `POST /api/wifi` (`{"ssid","password"}` → сохранить
    в NVS + reboot). В HTML-панель добавлена карточка «Wi-Fi подключение»
    (сканирование, клик по сети, пароль, «Сохранить и перезагрузить»);
    `/api/status` отдаёт `wifi_mode`/`wifi_ssid`/`wifi_ip`/`wifi_ap_ip`.
  - `web_server.h`: аудио-зависимости (`PcmPipeline`, `DelayLine`, `EspNowTransport`,
    статусы) стали опциональными (указатели, дефолт `nullptr`) — мастер ESP32-S3
    (этап 1, без аудио-конвейера) использует тот же Web UI для настройки Wi-Fi
    и диагностики; аудио-эндпоинты возвращают `unavailable`.
  - `master_s3/src/main.cpp`: подключён `MasterWebServer`; при неудачном
    STA-подключении (сеть не найдена/неверный пароль) мастер поднимает AP
    настройки и запускает Web UI на `http://192.168.4.1` (`setup_mode`);
    добавлена serial-команда `wifi <ssid> <password>`.
  - `espnow.h`: совместимость с IDF 5.x (core 3.x) — сигнатуры
    `esp_now_recv_cb_t`/`esp_now_send_cb_t` изменились (MAC через
    `esp_now_recv_info_t::src_addr` / `wifi_tx_info_t::des_addr`);
    добавлены ветки `#if ESP_ARDUINO_VERSION_MAJOR >= 3`. Без этого `espnow.h`
    (подтягивается через `web_server.h`) не собирался в master_s3_wifi.
  - `platformio.ini`: env `master_s3_wifi` получил `-I firmware/master/include`
    (для `web_server.h`).
  - Сборка: `master_s3_wifi` (core 3.3.11) — SUCCESS (RAM 15.1%, Flash 15.3%);
    сателлиты S3 (core 2.0.17) — SUCCESS; `master_a2dp` — SUCCESS;
    host-тесты (`make test`) — все зелёные.

- **Авторизация + CSRF (ТЗ_Веб §18) — реализовано и проверено на железе (COM10)**:
  - Мутирующие эндпоинты (volume/crossover/delay/mute/transport/pair/save/reboot/
    factory_reset/wifi/connect/…/import/update) требуют `X-CSRF-Token` (`csrfOk()`),
    без сессии/токена — 401 `{"ok":false,"error":"csrf"}`.
  - Сессия — в RAM (`m_sessionActive` + случайный токен), cookie `session=…`;
    после ребута сессия сбрасывается (`/api/status` → `authed:false`, подтверждено
    на железе через логин → reboot → повторный запрос без cookie).
  - Аномалия «`authed:true` сразу после ребута»: причина — браузерный кеш
    `GET /api/status` (ответ без `Cache-Control`), SPA получала пред-ребутный ответ.
    Исправлено: `sendJson()` добавляет `Cache-Control: no-store` (проверено:
    заголовок присутствует, после ребута `authed:false`).
  - Критично для cookie-auth: кастомные заголовки регистрируются через
    `collectHeaders({"Cookie","X-CSRF-Token"},2)` ДО `begin()` — иначе
    `close()` в конструкторе WebServer сбрасывает список заголовков до одного
    `Authorization` (баг WebServer core 3.x, `WebServer.cpp:588/344`).
  - End-to-end (пароль `0000`): login 200 + Set-Cookie; PUT volume с токеном 200;
    crossover/delay/mute/save OK; reboot 200; после перезагрузки настройки
    сохранились (auto-save перед ребутом); logout очищает сессию.
  - **Полный SPA-сценарий прогоняется через Playwright (headless chromium) против
    железа (192.168.10.42) — 12/12 PASS**: загрузка SPA, показ логин-баннера без
    сессии, login скрывает баннер, dashboard (system=OK), слайдеры audio/delays
    с применением через PUT + CSRF без 401, hostname/version/logs загружаются,
    poll каждые 2с не даёт 401, logout возвращает баннер. После прогона настройки
    восстановлены (volume 72, sub delay 20 мс).


---

## 9. Аудит архитектуры (11.08.2026) — план рефакторинга

> Комплексный аудит (архитектура, SOLID, связность, алгоритмы, состояние,
> манифесты, структура). Код не менялся — зафиксированы только задачи.
> Критичность: 🔴 критично, 🟠 высоко, 🟡 средне, 🟢 низко.

### 9.1 Блокеры / критические

| ID | Крит. | Задача | Место | Статус |
|---|---|---|---|---|
| B11 | 🔴 | **Data race DSP-состояния**: `a2dpDataCallback` (задача A2DP) читает `m_target`/`m_crossoverHz`/`m_delaySamples`, а loop (Web UI/консоль) пишет их через `setVolume`/`setCrossoverHz`/`setDelayMs` без синхронизации. Нужен `portMUX_TYPE`/мьютекс вокруг сеттеров и `process()`; `g_cfg.transport` тоже меняется из двух задач | `master/src/main.cpp`, `web_server.h`, `pcm_pipeline.h`, `delay_line.h` | ⬜ (legacy `master` — см. C0.2) |
| B12 | 🔴 | **Неадаптация к sample rate A2DP**: pipeline/DelayLine сконфигурированы на `cfg.sampleRate` (48000), A2DP-источник часто отдаёт 44100 → неверный кроссовер/задержки. Получать реальную частоту из A2DP и переконфигурировать | `master/src/main.cpp` | ⬜ (legacy `master` — см. C0.2) |
| T11 | 🔴 | **Невоспроизводимая сборка**: git-зависимости `arduino-audio-tools` и `ESP32-A2DP` без фиксации SHA/тега. Зафиксировать ревизии или удалить неиспользуемые | `platformio.ini:68-69,121` | ⬜ |

### 9.2 Баг-лист аудита

| ID | Крит. | Проблема | Место | Статус |
|---|---|---|---|---|
| B13 | 🟠 | `JitterBuffer`: дефолт `m_targetLevel = 0` → `ready()` всегда true без конфигурации (щелчки/прерывания). Задать дефолт или требовать конфигурацию + тест | `firmware/common/audio/jitter_buffer.h` | ⬜ |
| B14 | 🟠 | Сателлит пишет в неинициализированный I2S при провале `initI2S` (риск зависания `i2s_write`). Нужен guard «I2S готов» | `firmware/satellite/src/main.cpp` | ⬜ |
| B15 | 🟡 | Лимитер стоит ДО volume — нет финальной защиты от клиппинга на выходе; перенести лимитер после volume | `firmware/common/audio/pcm_pipeline.h` | ⬜ |
| B16 | 🟡 | Контракт A2DP-входа (S16_LE stereo, `samples` = стерео-фреймы) не зафиксирован в коде | `firmware/master/src/main.cpp` | ⬜ (legacy `master` — см. C0.2) |
| B17 | 🟠 | **Дефолтные I2S-пины недействительны на ESP32-S3**: `SOC_GPIO_VALID_GPIO_MASK` исключает GPIO22–25 (внутренние SPI-флеша), а `AUDIO_I2S_WS=25`/`AUDIO_I2S_DATA_OUT=22` (и 26/25/22 в `docs/wiring.md`, `docs/hardware.md`) → `initI2S` на сателлите S3 падает с `i2s_set_pin: ws_io_num invalid` (проверено на железе, COM4). Требуются валидные S3-пины + синхронизация дока | `config.env`, `firmware/common/config/node_config.h:88-96`, `docs/wiring.md`, `docs/hardware.md` | ✅ (пины I2S → **4/5/6** (BCK/WS/DATA), OLED SCL 22→**18** (23 тоже невалиден на S3); обновлены `config.env`, `config.example.env`, `config.example.h`, `node_config.h`, `docs/wiring.md`, `docs/hardware.md`; `generated_config.h` перегенерирован; сателлит перепрошит на COM4 — boot-лог: `I2S ready`, `ESP-NOW ready`) |
| B18 | 🟠 | **Согласование RF-канала сателлита**: сателлит (ESP-NOW STA без ассоциации) по умолчанию слушает канал 1, мастер AP — канал 6 → пакеты не доходят. Исправлено хардкодом `esp_wifi_set_channel(6)` в `firmware/satellite/src/main.cpp` (**проверено на железе**: `wifi_channel: 6`, heartbeat принимается мастером). **Связь «без аудио-пакетов»**: мастер шлёт discovery-запрос broadcast каждые 2 с (`kFlagDiscoveryRequest`), сателлит запоминает MAC мастера (`addPeer` + unicast) и шлёт heartbeat (`kFlagDiscoveryResponse`) каждые 2 с; мастер помечает канал online по приёму heartbeat, таймаут 6 с → offline. Причина неработающего broadcast: `esp_now_send` возвращает `ESP_ERR_ESPNOW_NOT_FOUND` без зарегистрированного пира `FF:FF:FF:FF:FF:FF` — добавлен broadcast-пир в `EspNowTransport::begin()` (иначе и heartbeat, и discovery не уходили). Проверено на железе: `satellites.left=online` без аудио-потока; после удержания сателлита в reset 9 с → `left=offline`, после перезагрузки → `online`. Остаётся: автоопределение канала мастера в APSTA (канал AP следует за домашней сетью, F21) — макрос `AUDIO_ESPNOW_CHANNEL` уже добавлен в `config.env` | `firmware/common/transport/espnow.h`, `firmware/satellite/src/main.cpp`, `firmware/master_s3/src/main.cpp`, `config.env` | ✅ (см. выше; остаток — автоопределение канала в APSTA) |

### 9.3 Техдолг аудита (рефакторинг)

| ID | Крит. | Задача | Место | Статус |
|---|---|---|---|---|
| T12 | 🟠 | Удалить мёртвый код: `master_config.h`, `satellite_config.h` (`MasterPins`/`SatellitePins`/`masterPins`/`satellitePins`/`masterI2SDataOut` не используются), неиспользуемые `ui/display.h`/`ui/encoder.h` (без U8g2 в lib_deps), `audio-tools` из lib_deps `master_s3_wifi` (не используется) | `firmware/master/include`, `firmware/satellite/include`, `firmware/common/ui`, `platformio.ini` | ⬜ |
| T13 | 🟠 | Удалить мёртвые `build_flags -DAUDIO_NODE_ROLE=MASTER/-DAUDIO_SOURCE_MODE=A2DP/-DAUDIO_TRANSPORT_MODE=ESPNOW/-DAUDIO_NODE_ROLE=SATELLITE` (код читает `*_MASTER`/`*_A2DP` из generated_config.h). Единственный источник — `config.env` | `platformio.ini:59-61,78,87` | ⬜ |
| T14 | 🟠 | Дублирование I2S-обвязки (`initI2S`/`writeSample` vs `initI2SSub`/`writeSubSample`) — вынести в `firmware/common/audio/i2s_output.h` с guard-ами core 2.x/3.x | `master/src/main.cpp`, `satellite/src/main.cpp` | ⬜ |
| T15 | 🟠 | Ввести транспорт-интерфейс (`ITransport` с `sendTo/sendToChannel/broadcast`): убрать ветвления `if (transport == EspNow/Udp)` из `flushSatelliteBatches`/setup/loop (OCP) | `firmware/common/transport`, `master/src/main.cpp`, `satellite/src/main.cpp` | ⬜ |
| T16 | 🟠 | `web_server.h` лежит в `master/include`, используется `master_s3` — перенести в `firmware/common/web/`; разбить на web-core + опциональные audio-хендлеры; сгруппировать 9 параметров конструктора в `struct AudioContext` (ISP) | `firmware/master/include/web_server.h`, `platformio.ini` | ⬜ |
| T17 | 🟡 | Serial-консоль дублируется в 3 main.cpp — выделить общий парсер команд в `firmware/common/console.h` | `firmware/*/src/main.cpp` | ⬜ |
| T18 | 🟡 | CI: зафиксировать версию PlatformIO (`pipx install platformio==6.1.19`), добавить кэширование `~/.platformio` и `.pio-core-master` | `.github/workflows/ci.yml` | ⬜ |
| T19 | 🟡 | Миграция сателлитов на Arduino core 3.x (`driver/i2s_std.h`) → единый core-каталог, удаление `platformio.master.ini` (снимает конфликт фреймворков, legacy I2S в IDF 5.x deprecated) | `platformio.ini`, `platformio.master.ini`, `satellite/src/main.cpp` | ⬜ |
| T20 | 🟡 | Синхронизировать документацию: `architecture.md` §9 («старые env в истории» — неверно, они в файле), §10 (mDNS реализован — F16), структура §8 без `master_s3`; `flash_master.sh`/`README.md` ведут на `master_a2dp` вместо `master_s3_wifi` (`hardware.md`/`wiring.md` уже синхронизированы — B17) | `docs/`, `scripts/flash_master.sh`, `README.md` | ⬜ |
| T21 | 🟡 | `test/Arduino.h` перекрывает системное имя — вынести заглушки в `test/stubs/` с явным `-I` | `test/` | ⬜ |
| T22 | 🟢 | Убрать `WebServer`/`Preferences` из `lib_deps` (встроены в фреймворк, PIO их не тянет отдельно — запись избыточна) | `platformio.ini:37,66,116` | ⬜ |

### 9.4 Code review незакоммиченных изменений (11.08.2026) — закрыто

| ID | Крит. | Проблема ревью | Исправление | Статус |
|---|---|---|---|---|
| R1 | 🔴 | `/api/admin/setup` — сброс пароля без авторизации (backdoor) | Reject при `authEnabled==true` (`web_server.h:handleAdminSetup`) | ✅ |
| R2 | 🔴 | `/api/update` — reboot даже при неудачном `end(true)`; авторизация только по глобальному флагу | Cookie+CSRF auth (`csrfOk && isAuthed`), `Update.abort()` при ошибке, `Update.end(false)` + reboot только после валидного образа; флаг `m_updateActive` | ✅ |
| R3 | 🟠 | `auth.h:159` — `rand()` без `srand` | `esp_fill_random` (ESP32) / `rand()` fallback для host-тестов | ✅ |
| R4 | 🟠 | `web_server.h:1184` — XSS через `innerHTML` с SSID | `createTextNode`/`textContent` (профили и скан) | ✅ |
| R5 | 🟠 | CSRF-токен на неавторизованном `/api/status`; `csrfOk` не привязан к cookie | Токен только при `isAuthed(s)`; `csrfOk` требует совпадение cookie; `isAuthed` больше не доверяет глобальному `m_sessionActive` | ✅ |
| R6 | 🟠 | `storage.h` — `kVersion=2` без миграции → тихий сброс настроек | Миграции v1→v2 и v2→v3 (зеркала структур, сохранение полей, запись обратно); v3: удалены write-only `staticIp*`/`wifiAutoReconnect` | ✅ |
| R7 | 🟠 | `main.cpp:567` — reconnect `WiFi.mode(WIFI_STA)` убивает AP без fallback | Сохранение AP (ApSta/ApDirect), неблокирующий таймаут `kReconnectTimeoutMs`, fallback на setup AP | ✅ |
| R8 | 🟠 | `satellite/main.cpp:249` — хардкод канала 6 | Макрос `AUDIO_ESPNOW_CHANNEL` (config.env → generated_config.h), мастер `kDefaultWifiChannel` тоже из него | ✅ |
| R9 | 🟠 | `satellite/main.cpp:139` — мёртвая UDP-ветка heartbeat (мастер не слушает порт) | Удалена; heartbeat только по ESP-NOW | ✅ |
| R10 | 🟠 | Экспорт 12 полей vs импорт 4 (потеря данных) | Симметричный export/import (включая net-check, NTP, MAC), импорт с `clamp()` + auth | ✅ |
| R11 | 🟠 | `wifi/connect` теряет static-IP профиль | Полный профиль как в `/api/wifi/save` (ip_mode/ip/netmask/gateway/dns/priority) | ✅ |
| R12 | 🟠 | `main.cpp:553` — блокирующий internet check в loop | Отдельная FreeRTOS-задача `internetCheckTask` (stack 4 КБ, приоритет 1); loop только логирует смену статуса | ✅ |
| R13 | 🟢 | Discovery broadcast каждые 2 с постоянно | Только пока хоть один сателлит offline | ✅ |
| R14 | 🟢 | Дублирование констант heartbeat/timeout | Общие `kHeartbeatIntervalMs`/`kSatelliteTimeoutMs` в `audio_packet.h` | ✅ |
| R15 | 🟢 | Мёртвый код: `requireAuth`, `setInternetHttpFn`, `m_sessionStartMs`, `staticIpEnabled`, `wifiAutoReconnect` | Удалены | ✅ |

---

## 10. План устранения расхождений по ТЗ (аудит 11.08.2026)

> План-график по итогам проверки проекта на соответствие `ТЗ.md` и `ТЗ_Веб.md`.
> Легенда приоритетов: 🔴 критично (блокирует звук/приёмку), 🟠 высоко,
> 🟡 средне, 🟢 низко (чистка). Формат: `C<этап>.<задача>` — расхождения,
> связка с B/F/T-номерами указана в колонке «Связь».
>
> **Подробные технические карточки каждой задачи (проблема, план, код-скетчи,
> файлы, критерии, тесты, оценки) — в [`TASKS_DETAILED.md`](TASKS_DETAILED.md).**

### Этап 0. Решения по конфликтам ТЗ (полдня)

| ID | Расхождение | Действие | Приоритет | Статус |
|---|---|---|---|---|
| C0.1 | SSID режима настройки: ТЗ.md §6.3 `Audio21-Master` vs ТЗ_Веб §3.2 `Audio21-Setup` | Согласовать: оставить `Audio21-Master` (базовое ТЗ) и зафиксировать в ТЗ_Веб, либо ввести `AUDIO_WIFI_SETUP_SSID` для режима настройки | 🟠 | ⬜ |
| C0.2 | Legacy env `master_a2dp` использует `BluetoothA2DPSink` (запрещён §8.3) | Решить: удалить env + `firmware/master/` или пометить «отладочный стенд, вне поставки»; при удалении перенести `web_server.h` в `common/web` (T16) | 🟠 | ⬜ |
| C0.3 | Веб-часть реализована раньше аудиоядра | Принять как факт, приоритизировать аудиоядро (Этапы 1–3) | — | ✅ |

### Этап 1. 🔴 Приём аудио по UDP + вывод на сабвуфер (ТЗ §18, Этап 2) — ~1 неделя

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C1.1 | `udp_audio_receiver`: разбор пакета по §9.1 (magic `0xA210`, version, flags, `sequence`, `timestamp_samples`, `sample_rate`, `channels`, `bits_per_sample`, `payload_length`) | `firmware/common/transport/udp_audio_packet.h`, `common/audio/udp_audio_receiver.h` (новые) | T13/F13 | Разбор валидных/невалидных пакетов; host-тест | ✅ (11.08.2026: `udp_audio_packet.h` + host-тест в `transport_packet_test.cpp`; `udp_audio_receiver.h` — закрыт как C1.2) |
| C1.2 | Проверка `sequence`, concealment, ramp-out, mute при отсутствии потока >3 с (§9.3) | `udp_audio_receiver` | §9.3 | Пропуск пакетов → тишина/плавное затухание; тест | ✅ (11.08.2026: `common/audio/udp_audio_receiver.h` + host-тест; см. «Выполнено») |
| C1.3 | Jitter buffer мастера **в PSRAM**, 20–60 мс (§7.6, §16.2) | `common/audio/jitter_buffer.h` | B13 | Буфер в PSRAM, ready()/deficit работают | ✅ (11.08.2026: `master_s3/src/main.cpp` — `ps_malloc` 60 мс/48 кГц, target 30 мс, выдача через `audioOutTick()`; см. «Выполнено») |
| C1.4 | I2S-выход мастера (пины 4/5/6), вынести в общий `common/audio/i2s_output.h` | `master_s3/src/main.cpp`, `common/audio/i2s_output.h` (новый) | T14 | Звук со смартфона через PCM5102A (**критерий §18 Этап 2**) | ✅ (11.08.2026: `common/audio/i2s_output.h` (автовыбор `i2s_std.h`/`i2s.h` по `ESP_ARDUINO_VERSION_MAJOR`, моно L=R, DMA 8×256); сателлит переведён на обёртку; master_s3 — init + `tone <freq>`; обе ветки проверены на хосте. Критерий «звук на сабвуфере» — ручная проверка) |
| C1.5 | Заменить «счётчик байт» в loop() на реальный приём | `master_s3/src/main.cpp` | §18 Этап 2 | Пакеты не отбрасываются | ✅ (11.08.2026: loop() разбирает UDP-пакет `parseUdpPacket` (§9.1), стерео→моно, `feed()` + jitter + I2S; `g_audioActive` для Web UI; см. «Выполнено») |

### Этап 2. 🔴 DSP + кроссовер на S3-мастере (ТЗ §18, Этап 3) + 🟠 громкости — ~1 неделя

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C2.1 | Подключить `PcmPipeline` (volume → tone → limiter → LR4 → L/R/Sub) к `master_s3` | `master_s3/src/main.cpp` | F13 | Сабвуфер играет только НЧ (**критерий §18 Этап 3**) | ✅ (12.08.2026: `PcmPipeline` в аудио-пути loop() — стерео → `sub` (моно-микс → LPF), `concealGain()` при потерях; volume/mute/crossover из конфига; см. «Выполнено») |
| C2.2 | `sub_volume` + `left_volume`/`right_volume` (мин. набор §7.5) | `NodeConfig`, `web_server.h`, SPA | F18 | В `/api/status` и UI — 4 громкости | ⬜ (частично: общая громкость/мьют/кроссовер живые через pipeline; отдельные L/R/Sub громкости не реализованы) |
| C2.3 | Delay lines L/R/Sub на мастере S3 (0–200 мс, NVS) | `master_s3/src/main.cpp` | §7.4 | Задержки применяются без ребута | ✅ (12.08.2026: `DelayLine` L/R/Sub в PSRAM (ёмкость `kMaxDelayMs`=200 мс), задержки из конфига; в `delay_line.h` добавлен внешний буфер; см. «Выполнено») |
| C2.4 | Подключить аудио-хендлеры Web UI к реальному pipeline (сейчас — только конфиг) | `web_server.h` | §16 | Слайдеры реально меняют звук | ✅ (12.08.2026: `&g_pipeline` + `&g_delayLeft/Right/Sub` переданы в `MasterWebServer` — `/api/volume`, `/api/mute`, `/api/crossover`, `/api/delay` применяются к живым объектам; см. «Выполнено») |

### Этап 3. 🔴 Аудио-TX на сателлиты + 🟠 синхронизация (ТЗ §18, Этап 4–5) — ~1.5 недели

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C3.1 | Батчевый ESP-NOW/UDP TX аудио с мастера S3 (сейчас только heartbeat) | `master_s3/src/main.cpp` | F13 | Л/П сателлиты играют свои каналы (**критерий §18 Этап 4**) | ⬜ |
| C3.2 | `render_timestamp` в пакетах (§10.1) + clock recovery на мастере | `audio_packet.h`, `master_s3/src/main.cpp` | F14, F19, §11 | Метки времени заполняются и используются | ⬜ |
| C3.3 | Сателлит: ждать `JitterBuffer::ready()`; целевой уровень **20/40/80 мс** (сейчас 15/50 мс) | `satellite/src/main.cpp` | §10.3 | Нет щелчков на старте; буфер держит уровень | ⬜ |
| C3.4 | Дрейф-коррекция на сателлите | `satellite/src/main.cpp` | F14, §10.3 | Нет накопления/истощения буфера за 30 мин | ⬜ |
| C3.5 | `volume_control` на сателлите (§8.6) + fade-in/out | `satellite/src/main.cpp` | F15, F18 | Громкость сателлита регулируется, без щелчков | ⬜ |

### Этап 4. 🟠 OLED + энкодер (ТЗ §12.1–12.2, Этап 6) — ~1 неделя

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C4.1 | OLED SSD1306 + U8g2 (`display_ui`): громкость, источник, статусы, кроссовер, задержки | `common/ui/display.h`, `lib_deps` | F12, §12.2 | §12.2 отображается | ⬜ |
| C4.2 | Энкодер KY-040 (`encoder_ui`): короткое/длинное нажатие, меню | `common/ui/encoder.h`, `master_s3/src/main.cpp` | F12, §12.1 | Функции §12.1 работают | ⬜ |
| C4.3 | Подключить в `master_s3` (в `master_a2dp` опционально) | `master_s3/src/main.cpp` | §12 | Управление с дисплея работает | ⬜ |

### Этап 5. 🟠 Веб-доработки по ТЗ_Веб — ~1 неделя

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C5.1 | Применение статического IP из профиля (`WiFi.config`) | `master_s3/src/main.cpp` | ТЗ_Веб §6.3, §21.2 | Профиль со static IP получает заданный адрес | ⬜ |
| C5.2 | Session timeout (ввести `m_sessionStartMs`, проверять в `isAuthed`/`handleClient`) | `web_server.h` | ТЗ_Веб §11.4, §23.1 | Сессия гаснет через 3600 с | ⬜ |
| C5.3 | Rate limit для `/api/login` и scan | `web_server.h` | ТЗ_Веб §23.1 | Блокировка после N неудач | ⬜ |
| C5.4 | Лог-буфер 16–64 KB + фильтры `level`/`module` в `/api/logs` | `logs.h`, `web_server.h` | ТЗ_Веб §13.4, §17.5 | §13.4, §17.5 | ⬜ |
| C5.5 | `cpu_load_percent` в `/api/status`; время/NTP/SSID/RSSI/MAC на Dashboard | `web_server.h`, SPA | ТЗ_Веб §5.2, §24.1 | §5.2, §24.1 | ⬜ |
| C5.6 | Прогресс-бар OTA | SPA | ТЗ_Веб §12.3–12.4 | Прогресс виден | ⬜ |
| C5.7 | Ограничение доступа локальной подсетью (§23.2); MAC-фильтр/проверка источника UDP (§17) | `web_server.h`, `udp_audio_receiver` | ТЗ_Веб §23.2, ТЗ §17 | Неавторизованный/внешний доступ блокируется | ⬜ |

### Этап 6. 🟡 Надёжность и чистка — ~1 неделя

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C6.1 | Watchdog для основных задач | `master_s3/src/main.cpp`, `satellite/src/main.cpp` | §16.3 | Перезапуск задачи при зависании | ⬜ |
| C6.2 | Авто-переподключение Wi-Fi при обрыве в рантайме | `master_s3/src/main.cpp` | §16.3, ТЗ_Веб §21 | Восстановление после потери сети | ⬜ |
| C6.3 | PSRAM-аллокация больших буферов; лог min free heap | `delay_line.h`, `jitter_buffer.h`, diagnostics | §16.2 | Буферы в PSRAM, метрики в `/api/diagnostics` | ⬜ |
| C6.4 | Скрипты прошивки → S3 env; перенос `web_server.h` в `common/web` | `scripts/flash_*.sh`, `platformio.ini` | T16, T20 | `flash_master.sh` прошивает `master_s3_wifi` | ⬜ |
| C6.5 | Чистка: мёртвый код (T12–T15), лишние `-D` (T13), лишние `lib_deps` (T22) | `firmware/`, `platformio.ini` | T12–T15, T22 | Сборка без предупреждений, flash < 50% | ⬜ |
| C6.6 | Документация: `README`, `architecture.md`, `hardware.md` | `docs/`, `README.md` | T20 | Документация = фактическое состояние | ⬜ |
| C6.7 | `audio-tools` в `master_s3` (не используется) | `platformio.ini:121` | T12 | Убрать или использовать | ⬜ |

### Сводный график (старт 11.08.2026)

| Неделя | Даты | Этап | Ключевой результат |
|---|---|---|---|
| 0 | 11.08 | Решения C0.1–C0.3 | Нет конфликтов ТЗ, судьба legacy env определена |
| 1 | 11–16.08 | **Этап 1** | Звук со смартфона через UDP → сабвуфер (критерий §18 Этап 2) |
| 2 | 18–23.08 | **Этап 2** | Кроссовер/громкости/задержки работают на мастере S3 |
| 3 | 25–29.08 | **Этап 3** | Сателлиты играют свои каналы синхронно |
| 4 | 01–05.09 | **Этап 4** | OLED + энкодер |
| 5 | 08–12.09 | **Этап 5** | Статический IP, session timeout, rate limit, лог-фильтры, CPU load, OTA-прогресс |
| 6 | 15–19.09 | **Этап 6** | Watchdog, авто-реконнект, PSRAM, чистка, документация, приёмка §19 |

Итого: ~6 недель при полной занятости. Этап 1 — критический путь; Этапы 5–6
не зависят от аудиоядра и могут идти параллельно.

### Рекомендуемая последовательность

1. **Этап 1** (C1.1 → C1.2 → C1.4): парсер UDP-пакета + I2S — перевод проекта из «каркаса» в работающую систему.
2. Параллельно (не ждёт аудио): **C5.2, C5.3, C5.4** — session timeout, rate limit, лог-буфер.
3. После Этапов 1–3 — **Этап 4** (OLED/энкодер) и **Этап 6** (надёжность + приёмка §19).

---

## 11. Выполнено (обновление 11.08.2026)

- **C1.1 — `udp_audio_packet.h` (пакет смартфон → мастер, §9.1/§9.2)** — реализовано:
  - `firmware/common/transport/udp_audio_packet.h` (header-only, без Arduino):
    `UdpAudioHeader` (18 байт, packed, little-endian), `kUdpMagic=0xA210`,
    `kUdpProtocolVersion=1`, флаги `kUdpFlagEndOfStream`/`kUdpFlagKeyframe`,
    `kUdpMaxPayload=1200` (MTU-safe, §9.2), `buildUdpPacket`/`parseUdpPacket`
    (валидация magic/version/длины payload).
  - Host-тест в `test/transport_packet_test.cpp`: сборка/разбор стерео
    48 кГц/16 бит, пустой payload (heartbeat), битый magic, короткий буфер,
    длина payload больше буфера.
  - Проверено: 4 host-бинаря зелёные (g++/MinGW). `make -C test test` на
    Windows требует sh — вручную бинарники прогнаны; `make test` на Linux/macOS
    будет зелёным.

- **C1.2 — `udp_audio_receiver.h` (sequence, concealment, ramp-out, standby, §9.3)** — реализовано:
  - `firmware/common/audio/udp_audio_receiver.h` (header-only, без Arduino):
    `StreamState` (Active/Conceal/RampOut/Standby), пороги §9.3
    (50 мс / 200 мс / 3 с), `feed(seq, ts, pcm, n, nowMs)` с детекцией
    пропусков по sequence (включая wrap 2^32), `tick(nowMs)` — эскалация
    по времени от последнего валидного пакета, `concealGain()` — плавное
    затухание 50→200 мс, счётчики `packetsRx`/`packetsLost`.
  - Host-тест `test/udp_audio_receiver_test.cpp`: последовательность 0,1,3 →
    conceal; потеря 100 мс → conceal с gain ≈ 1/3; 210 мс → ramp to mute;
    3.2 с → standby; восстановление после standby; дубликаты/переупорядочивание
    не считаются потерями; wrap sequence; добавлен в `test/Makefile`.

- **C1.4 — `i2s_output.h` (I2S-выход, общий для мастера и сателлитов)** — реализовано:
  - `firmware/common/audio/i2s_output.h` (header-only, guard `ESP32 && ARDUINO`):
    автовыбор API по `ESP_ARDUINO_VERSION_MAJOR` — `driver/i2s_std.h` (IDF 5.x,
    master_s3, pioarduino core 3.3.11) или legacy `driver/i2s.h` (core 2.0.17,
    сателлиты); `I2sOutputPins {bck, ws, data}` + `init(pins, sampleRate, mono)`
    (моно дублирует сэмпл в оба канала L=R), `write(samples, n)`, `writeMono`,
    `writeStereo`, `silence`; DMA-буферы 8×256, `tx_desc_auto_clear`.
  - `master_s3/src/main.cpp`: инициализация I2S в setup(), статус `i2s: on/off`,
    serial-команда `tone <freq>` (синус 2 с, генерируется в loop() через
    `toneTick()`, не блокирует Wi-Fi/Web UI) — проверка PCM5102A без смартфона.
  - `satellite/src/main.cpp`: локальный I2S-код (`i2s_driver_install` + `i2s_write`)
    заменён на общую обёртку, поведение сохранено (моно, L=R).
  - Синтаксис обеих веток проверен на хосте (g++ с заглушками `driver/i2s.h` и
    `driver/i2s_std.h`); host-тесты зелёные. Полная сборка — CI. Критерий «тон на
    сабвуфере» — ручная проверка на железе.

- **C1.3/C1.5 — аудио-путь мастера UDP → jitter (PSRAM) → I2S** — реализовано:
  - `master_s3/src/main.cpp`: loop() вместо «счётчика байт» разбирает UDP-пакет
    (`parseUdpPacket`, §9.1), стерео PCM усредняется в моно (сабвуфер),
    `UdpAudioReceiver::feed()` детектит потери/state, валидные пакеты кладутся в
    `JitterBuffer` из `ps_malloc` (ёмкость 60 мс/48 кГц, target 30 мс, §7.6),
    `audioOutTick()` вычитывает в I2S (тишина до накопления целевого уровня —
    плавный старт без щелчков); `g_audioActive` — статус потока для Web UI.
  - Сборка: `master_s3_wifi` (pioarduino core 3.3.11) — SUCCESS (RAM 18%,
    Flash 17.2%); `satellite_s3_left` (core 2.0.17) — SUCCESS. Host-тесты не
    затронуты. Критерий «звук со смартфона через PCM5102A» — ручная проверка.

- **C2.1 — `PcmPipeline` подключён к мастеру (DSP: volume → tone → limiter → LR4)** — реализовано:
  - `master_s3/src/main.cpp`: глобальный `g_pipeline`, в setup — `configure(sampleRate)`
    + `setVolume/setMute/setCrossoverHz` из конфига; в loop() аудио-путь стал
    `UDP → feed() → DSP → sub → DelayLine → jitter → I2S`: для каждой стереопары
    `process(l, r)` → `out.sub` (моно-микс → LPF), умножение на `concealGain()`
    (плавное затухание при потерях §9.3), float→int16, задержка сабвуфера.
  - left/right (HPF) пока не используются — TX на сателлиты (Этап 3).
  - Команда `status` выводит `dsp:` (volume/mute/crossover) и `delays:`.

- **C2.3 — Delay lines L/R/Sub в PSRAM** — реализовано:
  - `delay_line.h`: добавлен необязательный `externalBuffer` (DelayLine не владеет
    внешним буфером и не освобождает его) — обратная совместимость сохранена
    (host-тест `DelayLine dl(200, sampleRate)` не изменён).
  - `master_s3/src/main.cpp`: `createDelayLinePsram()` — `ps_malloc` буфера
    (ёмкость `kMaxDelayMs` = 200 мс), объект `new DelayLine(...)`; задержки из
    конфига (`delayLeftMs/RightMs/SubMs`); при нехватке PSRAM — nullptr
    (задержка канала не применяется, web-хендлер работает только с конфигом).

- **C2.4 — аудио-хендлеры Web UI подключены к живому pipeline** — реализовано:
  - `master_s3/src/main.cpp`: в `MasterWebServer` переданы `&g_pipeline` и
    `&g_delayLeft/Right/Sub` (вместо nullptr) — `/api/volume`, `/api/mute`,
    `/api/crossover`, `/api/delay` теперь применяют настройки к реальным
    объектам DSP, а не только к `NodeConfig` (слайдеры реально меняют звук).

- **Кодировка (дефект двойной перекодировки)** — исправлено:
  - `master_s3/src/main.cpp` и `web_server.h`: устранён остаточный мусор
    UTF-8→CP1251 («вЂ”» → «—» и т.п.); весь проект проверен `rg` — чисто.

- Сборка после Этапа 2: `master_s3_wifi` — SUCCESS (RAM 18.1%, Flash 17.2%);
  `satellite_s3_left/right` — SUCCESS; host-тесты 5/5 зелёные.
  Критерий «сабвуфер играет только НЧ» — ручная проверка на железе.
