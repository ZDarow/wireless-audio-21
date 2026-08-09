# Wireless Audio 2.1 — План реализации и требования

> Документ фиксирует **как я вижу реализацию** проекта: архитектуру, каркас,
> требования к модулям и поэтапный план. Живой документ — обновляется по мере
> реализации.

---

## 1. Видение архитектуры

Система — **3 узла** на ESP32, общий код вынесен в `firmware/common` (header-only):

```
Smartphone (A2DP / Wi-Fi)
        │
        ▼
┌─────────────────────────────┐
│  MASTER (сабвуфер)          │
│  A2DP → DSP → splitter      │
│    ├─ L (HPF) → delay → TX ─┼──► LEFT SATELLITE
│    ├─ R (HPF) → delay → TX ─┼──► RIGHT SATELLITE
│    └─ Sub(LPF) → delay → I2S┼──► сабвуфер (локально)
└─────────────────────────────┘
        ▲
        │ serial-консоль / REST / OLED
        └── управление (volume, crossover, delay, pair, save)
```

Ключевые принципы:

- **Общий код — header-only** (`firmware/common`): компилируется в каждый узел,
  без дублирования. Подключается через `-I` в `platformio.ini`.
- **Роли** разделяются `src_dir` в `platformio.ini` (master / satellite), а не
  препроцессором — чистая изоляция.
- **Формат пакета фиксирован** (`audio_packet.h`, 16-байт заголовок) — общий
  для ESP-NOW и UDP, тестируется отдельно.
- **DSP-модули чистые** (без зависимостей от Arduino-железа) — их можно
  тестировать на хосте (gcc), как в проекте DQ-DSP.

---

## 2. Требования (из спецификации 2.1.txt)

### 2.1 Функциональные (MVP — текущая итерация)

| # | Требование | Статус |
|---|---|---|
| F1 | Приём A2DP со смартфона | 🔨 в каркасе |
| F2 | DSP: громкость, тембр, limiter | ✅ каркас |
| F3 | Кроссовер LPF/HPF, частота раздела 70–120 Гц | ✅ каркас |
| F4 | Задержки L/R/Sub 0–200 мс | ✅ каркас |
| F5 | ESP-NOW TX на сателлиты | ✅ каркас |
| F6 | UDP TX (альтернативный транспорт) | ✅ каркас |
| F7 | Локальный I2S на сабвуфер | ✅ каркас |
| F8 | Serial-консоль (status/volume/crossover/delay/pair/save/reboot) | ✅ каркас |
| F9 | NVS-хранение настроек | ✅ каркас |
| F10 | Сателлит: приём, jitter buffer, задержка, I2S | ✅ каркас |
| F11 | Web UI + REST API | ⬜ следующая итерация |
| F12 | OLED + энкодер | ⬜ следующая итерация |
| F13 | Wi-Fi UDP источник | ⬜ следующая итерация |

### 2.2 Нефункциональные требования

- **Задержка**: целевая < 50 мс end-to-end (ESP-NOW).
- **Анти-клик**: плавный фейд громкости (`VolumeControl`), `tx_desc_auto_clear`.
- **Надёжность**: ESP-NOW без роутера, авто-reconnect A2DP.
- **Переносимость**: общий код без железо-зависимостей → тестируется на хосте.

### 2.3 Аппаратные требования

- Мастер: ESP32-WROVER-E (PSRAM для A2DP) / ESP32-S3-N16R8 (Wi-Fi).
- Сателлиты: ESP32-WROOM-32E / ESP32-S3.
- DAC: PCM5102A (I2S), усилитель: TPA3116D2 (саб) / PAM8403 (сателлиты).
- OLED SSD1306 + энкодер KY-040 (мастер).

---

## 3. Каркас (что уже создано)

```
wireless-audio-21/
├── platformio.ini              # 3 env: master_a2dp, satellite_left/right
├── config.example.env          # переменные окружения
├── config.example.h            # ручная конфигурация
├── firmware/
│   ├── common/                 # header-only, общий код
│   │   ├── config/  node_config.h, storage.h
│   │   ├── audio/   crossover.h, delay_line.h, volume_control.h, pcm_pipeline.h
│   │   ├── transport/ audio_packet.h, espnow.h, udp.h
│   │   ├── ui/       display.h, encoder.h
│   │   └── util/     logger.h, timing.h
│   ├── master/  include/master_config.h, src/main.cpp
│   └── satellite/ include/satellite_config.h, src/main.cpp (TODO)
├── scripts/                    # generate_config.py, flash_*.sh (TODO)
├── test/                       # audio_filter_test, delay_line_test, transport_packet_test (TODO)
├── docs/                       # architecture.md, hardware.md, wiring.md (TODO)
└── README.md                   # (TODO)
```

---

## 4. Поэтапный план

### Этап 1 — Каркас и общий код (текущий)
- [x] Структура каталогов
- [x] `platformio.ini` (3 env)
- [x] `config.example.env` / `config.example.h`
- [x] `common/config`: `node_config.h`, `storage.h`
- [x] `common/util`: `logger.h`, `timing.h`
- [x] `common/audio`: `crossover.h`, `delay_line.h`, `volume_control.h`, `pcm_pipeline.h`, `jitter_buffer.h`
- [x] `common/transport`: `audio_packet.h`, `espnow.h`, `udp.h`
- [x] `common/ui`: `display.h`, `encoder.h`
- [x] `master/main.cpp` (A2DP + DSP + батчевый TX + I2S + консоль)
- [x] `satellite/main.cpp` (приём + jitter + задержка + I2S)

### Этап 2 — Скрипты и тесты
- [x] `scripts/generate_config.py` (config.env → generated_config.h)
- [x] `scripts/flash_master.sh`, `scripts/flash_satellite.sh`
- [x] `test/audio_filter_test.cpp` (host: кроссовер LR4, volume, delay, jitter, pipeline)
- [x] `test/transport_packet_test.cpp` (host: формат пакета, границы)
- [x] `test/Makefile` (автозависимости -MMD, make test)

### Этап 3 — Документация
- [x] `README.md`
- [x] `docs/PLAN.md`
- [ ] `docs/architecture.md`, `docs/hardware.md`, `docs/wiring.md`

### Этап 4 — Расширения (следующие итерации)
- [ ] REST API (ESPAsyncWebServer) + Web UI
- [ ] OLED-меню + энкодер
- [ ] Wi-Fi UDP источник
- [ ] Синхронизация воспроизведения (timestamp в пакете)
- [ ] Защита от щелчков при включении/остановке (fade-in/out)

---

## 5. Ключевые решения

| Решение | Почему |
|---|---|
| Header-only `common` | нет дублирования, переносимость, простота PlatformIO |
| Роли через env, не препроцессор | чистая изоляция, отдельные бинари |
| Единый формат пакета | ESP-NOW и UDP используют один кодек |
| Чистые DSP-модули | тестируются на хосте без железа |
| Задержка в мс → сэмплы | пересчёт по sample_rate, диапазон 0–200 мс |
| Батчевая TX (117 сэмплов/пакет) | ESP-NOW ≤ 250 байт; один пакет на семпл утопил бы эфир |
| Jitter buffer на сателлите | компенсация джиттера/потерь ESP-NOW |
| Кроссовер LR4 | 24 дБ/октаву, ровный суммарный отклик на частоте раздела |

## 6. Баги каркаса, пойманные host-тестами

| Баг | Как пойман | Исправление |
|---|---|---|
| `Crossover::m_stages` объявлен дважды (массив и счётчик) | ошибка компиляции | счётчик → `m_stageCount` |
| `ToneControl` использует несуществующий `m_treble` | ошибка компиляции | поле → `m_high` |
| `DelayLine::process` не учитывал `m_delaySamples` (задержка всегда = ёмкость) | тест импульса | чтение из `readIdx = write - delay` |
| HPF 2-го порядка слабо резал бас (12 дБ/окт) | тест частотной характеристики | pipeline переведён на LR4 (24 дБ/окт) |