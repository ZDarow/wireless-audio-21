# Wireless Audio 2.1

Беспроводная аудиосистема 2.1 на **ESP32**: мастер (сабвуфер) принимает звук по
**A2DP** или **Wi-Fi**, обрабатывает его DSP-конвейером (громкость → тембр →
лимитер → кроссовер), разделяет на каналы и передаёт на два беспроводных
сателлита (левый/правый) по **ESP-NOW** или **UDP**.

```
Smartphone ──A2DP──► ┌───────────────────────────┐
                      │  MASTER (сабвуфер)        │
                      │  DSP → splitter           │
                      │   ├─ L → TX ─────────────┼──► LEFT SATELLITE
                      │   ├─ R → TX ─────────────┼──► RIGHT SATELLITE
                      │   └─ Sub → I2S ──────────┼──► сабвуфер (локально)
                      └───────────────────────────┘
                            serial / REST / OLED (управление)
```

## Возможности (MVP)

- A2DP Sink (ESP32-A2DP) + Wi-Fi UDP вход
- DSP-конвейер: громкость (0–100), тембр, лимитер, кроссовер **Linkwitz-Riley 4**
  (70–120 Гц, дефолт 90 Гц)
- Задержки каналов L/R/Sub **0–200 мс** (выравнивание времени прихода)
- Транспорт: **ESP-NOW** (низкая задержка, без роутера) или **UDP** (по Wi-Fi)
- Пакетный формат: 16-байт заголовок + payload до 234 байт (лимит ESP-NOW)
- Сателлиты: приём → jitter buffer → задержка → I2S
- Serial-консоль: `status`, `volume`, `crossover`, `delay`, `pair`, `save`, `reboot`
- NVS-хранение настроек (Preferences)
- Host-тесты чистых модулей (gcc, без железа)

## Структура проекта

```
wireless-audio-21/
├── platformio.ini              # 3 env: master_a2dp, satellite_left/right
├── config.example.env          # конфигурация → generated_config.h
├── firmware/
│   ├── common/                 # header-only, общий код
│   │   ├── config/   node_config.h, storage.h
│   │   ├── audio/    crossover.h, delay_line.h, volume_control.h,
│   │   │            pcm_pipeline.h, jitter_buffer.h
│   │   ├── transport/ audio_packet.h, espnow.h, udp.h
│   │   ├── ui/       display.h, encoder.h
│   │   └── util/     logger.h, timing.h
│   ├── master/       src/main.cpp, include/master_config.h
│   └── satellite/    src/main.cpp, include/satellite_config.h
├── scripts/                    # generate_config.py, flash_master.sh, flash_satellite.sh
├── test/                       # host-тесты (make test)
└── docs/                       # PLAN.md (план/требования), архитектура
```

## Быстрый старт

### 1. Конфигурация

```bash
cp config.example.env config.env
# отредактируйте config.env: Wi-Fi, MAC сателлитов, частоты, GPIO
python3 scripts/generate_config.py config.env
```

### 2. Прошивка

```bash
# Мастер (сабвуфер)
./scripts/flash_master.sh /dev/ttyUSB0

# Сателлит (левый / правый)
./scripts/flash_satellite.sh left  /dev/ttyUSB1
./scripts/flash_satellite.sh right /dev/ttyUSB2
```

### 3. Host-тесты (без железа)

```bash
cd test && make test
```

### 4. Serial-консоль мастера

```
status                  # текущее состояние
volume 60               # громкость 0–100
volume mute / unmute    # глушение
crossover 90            # частота раздела 70–120 Гц
delay left 10           # задержка канала, мс
transport espnow|udp    # переключение транспорта
pair left AA:BB:CC:DD:EE:01   # привязка сателлита
save                    # сохранить в NVS
reboot
```

## Требования

- PlatformIO (`pip install platformio`)
- Python 3.10+
- gcc (для host-тестов)

Подробности: [docs/PLAN.md](docs/PLAN.md) — план, требования, архитектура,
карта расширений (Web UI, OLED, Wi-Fi источник).
