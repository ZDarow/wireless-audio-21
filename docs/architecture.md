# Архитектура Wireless Audio 2.1

> Версия документа: 1.1 (12.08.2026). Соответствует коду ветки `main`.

## 1. Обзор

Система состоит из трёх узлов на ESP32:

```
┌─────────────────────────┐
│  MASTER (сабвуфер)      │
│  ESP32 + I2S → сабвуфер │
└───────┬─────────────────┘
        │  ESP-NOW (250 Б/пакет) или UDP (unicast после discovery)
        ├───────────────────────┐
┌───────▼─────────┐   ┌─────────▼────────┐
│ SATELLITE LEFT  │   │ SATELLITE RIGHT  │
│ ESP32 + I2S →   │   │ ESP32 + I2S →    │
│ левый динамик   │   │ правый динамик   │
└─────────────────┘   └──────────────────┘
```

- **Мастер (целевой, ESP32-S3)** принимает аудио по Wi-Fi UDP PCM (S3 не
  поддерживает A2DP), обрабатывает DSP-конвейером и раздаёт три канала:
  левый/правый сателлиты (беспроводно) и сабвуфер (локальный I2S).
- **Legacy `master_a2dp`** (ESP32-WROVER, A2DP) — отладочный стенд (C0.2),
  вне поставки.
- **Сателлиты** принимают свой канал, компенсируют джиттер и задержку,
  выводят в I2S.

## 2. Поток данных

### Мастер (целевой, ESP32-S3)

```
Wi-Fi UDP RX (magic 0xA210, udp_audio_receiver.h)
  → JitterBuffer (устойчивость к джиттеру сети)
  → PcmPipeline.process(l, r):
      tone (bass/treble shelf) → volume (с фейдом) → limiter
      → HPF Linkwitz-Riley 4-го порядка (left, right)
      → LPF Linkwitz-Riley 4-го порядка (sub = моно-микс L+R)
  → left/right: DelayLine → громкость канала → батч 117 сэмплов → buildPacket → TX
  → sub:        DelayLine → громкость канала → I2S → ЦАП → усилитель сабвуфера
```

Legacy `master_a2dp` (стенд C0.2): тот же конвейер, но источник — callback
`a2dpDataCallback` от `BluetoothA2DPSink` (стерео int16).

### Сателлит

```
ESP-NOW/UDP RX → parsePacket → фильтр по каналу (left/right)
  → JitterBuffer.push (пачки семплов)
  → loop: JitterBuffer.pop → DelayLine → VolumeControl (громкость+fade, C3.5)
  → I2S → ЦАП → динамик
```

## 3. Формат пакета (спецификация §6.8)

16-байтный заголовок + payload (little-endian):

| Смещение | Размер | Поле | Описание |
|---|---|---|---|
| 0 | 2 | `magic` | 0x2151 («21») |
| 2 | 1 | `protocolVersion` | 1 |
| 3 | 1 | `flags` | 0x01 EOF, 0x02 keyframe, 0x04 discovery request, 0x08 discovery response |
| 4 | 4 | `timestampMs` | метка времени источника |
| 8 | 4 | `packetId` | монотонный счётчик |
| 12 | 1 | `channel` | 0x01 left, 0x02 right |
| 13 | 1 | `sampleFormat` | 0x00 int16, 0x01 float32 |
| 14 | 2 | `payloadLength` | байт полезной нагрузки |
| 16 | N | payload | PCM |

Ограничения:
- `kMaxPacketPayload = 234` байт (ESP-NOW: максимум 250 байт на пакет).
- `kMaxInt16Samples = 117` сэмплов на пакет.
- Размер заголовка фиксирован и проверяется `static_assert`.

## 4. Транспорт

### ESP-NOW (`firmware/common/transport/espnow.h`)

- Работает в STA-режиме, не требует подключения к точке доступа.
- Мастер регистрирует пиров по MAC (`addPeer`), шлёт `sendTo` на каждый
  сателлит отдельно.
- Сателлит регистрирует RX-callback и фильтрует пакеты по каналу.
- Пакет > 250 байт отклоняется (`ESP_ERR_INVALID_SIZE`).

### UDP (`firmware/common/transport/udp_transport.h`)

- Порт по умолчанию: **4210**.
- **Discovery (спецификация §5.6)**: мастер шлёт broadcast-запрос
  (`kFlagDiscoveryRequest`), сателлиты отвечают unicast-ответом
  (`kFlagDiscoveryResponse`) со своим каналом; мастер запоминает их IP
  (`m_leftIp`/`m_rightIp`).
- Аудио шлётся **unicast** на запомненный IP (`sendToChannel`); пока IP не
  известен — fallback на broadcast.
- Мастер повторяет discovery-запрос каждые 3 с, пока не знает оба IP.
- Сателлит отвечает на discovery-запросы автоматически (`handleDiscovery`),
  аудио обрабатывается только если пакет не discovery.

## 5. DSP-конвейер (`firmware/common/audio/`)

| Модуль | Файл | Назначение |
|---|---|---|
| `ToneControl` | `pcm_pipeline.h` | Два shelf-биквада: басы ~250 Гц, верхи ~4 кГц (AudioEQ Cookbook) |
| `PeakLimiter` | `pcm_pipeline.h` | Мягкий ограничитель пиков, порог 0.98 (после volume — B15) |
| `VolumeControl` | `volume_control.h` | Громкость 0..100, mute, плавный фейд (float и int16) |
| `Crossover` | `crossover.h` | Linkwitz-Riley 4-го порядка (24 дБ/октаву), LPF/HPF |
| `DelayLine` | `delay_line.h` | Кольцевой буфер, задержка 0..200 мс |
| `JitterBuffer` | `jitter_buffer.h` | FIFO с целевым уровнем наполнения, overwrite при переполнении |
| `UdpAudioReceiver` | `udp_audio_receiver.h` | Приём UDP-пакетов со смартфона (magic 0xA210), извлечение PCM |
| `I2sOutput` | `i2s_output.h` | Вывод на ЦАП через I2S (управляемый старт/стоп, B14) |

Диапазоны (спецификация §6.9–6.10, `node_config.h`):
- Громкость: 0..100 (мастер + покомпонентные L/R/Sub, C2.2).
- Кроссовер: 70..120 Гц (по умолчанию 90).
- Задержки: 0..200 мс.

## 6. Конфигурация и хранение

### Источники конфигурации

1. `config.env` → `scripts/generate_config.py` → `firmware/common/generated/generated_config.h`
   (23 макроса). Подключается через `-DGENERATED_CONFIG_H`.
2. `config.example.h` — ручной аналог (должен совпадать с генератором).
3. `NodeConfig::defaultConfig()` — дефолты из макросов + `clamp()`.

### NVS (`firmware/common/config/storage.h`)

- Namespace: `audio21`, ключи: `config` (байты `NodeConfig`), `version`.
- Версия `kVersion = 4`; при несовпадении версии или размера возвращаются
  дефолты, иначе выполняются миграции v1→v2→v3→v4 (добавленные поля —
  канальные громкости и пр. — заполняются дефолтами).
- `clamp()` применяется при загрузке — значения из NVS не могут выйти
  за допустимые диапазоны.
- Проверка round-trip — только на железе (см. `docs/TASKS.md`, T9).

## 7. Управление

### Serial-консоль (115200 бод)

Мастер: `status`, `volume <0..100|mute|unmute>`, `crossover <70..120>`,
`delay <left|right|sub> <0..200>`, `transport <espnow|udp>`,
`pair <left|right> <MAC>`, `save`, `reboot`.

Сателлит: `status`, `delay <0..200>`, `save`.

### Web UI (только мастер, спецификация §6.4)

- Адрес: `http://<IP-мастера>/` (печатается в serial при старте).
- Встроенный `WebServer` ESP32 + ArduinoJson (синхронный, без внешних ресурсов).
- REST API:

| Метод | Путь | Тело | Действие |
|---|---|---|---|
| GET | `/` | — | панель управления (HTML+JS) |
| GET | `/api/status` | — | состояние узла (JSON) |
| GET | `/api/wifi/status` | — | состояние Wi-Fi (SSID/RSSI/AP/интернет) |
| GET | `/api/wifi/scan` | — | скан сетей (кеш B9 или живой, C5.3) |
| POST | `/api/wifi/connect` | `{"ssid","password"}` | подключиться к сети |
| POST | `/api/wifi/save` | `{"ssid","password"}` | сохранить профиль без подключения |
| POST | `/api/wifi/forget` | `{"ssid"}` | удалить профиль |
| GET | `/api/wifi/profiles` | — | список сохранённых сетей |
| GET | `/api/net/internet` | — | статус интернета (NTP/DNS) |
| POST | `/api/net/check` | — | принудительная проверка интернета |
| PUT/POST | `/api/volume` | `{"volume":60}` / `{"channel":"sub","volume":50}` / `{"mute":true}` | громкость/mute (канальная C2.2) |
| PUT/POST | `/api/crossover` | `{"hz":90}` | частота раздела |
| PUT/POST | `/api/delay` | `{"channel":"left","ms":10}` | задержка канала |
| POST | `/api/mute` | `{"mute":true}` | глушение (отдельный эндпоинт) |
| POST | `/api/transport` | `{"mode":"espnow"}` | транспорт |
| POST | `/api/pair` | `{"side":"left","mac":"AA:BB:.."}` | привязка сателлита |
| POST | `/api/save` | — | сохранить в NVS |
| POST | `/api/system/reboot` | — | перезагрузка |
| POST | `/api/system/factory_reset` | — | сброс к заводским настройкам |
| GET | `/api/system/config/export` | — | экспорт конфига (JSON) |
| POST | `/api/system/config/import` | — | импорт конфига (JSON) |
| GET | `/api/logs` | `?level=&module=` | логи с фильтрами (C5.4) |
| GET | `/api/diagnostics` | — | heap/PSRAM/cpu_load/uptime (C6.3) |
| POST | `/api/login` | `{"password"}` | аутентификация (сессия+Cookie) |
| POST | `/api/logout` | — | завершить сессию |
| POST | `/api/admin/setup` | `{"password"}` | первичная установка пароля |
| POST | `/api/update` | — | OTA-прошивка (multipart, XHR-прогресс C5.6) |
| GET | `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, `/connecttest.txt`, `/fwlink` | — | captive portal / провайдерские проверки |

Аутентификация (C5.x): пароль SHA-256 + соль, cookie `session=`, X-CSRF-Token
на POST, rate limit 5 неудач/60 с, таймаут сессии 3600 с. Доступ к Web UI —
только из локальной подсети (C5.7 `clientIsLocal`).

Формат `/api/status`:

```json
{
  "system": { "version": "0.2.1", "hostname": "audio-master", "uptime_sec": 42,
    "heap_free": 123456, "psram_free": 123456, "cpu_load_percent": 12,
    "mac": "AA:BB:CC:DD:EE:FF", "time": "2026-08-12T10:00:00",
    "auth_enabled": true, "authed": true, "csrf": "..." },
  "wifi": { "ssid": "Home", "rssi": -45, "ip": "192.168.1.5",
    "ap_ip": "192.168.4.1", "mode": "ap_sta", "state": "connected",
    "internet": "ok" },
  "audio": { "source": "udp", "playing": true, "sample_rate": 48000,
    "bits": 16, "channels": 2, "volume": 50,
    "left_volume": 50, "right_volume": 50, "sub_volume": 50,
    "mute": false, "crossover_hz": 90 },
  "delays": { "left_ms": 0, "right_ms": 0, "sub_ms": 0 },
  "satellites": { "left": "online", "right": "offline" }
}
```

## 8. Структура репозитория

```
firmware/
├── common/                 # header-only, общий код
│   ├── audio/              # DSP: crossover, volume, pipeline, delay, jitter,
│   │                       # udp_audio_receiver, i2s_output
│   ├── config/             # node_config.h, storage.h (NVS, миграции v1..v4)
│   ├── transport/          # audio_packet, espnow, udp_transport,
│   │                       # udp_audio_packet, broadcast_ip
│   ├── web/                # auth, internet_check, logs, wifi_store
│   ├── ui/                 # display.h, encoder.h (для F12, Этап 4)
│   ├── util/               # logger.h, timing.h
│   └── generated/          # generated_config.h (не коммитится)
├── master_s3/              # целевой мастер (S3, Wi-Fi UDP)
│   ├── src/main.cpp
│   └── include/master_s3_config.h
├── master/                 # legacy-стенд (A2DP, C0.2)
│   ├── src/main.cpp
│   └── include/web_server.h   # ОБЩИЙ Web UI + REST (используется и S3)
└── satellite/              # сателлиты (left/right, S3 + legacy)
    └── src/main.cpp

scripts/generate_config.py  # генератор generated_config.h из config.env
scripts/flash_master.sh     # прошивка мастера (master_s3_wifi)
scripts/flash_satellite.sh  # прошивка сателлита (satellite_s3_left/right)
test/                       # host-тесты (gcc, без Arduino) + stubs/
docs/                       # PLAN, TASKS, architecture, hardware, wiring, REPO_AUDIT
```

## 9. Окружения PlatformIO

| Env | Роль | build_src_filter | Особые флаги |
|---|---|---|---|
| `master_s3_wifi` | мастер (S3) | `+<master_s3/src>` | Wi-Fi UDP источник, ESP-NOW/UDP, PSRAM, 16MB |
| `satellite_s3_left` | левый сателлит (S3) | `+<satellite/src>` | `-DAUDIO_SATELLITE_SIDE=0` |
| `satellite_s3_right` | правый сателлит (S3) | `+<satellite/src>` | `-DAUDIO_SATELLITE_SIDE=1` |
| `master_a2dp` | мастер (legacy, A2DP) | `+<master/src>` | отладочный стенд, вне поставки (C0.2) |
| `satellite_left` | левый сателлит (legacy) | `+<satellite/src>` | `-DAUDIO_SATELLITE_SIDE=0` |
| `satellite_right` | правый сателлит (legacy) | `+<satellite/src>` | `-DAUDIO_SATELLITE_SIDE=1` |

- Платформа: `pioarduino 55.03.311` (Arduino core 3.3.11 / IDF 5.5.5) для
  S3-окружений, `espressif32@6.9.0` (core 2.0.17) для legacy; плата
  `esp32-s3-devkitc1-n8r2` (8MB flash, без PSRAM).
- Мастер `master_s3_wifi` и сателлиты `satellite_s3_left/right` собираются
  через `platformio.master.ini` (изолированный core-каталог `.pio-core-master`)
  — только там lwip собран с NAPT (режим репитера APSTA, F21).
- Мастер/сателлиты S3: flash 8MB (`default_8MB.csv`), `memory_type=qio_opi`,
  без PSRAM, `CORE_DEBUG_LEVEL=2`, I2S пины 14/13/12.
- Библиотеки: ArduinoJson 7.4.3 (мастер). WebServer/Preferences/HTTPClient/Update/
  Network встроены в Arduino core (T22); arduino-audio-tools и ESP32-A2DP —
  только в legacy `master_a2dp` (отладочный стенд), зафиксированы по SHA (T11).
- Legacy env (`master_a2dp`, `satellite_left/right`) остаются в `platformio.ini`
  как отладочный стенд (C0.2), вне поставки; целевые — S3-окружения.

## 10. Известные ограничения (MVP)

- Источник аудио — Wi-Fi UDP PCM со смартфона (мастер — приёмник UDP,
  magic 0xA210, см. ТЗ §10). A2DP недоступен на ESP32-S3.
- `timestampMs` заполняется мастером в каждом пакете (C3.2), но коррекция
  дрейфа часов на сателлите (C3.4) — открытая задача.
- OLED-меню + энкодер — этап 4 (F12/C4.x).
- Канальные громкости L/R/Sub (C2.2) и fade сателлита (C3.5) реализованы;
  схема «разное время старта каналов» (ТЗ §8.2) — часть C3.4/F14.
- mDNS (`http://audio-master.local`) — реализовано (F16).
- ESP-NOW без шифрования (LMK/PMK не заданы), UDP-аудио без аутентификации
  источника — см. `docs/REPO_AUDIT.md` V1/V4.