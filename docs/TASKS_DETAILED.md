# Подробная техническая документация работ (Work Packages)

> Версия: 1.2 (12.08.2026)
> 1.2: статусы в сводной таблице синхронизированы с `docs/TASKS.md` (закрыты
> C2.1–C2.4, C3.1–C3.3/C3.5, C5.1–C5.6, C6.1–C6.7, C0.2).
> 1.1: исправлены ссылки на файлы/строки (main.cpp, web_server.h) и устаревшие
> формулировки: display.h/encoder.h — не заглушки; handleVolume уже применяет
> изменения к pipeline при не-null; render_timestamp — 4 байта (не 8); сессия —
> RAM-токен без m_sessionStartMs.
> Назначение: разворачивает раздел «10. План устранения расхождений по ТЗ»
> (`docs/TASKS.md`) до уровня исполнимых технических заданий: проблема,
> план, код-скетчи, файлы, критерии готовности, тесты, оценки.
> Источники требований: `ТЗ.md` (§), `ТЗ_Веб.md` (ТЗВ §).

---

## 0. Как пользоваться документом

Каждая карточка задачи (C<этап>.<номер>) содержит:

| Поле | Что там |
|---|---|
| **Проблема** | Фактическое состояние кода (со ссылками на файлы/строки) |
| **Требование** | Ссылка на пункт ТЗ/ТЗ_Веб |
| **План** | Порядок шагов реализации |
| **Код-скетч** | Минимальный работающий каркас (не полный код) |
| **Файлы** | Файлы на изменение/создание |
| **Зависимости** | Какие задачи должны быть готовы раньше |
| **Критерий готовности** | Как проверить, что задача сделана |
| **Тест** | Host-тест или ручная проверка |
| **Оценка** | Трудозатраты (чел.-ч) |

Статусы: ⬜ не начата · 🟡 в работе · ✅ готова · 🚫 заблокирована.

---

## 1. Целевая архитектура аудио-конвейера мастера (S3)

```
смартфон ──UDP:5004 PCM 48k/16/2 (§9.1, magic 0xA210)──► udp_audio_receiver
                                                            │ jitter buffer (PSRAM, 40 мс)
                                                            ▼
                                                    стерео int16 (L,R)
                                                            │
                                                     PcmPipeline
                                              volume → tone → limiter
                                              L ├─HPF(4)──► left  ──delay──► ESP-NOW TX → сателлит L
                                              R ├─HPF(4)──► right ──delay──► ESP-NOW TX → сателлит R
                                              (L+R)/2 ─LPF(4)─► sub ──delay──► I2S → PCM5102A → сабвуфер
                                                            │
                                                    render_timestamp
                                                            ▼
                                                 audio_packet.h (§10.1)
                                                 ESP-NOW broadcast/unicast
```

Три протокола, **не путать**:

| Поток | Документ | Заголовок | Magic | Существующий код |
|---|---|---|---|---|
| смартфон → мастер | §9.1 | 18 байт | `0xA210` | **нет** (создать `udp_audio_packet.h`) |
| мастер → сателлит | §10.1 | 16 байт | `0x2151` | `common/transport/audio_packet.h` (доработать: `render_timestamp`) |
| heartbeat/discovery | §10.1 флаги | тот же 16-байт | `0x2151` | работает (ESP-NOW, `kFlagDiscoveryResponse`) |

---

## 2. Команды сборки и тестов

```bash
# Мастер S3 (отдельный core, NAPT, Arduino core 3.x / IDF 5.5)
cd firmware
pio run -c platformio.master.ini -e master_s3_wifi

# Сателлиты (обычный core 2.0.17, legacy I2S API)
pio run -e satellite_s3_left
pio run -e satellite_s3_right

# Host-тесты чистых модулей (gcc, без Arduino)
make -C test test
```

CI: `.github/workflows/ci.yml` (собирает master_s3_wifi + сателлиты + host-тесты).
**T1:** Actions на GitHub не активированы — при первом push включить.

---

## 3. Трекер статусов (сводка)

| ID | Задача | Приоритет | Статус | Декомпозиция |
|---|---|---|---|---|
| C0.1 | SSID режима настройки | 🟠 | ⬜ | решение |
| C0.2 | Судьба legacy env `master_a2dp` | 🟠 | ✅ | решение |
| C1.1 | `udp_audio_packet.h` по §9.1 | 🔴 | ✅ | пакет |
| C1.2 | Контроль sequence / concealment / standby | 🔴 | ✅ | пакет |
| C1.3 | Jitter buffer мастера в PSRAM | 🔴 | ✅ | пакет |
| C1.4 | I2S-выход мастера (4/5/6) | 🔴 | ✅ | пакет |
| C1.5 | Приём вместо отбрасывания | 🔴 | ✅ | пакет |
| C2.1 | Подключить PcmPipeline | 🔴 | ✅ | пакет |
| C2.2 | L/R/Sub громкости | 🟠 | ✅ | пакет |
| C2.3 | Delay lines мастера (PSRAM) | 🟠 | ✅ | пакет |
| C2.4 | Web-хендлеры → живой pipeline | 🟠 | ✅ | пакет |
| C3.1 | ESP-NOW TX аудио | 🔴 | ✅ | пакет |
| C3.2 | `render_timestamp` в пакете | 🟠 | ✅ | пакет |
| C3.3 | Сателлит: ждать ready(), 20/40/80 мс | 🔴 | ✅ | пакет |
| C3.4 | Дрейф-коррекция сателлита | 🟠 | ⬜ | пакет |
| C3.5 | Volume/fade на сателлите | 🟠 | ✅ | пакет |
| C4.1 | OLED SSD1306 + U8g2 | 🟠 | ⬜ | пакет |
| C4.2 | Энкодер KY-040 | 🟠 | ⬜ | пакет |
| C4.3 | UI-задача в master_s3 | 🟠 | ⬜ | пакет |
| C5.1 | Применение статического IP | 🟠 | ✅ | пакет |
| C5.2 | Session timeout | 🟠 | ✅ | пакет |
| C5.3 | Rate limit login/scan | 🟠 | ✅ | пакет |
| C5.4 | Лог-буфер 16–64 КБ + фильтры | 🟠 | ✅ | пакет |
| C5.5 | `cpu_load_percent` + Dashboard | 🟡 | ✅ | пакет |
| C5.6 | OTA progress | 🟡 | ✅ | пакет |
| C5.7 | Доступ по подсети / MAC-фильтр | 🟡 | ⬜ | пакет |
| C6.1 | Watchdog задач | 🟡 | ✅ | пакет |
| C6.2 | Авто-реконнект Wi-Fi | 🟡 | ✅ | пакет |
| C6.3 | PSRAM-аллокации + метрики heap | 🟡 | ✅ | пакет |
| C6.4 | Скрипты flash → S3 env | 🟡 | ✅ | пакет |
| C6.5 | Чистка мёртвого кода | 🟢 | ✅ | пакет |
| C6.6 | Документация | 🟢 | ✅ | пакет |
| C6.7 | Убрать audio-tools из master_s3 | 🟢 | ✅ | пакет |

---

## 4. Карточки задач

### Этап 0 — Решения по конфликтам ТЗ

#### C0.1 — SSID режима настройки

- **Проблема:** ТЗ §6.3 задаёт AP `Audio21-Master`, ТЗ_Веб §3.2 — `Audio21-Setup`. В коде (`node_config.h:39`) — `Audio21-Master`.
- **Требование:** согласованная пара SSID/пароль без противоречий в документации.
- **План:**
  1. Выбрать основной SSID (`Audio21-Master`, базовая спецификация).
  2. Исправить ТЗ_Веб §3.2 или ввести переменную `AUDIO_WIFI_SETUP_SSID` для режима настройки.
  3. Обновить документацию.
- **Критерий:** обе спецификации и `config.env` говорят одно и то же; тест — grep по `Audio21-Setup`.
- **Оценка:** 0,5 ч (решение человека).

#### C0.2 — Судьба legacy env `master_a2dp`

- **Проблема:** `platformio.ini:53-69` содержит env `master_a2dp` с `ESP32-A2DP`/`BluetoothA2DPSink` — запрещено ТЗ §8.3 (S3 без Classic BT). `firmware/master/` (включая `web_server.h`) вне поставки.
- **Требование:** ТЗ §8.3 — никакого A2DP.
- **Варианты:** (а) удалить env и перенести `web_server.h` в `common/web`; (б) пометить «отладочный стенд, вне поставки».
- **Критерий:** в `platformio.ini` и `README` нет упоминания A2DP как продукта; скрипты прошивки (C6.4) не ссылаются на legacy env.
- **Оценка:** 1 ч (перенос файла) + 1 ч (чистка).

---

### Этап 1 — 🔴 Приём UDP-аудио + I2S-вывод на сабвуфер

#### C1.1 — `udp_audio_packet.h`: парсер пакета смартфона (§9.1) — ✅

- **Проблема:** протокола смартфон→мастер в коде нет. `master_s3/src/main.cpp:535-541` только считает байты и выбрасывает их (UDP-listener — `main.cpp:523-528`).
- **Требование:** §9.1 — структура пакета (magic `0xA210`), §9.2 — параметры (48 кГц, 2 канала, 16 бит, 5 мс ≈ 960 байт, MTU < 1200).
- **Реализовано (11.08.2026):** `firmware/common/transport/udp_audio_packet.h` (header-only, без Arduino):
  `UdpAudioHeader` (18 байт, packed, little-endian), `kUdpMagic=0xA210`, `kUdpProtocolVersion=1`,
  флаги `kUdpFlagEndOfStream`/`kUdpFlagKeyframe`, `kUdpMaxPayload=1200` (MTU-safe, §9.2),
  `buildUdpPacket`/`parseUdpPacket` (валидация magic/version/длины payload).
  Host-тест в `test/transport_packet_test.cpp`: сборка/разбор стерео 48 кГц/16 бит,
  пустой payload (heartbeat), битый magic, короткий буфер, длина payload больше буфера.
- **Код-скетч:**
  ```cpp
  // §9.1: offset 0..17
  struct UdpAudioHeader {
      uint16_t magic;            // 0xA210 (little-endian: на диске 0x10 0xA2)
      uint8_t  protocolVersion;  // 1
      uint8_t  flags;            // 0x01 = end of stream, 0x02 = keyframe
      uint32_t sequence;         // монотонный счётчик пакетов
      uint32_t timestampSamples; // счётчик сэмплов источника (для синхронизации)
      uint16_t sampleRate;       // 48000
      uint8_t  channels;         // 2
      uint8_t  bitsPerSample;    // 16
      uint16_t payloadLength;    // байт PCM
  } __attribute__((packed));
  static_assert(sizeof(UdpAudioHeader) == 18);
  constexpr uint16_t kUdpMagic = 0xA210;
  constexpr size_t kUdpMaxPayload = 1200; // MTU-safe
  ```
- **Файлы:** `common/transport/udp_audio_packet.h` (новый), `test/transport_packet_test.cpp`.
- **Зависимости:** —
- **Критерий:** `make -C test test` зелёный; валидный/невалидный пакет разбираются корректно.
- **Оценка:** 3 ч.

#### C1.2 — Контроль `sequence`, concealment, ramp-out, standby (§9.3) — ✅

- **Проблема:** потери пакетов сейчас игнорируются.
- **Требование:** §9.3: `>50 мс` — concealment/interpolation, `>200 мс` — ramp to mute, `>3 с` — standby.
- **Реализовано (11.08.2026):** `firmware/common/audio/udp_audio_receiver.h` (header-only, без Arduino):
  `StreamState` (Active/Conceal/RampOut/Standby), `feed(seq, ts, pcm, n, nowMs)` с детекцией
  пропуска по `sequence` (аккуратно к wrap 2^32; дубликаты/переупорядочивание не считаются
  потерями), `tick(nowMs)` — эскалация по времени от последнего валидного пакета,
  `concealGain()` — 1.0 → плавно 0.0 (50→200 мс), 0 при ramp-to-mute/standby,
  счётчики `packetsRx`/`packetsLost`.
  Host-тест `test/udp_audio_receiver_test.cpp` (добавлен в `test/Makefile`): 0,1,3 → conceal;
  потеря 100 мс → conceal, gain ≈ 1/3; 210 мс → RampOut; 3,2 с → Standby; восстановление;
  wrap sequence; без пакетов с момента старта → standby.
- **Зависимости:** C1.1.

#### C1.3 — Jitter buffer мастера в PSRAM (20–60 мс)

- **Проблема:** `jitter_buffer.h:19` аллоцирует `new int16_t[]` — внутренняя RAM (S3-N16R8: 512 КБ), 60 мс стерео 48 кГц ≈ 11,5 КБ, но конвейеру нужны ещё delay lines. По ТЗ §16.2 большие буферы — из PSRAM.
- **Требование:** §7.6, §16.2 — буфер в PSRAM.
- **План:**
  1. Добавить в `JitterBuffer` выделение через `ps_malloc` (обёртка `heap_caps_malloc`), с fallback на `malloc`.
  2. Выделять в `master_s3` буфер 40 мс (дефолт), диапазон 20–60 мс.
- **Код-скетч:**
  ```cpp
  explicit JitterBuffer(uint32_t capacity)
      : m_capacity(capacity) {
  #ifdef ESP32
      void* p = heap_caps_malloc(capacity * sizeof(int16_t),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      m_buffer = (int16_t*)(p ? p : malloc(capacity * sizeof(int16_t)));
  #else
      m_buffer = new int16_t[capacity];
  #endif
      clear();
  }
  ```
- **Файлы:** `common/audio/jitter_buffer.h`, `master_s3/src/main.cpp`.
- **Зависимости:** —
- **Критерий:** диагностика показывает PSRAM-аллокацию (логировать адрес через `heap_caps_get_info`); jitter 20/40/60 мс работает.
- **Оценка:** 2 ч.

#### C1.4 — I2S-выход мастера (BCK=4, WS=5, DATA=6) — ✅

- **Проблема:** I2S-вывода на мастере S3 нет вообще (S3 core 3.x — новый API `driver/i2s_std.h`, legacy `driver/i2s.h` в core 3.x удалён/депрекейтнут).
- **Требование:** §18 Этап 2 — вывод PCM через PCM5102A; §19.1.
- **Реализовано (11.08.2026):** `firmware/common/audio/i2s_output.h` (header-only, guard `ESP32 && ARDUINO`):
  - Автовыбор API по `ESP_ARDUINO_VERSION_MAJOR`: `driver/i2s_std.h` (IDF 5.x) для
    `master_s3` (pioarduino core 3.3.11), legacy `driver/i2s.h` (core 2.0.17) для сателлитов.
  - `I2sOutputPins {bck, ws, data}` + `init(pins, sampleRate, mono)`: `i2s_new_channel`/
    `I2S_STD_CLK_DEFAULT_CONFIG`/`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` (IDF 5.x) либо
    `i2s_driver_install`/`i2s_set_pin` (legacy); DMA-буферы 8×256, `tx_desc_auto_clear`.
  - `write(samples, n)`: моно-режим дублирует каждый сэмпл в оба канала (L=R),
    стерео — пары {L,R}; `writeMono`/`writeStereo`/`silence`.
  - `master_s3/src/main.cpp`: I2S-инициализация в setup(), статус `i2s: on/off`,
    serial-команда `tone <freq>` (синус 2 с через `toneTick()` в loop, не блокирует
    Wi-Fi/Web UI) — ручная проверка PCM5102A без смартфона.
  - `satellite/src/main.cpp`: локальный I2S-код заменён на общую обёртку (`initI2S`/
    `writeSample`), поведение сохранено (моно, L=R).
  - Синтаксис обеих веток проверен на хосте (g++ с заглушками `driver/i2s.h` и
    `driver/i2s_std.h`); host-тесты зелёные. Полная сборка — CI.
- **Код-скетч (S3, IDF 5.x):**
  ```cpp
  #include "driver/i2s_std.h"
  i2s_chan_handle_t tx;
  static bool initMasterI2S(const NodeConfig& cfg, uint32_t sampleRate) {
      i2s_chan_config_t chan = { .id = I2S_NUM_0, .role = I2S_ROLE_MASTER };
      i2s_new_channel(&chan, &tx, NULL);
      i2s_std_config_t std = {};
      std.clk_cfg = { .sample_rate_hz = sampleRate, .clk_src = I2S_CLK_SRC_DEFAULT };
      std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
      std.gpio_cfg = { .mclk = -1, .bclk = cfg.i2sBck, .ws = cfg.i2sWs, .dout = cfg.i2sDataOut };
      return i2s_channel_init_std_mode(tx, &std) == ESP_OK;
  }
  ```
- **Файлы:** `common/audio/i2s_output.h` (новый), `master_s3/src/main.cpp`.
- **Зависимости:** C1.5 (данные).
- **Критерий:** тон 440 Гц через serial-команду слышен на сабвуфере; §19.1.
- **Оценка:** 4 ч.

#### C1.5 — Реальный приём UDP вместо отбрасывания

- **Проблема:** `master_s3/src/main.cpp:535-541` читает пакет и выбрасывает в `discard[64]` (только счётчики `g_packetsRx`/`g_packetBytesRx`).
- **Требование:** §18 Этап 2.
- **План:**
  1. В `loop()`: `parsePacket` → `UdpAudioReceiver::feed()` → стерео-сэмплы в jitter buffer.
  2. Задача-потребитель: пока `jitter.ready()` — декодировать стерео, гнать через C2.1 pipeline, sub → I2S (C1.4).
  3. Обновить `status`-команду: `packets_rx`, `packets_lost`, `stream_state`.
- **Файлы:** `master_s3/src/main.cpp`, `common/audio/udp_audio_receiver.h`.
- **Зависимости:** C1.1–C1.4.
- **Критерий:** поток со смартфона (VLC/`ffmpeg` → UDP 5004) воспроизводится на сабвуфере (**§18 Этап 2**).
- **Оценка:** 4 ч.

---

### Этап 2 — 🔴 DSP + кроссовер + 🟠 громкости

#### C2.1 — Подключить `PcmPipeline` к master_s3

- **Проблема:** `pcm_pipeline.h` готов (volume → tone → limiter → LR4 HPF/LPF), но мастер передаёт `nullptr` в конструктор `MasterWebServer` (`master_s3/src/main.cpp:64`; параметры с дефолтом `nullptr` — `web_server.h:109-117`), и pipeline нигде не вызывается.
- **Требование:** §18 Этап 3 — LPF для SUB, HPF для LEFT/RIGHT.
- **План:**
  1. Создать в `master_s3` объект `PcmPipeline g_pipeline;` + `g_pipeline.configure(48000)`.
  2. В аудио-задаче: `PipelineOutput o = g_pipeline.process(l, r);` → `o.sub` в I2S мастера.
  3. Передать `&g_pipeline` в конструктор `MasterWebServer` вместо `nullptr`.
  4. Применить `g_cfg.masterVolume`/`mute`/`crossoverHz` из конфига.
- **Файлы:** `master_s3/src/main.cpp`, `common/web/web_server.h` (тип параметра), `common/audio/pcm_pipeline.h`.
- **Зависимости:** C1.5.
- **Критерий:** §18 Этап 3 — сабвуфер играет только НЧ (90 Гц LR4).
- **Оценка:** 4 ч.

#### C2.2 — Громкости L/R/Sub (`sub_volume`, `left_volume`, `right_volume`)

- **Проблема:** `node_config.h:171-172` — только `masterVolume` и `mute`. ТЗ §7.5 требует громкости L, R, Sub (мин. набор).
- **Требование:** §7.5.
- **План:**
  1. В `NodeConfig`: `int leftVolume=50, rightVolume=50, subVolume=50;` + `clamp()`.
  2. В `PcmPipeline`: `setChannelVolumes(l, r, sub)` (после кроссовера) или `VolumeControl` на каждый выход.
  3. В `web_server.h`: `handleVolume` принимает `channel` (`master|left|right|sub`), `/api/status` отдаёт `audio.volume{master,left,right,sub}`.
  4. SPA: 4 слайдера.
- **Файлы:** `common/config/node_config.h`, `common/audio/pcm_pipeline.h`, `common/web/web_server.h`, `master_s3/src/main.cpp`.
- **Зависимости:** C2.1.
- **Критерий:** §19.3 — 4 громкости меняются и сохраняются после перезагрузки.
- **Оценка:** 4 ч.

#### C2.3 — Delay lines мастера (0–200 мс) в PSRAM

- **Проблема:** `delay_line.h` используется только сателлитом; на мастере задержки из конфига никуда не применяются (только печатаются в диагностике `master_s3/src/main.cpp:196-197`).
- **Требование:** §7.4, §6.9 (0–200 мс), §18 Этап 5.
- **План:**
  1. Создать `DelayLine` на каждый канал (L/R/Sub) в `master_s3`; память — `ps_malloc` (200 мс × 48 кГц = 9600 сэмплов/канал).
  2. Применять `g_cfg.delayLeftMs/RightMs/SubMs`; хендлер `/api/delay` обновляет в рантайме (C2.4).
- **Файлы:** `common/audio/delay_line.h`, `master_s3/src/main.cpp`.
- **Зависимости:** C2.1.
- **Критерий:** §19.5 — регулировка 0–200 мс слышима и без артефактов.
- **Оценка:** 3 ч.

#### C2.4 — Web-хендлеры → живой pipeline

- **Проблема:** хендлеры уже применяют изменения к «живому» pipeline при не-`nullptr` указателях (`handleVolume` — `web_server.h:524,532`, `handleCrossover` — `:556`, `handleDelay` — `:573-579`), но `master_s3` передаёт `nullptr` (`main.cpp:64`) — звук не меняется, пока не создан конвейер (C2.1).
- **Требование:** ТЗВ §16 (REST меняет поведение сразу), §19.3.
- **План:**
  1. В `loop()` мастера: если `g_cfg` изменился — применить к pipeline (volume/mute/crossover/delays).
  2. Либо передать в `MasterWebServer` колбэк-интерфейс `AudioBackend` (`setVolume`, `setCrossoverHz`, `setDelay`), который вызывает pipeline напрямую.
- **Файлы:** `common/web/web_server.h`, `master_s3/src/main.cpp`.
- **Зависимости:** C2.1–C2.3.
- **Критерий:** слайдер громкости в Web UI слышимо меняет звук без reboot.
- **Оценка:** 3 ч.

---

### Этап 3 — 🔴 TX на сателлиты + 🟠 синхронизация

#### C3.1 — ESP-NOW TX аудио с мастера

- **Проблема:** мастер шлёт только discovery-запрос (`master_s3/src/main.cpp:110-115`, вызов в `loop()` — `:552-555`); аудио-пакетов нет.
- **Требование:** §18 Этап 4.
- **План:**
  1. В аудио-задаче после pipeline: `left`/`right` (float → int16) паковать по `kMaxPacketPayload=234` байт (117 сэмплов ≈ 2,4 мс) и `g_espnow.sendTo(leftSatMac/rightSatMac, ...)`.
  2. `packetId` — инкремент; `kFlagKeyframe` каждый ~10-й пакет.
  3. Буферизовать по 1–2 пакета на канал (ESP-NOW очередь).
- **Код-скетч:**
  ```cpp
  constexpr size_t kSamplesPerPkt = kMaxInt16Samples; // 117
  static void sendAudio(uint8_t channel, const int16_t* src, size_t n, uint32_t ts, uint32_t& pktId) {
      uint8_t buf[kMaxPacketSize];
      for (size_t off = 0; off < n; off += kSamplesPerPkt) {
          size_t cnt = min(kSamplesPerPkt, n - off);
          size_t sz = buildPacket(buf, sizeof(buf), channel, kSampleFormatInt16,
                                  src + off, cnt * 2, ts, pktId++, kFlagKeyframe);
          g_espnow.sendTo(channel == kChannelLeft ? g_cfg.leftSatMac : g_cfg.rightSatMac, buf, sz);
      }
  }
  ```
- **Файлы:** `master_s3/src/main.cpp`, `common/transport/espnow.h` (проверить очередь).
- **Зависимости:** C2.1, C3.2.
- **Критерий:** §18 Этап 4 — сателлиты воспроизводят свои каналы.
- **Оценка:** 5 ч.

#### C3.2 — `render_timestamp` в `audio_packet.h` (§10.1)

- **Проблема:** текущий заголовок не совпадает с §10.1: сейчас `timestampMs` на offset 4 + `packetId` на offset 8 (`audio_packet.h:15-16`); по ТЗ — `packet_id`(4) на offset 4 + `render_timestamp`(4, сэмплы) на offset 8. Размер заголовка остаётся 16 байт.
- **Требование:** §10.1, §11.2.
- **План:**
  1. Переопределить `AudioPacketHeader`: `packetId` → offset 4, `renderTimestamp` (сэмплы) → offset 8 (оба по 4 байта, размер заголовка прежний — 16).
  2. Обновить `buildPacket`/`parsePacket` и всех вызывающих (мастер, сателлит, тесты).
  3. Heartbeat/discovery-флаги не трогаем (пустой payload, `render_timestamp=0`).
- **Файлы:** `common/transport/audio_packet.h`, `master_s3/src/main.cpp`, `satellite/src/main.cpp`, `test/transport_packet_test.cpp`.
- **Зависимости:** — (ломает совместимость — обновляются все узлы сразу).
- **Критерий:** `make -C test test` зелёный; в статусе сателлита виден первый/последний `render_timestamp`.
- **Оценка:** 3 ч.

#### C3.3 — Сателлит: ждать `ready()`, уровень 20/40/80 мс

- **Проблема:** `satellite/src/main.cpp:236-237` — ёмкость 50 мс (`sampleRate/20`), цель 15 мс; `loop:307-312` играет немедленно, не дожидаясь `ready()`.
- **Требование:** §10.3 — min 20 / default 40 / max 80 мс; пункт 4 — «начинать только после заполнения».
- **План:**
  1. Ёмкость 80 мс (`sampleRate / 12.5`), цель 40 мс (настраивается).
  2. В `loop()`: `if (!g_jitter->ready()) { /* тишина */ } else { pop → delay → I2S }`.
  3. После старта поддерживать уровень: если `available()` упал ниже цели на старте — снова тишина до `ready()`.
- **Файлы:** `satellite/src/main.cpp`, возможно `common/audio/jitter_buffer.h`.
- **Зависимости:** C3.2 (использовать `render_timestamp` не обязательно на этом шаге).
- **Критерий:** нет щелчков на старте; уровень буфера держится около цели.
- **Оценка:** 3 ч.

#### C3.4 — Дрейф-коррекция сателлита

- **Проблема:** частота сэмплов I2S и прихода пакетов расходятся (±50 ppm); буфер постепенно пустеет/переполняется.
- **Требование:** §10.3 п.6, §11.2.
- **План:**
  1. По `render_timestamp` вычислить смещение «реального времени» vs позиции буфера.
  2. Если уровень растёт (> цель + 5 мс) — один раз скипнуть семпл (или ускорить I2S клок на ±0,1%); если падает — повторить семпл.
  3. Логировать `drift_corrections` в статусе.
- **Код-скетч:**
  ```cpp
  // в цикле pop: дельта уровня каждые 1000 pop
  if (g_jitter->available() > targetHighSamples) { g_jitter->pop(discard); }
  else if (g_jitter->available() < targetLowSamples) { /* повторить прошлый семпл */ }
  ```
- **Файлы:** `satellite/src/main.cpp`.
- **Зависимости:** C3.3.
- **Критерий:** §19.4 — 30 минут без накопления/истощения буфера.
- **Оценка:** 4 ч.

#### C3.5 — Volume и fade на сателлите (§8.6)

- **Проблема:** на сателлите нет ни громкости, ни fade — звук стартует/останавливается щелчком.
- **Требование:** §8.6, §15.2.
- **План:**
  1. `VolumeControl` (уже есть в `common/audio/volume_control.h`) на выходе сателлита; уровень — `g_cfg.left/rightVolume` (зеркало с мастера) или локальное поле.
  2. `fade-in` 5–10 мс на старте, `fade-out` при `master timeout` (>1 с нет пакетов).
- **Файлы:** `satellite/src/main.cpp`, `common/audio/volume_control.h` (проверить API).
- **Зависимости:** C3.3.
- **Критерий:** §19.3 — громкость сателлита регулируется; без щелчков.
- **Оценка:** 3 ч.

---

### Этап 4 — 🟠 OLED + энкодер

#### C4.1 — OLED SSD1306 (128×64, I2C) + U8g2

- **Проблема:** `common/ui/display.h` реализует только базовый статусный экран (`Display::showStatus`, `display.h:34-53`); страниц §12.2 (задержки/кроссовер/статусы сателлитов) нет, дисплей не подключён к `master_s3`, U8g2 в `lib_deps` отсутствует (`platformio.ini:34-35`).
- **Требование:** §12.2 — громкость, источник, статус Wi-Fi, статусы сателлитов, кроссовер, задержки.
- **План:**
  1. `lib_deps`: `olikraus/U8g2`.
  2. Реализовать `DisplayUi`: страницы (главная/задержки/кроссовер/сателлиты), обновление раз в 200 мс.
  3. Питание данных — разделяемые атомарные переменные статуса из `master_s3`.
- **Файлы:** `common/ui/display.h`, `master_s3/src/main.cpp`, `platformio.ini` (lib_deps master_s3_wifi).
- **Зависимости:** C4.3.
- **Критерий:** §12.2 — все пункты отображаются.
- **Оценка:** 5 ч.

#### C4.2 — Энкодер KY-040

- **Проблема:** `common/ui/encoder.h` реализован (`RotaryEncoder`, `encoder.h:9-53`), но нет машины состояний меню и привязки к действиям `UiActions`; энкодер не подключён к `master_s3`.
- **Требование:** §12.1 — короткое нажатие (выбор), вращение (изменение), длинное (назад/меню); функции: громкость, задержки L/R/Sub, кроссовер, статус сателлитов, сохранение.
- **План:**
  1. `lib_deps`: `mathertel/RotaryEncoder`.
  2. Машина состояний меню; действия через общий `UiActions` (вызывают те же сеттеры, что и Web).
- **Файлы:** `common/ui/encoder.h`, `master_s3/src/main.cpp`.
- **Зависимости:** C4.3.
- **Критерий:** §12.1 — все 7 функций работают с энкодера.
- **Оценка:** 4 ч.

#### C4.3 — FreeRTOS-задача UI в master_s3

- **Проблема:** блокирующие I2C-выводы U8g2 в `loop()` убьют аудио.
- **План:**
  1. `xTaskCreatePinnedToCore(core=1, prio=1, stack=8192)` — задача UI.
  2. Общий snapshot статуса (`portMUX`/атомика) между loop и UI-задачей.
- **Файлы:** `master_s3/src/main.cpp`.
- **Критерий:** аудио не прерывается при обновлении OLED.
- **Оценка:** 3 ч.

---

### Этап 5 — 🟠 Веб-доработки (ТЗ_Веб)

#### C5.1 — Применение статического IP

- **Проблема:** `wifi_store.h:18-23` хранит `ip/netmask/gateway/dns`, но `WiFi.config()` нигде не вызывается.
- **Требование:** ТЗВ §6.3, §21.2.
- **План:**
  1. В `master_s3 initWifi()` перед `WiFi.begin()`: если профиль/конфиг имеет `staticIpEnabled` — `WiFi.config(ip, gateway, netmask, dns)`.
  2. Применить при reconnect (loop, C6.2).
- **Файлы:** `master_s3/src/main.cpp`, `common/web/wifi_store.h`.
- **Критерий:** профиль со static IP получает заданный адрес (проверить `net`-командой).
- **Оценка:** 2 ч.

#### C5.2 — Session timeout

- **Проблема:** сессия — RAM-токен (`web_server.h:863-864`), создаётся в `handleLogin` (`:753-767`) и `handleAdminSetup` (`:776-796`); срок жизни не проверяется — сессия живёт до перезагрузки. `kMaxSessionAgeSec=3600` определён в `auth.h:131`, но нигде не используется.
- **Требование:** ТЗВ §11.4, §23.1.
- **План:**
  1. Завести `m_sessionStartMs` (фиксировать при логине/настройке администратора); в `isAuthed()`/`handleClient`: если `millis() - m_sessionStartMs > kMaxSessionAgeSec * 1000` — разлогинить (сбросить токен и cookie).
  2. Опционально: слайдинг-окно (продлевать при активности).
- **Файлы:** `common/web/web_server.h`.
- **Критерий:** через 3600 с без активности сессия недействительна.
- **Оценка:** 2 ч.

#### C5.3 — Rate limit для `/api/login` и `/api/wifi/scan`

- **Проблема:** brute-force/спам скан-запросов не ограничен.
- **Требование:** ТЗВ §23.1.
- **План:**
  1. Счётчик неудачных входов по IP клиента; после 5 неудач — блок 60 с (429).
  2. `/api/wifi/scan` — не чаще 1 раза в 10 с на IP.
- **Файлы:** `common/web/web_server.h`.
- **Критерий:** после N неудач логин отвечает 429.
- **Оценка:** 3 ч.

#### C5.4 — Лог-буфер 16–64 КБ + фильтры

- **Проблема:** `master_s3/src/main.cpp:89-90` — `g_logStorage[32][LogRing::kLineSize]` ≈ 6 КБ (ТЗВ §13.4 требует 16–64 КБ); `/api/logs` (ТЗВ §17.5) без фильтров.
- **План:**
  1. `g_logStorage[128][192]` = 24 КБ (или 256×192 = 48 КБ), в PSRAM при наличии.
  2. `GET /api/logs?limit=&level=&module=` — фильтрация по severity и категории.
  3. Не логировать пароли/полные HTTP-ответы (уже ограничено `kLineSize`).
- **Файлы:** `master_s3/src/main.cpp`, `common/web/logs.h`, `common/web/web_server.h` (`handleLogs`).
- **Критерий:** §13.4/§17.5: объём 16–64 КБ, фильтры работают.
- **Оценка:** 3 ч.

#### C5.5 — `cpu_load_percent` в `/api/status` + Dashboard

- **Проблема:** `/api/status` не содержит `cpu_load_percent`, `mac` и `ntp_time` (ТЗВ §5.2, SSID/RSSI/uptime уже есть); Dashboard не показывает время/NTP/MAC и нагрузку CPU.
- **План:**
  1. Измерение idle-time задачи loop (`esp_timer_get_time()` между итерациями) → `cpu_load_percent`.
  2. В `/api/status` и SPA: `uptime`, `ntp_time` (из `time()`), `wifi_ssid`, `rssi`, `mac`.
- **Файлы:** `common/web/web_server.h` (`handleStatus`), SPA-блок.
- **Критерий:** §5.2/§24.1 — метрики видны.
- **Оценка:** 3 ч.

#### C5.6 — OTA progress

- **Проблема:** `Update` (lib_deps) подключён, но прогресс в SPA не отображается.
- **Требование:** ТЗВ §12.3–12.4.
- **План:** при `Update.write` — слать `{progress}` через `WebServer` (poll `/api/ota/status` или SSE); в SPA — прогресс-бар.
- **Файлы:** `common/web/web_server.h`, SPA.
- **Критерий:** при OTA виден прогресс.
- **Оценка:** 3 ч.

#### C5.7 — Доступ по подсети / MAC-фильтр источника UDP

- **Проблема:** Web и UDP-listener принимают любые адреса.
- **Требование:** ТЗВ §23.2, ТЗ §17.
- **План:**
  1. В Web: проверять `client().remoteIP()` — разрешены подсети AP (192.168.4.x) и STA.
  2. В `UdpAudioReceiver`: белый список IP/сетей источника (§17).
- **Файлы:** `common/web/web_server.h`, `common/audio/udp_audio_receiver.h`.
- **Критерий:** доступ извне блокируется.
- **Оценка:** 3 ч.

---

### Этап 6 — 🟡 Надёжность и чистка

#### C6.1 — Watchdog задач

- **Проблема:** зависание аудио-задачи/loop не отслеживается.
- **Требование:** §16.3.
- **План:** `esp_task_wdt_init` + `esp_task_wdt_add` для аудио-задачи; reset при срабатывании.
- **Файлы:** `master_s3/src/main.cpp`, `satellite/src/main.cpp`.
- **Критерий:** при зависании — перезапуск задачи (лог в boot).
- **Оценка:** 2 ч.

#### C6.2 — Авто-переподключение Wi-Fi в рантайме

- **Проблема:** `initWifi` не переподключается при обрыве STA (только по запросу Web UI).
- **Требование:** §16.3, ТЗВ §21.
- **План:** `WiFi.onEvent(WIFI_EVENT_STA_DISCONNECTED)` → таймер-ретраи (5 попыток, backoff), при неудаче — setup AP.
- **Файлы:** `master_s3/src/main.cpp`.
- **Критерий:** после потери сети — авто-восстановление.
- **Оценка:** 3 ч.

#### C6.3 — PSRAM-аллокации + метрики heap

- **Проблема:** jitter/delay могут уйти во внутреннюю RAM.
- **Требование:** §16.2, §14.3.
- **План:** все большие буферы — `ps_malloc`; в `/api/diagnostics` — `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` и `ESP.getMinFreeHeap()`.
- **Файлы:** `master_s3/src/main.cpp`, `common/audio/*`.
- **Критерий:** 30 минут проигрывания — heap/PSRAM не деградируют (§19.4).
- **Оценка:** 3 ч.

#### C6.4 — Скрипты прошивки → S3 env

- **Проблема:** `scripts/flash_master.sh` прошивает `master_a2dp`, `flash_satellite.sh` — `satellite_left/right` (legacy, не S3).
- **План:** заменить env на `master_s3_wifi` / `satellite_s3_left|right` (выбор по параметру). Перенести `web_server.h` в `common/web` (если C0.2 = удаление legacy).
- **Файлы:** `scripts/flash_master.sh`, `scripts/flash_satellite.sh`, `platformio.ini`.
- **Критерий:** `./flash_master.sh` собирает и прошивает `master_s3_wifi`.
- **Оценка:** 2 ч.

#### C6.5 — Чистка мёртвого кода

- **Проблема:** техдолг T12–T15, T22 (лишние `-D`, лишние `lib_deps`).
- **План:** пройти `docs/TASKS.md` по T-задачам, удалить неиспользуемое.
- **Критерий:** сборка без предупреждений.
- **Оценка:** 3 ч.

#### C6.6 — Документация

- **План:** обновить `README.md`, `docs/architecture.md`, `docs/hardware.md` под факт (S3, UDP-источник, нет A2DP).
- **Критерий:** документация соответствует коду.
- **Оценка:** 3 ч.

#### C6.7 — Убрать `audio-tools` из master_s3

- **Проблема:** `platformio.ini:121` — `arduino-audio-tools` подключён, но не используется (I2S — через IDF API, C1.4).
- **План:** удалить из `lib_deps` env `master_s3_wifi`.
- **Критерий:** сборка без этой зависимости, размер прошивки меньше.
- **Оценка:** 0,5 ч.

---

## 5. Матрица соответствия критериям приёмки §19

| §19 | Критерий | Какие задачи закрывают | Статус |
|---|---|---|---|
| 19.1 | Базовая работоспособность (PSRAM, Wi-Fi, UDP, I2S) | C1.1–C1.5, C6.3 | ⬜ |
| 19.2 | Система 2.1 (мастер разделяет, саб НЧ, L/R каналы) | C2.1, C3.1 | ⬜ |
| 19.3 | Управление (громкость, задержки, кроссовер, сохранение) | C2.2–C2.4, C3.5, C4.1–C4.2 | ⬜ |
| 19.4 | Стабильность 30 мин, потери, heap, авто-восстановление | C3.3–C3.4, C6.1–C6.3 | ⬜ |
| 19.5 | Задержка 50–150 мс, регулировка 0–200 мс | C2.3, C1.3 | ⬜ |

---

## 6. Глоссарий полей пакетов

**Смартфон → мастер (UDP, §9.1), 18 байт:**
`magic(0xA210)` `protocol_version(1)` `flags` `sequence(4)` `timestamp_samples(4)` `sample_rate(2)` `channels(1)` `bits_per_sample(1)` `payload_length(2)`.

**Мастер → сателлит (ESP-NOW/UDP, §10.1), 16 байт (после C3.2):**
`magic(0x2151)` `protocol_version(1)` `flags` `packet_id(4)` `render_timestamp(4)` `channel_id(1)` `sample_format(1)` `payload_length(2)`.

**Флаги:** `0x01 EOF`, `0x02 keyframe`, `0x04 discovery request`, `0x08 discovery response`.
**Channel ID:** `0x01 LEFT`, `0x02 RIGHT`.
