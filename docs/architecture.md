# Архитектура Wireless Audio 2.1

> Версия документа: 1.0 (10.08.2026). Соответствует коду ветки `main`.

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

- **Мастер** принимает аудио по Bluetooth A2DP (или Wi-Fi UDP — этап 4),
  обрабатывает DSP-конвейером и раздаёт три канала: левый/правый сателлиты
  (беспроводно) и сабвуфер (локальный I2S).
- **Сателлиты** принимают свой канал, компенсируют джиттер и задержку,
  выводят в I2S.

## 2. Поток данных

### Мастер

```
A2DP (BluetoothA2DPSink)
  → callback a2dpDataCallback (стерео int16)
  → PcmPipeline.process(l, r):
      tone (bass/treble shelf) → limiter → volume (с фейдом)
      → HPF Linkwitz-Riley 4-го порядка (left, right)
      → LPF Linkwitz-Riley 4-го порядка (sub = моно-микс L+R)
  → left/right: DelayLine → батч 117 сэмплов → buildPacket → TX
  → sub:        DelayLine → I2S → ЦАП → усилитель сабвуфера
```

### Сателлит

```
ESP-NOW/UDP RX → parsePacket → фильтр по каналу (left/right)
  → JitterBuffer.push (пачки семплов)
  → loop: JitterBuffer.pop → DelayLine → I2S → ЦАП → динамик
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

### UDP (`firmware/common/transport/udp.h`)

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
| `PeakLimiter` | `pcm_pipeline.h` | Мягкий ограничитель пиков, порог 0.98 |
| `VolumeControl` | `volume_control.h` | Громкость 0..100, mute, плавный фейд (float и int16) |
| `Crossover` | `crossover.h` | Linkwitz-Riley 4-го порядка (24 дБ/октаву), LPF/HPF |
| `DelayLine` | `delay_line.h` | Кольцевой буфер, задержка 0..200 мс |
| `JitterBuffer` | `jitter_buffer.h` | FIFO с целевым уровнем наполнения, overwrite при переполнении |

Диапазоны (спецификация §6.9–6.10, `node_config.h`):
- Громкость: 0..100.
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
- Версия `kVersion = 1`; при несовпадении версии или размера возвращаются
  дефолты.
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
| PUT | `/api/volume` | `{"volume":60}` / `{"mute":true}` | громкость/mute |
| PUT | `/api/crossover` | `{"hz":90}` | частота раздела |
| PUT | `/api/delay` | `{"channel":"left","ms":10}` | задержка канала |
| POST | `/api/transport` | `{"mode":"espnow"}` | транспорт |
| POST | `/api/pair` | `{"side":"left","mac":"AA:BB:.."}` | привязка сателлита |
| POST | `/api/save` | — | сохранить в NVS |
| POST | `/api/reboot` | — | перезагрузка |

Формат `/api/status`:

```json
{
  "role": "master",
  "source": "a2dp",
  "connected": true,
  "transport": "espnow",
  "sample_rate": 44100,
  "bits": 16,
  "channels": 2,
  "volume": 50,
  "mute": false,
  "crossover_hz": 90,
  "delay_left_ms": 0,
  "delay_right_ms": 0,
  "delay_sub_ms": 0,
  "satellites": { "left": "online", "right": "offline" }
}
```

## 8. Структура репозитория

```
firmware/
├── common/                 # header-only, общий код
│   ├── audio/              # DSP: crossover, volume, pipeline, delay, jitter
│   ├── config/             # node_config.h, storage.h (NVS)
│   ├── transport/          # audio_packet.h, espnow.h, udp.h
│   ├── util/               # logger.h, timing.h
│   └── generated/          # generated_config.h (не коммитится)
├── master/                 # мастер-узел
│   ├── src/main.cpp
│   └── include/            # master_config.h, web_server.h
└── satellite/              # сателлиты (left/right через -DAUDIO_SATELLITE_SIDE)
    ├── src/main.cpp
    └── include/satellite_config.h

scripts/generate_config.py  # генератор generated_config.h из config.env
test/                       # host-тесты (gcc, без Arduino)
docs/                       # PLAN.md, TASKS.md, architecture.md, hardware.md, wiring.md
```

## 9. Окружения PlatformIO

| Env | Роль | build_src_filter | Особые флаги |
|---|---|---|---|
| `master_a2dp` | мастер | `+<master/src>` | A2DP, ESP-NOW, PSRAM, Web UI |
| `satellite_left` | левый сателлит | `+<satellite/src>` | `-DAUDIO_SATELLITE_SIDE=0` |
| `satellite_right` | правый сателлит | `+<satellite/src>` | `-DAUDIO_SATELLITE_SIDE=1` |

- Платформа: `espressif32@6.9.0`, фреймворк Arduino, плата `esp32dev`.
- Partition: `huge_app.csv` (~3 МБ app, без OTA) — ESP32-A2DP не влезает
  в дефолтные 1.31 МБ.
- Библиотеки pschatzmann (ESP32-A2DP, arduino-audio-tools) — git-URL'ами,
  т.к. отсутствуют в реестре PlatformIO.

## 10. Известные ограничения (MVP)

- A2DP-источник: мастер выступает как Bluetooth-приёмник (sink); телефон
  подключается к «Audio21-Master».
- Синхронизация по `timestampMs` (компенсация дрейфа часов) — этап 4 (F14).
- Wi-Fi UDP-источник вместо A2DP — этап 4 (F13).
- OLED-меню + энкодер — этап 4 (F12).
- Fade-in/out при старте/остановке — этап 4 (F15).
- mDNS (`http://audio-master.local`) — этап 4 (F16).