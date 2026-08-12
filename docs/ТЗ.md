# Техническое задание  
## Адаптация беспроводной аудиосистемы 2.1 под ESP32-S3

---

## 1. Цель проекта

Адаптировать беспроводную аудиосистему 2.1 под использование **ESP32-S3** в качестве мастер-узла сабвуфера.

Так как ESP32-S3 не поддерживает Classic Bluetooth A2DP, источник аудиосигнала должен быть перенесён с Bluetooth A2DP на **Wi-Fi audio**.

Целевая система:

```text
Смартфон
   │
   │ Wi-Fi UDP / RTP / HTTP audio
   ▼
Мастер-узел ESP32-S3 в сабвуфере
   │
   ├── DSP: громкость, кроссовер, задержки
   │
   ├── НЧ-канал → локальный I2S DAC → усилитель сабвуфера
   │
   ├── L-канал → беспроводной передатчик → левый сателлит
   └── R-канал → беспроводной передатчик → правый сателлит
```

---

## 2. Область применения

Настоящее ТЗ распространяется на:

- мастер-узел сабвуфера;
- левый беспроводной сателлит;
- правый беспроводной сателлит;
- прошивки ESP32-S3;
- транспорт аудио между смартфоном и мастером;
- транспорт аудио между мастером и сателлитами;
- конфигурацию, управление и диагностику.

---

## 3. Исходные ограничения

### 3.1 Ограничения ESP32-S3

ESP32-S3 поддерживает:

```text
Wi-Fi 802.11 b/g/n
Bluetooth LE
USB OTG
I2S
PSRAM
```

ESP32-S3 не поддерживает:

```text
Classic Bluetooth BR/EDR
Bluetooth A2DP Sink
```

Следовательно, использование смартфона как Bluetooth A2DP Source невозможно.

### 3.2 Обязательное следствие

Источник аудио должен быть реализован через:

```text
Wi-Fi UDP
Wi-Fi RTP
Wi-Fi HTTP stream
или иной сетевой аудиопротокол
```

Основным вариантом считать:

```text
Wi-Fi UDP PCM stream
```

---

## 4. Целевая архитектура

### 4.1 Общая схема

```text
Android Smartphone
   │
   │ PCM audio over Wi-Fi UDP
   ▼
ESP32-S3 Master
   │
   ├── Wi-Fi receiver
   ├── Jitter buffer
   ├── Clock sync
   ├── Volume
   ├── Crossover
   ├── Delay lines
   │
   ├── Subwoofer path:
   │      LPF → delay → I2S → PCM5102A → TPA3116D2 → subwoofer
   │
   ├── Left path:
   │      HPF → delay → packetizer → ESP-NOW / UDP → left satellite
   │
   └── Right path:
          HPF → delay → packetizer → ESP-NOW / UDP → right satellite
```

### 4.2 Сателлиты

```text
ESP32-S3 Satellite
   │
   ├── ESP-NOW RX или UDP RX
   ├── Jitter buffer
   ├── Channel delay
   ├── Volume
   └── I2S → DAC → amplifier → speaker
```

---

## 5. Требования к аппаратной части

### 5.1 Мастер-узел

Микроконтроллер:

```text
ESP32-S3-WROOM-1-N16R8
или ESP32-S3-DevKitC-1-N16R8
```

Обязательные требования:

| Параметр | Требование |
|---|---|
| Flash | не менее 16 MB |
| PSRAM | не менее 8 MB |
| Wi-Fi | 2.4 GHz |
| USB | для прошивки и логов |
| Питание логики | 5 В от отдельного DC-DC |
| Аудиовыход | I2S |

Рекомендуемая замена прежнего мастера:

```text
Было:
ESP32 DevKit V1 / ESP32-WROOM-32, без PSRAM

Стало:
ESP32-S3-N16R8, 8 MB PSRAM
```

### 5.2 Питание мастера

Сохраняется исходная силовая часть:

```text
220 В
  │
  ├── предохранитель
  ├── RS-400W-24V
  │
  ├── 24 В → усилитель сабвуфера
  │
  └── 24 В → DC-DC 5 В → ESP32-S3, OLED, энкодер, логика
```

Требования:

- раздельная силовая и цифровая земля в одной точке;
- LC-фильтр по входу 5 В;
- электролитические и керамические конденсаторы по питанию аудиоцепей;
- запрещается питать ESP32-S3 напрямую от шины усилителя без дополнительной фильтрации.

### 5.3 Аудиотракт мастера

Обязательные компоненты:

| Компонент | Назначение |
|---|---|
| PCM5102A | внешний I2S ЦАП |
| TPA3116D2 | усилитель сабвуфера |
| OLED SSD1306 | локальная индикация |
| KY-040 | роторный энкодер |
| NVS / Preferences | хранение настроек |

### 5.4 Сателлиты

Минимальная конфигурация:

```text
ESP32-S3 mini / ESP32-S3-WROOM-1
PCM5102A
усилитель класса D
питание 5 В или 12 В
```

Допускается использование ESP32-WROOM-32E в сателлитах, если не требуется PSRAM и сложная обработка.

Но для унификации рекомендуется:

```text
Master:    ESP32-S3-N16R8
Satellite: ESP32-S3
```

---

## 6. Требования к беспроводным каналам

### 6.1 Канал смартфон → мастер

Основной режим:

```text
Wi-Fi UDP PCM
```

Поддерживаемые режимы:

| Режим | Описание | Приоритет |
|---|---|---|
| AP_DIRECT | мастер создаёт Wi-Fi точку доступа, смартфон подключается напрямую | основной |
| STA_NETWORK | мастер подключается к домашней Wi-Fi сети, смартфон находится в той же сети | дополнительный |
| UDP_UNICAST | одноадресная передача аудио на мастер | основной |
| UDP_MULTICAST | групповая передача аудио | опционально |

### 6.2 Канал мастер → сателлиты

Основной транспорт:

```text
ESP-NOW
```

Резервный транспорт:

```text
Wi-Fi UDP unicast
```

Требуемая поддержка:

| Режим | Обязательность |
|---|---|
| ESP-NOW unicast left | обязательно |
| ESP-NOW unicast right | обязательно |
| UDP fallback | желательно |
| ESP-NOW broadcast | опционально |
| Wi-Fi mesh | не требуется |

### 6.3 Радиопланирование

Для устойчивой работы задать фиксированный Wi-Fi канал.

Рекомендуется:

```text
channel = 6
bandwidth = 20 MHz
```

В режиме `AP_DIRECT`:

```text
SSID: Audio21-Master
Password: audio21master
IP: 192.168.4.1
Audio UDP port: 5004
```

Сателлиты должны работать на том же Wi-Fi канале, что и мастер.

### 6.4 Требования к задержке

Целевые значения:

| Участок | Целевая задержка |
|---|---:|
| Смартфон → мастер | 20–60 ms |
| Мастер → сателлит | 5–30 ms |
| Полный тракт | 50–150 ms |
| Регулируемая компенсация | 0–200 ms на канал |

---

## 7. Требования к аудио и DSP

### 7.1 Формат аудио

Входной поток:

```text
PCM
16 bit
little endian
mono/stereo
44100 Hz или 48000 Hz
```

Рекомендуемый формат по умолчанию:

```text
48000 Hz
16 bit
2 channels
```

Причина:

```text
Android чаще работает с 48 kHz,
что снижает необходимость ресемплинга на источнике.
```

### 7.2 Каналы

После обработки мастер должен формировать:

```text
LEFT   — для левого сателлита
RIGHT  — для правого сателлита
SUB    — для локального сабвуфера
```

### 7.3 Кроссовер

Требуемые фильтры:

| Канал | Фильтр | Назначение |
|---|---|---|
| SUB | LPF | пропускать низкие частоты |
| LEFT | HPF | ограничить низкие частоты |
| RIGHT | HPF | ограничить низкие частоты |

Параметры по умолчанию:

```text
crossover frequency = 90 Hz
filter type = Linkwitz-Riley или Butterworth
filter order = 2–4
adjustable range = 70–120 Hz
```

### 7.4 Задержки

Каждый канал должен иметь независимую цифровую линию задержки.

Диапазон:

```text
0–200 ms
шаг 1 ms
```

Каналы:

```text
delay_left_ms
delay_right_ms
delay_sub_ms
```

Настройки должны сохраняться в NVS.

### 7.5 Громкость

Требуется реализовать:

```text
master_volume
left_volume
right_volume
sub_volume
mute
unmute
fade-in / fade-out
```

Минимальный обязательный набор:

```text
master_volume
sub_volume
mute
```

### 7.6 Буферизация

Требуемые буферы:

| Буфер | Назначение |
|---|---|
| Wi-Fi RX jitter buffer | компенсация неравномерности сети |
| Satellite jitter buffer | компенсация джиттера беспроводного канала |
| Delay buffer | программная задержка канала |
| I2S DMA buffer | вывод аудио |

Рекомендуемые размеры:

```text
Wi-Fi RX buffer:     20–60 ms
Satellite buffer:    20–60 ms
Delay buffer max:    200 ms
```

Большие аудио-буферы размещать в PSRAM.

---

## 8. Требования к программному обеспечению

### 8.1 Платформа

Среда разработки:

```text
PlatformIO Core
Linux Mint 22.3
терминальный workflow
```

Framework:

```text
Arduino framework для ESP32-S3
или ESP-IDF при необходимости более точного контроля
```

### 8.2 Окружения PlatformIO

Необходимо создать окружения:

```text
master_s3_wifi
satellite_s3_left
satellite_s3_right
```

### 8.3 Зависимости

Обязательные библиотеки:

| Библиотека | Назначение |
|---|---|
| `pschatzmann/arduino-audio-tools` | аудиопотоки, I2S, фильтры, буферы |
| `ArduinoJson` | конфигурация, REST API |
| `ESPAsyncWebServer` | web-интерфейс, REST |
| `U8g2` | OLED SSD1306 |
| `RotaryEncoder` | энкодер |
| `Preferences` / NVS | хранение настроек |

Не использовать:

```text
ESP32-A2DP
BluetoothA2DPSink
Classic Bluetooth stack
```

### 8.4 Структура проекта

```text
wireless-audio-21/
├── README.md
├── platformio.ini
├── config.example.env
├── docs/
├── firmware/
│   ├── common/
│   │   ├── audio/
│   │   ├── transport/
│   │   ├── config/
│   │   ├── ui/
│   │   └── util/
│   ├── master_s3/
│   │   ├── include/
│   │   └── src/
│   └── satellite_s3/
│       ├── include/
│       └── src/
├── hardware/
├── scripts/
└── test/
```

### 8.5 Программные модули мастера

Мастер должен содержать следующие модули:

```text
wifi_manager
udp_audio_receiver
jitter_buffer
clock_recovery
audio_pipeline
crossover
delay_lines
volume_control
espnow_transmitter
udp_transmitter
storage
web_server
rest_api
display_ui
encoder_ui
diagnostics
```

### 8.6 Программные модули сателлита

Сателлит должен содержать:

```text
wifi_or_espnow_receiver
jitter_buffer
delay_line
volume_control
i2s_output
storage
diagnostics
pairing
```

---

## 9. Требования к сетевому протоколу смартфона

### 9.1 Основной формат пакета

Использовать UDP-пакет со структурой:

```text
Offset  Size  Field
0       2     magic
2       1     protocol_version
3       1     flags
4       4     sequence
8       4     timestamp_samples
12      2     sample_rate
14      1     channels
15      1     bits_per_sample
16      2     payload_length
18      N     PCM payload
```

### 9.2 Рекомендуемые параметры

```text
magic = 0xA210
protocol_version = 1
sample_rate = 48000
channels = 2
bits_per_sample = 16
payload_duration = 5 ms
payload_size ≈ 960 bytes
MTU safe < 1200 bytes
```

### 9.3 Поведение при потере пакетов

Требуется реализовать:

```text
обнаружение потери по sequence;
короткое concealment;
ramp-out при длительной потере;
mute при отсутствии потока более 3 секунд.
```

Рекомендуемые пороги:

```text
packet loss > 50 ms  → concealment / interpolation
packet loss > 200 ms → ramp to mute
no stream > 3 s      → standby
```

---

## 10. Требования к протоколу мастер → сателлит

### 10.1 ESP-NOW пакет

Структура:

```text
Offset  Size  Field
0       2     magic
2       1     protocol_version
3       1     flags
4       4     packet_id
8       4     render_timestamp
12      1     channel_id
13      1     sample_format
14      2     payload_length
16      N     PCM payload
```

Значения `channel_id`:

```text
0x01 = LEFT
0x02 = RIGHT
```

### 10.2 Ограничения ESP-NOW

Учитывать:

```text
max ESP-NOW payload = 250 bytes
реальная полезная нагрузка = 200–220 bytes
```

Следовательно, пакет должен быть компактным.

Рекомендация:

```text
1 channel
16 bit
48–96 samples per packet
```

### 10.3 Синхронизация сателлитов

Сателлиты должны:

1. Принимать пакеты.
2. Использовать `render_timestamp`.
3. Хранить данные в jitter buffer.
4. Начинать воспроизведение только после заполнения буфера.
5. Поддерживать целевой уровень буфера.
6. Корректировать дрейф частоты.

Целевой уровень буфера:

```text
min = 20 ms
default = 40 ms
max = 80 ms
```

---

## 11. Требования к синхронизации

### 11.1 Проблема

Разные задержки возникают из-за:

```text
Wi-Fi джиттера;
ESP-NOW передачи;
буферов;
разного расстояния до слушателя;
разных ЦАП и усилителей.
```

### 11.2 Обязательное требование

Система должна поддерживать программное выравнивание задержек:

```text
left delay
right delay
sub delay
```

### 11.3 Мастер как источник времени

Мастер должен формировать единый временной ориентир:

```text
master sample clock
master frame counter
render timestamp
```

Сателлиты не должны самостоятельно изменять темп воспроизведения без алгоритма clock recovery.

---

## 12. Требования к управлению

### 12.1 Локальное управление

Используется энкодер:

```text
короткое нажатие — выбор пункта;
вращение — изменение значения;
длинное нажатие — назад или меню.
```

Минимальные функции:

```text
громкость;
задержка сабвуфера;
задержка левого канала;
задержка правого канала;
частота кроссовера;
статус сателлитов;
сохранение настроек.
```

### 12.2 OLED-дисплей

Отображать:

```text
громкость;
источник;
статус Wi-Fi;
статус левого сателлита;
статус правого сателлита;
частоту кроссовера;
задержки.
```

### 12.3 Web-интерфейс

Обязательные endpoint:

```text
GET  /api/status
PUT  /api/volume
PUT  /api/delay
PUT  /api/crossover
POST /api/save
POST /api/reboot
```

Пример:

```bash
curl -X PUT http://audio-master.local/api/delay \
  -H "Content-Type: application/json" \
  -d '{"channel":"sub","delay_ms":18}'
```

---

## 13. Требования к конфигурации

### 13.1 Файл конфигурации

Использовать:

```text
config.env
```

### 13.2 Переменные конфигурации

| Переменная | Назначение |
|---|---|
| `AUDIO_NODE_ROLE` | MASTER или SATELLITE |
| `AUDIO_SOURCE_MODE` | WIFI |
| `AUDIO_TRANSPORT_MODE` | ESPNOW или UDP |
| `AUDIO_WIFI_MODE` | AP_DIRECT или STA |
| `AUDIO_WIFI_SSID` | имя сети |
| `AUDIO_WIFI_PASSWORD` | пароль |
| `AUDIO_UDP_PORT` | порт приёма аудио |
| `AUDIO_SAMPLE_RATE` | частота дискретизации |
| `AUDIO_LEFT_SAT_MAC` | MAC левого сателлита |
| `AUDIO_RIGHT_SAT_MAC` | MAC правого сателлита |
| `AUDIO_CROSSOVER_HZ` | частота раздела |
| `AUDIO_DELAY_LEFT_MS` | задержка левого канала |
| `AUDIO_DELAY_RIGHT_MS` | задержка правого канала |
| `AUDIO_DELAY_SUB_MS` | задержка сабвуфера |

---

## 14. Требования к сборке ESP32-S3

### 14.1 PlatformIO

Обязательные параметры для ESP32-S3-N16R8:

```ini
[env:master_s3_wifi]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
monitor_speed = 115200
build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=2
```

### 14.2 Обязательные проверки при старте

Прошивка мастера должна выводить:

```text
chip model;
flash size;
PSRAM size;
Wi-Fi mode;
IP address;
UDP audio port;
I2S pins;
satellite MAC addresses;
transport mode;
crossover frequency;
delay settings.
```

### 14.3 Критерий корректности PSRAM

При старте должно быть:

```text
PSRAM size > 0
```

Если:

```text
PSRAM size = 0
```

сборка или плата выбраны неверно.

---

## 15. Требования к смартфону

### 15.1 Исходные данные

Целевой источник:

```text
Android smartphone
Redmi Note 9 Pro / lineage_miatoll
Android 15
SDK 35
```

### 15.2 Обязанности источника

Смартфон должен:

```text
подключиться к Wi-Fi сети мастера;
открывать UDP-сокет;
захватывать или декодировать аудио;
формировать PCM 16 bit;
отправлять пакеты с sequence и timestamp;
поддерживать минимальный интервал отправки 5 ms.
```

### 15.3 Варианты реализации источника

Допустимые варианты:

1. Кастомное Android-приложение.
2. Существующее приложение с функцией audio over UDP/RTP.
3. Скрипт или медиаплеер с поддержкой RTP/UDP.
4. Временный тестовый генератор пакетов с ПК.

### 15.4 Требование к приложению источника

Если используется кастомное Android-приложение, оно должно поддерживать:

```text
PCM 16 bit;
44100 Hz или 48000 Hz;
stereo;
UDP unicast;
sequence number;
timestamp;
настройку target IP;
настройку target port;
настройку packet duration.
```

---

## 16. Требования к надёжности

### 16.1 Устойчивость к потере пакетов

Требуется:

```text
корректно восстанавливать порядок пакетов;
скрывать одиночные потери;
плавно глушить звук при длительной потере;
восстанавливать воспроизведение после возобновления потока.
```

### 16.2 Контроль памяти

Минимальные требования:

```text
использовать PSRAM для больших буферов;
DMA-буферы I2S размещать в подходящей внутренней памяти;
контролировать heap и PSRAM free;
не допускать утечек памяти;
логировать минимальный free heap.
```

### 16.3 Стабильность Wi-Fi

Требуется:

```text
отключить Wi-Fi power saving для аудиоузлов;
фиксированный Wi-Fi канал;
повторное подключение при обрыве;
автоматическое восстановление сателлитов;
watchdog для основных задач.
```

---

## 17. Требования к безопасности

Минимальный набор:

```text
WPA2-PSK для Wi-Fi AP;
фильтрация MAC-адресов сателлитов;
проверка magic в пакетах;
проверка protocol version;
запрет принятия пакетов от неизвестных источников;
опциональный token в заголовке.
```

---

## 18. Этапы реализации

### Этап 1. Базовая плата ESP32-S3

Проверить:

```text
прошивка загружается;
serial работает;
PSRAM видна;
Flash 16 MB работает;
Wi-Fi включается.
```

### Этап 2. Wi-Fi приём аудио

Реализовать:

```text
Wi-Fi AP или STA;
UDP listener;
приём PCM-пакетов;
проверку sequence;
вывод PCM через I2S.
```

Критерий:

```text
звук со смартфона воспроизводится через PCM5102A.
```

### Этап 3. DSP и кроссовер

Реализовать:

```text
master volume;
LPF для SUB;
HPF для LEFT/RIGHT;
delay lines.
```

Критерий:

```text
сабвуфер играет только низкие частоты.
```

### Этап 4. Беспроводная передача на сателлиты

Реализовать:

```text
ESP-NOW TX;
pairing;
packet id;
render timestamp;
jitter buffer на сателлитах.
```

Критерий:

```text
левый и правый сателлит воспроизводят свои каналы.
```

### Этап 5. Синхронизация

Реализовать:

```text
delay sub;
delay left;
delay right;
buffer level control;
drift compensation.
```

Критерий:

```text
звук сабвуфера и сателлитов совпадает по времени.
```

### Этап 6. Управление

Реализовать:

```text
OLED;
энкодер;
web UI;
REST API;
NVS storage.
```

---

## 19. Критерии приёмки

Проект считается адаптированным под ESP32-S3, если выполнены следующие условия.

### 19.1 Базовая работоспособность

```text
ESP32-S3 определяется корректно;
PSRAM доступна;
Wi-Fi работает;
UDP audio принимается;
I2S выводит звук.
```

### 19.2 Система 2.1

```text
смартфон передаёт аудио по Wi-Fi;
мастер разделяет сигнал;
сабвуфер воспроизводит НЧ;
левый сателлит воспроизводит левый канал;
правый сателлит воспроизводит правый канал.
```

### 19.3 Управление

```text
громкость регулируется;
задержки изменяются;
частота кроссовера изменяется;
настройки сохраняются после перезагрузки.
```

### 19.4 Стабильность

```text
воспроизведение без выраженных прерываний не менее 30 минут;
потери пакетов не приводят к зависанию;
heap/PSRAM не деградируют;
сателлиты автоматически восстанавливают соединение.
```

### 19.5 Задержка

```text
полная задержка тракта находится в пределах 50–150 ms;
программная регулировка 0–200 ms работает.
```

---

## 20. Риски и ограничения

| Риск | Влияние | Меры |
|---|---|---|
| ESP32-S3 не имеет Classic Bluetooth | невозможно использовать A2DP | использовать Wi-Fi audio |
| Нет готового Android-приложения | потребуется источник | кастомное приложение или внешний UDP/RTP source |
| Wi-Fi джиттер | прерывания звука | jitter buffer, фиксированный канал, отключение power save |
| ESP-NOW payload 250 bytes | высокая частота пакетов | компактные пакеты, опциональное сжатие |
| Общая радиосреда Wi-Fi и ESP-NOW | возможные потери | одинаковый канал, мониторинг качества связи |
| Ограничения SoftAP | нестабильность при многих клиентах | использовать не более 2 сателлитов и смартфона |
| Дрейф часов источника и мастера | рассинхронизация | clock recovery, адаптивный буфер |
| Отсутствие PSRAM на плате S3 | нехватка памяти | использовать только N16R8 или аналог с PSRAM |

---

## 21. Итоговая обязательная конфигурация

Для адаптации под ESP32-S3 принять следующую целевую схему:

```text
Master MCU:        ESP32-S3-N16R8
Source:            Android smartphone over Wi-Fi UDP
Transport phone:   Wi-Fi UDP PCM
Transport sat:     ESP-NOW primary, UDP fallback
Audio format:      PCM 16 bit, 48000 Hz, stereo
DSP:               crossover, volume, delay
Sub output:        local I2S → PCM5102A → TPA3116D2
Satellites:        ESP32-S3 + PCM5102A + class-D amplifier
Config storage:    NVS
Control:           OLED + encoder + Web + REST API
Bluetooth A2DP:    не используется
```