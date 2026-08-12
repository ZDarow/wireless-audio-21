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
| F11 | Web UI + REST API | ✅ реализовано |
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
- DAC: PCM5102A (I2S), усилитель: TPA3110 XH-A232 (аналоговый вход, 12–24 В).
- OLED SSD1306 + энкодер KY-040 (мастер).

---

## 3. Каркас (что уже создано)

```
wireless-audio-21/
├── platformio.ini              # env: master_s3_wifi, satellite_s3_left/right (+ legacy)
├── platformio.master.ini       # master_s3_wifi: изолированный core 3.x (pioarduino)
├── config.example.env          # переменные окружения → generated_config.h
├── config.example.h            # ручной аналог (синхронизирован, T6)
├── firmware/
│   ├── common/                 # header-only, общий код
│   │   ├── config/   node_config.h, storage.h (NVS, миграции v1..v4)
│   │   ├── audio/    crossover.h, delay_line.h, volume_control.h, pcm_pipeline.h,
│   │   │            jitter_buffer.h, udp_audio_receiver.h, i2s_output.h
│   │   ├── transport/ audio_packet.h, espnow.h, udp_transport.h,
│   │   │            udp_audio_packet.h, broadcast_ip.h
│   │   ├── web/      auth.h, internet_check.h, logs.h, wifi_store.h
│   │   ├── ui/       display.h, encoder.h (для F12, Этап 4)
│   │   └── util/     logger.h, timing.h
│   ├── master_s3/  src/main.cpp, include/master_s3_config.h   # целевой мастер (S3)
│   ├── master/     src/main.cpp, include/web_server.h         # legacy A2DP-стенд (C0.2)
│   └── satellite/  src/main.cpp                               # сателлиты (S3 + legacy)
├── scripts/                    # generate_config.py, flash_master.sh, flash_satellite.sh
├── test/                       # host-тесты (make test) + stubs/
├── docs/                       # PLAN, TASKS, architecture, hardware, wiring, REPO_AUDIT
├── .github/workflows/ci.yml    # CI: host-тесты + сборка S3
└── README.md
```

---

## 4. Поэтапный план

### Этап 1 — Каркас и общий код
- [x] Структура каталогов
- [x] `platformio.ini` (6 env: S3-целевые + legacy-стенд)
- [x] `config.example.env` / `config.example.h`
- [x] `common/config`: `node_config.h`, `storage.h`
- [x] `common/util`: `logger.h`, `timing.h`
- [x] `common/audio`: `crossover.h`, `delay_line.h`, `volume_control.h`, `pcm_pipeline.h`, `jitter_buffer.h`, `udp_audio_receiver.h`, `i2s_output.h`
- [x] `common/transport`: `audio_packet.h`, `espnow.h`, `udp_transport.h`, `udp_audio_packet.h`, `broadcast_ip.h`
- [x] `common/web`: `auth.h`, `internet_check.h`, `logs.h`, `wifi_store.h`
- [x] `common/ui`: `display.h`, `encoder.h` (зарезервированы для F12)
- [x] `master_s3/main.cpp` (Wi-Fi UDP + DSP + канальные громкости + батчевый TX + I2S + консоль)
- [x] `master/main.cpp` (legacy A2DP-стенд, C0.2)
- [x] `satellite/main.cpp` (приём + jitter + задержка + громкость/fade + I2S)

### Этап 2 — Скрипты и тесты
- [x] `scripts/generate_config.py` (config.env → generated_config.h)
- [x] `scripts/flash_master.sh`, `scripts/flash_satellite.sh` (S3-целевые, C6.4)
- [x] `test/audio_filter_test.cpp` (host: кроссовер LR4, volume, delay, jitter, pipeline)
- [x] `test/transport_packet_test.cpp` (host: формат пакета, границы)
- [x] `test/Makefile` (автозависимости -MMD, make test) + `test/stubs/` (T21)

### Этап 3 — Документация
- [x] `README.md`
- [x] `docs/PLAN.md`, `docs/architecture.md`, `docs/hardware.md`, `docs/wiring.md`, `docs/TASKS.md`
- [x] `docs/REPO_AUDIT.md` (аудит репозитория, 12.08.2026)

### Этап 4 — Расширения (следующие итерации)
- [x] REST API + Web UI (встроенный WebServer ESP32 + ArduinoJson, auth + CSRF)
- [x] Wi-Fi UDP источник (мастер-S3, magic 0xA210, §10)
- [x] TX аудио мастер→сателлиты (ESP-NOW/UDP, батч 117, C3.1/C3.2)
- [x] Канальные громкости L/R/Sub (C2.2), fade-in/out (C3.5)
- [x] OTA с прогресс-баром (C5.6), доступ из локальной подсети (C5.7)
- [ ] OLED-меню + энкодер (F12/C4.x)
- [ ] Синхронизация воспроизведения — коррекция дрейфа (C3.4)

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