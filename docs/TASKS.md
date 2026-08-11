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
| B2 | Конфликт пинов в `config.example.h`: `AUDIO_I2S_DATA_OUT=22` и `AUDIO_OLED_SCL=22` (актуально при реализации F12) | `config.example.h` | Средний | ✅ (OLED SCL → 23) |
| B3 | `UdpTransport::broadcast()` считает broadcast-IP как `~localIP` + `bc[3]=255` — корректен только для /24-подсети | `firmware/common/transport/udp_transport.h:53` | Низкий | ✅ (broadcast по маске подсети, `broadcast_ip.h`) |
| B4 | `handleVolume` без поля `volume` в JSON молча ставит громкость 0 вместо ошибки | `firmware/master/include/web_server.h:118` | Низкий | ✅ (ошибка `missing volume|mute`) |
| B5 | `generate_config.py:133` повторно вызывает `expand(env)` при выводе счётчика макросов (косметика) | `scripts/generate_config.py` | Низкий | ✅ (результат переиспользуется) |
| B6 | Риск нестабильности A2DP + Wi-Fi STA coexistence на ESP32 (нужна проверка на железе) | мастер | Риск | ⬜ |
| B7 | Partition `huge_app.csv` без OTA — осознанное MVP-решение, блокирует будущий OTA | `platformio.ini:25` | Замечание | ⬜ |
| B8 | Имя `udp.h` в case-insensitive ФС (Windows) конфликтует с системным `Udp.h` из Arduino (включается `WiFiUdp.h`) — сборка падала. Файл переименован в `udp_transport.h` | `firmware/common/transport/udp_transport.h` | Высокий | ✅ (переименование + сборка 3 env SUCCESS) |
| B9 | **APSTA-репитер (core 3.x): мастер не отвечает на AP-интерфейсе** — клиент за AP получает IP (DHCP), интернет через NAPT работает (ping 8.8.8.8, HTTP/HTTPS 200), но unicast на AP-IP не обрабатывается: ping/ARP `192.168.4.1` с клиента — 100% loss, Web UI недоступен по `192.168.4.1` (доступен только через STA-IP `192.168.1.129`). Диагностика `net` с мастера: `ping ap_gw` FAIL при живом AP-интерфейсе. Похоже на ограничение APSTA в Arduino core 3.x / IDF 5.5 (AP netif не принимает пакеты на свой IP, если активен STA) | `firmware/master_s3/src/main.cpp`, Wi-Fi APSTA | Средний | ⬜ |
| B10 | **IPv6 не форвардится через NAPT** — DNS возвращает AAAA-записи, клиенты за AP мастера пытаются идти по IPv6 и «зависают» (curl без `-4` = FAIL, HTTP 000), хотя IPv4 полностью работает. Нужно: не отдавать IPv6-маршрут/RA на AP (проверить, что отключено) или документировать требование IPv4 | мастер (APSTA) | Низкий | ⬜ |

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