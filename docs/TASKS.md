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
| T1 | **CI не запускается**: на GitHub 0 runs при активном workflow и включённых Actions. Вероятно, Actions не активированы в настройках репозитория (Settings → Actions → Enable). В `ci.yml` добавлен `workflow_dispatch` | `.github/workflows/ci.yml` | Высокий | ✅ (workflow_dispatch добавлен, Actions активированы) |

---

## 2. Техдолг и мелкие правки

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| T2 | Docstring `generate_config.py` упоминает флаг `--out`, фактический интерфейс — позиционный аргумент. Привести справку/docstring в соответствие с реальным CLI | `scripts/generate_config.py` | Низкий | ✅ |
| T3 | Расхождение со спецификацией §6.4: REST использует POST вместо PUT для volume/crossover/delay. Решить: привести к PUT или зафиксировать POST в документации | `firmware/common/web/web_server.h` | Низкий | ✅ (приведено к PUT) |
| T4 | `/api/status` отдаёт `left_online`/`right_online` (bool), спецификация §6.4 ожидает `satellites: {left, right}` со значениями «online»/«offline». Привести формат ответа к спецификации (обратная совместимость не нужна — MVP) | `firmware/common/web/web_server.h` | Низкий | ✅ |
| T5 | UDP-транспорт: мастер шлёт broadcast, спецификация §5.6 предполагает UDP unicast по MAC/IP сателлитов. Нужен discovery (сателлиты отвечают на broadcast-запрос, мастер запоминает IP) или явная настройка IP в конфиге | `firmware/common/transport/udp_transport.h`, `firmware/master/src/main.cpp` | Средний | ✅ (discovery: request/response, unicast после запоминания IP) |
| T6 | `config.example.h` — ручная конфигурация; проверить, что она не расходится с `generated_config.h` (макросы дублируются в двух источниках) | `config.example.h`, `firmware/common/generated/generated_config.h` | Низкий | ✅ (23 макроса совпадают, обновлён комментарий) |

---

## 2.1 Баг-лист по итогам ревизии кода (10.08.2026)

| ID | Проблема | Место | Приоритет | Статус |
|---|---|---|---|---|
| B1 | `g_leftOnline`/`g_rightOnline` нигде не выставляются в true: статус сателлитов в `/api/status` и serial всегда «offline». Нет ESP-NOW sent-callback и UDP-сердцебиений | `firmware/master/src/main.cpp:47` | Высокий | ✅ (ESP-NOW sent-callback по MAC + UDP discovery каждые 3 с + timeout 5 с) |
| B2 | Конфликт пинов в `config.example.h`: `AUDIO_I2S_DATA_OUT=22` и `AUDIO_OLED_SCL=22` (актуально при реализации F12) | `config.example.h` | Средний | ✅ (OLED SCL → 18 — валидный пин S3, 23 невалиден; см. B17) |
| B3 | `UdpTransport::broadcast()` считает broadcast-IP как `~localIP` + `bc[3]=255` — корректен только для /24-подсети | `firmware/common/transport/udp_transport.h:53` | Низкий | ✅ (broadcast по маске подсети, `broadcast_ip.h`) |
| B4 | `handleVolume` без поля `volume` в JSON молча ставит громкость 0 вместо ошибки | `firmware/common/web/web_server.h:528` | Низкий | ✅ (ошибка `missing volume|mute`) |
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
| F12 | OLED-меню (SSD1306, U8g2) + энкодер (KY-040): громкость, кроссовер, задержки, статус сателлитов | `firmware/common/ui/display.h`, `encoder.h` | Средний | ✅ (14.09.2026: OLED SSD1306 подключён — `Display g_display(AUDIO_OLED_SDA, AUDIO_OLED_SCL)`, `U8g2lib` добавлен в lib_deps, `g_display.begin()` + `showText()` в setup(); пины SDA=GPIO21, SCL=GPIO18; энкодер — отдельная задача) |
| F13 | Wi-Fi UDP источник (мастер принимает поток по UDP вместо A2DP). ESP32-S3 не поддерживает A2DP — Wi-Fi UDP PCM (смартфон → мастер) становится единственным источником | `firmware/master_s3/src/main.cpp` | Средний | ⬜ |
| F14 | Синхронизация воспроизведения по `timestampMs` в пакете (компенсация дрейфа часов) | `firmware/common/transport/audio_packet.h`, `firmware/satellite/src/main.cpp` | Средний | ⬜ |
| F15 | Защита от щелчков при включении/остановке: fade-in/out на мастере и сателлитах | `firmware/common/audio/volume_control.h` | Средний | ✅ (fade-in/out в `VolumeControl` применён: мастер-конвейер (C2.1) и сателлит (C3.5); полная схема «разное время старта каналов» — F18) |
| F16 | mDNS: доступ к Web UI по `http://audio-master.local` | `firmware/master_s3/src/main.cpp` | Низкий | ✅ |
| F17 | Документация: `docs/architecture.md`, `docs/hardware.md`, `docs/wiring.md` | `docs/` | Низкий | ✅ |
| F18 | ESP32-S3: покомпонентные громкости (master/left/right/sub) + fade-in/out, разное время старта каналов (ТЗ §8.2) | `firmware/common/audio/volume_control.h` | Средний | ✅ (громкости L/R/Sub — C2.2; fade-in/out — C3.5/F15; «разное время старта каналов» — остаётся ручная проверка/доработка) |
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
| T11 | 🔴 | **Невоспроизводимая сборка**: git-зависимости `arduino-audio-tools` и `ESP32-A2DP` без фиксации SHA/тега. Зафиксировать ревизии или удалить неиспользуемые | `platformio.ini:68-69,121` | ✅ (12.08.2026: SHA зафиксированы — `audio-tools#4ba48c9d`, `ESP32-A2DP#3245602`; `audio-tools` удалён из `master_s3_wifi` — C6.7) |

### 9.2 Баг-лист аудита

| ID | Крит. | Проблема | Место | Статус |
|---|---|---|---|---|
| B13 | 🟠 | `JitterBuffer`: дефолт `m_targetLevel = 0` → `ready()` всегда true без конфигурации (щелчки/прерывания). Задать дефолт или требовать конфигурацию + тест | `firmware/common/audio/jitter_buffer.h` | ✅ (12.08.2026: дефолт target = 50% ёмкости в конструкторе + host-тест `test_jitter_buffer_default_target`) |
| B14 | 🟠 | Сателлит пишет в неинициализированный I2S при провале `initI2S` (риск зависания `i2s_write`). Нужен guard «I2S готов» | `firmware/satellite/src/main.cpp` | ✅ (12.08.2026: `g_i2sReady` + guard в `writeSample`) |
| B15 | 🟡 | Лимитер стоит ДО volume — нет финальной защиты от клиппинга на выходе; перенести лимитер после volume | `firmware/common/audio/pcm_pipeline.h` | ✅ (12.08.2026: порядок tone → volume → limiter → crossover) |
| B16 | 🟡 | Контракт A2DP-входа (S16_LE stereo, `samples` = стерео-фреймы) не зафиксирован в коде | `firmware/master/src/main.cpp` | ⬜ (legacy `master` — см. C0.2) |
| B17 | 🟠 | **Дефолтные I2S-пины недействительны на ESP32-S3**: `SOC_GPIO_VALID_GPIO_MASK` исключает GPIO22–25 (внутренние SPI-флеша), а `AUDIO_I2S_WS=25`/`AUDIO_I2S_DATA_OUT=22` (и 26/25/22 в `docs/wiring.md`, `docs/hardware.md`) → `initI2S` на сателлите S3 падает с `i2s_set_pin: ws_io_num invalid` (проверено на железе, COM4). Требуются валидные S3-пины + синхронизация дока | `config.env`, `firmware/common/config/node_config.h:88-96`, `docs/wiring.md`, `docs/hardware.md` | ✅ (пины I2S → **4/5/6** (BCK/WS/DATA), OLED SCL 22→**18** (23 тоже невалиден на S3); обновлены `config.env`, `config.example.env`, `config.example.h`, `node_config.h`, `docs/wiring.md`, `docs/hardware.md`; `generated_config.h` перегенерирован; сателлит перепрошит на COM4 — boot-лог: `I2S ready`, `ESP-NOW ready`) |
| B18 | 🟠 | **Согласование RF-канала сателлита**: сателлит (ESP-NOW STA без ассоциации) по умолчанию слушает канал 1, мастер AP — канал 6 → пакеты не доходят. Исправлено хардкодом `esp_wifi_set_channel(6)` в `firmware/satellite/src/main.cpp` (**проверено на железе**: `wifi_channel: 6`, heartbeat принимается мастером). **Связь «без аудио-пакетов»**: мастер шлёт discovery-запрос broadcast каждые 2 с (`kFlagDiscoveryRequest`), сателлит запоминает MAC мастера (`addPeer` + unicast) и шлёт heartbeat (`kFlagDiscoveryResponse`) каждые 2 с; мастер помечает канал online по приёму heartbeat, таймаут 6 с → offline. Причина неработающего broadcast: `esp_now_send` возвращает `ESP_ERR_ESPNOW_NOT_FOUND` без зарегистрированного пира `FF:FF:FF:FF:FF:FF` — добавлен broadcast-пир в `EspNowTransport::begin()` (иначе и heartbeat, и discovery не уходили). Проверено на железе: `satellites.left=online` без аудио-потока; после удержания сателлита в reset 9 с → `left=offline`, после перезагрузки → `online`. Остаётся: автоопределение канала мастера в APSTA (канал AP следует за домашней сетью, F21) — макрос `AUDIO_ESPNOW_CHANNEL` уже добавлен в `config.env` | `firmware/common/transport/espnow.h`, `firmware/satellite/src/main.cpp`, `firmware/master_s3/src/main.cpp`, `config.env` | ✅ (см. выше; остаток — автоопределение канала в APSTA) |

### 9.3 Техдолг аудита (рефакторинг)

| ID | Крит. | Задача | Место | Статус |
|---|---|---|---|---|
| T12 | 🟠 | Удалить мёртвый код: `master_config.h`, `satellite_config.h` (`MasterPins`/`SatellitePins`/`masterPins`/`satellitePins`/`masterI2SDataOut` не используются), неиспользуемые `ui/display.h`/`ui/encoder.h` (без U8g2 в lib_deps), `audio-tools` из lib_deps `master_s3_wifi` (не используется) | `firmware/master/include`, `firmware/satellite/include`, `firmware/common/ui`, `platformio.ini` | ✅ (12.08.2026: удалены `master_config.h`/`satellite_config.h` + include из main.cpp; `audio-tools` убран из S3 (C6.7); `ui/display.h`/`ui/encoder.h` СОХРАНЕНЫ — нужны для F12/Этапа 4) |
| T13 | 🟠 | Удалить мёртвые `build_flags -DAUDIO_NODE_ROLE=MASTER/-DAUDIO_SOURCE_MODE=A2DP/-DAUDIO_TRANSPORT_MODE=ESPNOW/-DAUDIO_NODE_ROLE=SATELLITE` (код читает `*_MASTER`/`*_A2DP` из generated_config.h). Единственный источник — `config.env` | `platformio.ini:59-61,78,87` | ✅ (12.08.2026: удалены; код читает `AUDIO_NODE_ROLE_MASTER` и т.п. из generated_config.h) |
| T14 | 🟠 | Дублирование I2S-обвязки (`initI2S`/`writeSample` vs `initI2SSub`/`writeSubSample`) — вынести в `firmware/common/audio/i2s_output.h` с guard-ами core 2.x/3.x | `master/src/main.cpp`, `satellite/src/main.cpp` | ✅ (закрыто через C1.4: `common/audio/i2s_output.h`, сателлит и master_s3 переведены на обёртку) |
| T15 | 🟠 | Ввести транспорт-интерфейс (`ITransport` с `sendTo/sendToChannel/broadcast`): убрать ветвления `if (transport == EspNow/Udp)` из `flushSatelliteBatches`/setup/loop (OCP) | `firmware/common/transport`, `master/src/main.cpp`, `satellite/src/main.cpp` | ✅ (12.09.2026: `ITransport* g_transport` в `master_s3/src/main.cpp`; `sendAudioToSatellite()` — `g_transport->sendToChannel()`, `sendDiscoveryRequest()` — `g_transport->broadcast()`, `loop()` — `g_transport->update()`; ветвление `if (g_cfg.transport == TransportMode::Udp)` убрано из setup/loop) |
| T16 | 🟠 | `web_server.h` лежит в `master/include`, используется `master_s3` — перенести в `firmware/common/web/`; разбить на web-core + опциональные audio-хендлеры; сгруппировать 9 параметров конструктора в `struct AudioContext` (ISP) | `firmware/common/web/web_server.h`, `platformio.ini` | ✅ (13.08.2026: файл перенесён; include-пути покрываются существующими `-I firmware/common/web` в build_flags; декомпозиция SPA/класса — отдельная задача A1. 14.09.2026: убраны stale `-I firmware/master/include` и `-I firmware/satellite/include` из `platformio.ini` — каталоги пусты) |
| T17 | 🟡 | Serial-консоль дублируется в 3 main.cpp — выделить общий парсер команд в `firmware/common/console.h` | `firmware/*/src/main.cpp` | ✅ (12.09.2026: `common/console.h` создан, `MasterS3Console`/`LegacyMasterConsole`/`SatelliteConsole` наследуют `audio21::Console`; `#include "console.h"` → `#include "common/console.h"` во всех main.cpp; конфликт с Arduino `Console` библиотекой решен квалификацией `audio21::Console`) |
| T18 | 🟡 | CI: зафиксировать версию PlatformIO (`pipx install platformio==6.1.19`), добавить кэширование `~/.platformio` и `.pio-core-master` | `.github/workflows/ci.yml` | ✅ (12.08.2026: версия зафиксирована, кэш пакетов + core-каталога) |
| T19 | 🟢 | Миграция сателлитов на ESP-IDF (left + right, `EXTRA_COMPONENT_DIRS`, `platformio.master.ini` удалён) | `platformio.ini`, `platformio.master.ini`, `satellite_s3_idf_left/`, `satellite_s3_idf_right/` | ✅ |
| T20 | 🟡 | Синхронизировать документацию: `architecture.md` §9 («старые env в истории» — неверно, они в файле), §10 (mDNS реализован — F16), структура §8 без `master_s3`; `flash_master.sh`/`README.md` ведут на `master_a2dp` вместо `master_s3_wifi` (`hardware.md`/`wiring.md` уже синхронизированы — B17) | `docs/`, `scripts/flash_master.sh`, `README.md` | ✅ (12.08.2026: architecture.md §9/§10, README (структура, env, REST, требования), flash-скрипты → S3 env) |
| T21 | 🟡 | `test/Arduino.h` перекрывает системное имя — вынести заглушки в `test/stubs/` с явным `-I` | `test/` | ✅ (12.08.2026: `test/stubs/`, Makefile `-Istubs`) |
| T22 | 🟢 | Убрать `WebServer`/`Preferences` из `lib_deps` (встроены в фреймворк, PIO их не тянет отдельно — запись избыточна) | `platformio.ini:37,66,116` | ⬜ (не применимо: на pioarduino core 3.x `Preferences`/`WebServer`/`HTTPClient`/`Update`/`Network` НЕ встроены в фреймворк — сборка падает без них; для espressif32@6.9.0 встроены, но запись безвредна) |

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
> Легенда приоритетов: 🔴 критично (блокирует звук/приёмку), 🟠 высоко,
> 🟡 средне, 🟢 низко (чистка). Формат: `C<этап>.<задача>` — расхождения,
> связка с B/F/T-номерами указана в колонке «Связь».
>
> **Подробные технические карточки каждой задачи (проблема, план, код-скетчи,
> файлы, критерии, тесты, оценки) — в [`TASKS_DETAILED.md`](TASKS_DETAILED.md).**

### Этап 0. Решения по конфликтам ТЗ (полдня)

| ID | Расхождение | Действие | Приоритет | Статус |
|---|---|---|---|---|
| C0.1 | SSID режима настройки: ТЗ.md §6.3 `Audio21-Master` vs ТЗ_Веб §3.2 `Audio21-Setup` | Согласовать: оставить `Audio21-Master` (базовое ТЗ) и зафиксировать в ТЗ_Веб, либо ввести `AUDIO_WIFI_SETUP_SSID` для режима настройки | 🟠 | ✅ (12.08.2026: решение — оставить `Audio21-Master` (базовое ТЗ §6.3); ТЗ_Веб §3.2 считается устаревшим; в коде/конфиге единый SSID) |
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
| C2.2 | `sub_volume` + `left_volume`/`right_volume` (мин. набор §7.5) | `NodeConfig`, `web_server.h`, SPA | F18 | В `/api/status` и UI — 4 громкости | ✅ (12.08.2026: `NodeConfig` v4 (`leftVolume`/`rightVolume`/`subVolume`), `PcmPipeline::setChannelVolumes()` (после кроссовера), `PUT /api/volume` с `channel`=master|left|right|sub, слайдеры в SPA, NVS-миграция v3→v4; сателлит берёт громкость своей стороны) |
| C2.3 | Delay lines L/R/Sub на мастере S3 (0–200 мс, NVS) | `master_s3/src/main.cpp` | §7.4 | Задержки применяются без ребута | ✅ (12.08.2026: `DelayLine` L/R/Sub в PSRAM (ёмкость `kMaxDelayMs`=200 мс), задержки из конфига; в `delay_line.h` добавлен внешний буфер; см. «Выполнено») |
| C2.4 | Подключить аудио-хендлеры Web UI к реальному pipeline (сейчас — только конфиг) | `web_server.h` | §16 | Слайдеры реально меняют звук | ✅ (12.08.2026: `&g_pipeline` + `&g_delayLeft/Right/Sub` переданы в `MasterWebServer` — `/api/volume`, `/api/mute`, `/api/crossover`, `/api/delay` применяются к живым объектам; см. «Выполнено») |

### Этап 3. 🔴 Аудио-TX на сателлиты + 🟠 синхронизация (ТЗ §18, Этап 4–5) — ~1.5 недели

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C3.1 | Батчевый ESP-NOW/UDP TX аудио с мастера S3 (сейчас только heartbeat) | `master_s3/src/main.cpp` | F13 | Л/П сателлиты играют свои каналы (**критерий §18 Этап 4**) | ✅ (12.08.2026: батч 117 семплов → пакет 234 Б, `sendAudioToSatellite()` — ESP-NOW unicast по MAC / UDP unicast по IP (fallback broadcast); пиры сателлитов в `initEspNow()`, UDP-discovery в loop, `tx_packets` в status. Сборка SUCCESS. Критерий «сателлиты играют» — ручная проверка) |
| C3.2 | `render_timestamp` в пакетах (§10.1) + clock recovery на мастере | `audio_packet.h`, `master_s3/src/main.cpp` | F14, F19, §11 | Метки времени заполняются и используются | ✅ (частично: `timestampMs = millis()` заполняется в каждом пакете (C3.1); clock recovery на сателлите (дрейф-коррекция) — остаётся C3.4) |
| C3.3 | Сателлит: ждать `JitterBuffer::ready()`; целевой уровень **20/40/80 мс** (сейчас 15/50 мс) | `satellite/src/main.cpp` | §10.3 | Нет щелчков на старте; буфер держит уровень | ✅ (12.08.2026: target 20 мс; на старте ждём `ready()` (флаг `g_streaming`), при истощении буфера — снова ждём накопления) |
| C3.4 | Дрейф-коррекция на сателлите | `satellite/src/main.cpp`, `common/audio/drift_correction.h` | F14, §10.3 | Нет накопления/истощения буфера за 30 мин | ✅ (11.08.2026: медианный фильтр 8 отсчётов по `timestampMs`, подстройка `targetMs` 15–50 мс; host-тест `test/drift_correction_test.cpp` PASS) |
| C3.5 | `volume_control` на сателлите (§8.6) + fade-in/out | `satellite/src/main.cpp` | F15, F18 | Громкость сателлита регулируется, без щелчков | ✅ (12.08.2026: `VolumeControl` на сателлите, громкость из `leftVolume`/`rightVolume` по стороне, плавный fade-in/out через `process(float)`) |

### Этап 4. 🟠 OLED + энкодер (ТЗ §12.1–12.2, Этап 6) — ~1 неделя

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C4.1 | OLED SSD1306 + U8g2 (`display_ui`): громкость, источник, статусы, кроссовер, задержки | `common/ui/display.h`, `lib_deps` | F12, §12.2 | §12.2 отображается | ✅ (14.09.2026: OLED подключён, `g_display.begin()` + `showText()` в setup; полный статусный экран с громкостью/кроссовером/задержками — оставшийся функционал) |
| C4.2 | Энкодер KY-040 (`encoder_ui`): короткое/длинное нажатие, меню | `common/ui/encoder.h`, `master_s3/src/main.cpp` | F12, §12.1 | Функции §12.1 работают | ✅ (19.11.2026: энкодер инициализирован в setup(), `handleEncoder()` в loop() — навигация вращением, редактирование кнопкой, авто-выход в STATUS через 10 с) |
| C4.3 | Подключить в `master_s3` (в `master_a2dp` опционально) | `master_s3/src/main.cpp` | §12 | Управление с дисплея работает | ✅ (19.11.2026: `g_encoder` + `g_display` в `master_s3/src/main.cpp`, меню STATUS/VOLUME/CROSSOVER/DELAY L/R/SUB/SAT L/R, изменения применяются к живым объектам DSP) |

### Этап 5. 🟠 Веб-доработки по ТЗ_Веб — ~1 неделя

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C5.1 | Применение статического IP из профиля (`WiFi.config`) | `master_s3/src/main.cpp` | ТЗ_Веб §6.3, §21.2 | Профиль со static IP получает заданный адрес | ✅ (12.08.2026: `applyStaticIpFromProfile()` перед `WiFi.begin()` в STA/APSTA) |
| C5.2 | Session timeout (ввести `m_sessionStartMs`, проверять в `isAuthed`/`handleClient`) | `web_server.h` | ТЗ_Веб §11.4, §23.1 | Сессия гаснет через 3600 с | ✅ (12.08.2026: `kSessionTimeoutMs=3600 с`, гашение в `handleClient()`) |
| C5.3 | Rate limit для `/api/login` и scan | `web_server.h` | ТЗ_Веб §23.1 | Блокировка после N неудач | ✅ (12.08.2026: 5 неудач → блокировка 60 с (429); живой скан не чаще 5 с) |
| C5.4 | Лог-буфер 16–64 KB + фильтры `level`/`module` в `/api/logs` | `logs.h`, `web_server.h` | ТЗ_Веб §13.4, §17.5 | §13.4, §17.5 | ✅ (12.08.2026: буфер 96×192 Б ≈ 18 КБ; `/api/logs?level=&module=` — фильтрация по severity и категории) |
| C5.5 | `cpu_load_percent` в `/api/status`; время/NTP/SSID/RSSI/MAC на Dashboard | `web_server.h`, SPA | ТЗ_Веб §5.2, §24.1 | §5.2, §24.1 | ✅ (12.08.2026: замер занятости loop() в main.cpp → `/api/status` и `/api/diagnostics`; Dashboard: время (NTP), SSID, RSSI, MAC, CPU load) |
| C5.6 | Прогресс-бар OTA | SPA | ТЗ_Веб §12.3–12.4 | Прогресс виден | ✅ (12.08.2026: XHR `upload.onprogress` — прогресс отправки файла в % + полоса `#uBar`; серверный прогресс невозможен — `HTTPUpload` не даёт общий размер тела, `Update.progress()` не работает при `UPDATE_SIZE_UNKNOWN`) |
| C5.7 | Ограничение доступа локальной подсетью (§23.2); MAC-фильтр/проверка источника UDP (§17) | `web_server.h`, `udp_audio_receiver` | ТЗ_Веб §23.2, ТЗ §17 | Неавторизованный/внешний доступ блокируется | ✅ (Web UI — только из подсети STA/AP (`clientIsLocal` в `isAuthed`); UDP-аудио — HMAC-SHA256, магия 0xA211, ключ `AUDIO_UDP_AUTH_KEY` (по умолчанию отключено); неверный HMAC → пакет отбрасывается) |

### Этап 6. 🟡 Надёжность и чистка — ~1 неделя

| ID | Задача | Место | Связь | Критерий готовности | Статус |
|---|---|---|---|---|---|
| C6.1 | Watchdog для основных задач | `master_s3/src/main.cpp`, `satellite/src/main.cpp` | §16.3 | Перезапуск задачи при зависании | ✅ (12.08.2026: `esp_task_wdt` (IDF 5.x API) для loop мастера, таймаут 30 с, panic; сброс в loop) |
| C6.2 | Авто-переподключение Wi-Fi при обрыве в рантайме | `master_s3/src/main.cpp` | §16.3, ТЗ_Веб §21 | Восстановление после потери сети | ✅ (12.08.2026: потеря STA → `WiFi.begin` + существующий fallback на setup AP) |
| C6.3 | PSRAM-аллокация больших буферов; лог min free heap | `delay_line.h`, `jitter_buffer.h`, diagnostics | §16.2 | Буферы в PSRAM, метрики в `/api/diagnostics` | ✅ (PSRAM-аллокация — C1.3/C2.3; `/api/diagnostics`: heap/psram free+min free, cpu_load, uptime) |
| C6.4 | Скрипты прошивки → S3 env; перенос `web_server.h` в `common/web` | `scripts/flash_*.sh`, `platformio.ini` | T16, T20 | `flash_master.sh` прошивает `master_s3_wifi` | ✅ (частично: скрипты → S3 env; перенос `web_server.h` в `common/web` — не делался, T16 остаётся) |
| C6.5 | Чистка: мёртвый код (T12–T15), лишние `-D` (T13), лишние `lib_deps` (T22) | `firmware/`, `platformio.ini` | T12–T15, T22 | Сборка без предупреждений, flash < 50% | ✅ (частично: T12/T13/T14 закрыты, T22 — не применимо для pioarduino; T15 (ITransport) остаётся) |
| C6.6 | Документация: `README`, `architecture.md`, `hardware.md` | `docs/`, `README.md` | T20 | Документация = фактическое состояние | ✅ (12.08.2026: README + architecture.md синхронизированы; hardware/wiring — ранее B17; 19.11.2026: обновлены C5.7 HMAC-аутентификация UDP, C4.x OLED-меню + энкодер, C3.4 дрейф-коррекция, формат пакета 0xA210/0xA211) |
| C6.7 | `audio-tools` в `master_s3` (не используется) | `platformio.ini:121` | T12 | Убрать или использовать | ✅ (12.08.2026: удалён из `master_s3_wifi`) |

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

## 12. Выполнено (обновление 12.08.2026, вечер) — «закрыть всё, что можно без железа»

- **C2.2 — покомпонентные громкости L/R/Sub (ТЗ §7.5)** — реализовано:
  - `node_config.h`: `leftVolume`/`rightVolume`/`subVolume` (0..100, дефолт 50),
    `clamp()` расширен.
  - `pcm_pipeline.h`: три `VolumeControl` (L/R/Sub) + `setChannelVolumes(left,right,sub)`
    — применяются ПОСЛЕ кроссовера (каждый канал независимо от master).
  - `web_server.h`: `PUT /api/volume` принимает `{"channel":"master|left|right|sub",
    "volume":N}`; `/api/status` отдаёт `audio.left_volume/right_volume/sub_volume`;
    в SPA — слайдеры Master/Left/Right/Sub.
  - `storage.h`: версия NVS **v3→v4** + миграция (новые поля = дефолты 50).
  - `master_s3/src/main.cpp`: `setChannelVolumes(...)` из конфига в setup.
  - Сателлит (C3.5): громкость своей стороны (`leftVolume`/`rightVolume`).

- **B13/B14/B15 — баги аудита закрыты**:
  - B13: `JitterBuffer` — дефолт `m_targetLevel = capacity/2` в конструкторе
    (`ready()` осмыслен без конфигурации) + host-тест `test_jitter_buffer_default_target`.
  - B14: сателлит — guard `g_i2sReady` (не писать в неинициализированный I2S).
  - B15: `PcmPipeline` — лимитер перенесён ПОСЛЕ volume (финальная защита от
    клиппинга): порядок tone → volume → limiter → crossover.

- **C3.3/C3.5 — сателлит**:
  - C3.3: целевой уровень jitter 20 мс (§10.3); на старте ждём `JitterBuffer::ready()`
    (флаг `g_streaming`), при истощении буфера — снова ждём накопления (без щелчков).
  - C3.5: `VolumeControl` на сателлите — громкость канала + плавный fade-in/out.

- **C5.1–C5.5 — веб-доработки (ТЗ_Веб)**:
  - C5.1: статический IP из профиля — `applyStaticIpFromProfile()` перед `WiFi.begin()`.
  - C5.2: session timeout 3600 с (`handleClient()` гасит сессию).
  - C5.3: rate limit — 5 неудачных логинов → блокировка 60 с (429); живой скан
    Wi-Fi не чаще 5 с (кеш B9 отдаётся без ограничения).
  - C5.4: лог-буфер 96×192 Б ≈ 18 КБ (ТЗ §13.4: 16–64 КБ); `/api/logs?level=&module=`
    — фильтрация по severity и категории.
  - C5.5: `cpu_load_percent` (замер занятости loop() в main.cpp) в `/api/status`
    и `/api/diagnostics`; Dashboard: время (NTP), SSID, RSSI, MAC, CPU load.

- **C6.1–C6.7 — надёжность и чистка**:
  - C6.1: watchdog loop мастера (`esp_task_wdt`, IDF 5.x API, таймаут 30 с, panic).
  - C6.2: авто-переподключение STA при обрыве в рантайме (fallback на setup AP).
  - C6.3: метрики heap/PSRAM (free+min free), cpu_load, uptime в `/api/diagnostics`;
    PSRAM-аллокация буферов — ранее (C1.3/C2.3).
  - C6.4: `flash_master.sh` → `master_s3_wifi` (через `platformio.master.ini`),
    `flash_satellite.sh` → `satellite_s3_left/right`.
  - C6.5/C6.7: удалены мёртвые `master_config.h`/`satellite_config.h` (T12),
    мёртвые `-D AUDIO_NODE_ROLE=...` (T13), `audio-tools` из S3 (C6.7);
    `ui/display.h`/`ui/encoder.h` сохранены (нужны для F12/Этапа 4).
  - C6.6/T20: README + architecture.md синхронизированы (env, структура, REST,
    ограничения); T21: заглушки Arduino → `test/stubs/`; T18: CI — версия PIO
    зафиксирована + кэш пакетов/core-каталога.

- **C0.1 — решение по SSID**: оставлен `Audio21-Master` (базовое ТЗ §6.3);
  ТЗ_Веб §3.2 (`Audio21-Setup`) считается устаревшим.

- **T22 — не применимо**: на pioarduino core 3.x `Preferences`/`WebServer`/
  `HTTPClient`/`Update`/`Network` НЕ встроены в фреймворк — сборка падает без
  них в `lib_deps` (проверено сборкой); для espressif32@6.9.0 встроены, но
  запись безвредна.

- **C3.1/C3.2 — TX аудио мастер→сателлиты (Этап 3, §10)** — реализовано:
  - `master_s3/src/main.cpp`: батч 117 семплов (≈2.4 мс @48 кГц) → пакет 234 Б
    (лимит ESP-NOW); left/right каналы из `PcmPipeline::process()` (HPF) с
    `concealGain()` накапливаются в `g_txLeft/g_txRight`, отправка по заполнению
    батча (`flushTxBatch()`); `sendAudioToSatellite()` — ESP-NOW unicast по MAC
    (`leftSatMac/rightSatMac`, пиры в `initEspNow()`) или UDP unicast по IP канала
    (`g_udpTx.sendToChannel`, fallback broadcast до завершения discovery);
    `timestampMs = millis()` в каждом пакете (C3.2, §10.1); UDP-discovery-ответы
    сателлитов обрабатываются в loop; `tx_packets` в serial-status.
  - Сборка: `master_s3_wifi` SUCCESS (RAM 22.0%, Flash 17.6%). Критерий
    «сателлиты играют свои каналы» — ручная проверка на железе (§18 Этап 4).

- **C5.6 — прогресс-бар OTA** — реализовано: XHR `upload.onprogress` (браузер
  знает размер файла) + полоса `#uBar` в SPA. Серверный прогресс невозможен:
  `HTTPUpload` не отдаёт общий размер тела, `Update.progress()` не работает при
  `UPDATE_SIZE_UNKNOWN`.

- **C5.7 — доступ только из локальной подсети** — реализовано: `clientIsLocal()`
  (подсеть STA или AP) в `isAuthed()` — Web UI недоступен извне (§23.2).
  MAC-фильтр/проверка источника UDP-аудио (§17) — остаётся.

- **T11 — SHA git-зависимостей legacy** — реализовано: `audio-tools#4ba48c9d`,
  `ESP32-A2DP#3245602` в `platformio.ini`; `master_a2dp` собран SUCCESS
  (audio-tools @ 1.2.5+sha.4ba48c9d4, ESP32-A2DP @ 1.8.11+sha.3245602).

- Сборка после закрытия: **все 6 env SUCCESS** — `master_s3_wifi` (RAM 22.0%,
  Flash 17.6%), `satellite_s3_left/right` (RAM 13.9%, Flash 22.9%),
  `master_a2dp` (RAM 20.7%, Flash 60.7%), `satellite_left/right` (RAM 14.0%,
  Flash 24.5%); host-тесты 5/5 зелёные (включая новый тест B13).

- Осталось (только железо/большой рефакторинг): B6/B7/B11/B12/B16 (legacy `master_a2dp` — стенд C0.2), B10 (IPv6 — осознанное ограничение), T19 (сателлиты на core 3.x), T22 (см. выше), F14 (синхронизация), C3.1/C3.2 — ручная проверка звука на железе (§18 Этап 4).

## 10. Новые задачи из веб-анализа (22.11.2026)

> Добавлено по итогам глубокого веб-анализа открытых решений (см. `docs/TECHNICAL_SURVEY.md`,
> `docs/INTEGRATION_RECOMMENDATIONS.md`, `docs/EXTERNAL_REFERENCES.md`).
> Формат: `V<номер>` — уязвимости безопасности, `N<номер>` — новые задачи интеграции.

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| V4 | **UDP-аудио без аутентификации источника** — добавить ключ `AUDIO_UDP_AUTH_KEY`, проверять HMAC-SHA256 для пакетов `0xA211`, отбрасывать `0xA210` если ключ настроен | `firmware/common/transport/udp_audio_packet.h`, `firmware/master_s3/src/main.cpp` | 🔴 Критично | ✅ (22.11.2026: ключ по умолчанию в `config.example.env`, `main.cpp` отбрасывает 0xA210 когда ключ настроен, HMAC-проверка в `udp_audio_packet.h`, хост-тест в `transport_packet_test.cpp`) |
| V5 | **OTA без подписи образа** — интегрировать проверку Ed25519 подписи перед `Update.end()`; открытый ключ `AUDIO_OTA_PUBLIC_KEY` в NVS v6; заголовок `X-OTA-Signature` обязателен если ключ настроен; CI подписывает бинарник закрытым ключом | `firmware/common/web/web_server.cpp`, `firmware/common/config/node_config.h`, `firmware/common/config/storage.h`, `.github/workflows/ci.yml` | 🔴 Критично | ✅ (22.11.2026: поле `otaPublicKey` в NodeConfig, миграция v5→v6 в storage.h (kVersion=6), отслеживание SHA-256 при загрузке, проверка Ed25519 через mbedTLS на ESP32, заголовок X-OTA-Signature, `AUDIO_OTA_PUBLIC_KEY` в config.example.env) |
| S-1 | **Дефолтный пароль AP `audio21master`** — при первом запуске блокировать Web UI до смены пароля (forced password change), флаг в NVS v5 | `firmware/common/web/web_server.h`, `firmware/common/config/storage.h`, `firmware/common/config/node_config.h` | 🔴 Критично | ✅ (22.11.2026: флаг `forcedPasswordChange` в NodeConfig (default=true), миграция v4→v5 в storage.h (kVersion=5), Web UI блокируется 403 до смены пароля, флаг сбрасывается после успешной смены через /api/wifi/ap_password, `/api/status` отдаёт `forced_password_change`) |
| N1 | **Активация GitHub Actions** — включить Actions в настройках репозитория (Settings → Actions → Enable), workflow уже написан с `workflow_dispatch` | `.github/workflows/ci.yml` | 🟠 Высоко | ✅ (22.11.2026: Actions уже включены на стороне GitHub, `enabled=true`, `allowed_actions=all`) |
| N2 | **Декомпозиция web_server.h** — вынести SPA в отдельные PROGMEM-файлы, разбить `web_server.cpp` на модули (`web_auth.cpp`, `web_api.cpp`, `web_ota.cpp`, `web_wifi.cpp`, `web_logs.cpp`), заменить `String` на `std::string`/`char[]` | `firmware/common/web/` | 🟠 Высоко | ✅ (22.11.2026: `web_server.cpp` разбит на `web_auth.cpp` (авторизация/сессии/CSRF), `web_api.cpp` (API эндпоинты: status, wifi, net, volume, mute, crossover, delay, transport, pair, save, system, config, logs, diagnostics), `web_ota.cpp` (OTA загрузка + проверка подписи Ed25519). `web_server.cpp` оставлен с базовыми методами (конструктор, begin, handleClient, bindRoutes, ответы, страницы). `web_server.h` без изменений — все объявления методов на месте. |
| N3 | **SSE endpoint для статуса сателлитов** — добавить `GET /api/events` (text/event-stream) вместо опроса `/api/status` каждые 2 с | `firmware/common/web/web_server.cpp`, SPA | 🟠 Высоко | ✅ (22.11.2026: `web_sse.cpp` с `handleSSE()` и `sseBroadcast()`, роут `/api/events` в `bindRoutes()`, глобальный `g_webServer` для вызова из `main.cpp`, интеграция в `master_s3/src/main.cpp` — события при изменении `g_leftOnline`/`g_rightOnline` (online и offline/таймаут)) |
| N4 | **HIL тестирование** — self-hosted runner с реальным ESP32-S3 для flashing + serial валидации | GitHub Actions self-hosted runner | 🟠 Высоко | ⬜ |
| N5 | **Preset-менеджмент** — сохранение/загрузка конфигураций DSP в NVS + экспорт/импорт JSON через REST API | `firmware/common/web/web_server.cpp`, `firmware/common/config/storage.h` | 🟡 Средне | ⬜ |
| N6 | **Dirty indicator на ползунках Web UI** — визуальная обратная связь о несохранённых изменениях | SPA (HTML/JS) | 🟡 Средне | ⬜ |
| N7 | **Закрепить версии зависимостей** — `ArduinoJson` точная версия (не `^7.4.3`), Monocypher для Ed25519 | `platformio.ini` | 🟡 Средне | ✅ (22.11.2026: ArduinoJson@7.4.3 закреплено, git-SHA для audio-tools/ESP32-A2DP закреплены (T11), комментарий с требованием закрепить Preferences/WebServer/HTTPClient/Update/Network/U8g2 добавлен в platformio.ini) |
| N8 | **mDNS проверка на железе** — F16 помечен ✅, но требует ручной проверки `http://audio-master.local` | `firmware/master_s3/src/main.cpp` | 🟢 Низко | ⬜ |
| N9 | **Десктопное приложение (Tauri 2)** — опционально, только при необходимости альтернативного UI | Вне репозитория | 🟢 Низко | ⬜ |

---

## 11. Выполнено (обновление 02.09.2026)

- **Прошивка ESP32-S3 (мастер = сабвуфер)** — `pio run -e master_s3_wifi -t upload --upload-port COM10` — **SUCCESS** (150 с):
  - COM10: ESP32-S3 (QFN56, rev v0.2), Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, PSRAM 2MB, MAC `20:6e:f1:d6:03:f0`.
  - Прошивка: bootloader → partitions → boot_app0 → firmware.bin (1 166 215 байт, Flash 34.9%).
  - Hard reset через RTS pin после прошивки.
  - **Баг с кодировкой**: вывод esptool падал с `UnicodeEncodeError: 'charmap' codec can't encode characters` (терминал cp1251). Решение: `cmd /c "set PYTHONUTF8=1& platformio.exe ..."` — запуск через `PYTHONUTF8=1` в cmd.exe.
- **Конфликт имён `Console` (Arduino `Console @ 3.3.11` vs `audio21::Console`)** — решён:
  - `#include "console.h"` → `#include "common/console.h"` во всех 3 main.cpp (мастер, legacy, сателлит).
  - `class X : public Console` → `class X : public audio21::Console`.
  - `using Console::Console` → `using audio21::Console::Console`.
  - Без этих исправлений сборка `master_s3_wifi` падала с переопределением `Console` библиотеки Arduino.
- **Исправлена коррупция `platformio.ini`**: строка `-DAUDIO_I2S_DATA_OUT=12` была повреждена (`12?`), восстановлена из git.
- **T17 (`console.h`)** закрыта: `firmware/common/console.h` с базовым классом `audio21::Console`, `MasterS3Console`/`LegacyMasterConsole`/`SatelliteConsole` наследуют `audio21::Console`, общие команды `save`/`reboot`/`status` в базовом классе.
- **T1 (CI)** закрыта: GitHub Actions активированы, `workflow_dispatch` добавлен.
- **T15 (ITransport)** закрыта: `ITransport* g_transport` в `master_s3/src/main.cpp`; `sendAudioToSatellite()` — `g_transport->sendToChannel()`, `sendDiscoveryRequest()` — `g_transport->broadcast()`, `loop()` — `g_transport->update()`; ветвление `if (g_cfg.transport == TransportMode::Udp)` убрано из setup/loop (OCP).
- **F12 (OLED)** закрыта: `olikraus/U8g2` добавлен в `lib_deps`; `Display g_display(AUDIO_OLED_SDA, AUDIO_OLED_SCL)` в `master_s3/src/main.cpp`; `g_display.begin()` + `showText()` в `setup()`; пины SDA=GPIO16, SCL=GPIO17.

## 12. Выполнено (обновление 22.11.2026) — реорганизация репозитория

- **Удаление мёртвого кода и артефактов**:
  - `config.example.h` — удалён (единственный источник конфига — `config.env` + `node_config.h`; T6/T13).
  - `docs/аудит.txt` — удалён (актуальные задачи — в `docs/TASKS.md`/`docs/REPO_AUDIT.md`).
  - `firmware/idf_component.yml` — удалён (не используется, проект на PlatformIO).
  - `firmware/master_s3/include/master_s3_config.h` — удалён; `kDefaultWifiChannel` перенесён в `firmware/common/config/node_config.h` (T12).
  - `test/*.bin`, `test/*.bin.d` — удалены (скомпилированные артефакты хост-тестов).

- **Рефакторинг `firmware/master_s3/src/main.cpp`**:
  - Убран `#include "master_s3_config.h"` (файл удалён).
  - Includes перегруппированы по категориям (system Arduino → ESP-IDF → FreeRTOS → C stdlib → проект).
  - Удалены неиспользуемые includes: `<esp_mac.h>`, `<esp_coexist.h>`, `<stdexcept>`, `<math.h>`.
  - Переименован `g_udpBuf` → `g_udpAudioBuffer` (4 вхождения) — имя было запутанным.

- **Линеринг**:
  - Создан `.clang-tidy` (проверки clang-analyzer, cppcoreguidelines, performance, portability, readability, bugprone, modernize с исключениями для embedded).
  - Добавлен шаг `clang-format --dry-run` в `.github/workflows/ci.yml`.
  - `.gitignore`: добавлен `.pio-core-master/` (изолированный core PlatformIO для `master_s3_wifi`).

- **Новые документы**:
  - `docs/CHANGES_REORG.md` — этот отчёт (все изменения + чек-лист предотвращения хаоса).
  - `docs/EXTERNAL_REFERENCES.md`, `docs/INTEGRATION_RECOMMENDATIONS.md`, `docs/TECHNICAL_SURVEY.md`, `docs/WORK_PLAN.md`.

- **Хост-тесты**: все 6 зелёные (`make test`), включая после переименования `g_udpAudioBuffer`.

- **Обновлённые ссылки**: `AGENTS.md`, `README.md`, `docs/PLAN.md`, `docs/architecture.md` — ссылки на удалённые файлы убраны.

- **Статус**: все изменения в working tree, не закоммичены. Следующий шаг — коммит + проверка сборки PlatformIO (pio run -c platformio.master.ini).

---

## 13. Задачи из комплексного аудита репозитория (22.11.2026)

> Добавлено по итогам глубокого аудита: README/LICENSE/CI-CD, архитектура, антипаттерны,
> ссылки, i18n. Формат: `C<категория>.<номер>` — категория задачи (безопасность, CI,
> архитектура, локализация, документация).

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| C2 | **Добавить файл LICENSE (GPL-3.0)** — репозиторий публичный, legacy `master_a2dp` линкуется с `arduino-audio-tools` (GPL-3.0). Без LICENSE репозиторий не соответствует лицензии зависимостей. | корень репозитория | 🔴 Критично | ✅ (23.11.2026: файл LICENSE с полным текстом GPL-3.0 добавлен в корень репозитория) |
| CI-2 | **Добавить `permissions: contents: read`** в `.github/workflows/ci.yml` (требование GitHub для workflows) | `.github/workflows/ci.yml` | 🟠 Высоко | ✅ (уже в коммите a945df3: строка 12 ci.yml) |
| CI-3 | **Добавить `timeout-minutes: 30`** в job ci.yml (защита от зависших runs) | `.github/workflows/ci.yml` | 🟠 Высоко | ✅ (уже в коммите a945df3: строка 23 ci.yml) |
| CI-4 | **Заменить плавающие теги actions (`@v4`, `@v5`) на SHA-пины** — воспроизводимая сборка CI | `.github/workflows/ci.yml` | 🟠 Высоко | ✅ (уже в коммите a945df3: checkout@11d5960..., setup-python@a26af69..., cache@0057852...) |
| CI-9 | **Включить vulnerability alerts + `.github/dependabot.yml`** — автоматическое обновление уязвимых зависимостей | `.github/` | 🟠 Высоко | ✅ (23.11.2026: создан `.github/dependabot.yml` с pip и github-actions ecosystem; vulnerability alerts включены через GitHub API) |
| M1 | **Ветвление + защита ветки `main`** — feature-ветки, PR с ревью, защита main от прямого пуша | GitHub репозиторий | 🟠 Высоко | ✅ (23.11.2026: защита ветки main через GitHub API — 1 approving review, dismiss_stale_reviews=true, enforce_admins=false) |
| AP-1 | **Заменить `String` на `char[]`/`std::string` в веб-путях** — предотвратить фрагментацию heap на ESP32-S3 | `firmware/common/web/web_server.h`, `web_api.cpp`, `web_server.cpp`, `master_s3/src/main.cpp` | 🟡 Средне | ✅ (23.11.2026: `WifiNetInfo::ssid` → `char ssid[33]`, `macToStr(String)` → `macToStr(char out[18])`, `logLineHasModule(const String&)` → `logLineHasModule(const char*)`, `info.ssid = WiFi.SSID(i)` → `strlcpy(info.ssid, WiFi.SSID(i).c_str(), sizeof(info.ssid))`) |
| AP-4 | **Дефолт `AUDIO_UDP_AUTH_KEY` непустой** — аутентификация UDP-аудио включена по умолчанию (не пустая строка) | `config.example.env`, `node_config.h` | 🟠 Высоко | ✅ (уже в коммите 935e9b8: `AUDIO_UDP_AUTH_KEY=change-me-32-chars-min-at-least`, 32 символа) |
| M2 | **Создать тег `v0.1.0` + GitHub Release** — первая публичная версия с описанием изменений | GitHub репозиторий | 🟡 Средне | ✅ (23.11.2026: тег v0.1.0 создан и запушен, GitHub Release опубликован: https://github.com/ZDarow/wireless-audio-21/releases/tag/v0.1.0) |
| M3 | **Декомпозиция `master_s3/src/main.cpp`** — выделить класс `MasterApp` (сеть, DSP, веб-сервер, консоль) вместо монолита ~979 строк | `firmware/master_s3/src/main.cpp` | 🟡 Средне | ⬜ |
| M4 | **Шаблоны issues/PR, `CONTRIBUTING.md`, `SECURITY.md`, `CHANGELOG.md`** — стандарт для контрибьюторов | корень репозитория, `.github/` | 🟡 Средне | ✅ (23.11.2026: созданы .github/ISSUE_TEMPLATE/bug_report.md, .github/ISSUE_TEMPLATE/feature_request.md, .github/PULL_REQUEST_TEMPLATE.md, CONTRIBUTING.md, SECURITY.md, CHANGELOG.md) |
| M5 | **Добавить `.editorconfig` и `.gitattributes`** — единый стиль кода и окончания строк для всех агентов | корень репозитория | 🟢 Низко | ✅ (23.11.2026: созданы `.editorconfig` (charset=utf-8, end_of_line=lf, indent_size=4, insert_final_newline=true, max_line_length=120, trim_trailing_whitespace=true) и `.gitattributes` (text=auto eol=lf, бинарники не diff'ятся) |
| L3 | **Закрепить точную версию `ArduinoJson`** (не `^7.4.3`) + остальные библиотеки (`Preferences`, `WebServer`, `HTTPClient`, `Update`, `Network`, `U8g2`) | `platformio.ini` | 🟡 Средне | ✅ (уже в коммите a945df3: ArduinoJson@7.4.3 закреплено, комментарий N7 с требованием закрепить остальные библиотеки добавлен в platformio.ini) |
| L4 | **Добавить бейджи shields.io в `README.md`** (CI status, license, PlatformIO) | `README.md` | 🟢 Низко | ✅ (уже в README: строки 3-6 — CI, License GPL-3.0, PlatformIO 6.1.19, ESP32-S3) |
| L1 | **Единый стиль коммитов (Conventional Commits)** — `feat:`, `fix:`, `refactor:`, `docs:`, `ci:`, `security:` | все коммиты | 🟢 Низко | ✅ (используется во всех коммитах сессии) |
| L2 | **Исправить замечания yamllint к `ci.yml`** (строка 33 >80 символов, `\n` в конце файла) | `.github/workflows/ci.yml` | 🟢 Низко | ✅ (23.11.2026: все строки <=80 символов, файл заканчивается на LF) |
| i18n | **Полная локализация Web UI** — перевести непереведённые строки навигации (Dashboard, Wi-Fi, Internet, Audio, Delays, Satellites, System, Update, Logs, Mute, Reboot, Info, Error) на русский или добавить переключатель ru/en | `firmware/common/web/web_spa.h` | 🟡 Средне | ✅ (23.11.2026: навигация переведена (Панель, Интернет, Аудио, Задержки, Сателлиты, Система, Обновление, Логи), кнопки (Выкл звук, Перезагрузка), режимы Wi-Fi (Точка доступа, Клиент, Репитер), каналы аудио (Мастер, Левый, Правый, Саб), Имя хоста, уровни логов (Инфо, Предупр., Ошибка), сообщение об ошибке HTTP) |

---

## 14. Задачи из глубокого аудита кодовой базы (23.11.2026)

> Добавлено по итогам глубокого аудита: проверка логики, целостности архитектуры, выявление
> синтаксических, типологических и логических ошибок, конфликтов зависимостей, антипаттернов
> и уязвимостей. Формат: `P<категория>.<номер>` — категория (безопасность, архитектура, логика,
> тип, конфигурация, стиль). Критичность: 🔴 критично, 🟠 высоко, 🟡 средне, 🟢 низко.

### 14.1 Критичные (🔴 — блокируют сборку/работу/безопасность)

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| P1 | **`generated_config.h` не существует** — `config.env` + `scripts/generate_config.py` не запущены; `build_flags` содержит `-DGENERATED_CONFIG_H` и `-I firmware/common/generated`, но директория/файл отсутствуют. Сборка PlatformIO падает без этого файла | `config.env`, `scripts/generate_config.py`, `firmware/common/generated/` | 🔴 Критично | ✅ (23.11.2026: config.env создан со случайными ключами, generate_config.py обновлён (AUDIO_UDP_AUTH_KEY, AUDIO_OTA_PUBLIC_KEY в STRING_KEYS), generated_config.h сгенерирован (32 макроса), оба файла в .gitignore) |
| P2 | **`master_app.h/.cpp` не существуют** — утверждалось, что созданы при декомпозиции (A1/M3), но файлов нет. `main.cpp` остался монолитом ~1528 строк. Декомпозиция не выполнена | `firmware/master_s3/src/master_app.h`, `firmware/master_s3/src/master_app.cpp`, `firmware/master_s3/src/main.cpp` | 🔴 Критично | ⬜ |
| P3 | **mbedTLS Ed25519 не поддерживается в X.509 сертификатах** — `mbedtls_ed25519_verify` для raw-ключей работает, но это нестандартное использование для OTA подписи. Риск недостоверной проверки подписи образа | `firmware/common/web/web_ota.cpp:112-122`, `firmware/common/web/web_server.h` | 🔴 Критично | ✅ (23.11.2026: добавлено предупреждение в web_ota.cpp о raw-key API вместо X.509, ссылки на mbedtls#2452, #6166, esp-idf#15336) |
| P4 | **ESP-NOW broadcast не зашифрован** — архитектурное ограничение протокола (multicast CCMP не поддерживается). Heartbeat/discovery остаётся открытым. Требуется документировать как известное ограничение или добавить сквозное шифрование полезной нагрузки | `firmware/common/transport/espnow.h` (broadcast-пир, `encrypt = false`) | 🔴 Критично | ✅ (23.11.2026: добавлено предупреждение в заголовке espnow.h о нешифрованном broadcast) |
| P5 | **PBKDF2 итерации 10000 — недостаточно** — OWASP Password Storage Cheat Sheet 2026 рекомендует 210000+ для PBKDF2-HMAC-SHA256. Слабая защита паролей администратора | `firmware/common/auth.h:200` (`kPbkdf2Iterations`) | 🔴 Критично | ⬜ |

### 14.2 Высокие (🟠 — уязвимости, риски логики)

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| P6 | **CVE-2024-42483 replay attack на ESP-NOW** — нет анти-replay защиты. Кэш магических значений ESP-NOW ограничен 32 слотами, не дифференцирован по типу сообщения. Атакующий может флудить cleartext-сообщениями и перезаписывать кэш, создавая возможность ре инъекции перехваченных пакетов | `firmware/common/transport/espnow.h`, `firmware/master_s3/src/main.cpp`, `firmware/satellite/src/main.cpp` | 🟠 Высоко | ⬜ |
| P7 | **`AUDIO_UDP_AUTH_KEY=change-me-32-chars-min-at-least`** — не случайный ключ, а подсказка для замены. Если пользователь не заменит, аутентификация формально включена, но ключ угадываемый | `config.example.env`, `firmware/common/config/node_config.h` | 🟠 Высоко | ⬜ |
| P8 | **SSE — `new WiFiClient(s.client())`** — копирование объекта WiFiClient из WebServer. Может привести к двойному закрытию сокета или использованию после освобождения | `firmware/common/web/web_sse.cpp:38` | 🟠 Высоко | ⬜ |
| P9 | **`delete c` на WiFiClient** — объект не был создан через `new` в контексте WebServer. Удаление может привести к неопределённому поведению | `firmware/common/web/web_sse.cpp:15` | 🟠 Высоко | ⬜ |
| P10 | **Потеря точности аудио на сателлите** — `out = (int16_t)(g_vol.process(out / 32768.0f) * 32768.0f)` — двойное преобразование int16→float→int16 с возможным клиппингом при промежуточных вычислениях | `firmware/satellite/src/main.cpp:358` | 🟠 Высоко | ⬜ |
| P11 | **`esp_wifi_get_channel()` — ESP-IDF функция** — может отсутствовать в Arduino core 3.x (pioarduino). Риск компиляции на новых версиях | `firmware/satellite/src/main.cpp:194` | 🟠 Высоко | ⬜ |
| P12 | **`strlcpy()` — не стандарт C++** — BSD-расширение, может не скомпилироваться на некоторых цепочках инструментов | `firmware/common/config/node_config.h`, `firmware/common/config/storage.h` | 🟠 Высоко | ⬜ |
| P13 | **`strlcpy()` в `storage.h`** — см. P12 | `firmware/common/config/storage.h` | 🟠 Высоко | ⬜ |

### 14.3 Средние (🟡 — логика, антипаттерны, кодовое качество)

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| P14 | **Монолит `main.cpp` ~1528 строк** — смешаны сеть, DSP, веб-сервер, консоль, OLED, энкодер. Нарушение SRP, трудно тестировать и поддерживать | `firmware/master_s3/src/main.cpp` | 🟡 Средне | ⬜ |
| P15 | **CSRF токен пустой при первом запуске** — в `web_spa.h` `csrf` инициализируется пустой строкой. Первые вызовы API до `bootCheck()` отправят пустой CSRF-токен | `firmware/common/web/web_spa.h:227,253` | 🟡 Средне | ⬜ |
| P16 | **OTA без CSRF при первом запуске** — `xhr.setRequestHeader('X-CSRF-Token', csrf)` для OTA — `csrf` может быть пустым, если `bootCheck()` не выполнился | `firmware/common/web/web_spa.h:381` | 🟡 Средне | ⬜ |
| P17 | **JitterBuffer — нет уведомления о переполнении** — при переполнении старые семплы вытесняются без уведомления | `firmware/common/audio/jitter_buffer.h:61-66` | 🟡 Средне | ⬜ |
| P18 | **DelayLine — запутанная арифметика** — `size_t readIdx = (m_writeIdx + m_capacitySamples - m_delaySamples) % m_capacitySamples` | `firmware/common/audio/delay_line.h:53` | 🟡 Средне | ⬜ |
| P19 | **Дублирование `sendForcedPasswordChange`** — в 20+ обработчиках `web_api.cpp`. Лучше middleware/обёртка | `firmware/common/web/web_api.cpp` | 🟡 Средне | ⬜ |
| P20 | **`PcmPipeline` гранично безопасно** — `(l + r) * 0.5f` может дать до ~0.98 (оба канала после лимитера ~0.98). В пределах [-1,1], но нет clamping | `firmware/common/audio/pcm_pipeline.h:140` | 🟡 Средне | ⬜ |

### 14.4 Низкие (🟢 — стиль, документация, улучшения)

| ID | Задача | Место | Приоритет | Статус |
|---|---|---|---|---|
| P21 | **`AUDIO_WIFI_AP_PASSWORD=audio21master`** — дефолтный пароль точки доступа в репозитории | `config.example.env:25` | 🟢 Низко | ⬜ |
| P22 | **`AUDIO_ESPNOW_PMK/LMK` дефолтные ключи в репозитории** — любой, кто знает ключи, может расшифровать ESP-NOW трафик | `config.example.env:41-42` | 🟢 Низко | ⬜ |
| P23 | **`kSalt` статическая соль PBKDF2** — не per-device. Требуется per-device соль в NVS v5 | `firmware/common/auth.h:196` | 🟢 Низко | ⬜ |
| P24 | **Баннер с дефолтным паролем AP в HTML** — `audio21master` в plain text в `web_spa.h` | `firmware/common/web/web_spa.h:46-49` | 🟢 Низко | ⬜ |
| P25 | **Библиотеки без версий в `platformio.ini`** — `WebServer`, `HTTPClient`, `Update`, `Network`, `U8g2` без закрепления версии. Невоспроиззимая сборка | `platformio.ini:119-125` | 🟢 Низко | ⬜ |

---

## 15. ESP-IDF миграция (`firmware/master_s3_idf/`)

> Миграция мастера с Arduino/PlatformIO на чистый ESP-IDF v5.3.1 для устранения
> циклических перезагрузок `TG1WDT_SYS_RST` (watchdog). Проект в отдельной
> ветке `feature/esp32-idf-migration`, собирается через `idf.py build`.

### 15.1 Выполнено (Этапы 1–4)

| ID | Задача | Место | Статус |
|----|--------|-------|--------|
| E1 | Скелет проекта: CMakeLists.txt, main/CMakeLists.txt, main/main.cpp | `firmware/master_s3_idf/` | ✅ |
| E2 | Компонент `dsp_pipeline`: обёртка над `firmware/common/audio/` (duplicates удалены, Arduino.h shim удалён) | `components/dsp_pipeline/` | ✅ |
| E3 | Компонент `espnow_transport`: ESP-NOW unicast/broadcast (0xA210/0xA211/0xA212), AudioPacketHeader 16 байт, peer registration | `components/espnow_transport/` | ✅ |
| E4 | Компонент `udp_audio`: UDP порт 5004 PCM приём, HMAC-SHA256 через mbedTLS, UdpAudioHeader 40 байт | `components/udp_audio/` | ✅ |
| E5 | Компонент `web_server`: HTTP REST API + SSE, cJSON, эндпоинты /api/*, /api/system/* | `components/web_server/` | ✅ |
| E6 | Компонент `i2s_output`: esp_driver_i2s Master TX моно 16-bit для PCM5102A (BCK=4, WS=5, DATA=6) | `components/i2s_output/` | ✅ |
| E7 | Компонент `nvs_config`: NVS хранение NodeConfig (C API nvs_flash/nvs.h), kConfigVersion=1, load/save/erase, единая структура с `common/config/node_config.h` | `components/nvs_config/` | ✅ |
| E8 | Компонент `dns_captive`: UDP DNS сервер на порту 53, отвечает softAP IP на все запросы (captive portal) | `components/dns_captive/` | ✅ |
| E9 | Компонент `ota_update`: OTA через esp_https_ota, endpoint POST /api/system/ota | `components/ota_update/` | ✅ |
| E10 | partitions.csv с ota_0/ota_1 разделами для OTA | `partitions.csv` | ✅ |
| E11 | main/main.cpp: интеграция всех компонентов, TX аудио на сателлиты (батч 117 сэмплов → ESP-NOW), джиттер-буфер в пути приёма, 100 Гц loop с vTaskDelayUntil, NVS load/save, Web UI callbacks | `main/main.cpp` | ✅ |
| E12 | Web server: GET/POST /api/wifi — чтение/запись Wi-Fi конфига (mode, apSsid, apPassword, staSsid, staPassword), сохранение в NVS | `components/web_server/` + `main/main.cpp` | ✅ |
| E13 | Сборка, прошивка, стабильность: 24+ сек uptime без TG1WDT_SYS_RST, heap стабилен ~255 КБ | ESP32-S3 /dev/ttyACM0 | ✅ |
| E14 | mDNS через сырой UDP (порт 5353, multicast 224.0.0.251) — доступ по http://audio-master.local | `components/mdns_raw/` | ✅ |
| E15 | Применение Wi-Fi настроек без перезагрузки (переключение AP/STA/APSTA в рантайме) | `main/main.cpp` | ✅ |
| E16 | Сателлит левый на ESP-IDF (EXTRA_COMPONENT_DIRS → компоненты мастера, 8MB флеш, AUDIO_SATELLITE_SIDE=0): ESP-NOW RX → jitter → DelayLine → VolumeControl → I2S, heartbeat découverte, консоль `status`/`delay` | `firmware/satellite_s3_idf_left/` | ✅ |
| E17 | Сателлит правый на ESP-IDF (AUDIO_SATELLITE_SIDE=1) | `firmware/satellite_s3_idf_right/` | ✅ |
| E18 | Unified NodeConfig: `common/config/node_config.h` без `<Arduino.h>`, поля `version`/`udpAuthKey`, используется nvs_config и оба сателлита | `common/config/node_config.h` | ✅ |
| E19 | PlatformIO: удалены env `master_s3_wifi`/`satellite_s3_left/right` и `platformio.master.ini`; `default_envs = master_a2dp` | `platformio.ini` | ✅ |
| E21 | Captive портал HTML страница: `/captive` GET/POST, форма SSID/пароля, сканирование сетей, DHCP/static toggle, интеграция с `setWifiConfigCallback` | `components/web_server/src/web_server.cpp`, `main/main.cpp` | ✅ |
| E22 | Agent endpoints: `GET /api/agent/health` (без CSRF), `GET /api/agent/ping`, `GET /api/agent/diagnostics` (с CSRF) — диагностика для агентов | `components/web_server/`, `main/main.cpp` | ✅ |
| E23 | Детальный статус сателлитов в `/api/agent/diagnostics`: MAC, RSSI, lastSeenMs для обоих сателлитов | `main/main.cpp` | ✅ |

### 15.2 Отложено / не выполнено

| ID | Задача | Место | Причина | Статус |
|----|--------|-------|---------|--------|
| E21 | Ручное тестирование звука на железе (§18 из ТЗ) | Железо | Требует подключения смартфона и измерений | ⬜ |

### 15.3 Известные проблемы

- **mDNS**: реализован через сырой UDP (порт 5353, multicast 224.0.0.251) в компоненте `mdns_raw`. Доступ по `http://audio-master.local` работает. Официальный компонент `espressif/mdns` не используется из-за отсутствия в Kconfig v5.3.1.
- **sdkconfig.defaults**: очищен от несуществующего ключа `CONFIG_LWIP_MDNS_RESPONDER` (не является Kconfig опцией в v5.3.1).
- **IP в логе dns_captive**: исправлен порядок байт (было `1.4.168.192`, стало `192.168.4.1`).
- **ESP-IDF v5.3.1 уязвимость**: CVE-2026-45541 (WebSocket NULL deref в esp_http_server) — не достижима, так как WebSocket не используется. Рекомендуется обновление на v5.3.6+.
- **Критичные проблемы из аудита**: CSRF-защита веб-API, OTA без подписи, дефолтный пароль AP "1234567890" — описаны в `docs/AUDIT_REPORT_2026-11-23.md`, требуют исправления перед продакшеном.


