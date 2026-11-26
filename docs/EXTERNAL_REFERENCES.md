# Готовые примеры кода и конфигураций для быстрого старта

> Версия: 1.0 (22.11.2026)
> Назначение: копируемые фрагменты кода и конфигурации для интеграции рекомендаций
> из `docs/INTEGRATION_RECOMMENDATIONS.md` в проект Wireless Audio 2.1.

---

## 1. Конфигурация

### 1.1 config.env (расширенный с ключом аутентификации UDP)

```env
# === Роль узла ===
AUDIO_NODE_ROLE=MASTER

# === Источник аудио ===
AUDIO_SOURCE_MODE=WIFI
AUDIO_TRANSPORT_MODE=UDP

# === Wi-Fi ===
AUDIO_WIFI_MODE=APSTA
AUDIO_WIFI_AP_SSID=Audio21-Master
AUDIO_WIFI_AP_PASSWORD=change-me-first-run
AUDIO_WIFI_SSID=Your-Home-Network
AUDIO_WIFI_PASSWORD=Your-WiFi-Password
AUDIO_HOSTNAME=audio-master

# === ESP-NOW ключи ===
AUDIO_ESPNOW_PMK=audio21-espnow-1
AUDIO_ESPNOW_LMK=audio21-espnow-2

# === Аутентификация UDP-аудио (V4) ===
# Ключ для HMAC-SHA256 проверки источника (минимум 32 символа)
# Если пусто — принимаются пакеты 0xA210 без проверки (небезопасно)
AUDIO_UDP_AUTH_KEY=change-me-32-chars-min-at-least

# === UDP аудио порт ===
AUDIO_UDP_PORT=5004

# === MAC-адреса сателлитов ===
AUDIO_LEFT_SAT_MAC=AA:BB:CC:DD:EE:01
AUDIO_RIGHT_SAT_MAC=0a:1b:2c:3d:4e:5f

# === Аудио параметры ===
AUDIO_SAMPLE_RATE=48000
AUDIO_BITS_PER_SAMPLE=16
AUDIO_CHANNELS=2
AUDIO_CROSSOVER_HZ=90

# === Задержки каналов (0–200 мс) ===
AUDIO_DELAY_LEFT_MS=0
AUDIO_DELAY_RIGHT_MS=0
AUDIO_DELAY_SUB_MS=0

# === GPIO (ESP32-S3, валидные пины) ===
AUDIO_I2S_BCK=5
AUDIO_I2S_WS=4
AUDIO_I2S_DATA_OUT=6
AUDIO_OLED_SDA=21
AUDIO_OLED_SCL=17
AUDIO_ENC_A=32
AUDIO_ENC_B=33
AUDIO_ENC_BTN=34

# === ESP-NOW канал ===
AUDIO_ESPNOW_CHANNEL=6
```

### 1.2 platformio.ini (с точными версиями зависимостей)

```ini
[platformio]
default_envs = master_s3_wifi, satellite_s3_left, satellite_s3_right

[env:master_s3_wifi]
platform = pioarduino@55.03.311
board = esp32-s3-devkitc-1
framework = arduino
core_dir = .pio-core-master
build_flags = 
    -I${PROJECT_DIR}/firmware/common
    -DAUDIO_NODE_ROLE=MASTER
    -DAUDIO_SOURCE_MODE=WIFI
    -DAUDIO_TRANSPORT_MODE=UDP
lib_deps = 
    bblanchon/ArduinoJson@7.4.3
    olikraus/U8g2@2.35.15
    mathertel/RotaryEncoder@1.1.4
    # Monocypher для Ed25519 подписи OTA (V5)
    # https://github.com/loupvaillant/Monocypher

[env:satellite_s3_left]
platform = pioarduino@55.03.311
board = esp32-s3-devkitc-1
framework = arduino
core_dir = .pio-core-master
build_flags = 
    -I${PROJECT_DIR}/firmware/common
    -DAUDIO_NODE_ROLE=SATELLITE
    -DAUDIO_SATELLITE_SIDE=0
lib_deps = 
    bblanchon/ArduinoJson@7.4.3

[env:satellite_s3_right]
platform = pioarduino@55.03.311
board = esp32-s3-devkitc-1
framework = arduino
core_dir = .pio-core-master
build_flags = 
    -I${PROJECT_DIR}/firmware/common
    -DAUDIO_NODE_ROLE=SATELLITE
    -DAUDIO_SATELLITE_SIDE=1
lib_deps = 
    bblanchon/ArduinoJson@7.4.3
```

---

## 2. V4 — Аутентификация UDP-источника

### 2.1 Дополнение в `udp_audio_packet.h`

```cpp
// firmware/common/transport/udp_audio_packet.h — добавление в существующий файл
#include "auth.h"

struct UdpAudioPacket {
    static constexpr uint16_t MAGIC_AUTH = 0xA211;
    static constexpr uint16_t MAGIC_NOAUTH = 0xA210;
    
    // Проверка HMAC-SHA256 для аутентифицированных пакетов
    // data — весь пакет включая заголовок, len — общая длина
    // Последние 8 байт payload — это обрезанный HMAC-SHA256
    static bool verifyHmac(const uint8_t* data, size_t len, const char* key) {
        if (!key || !*key) return false; // ключ не настроен
        if (len < 18 + 8) return false;  // минимум заголовок + 8 байт HMAC
        
        size_t payloadLen = len - 18 - 8; // без заголовка и без HMAC
        const uint8_t* payload = data + 18;
        const uint8_t* receivedHmac = data + 18 + payloadLen;
        
        uint8_t computedHmac[32];
        hmacSha256((const uint8_t*)key, strlen(key), payload, payloadLen, computedHmac);
        
        // Сравниваем первые 8 байт (как в протоколе)
        return memcmp(computedHmac, receivedHmac, 8) == 0;
    }
};
```

### 2.2 Интеграция в `master_s3/src/main.cpp` (обработчик UDP RX)

```cpp
// В функции parseUdpPacket или там, где обрабатывается входящий UDP-пакет:
if (header.magic == UdpAudioPacket::MAGIC_AUTH) {
    if (!UdpAudioPacket::verifyHmac(packetData, packetLen, AUDIO_UDP_AUTH_KEY)) {
        logWarn("UDP: HMAC verification failed, dropping packet from %s",
                remoteIP.toString().c_str());
        g_packetsDroppedAuth++;
        return; // отбрасываем пакет
    }
    g_packetsRxAuth++;
} else if (header.magic == UdpAudioPacket::MAGIC_NOAUTH) {
    if (AUDIO_UDP_AUTH_KEY && *AUDIO_UDP_AUTH_KEY) {
        logWarn("UDP: unauthenticated packet (0xA210) from %s, key configured",
                remoteIP.toString().c_str());
        g_packetsDroppedNoAuth++;
        return;
    }
    logWarn("UDP: unauthenticated packet (0xA210), no key configured");
}
```

---

## 3. V5 — Подпись OTA (SimpleOTA Ed25519)

### 3.1 CI — подпись прошивки (GitHub Actions шаг)

```yaml
- name: Sign firmware (Ed25519)
  run: |
    # Генерируем ключ (в production — использовать существующий ключ из secrets)
    openssl genpkey -algorithm ED25519 -out signing_key.pem
    openssl pkey -in signing_key.pem -pubout -out signing_key_pub.pem
    
    # Подписываем прошивку
    openssl dgst -sha256 -sign signing_key.pem -out firmware.bin.sig firmware.bin
    
    # Загружаем артефакты
    echo "SIGNATURE=$(base64 -w0 firmware.bin.sig)" >> $GITHUB_OUTPUT
    echo "PUBLIC_KEY=$(base64 -w0 signing_key_pub.pem)" >> $GITHUB_OUTPUT
```

### 3.2 Проверка подписи на устройстве (в `web_server.cpp`)

```cpp
#include <monocypher.h>

// Пinned public key (в production — из NVS, здесь пример из конфига)
static const uint8_t kOtaPublicKey[32] = {
    0x?? // заполнить из signing_key_pub.pem
};

bool verifyOtaSignature(const uint8_t* image, size_t imageLen,
                        const uint8_t* signature, size_t sigLen) {
    if (sigLen != 64) return false; // Ed25519 signature = 64 bytes
    
    crypto_ed25519_context ctx;
    crypto_ed25519_publickey_from(ctx, kOtaPublicKey);
    return crypto_ed25519_check(ctx, signature, image, imageLen) == 0;
}

// В handleUpdateUpload:
void handleUpdateUpload(AsyncWebServerRequest* request, String filename,
                        size_t index, uint8_t* data, size_t len, bool final) {
    if (!isAuthed(request) || !csrfOk(request)) {
        request->send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}");
        return;
    }
    
    if (!index) {
        // Начало загрузки
        g_otaImageBuf = (uint8_t*)ps_malloc(UPDATE_SIZE_UNKNOWN ? 0 : len);
        g_otaImageLen = 0;
    }
    
    // Аккумулируем образ в буфере (или пишем напрямую во flash с подписью после)
    memcpy(g_otaImageBuf + g_otaImageLen, data, len);
    g_otaImageLen += len;
    
    if (final) {
        // Получаем подпись из заголовка запроса
        String sigHeader = request->getHeader("X-OTA-Signature");
        if (sigHeader.length() != 128) { // hex 64 байт
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing signature\"}");
            return;
        }
        
        // Декодируем hex подпись
        uint8_t signature[64];
        hexDecode(sigHeader.c_str(), signature, 64);
        
        // Проверяем подпись
        if (!verifyOtaSignature(g_otaImageBuf, g_otaImageLen, signature, 64)) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid signature\"}");
            return;
        }
        
        // Подпись верна — прошиваем
        if (!Update.begin(g_otaImageLen)) {
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"update begin failed\"}");
            return;
        }
        Update.write(g_otaImageBuf, g_otaImageLen);
        if (!Update.end(true)) {
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"update end failed\"}");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true}");
        ESP.restart();
    }
}
```

### 3.3 Генерация ключей (одноразово, локально)

```bash
# Генерация пары ключей Ed25519
openssl genpkey -algorithm ED25519 -out ota_signing_key.pem
openssl pkey -in ota_signing_key.pem -pubout -out ota_signing_key_pub.pem

# Публичный ключ в hex для вставки в код
xxd -p ota_signing_key_pub.pem | tr -d '\n' | head -c 64
```

---

## 4. C3.4 — Дрейф-коррекция сателлита

### 4.1 Интеграция в `satellite/src/main.cpp`

```cpp
#include "common/audio/drift_correction.h"

// Глобальные объекты
static DriftCorrector g_driftCorrector;
static uint32_t g_lastPacketTimestampMs = 0;
static bool g_streaming = false;

// При приёме пакета (в parsePacket или обработчике ESP-NOW/UDP):
void onAudioPacketReceived(const AudioPacketHeader* header, const uint8_t* payload, size_t payloadLen) {
    if (header->channel != g_cfg.satelliteSide) return; // не наш канал
    
    g_lastPacketTimestampMs = header->renderTimestampMs;
    g_streaming = true;
    
    // Дрейф-коррекция
    int32_t driftMs = g_driftCorrector.process(header->renderTimestampMs, millis());
    g_jitterBuffer->setTargetMs(30 + driftMs); // базовый 30 мс + коррекция
    
    // Помещаем в jitter buffer
    g_jitterBuffer->push(payload, payloadLen);
}

// В loop() — воспроизведение:
void loop() {
    if (!g_streaming || !g_jitterBuffer->ready()) {
        // Ждём накопления буфера — тишина
        i2sWriteSilence();
        return;
    }
    
    // Вычисляем время начала воспроизведения пакета (PTP-подобная синхронизация)
    uint32_t playAtMs = g_lastPacketTimestampMs + g_jitterBuffer->getTargetMs();
    
    if (millis() >= playAtMs) {
        int16_t sample = g_jitterBuffer->pop();
        i2sWriteSample(sample);
    }
    
    // Периодическая проверка дрейфа (каждые 1000 мс)
    static uint32_t lastDriftCheck = 0;
    if (millis() - lastDriftCheck > 1000) {
        lastDriftCheck = millis();
        int32_t driftMs = g_driftCorrector.medianMs();
        logInfo("drift: %d ms, jitter target: %d ms", driftMs, g_jitterBuffer->getTargetMs());
    }
}
```

### 4.2 Host-тест для дрейф-коррекции (уже существует)

`test/drift_correction_test.cpp` — уже в репозитории, проходит.

---

## 5. A1 — Дekomпозиция web_server.h (структура)

### 5.1 Предлагаемая структура файлов

```
firmware/common/web/
├── web_server.h          # Объявления MasterWebServer, AudioContext
├── web_server.cpp        # Основной класс, маршрутизация
├── web_auth.cpp          # Аутентификация, сессии, CSRF
├── web_api.cpp           # REST API хендлеры (volume, crossover, delay, etc.)
├── web_ota.cpp           # OTA-обработчики
├── web_wifi.cpp          # Wi-Fi профили, скан, подключение
├── web_logs.cpp          # Логи, диагностика
├── web_spa.h             # SPA HTML/CSS/JS (PROGMEM)
├── auth.h                # SHA-256, HMAC, PBKDF2, сессии, CSRF (без изменений)
├── wifi_store.h          # Wi-Fi профили в NVS (без изменений)
├── logs.h                # Кольцевой буфер логов (без изменений)
└── internet_check.h      # Проверка доступа в интернет (без изменений)
```

### 5.2 Struct AudioContext (ISP)

```cpp
// Вместо 9 параметров конструктора
struct AudioContext {
    PcmPipeline* pipeline = nullptr;
    DelayLine* delayLeft = nullptr;
    DelayLine* delayRight = nullptr;
    DelayLine* delaySub = nullptr;
    // можно добавить: VolumeControl*, ITransport*, etc.
};

// Конструктор принимает один объект
MasterWebServer(AudioContext ctx);
```

---

## 6. SSE endpoint для статуса сателлитов

### 6.1 Серверная часть (в `web_server.cpp`)

```cpp
server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->setContentType("text/event-stream");
    request->setCacheControl("no-cache");
    request->setHeader("Connection", "keep-alive");
    request->setHeader("Access-Control-Allow-Origin", "*");
    
    auto response = request->beginResponseStream("text/event-stream");
    
    // Отправляем текущий статус
    response->printf("data: {\"clients\":%d,\"uptime\":%lu,\"heap\":%lu}\n\n",
                     getSatelliteClientCount(), millis(), ESP.getFreeHeap());
    
    response->send();
    
    // В реальной реализации — регистрировать callback для push-уведомлений
    // при изменении статуса сателлитов
});
```

### 6.2 Клиентская часть (JavaScript в SPA)

```javascript
const evtSource = new EventSource('/api/events');
evtSource.onmessage = (e) => {
    const data = JSON.parse(e.data);
    updateSatelliteStatus(data.clients);
    updateHeapIndicator(data.heap);
};
evtSource.onerror = () => {
    evtSource.close();
    setTimeout(() => {
        // Переподключение через 5 секунд
        location.reload();
    }, 5000);
};
```

---

## 7. CI/CD workflow (активированный)

### 7.1 `.github/workflows/ci.yml` (полная версия)

```yaml
---
name: CI

on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: ci-${{ github.ref }}
  cancel-in-progress: true

jobs:
  build-and-test:
    runs-on: ubuntu-24.04
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262

      - uses: actions/setup-python@a26af69be951a213d495a4c3e4e4022e16d87065
        with:
          python-version: '3.12'

      - name: Install PlatformIO
        run: pipx install platformio==6.1.19

      - name: Cache PlatformIO packages
        uses: actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830
        with:
          path: |
            ~/.platformio/.cache
            ~/.platformio/packages
            ~/.platformio/platforms
            .pio-core-master
          key: pio-${{ runner.os }}-${{ hashFiles('platformio.ini', 'platformio.master.ini') }}

      - name: Generate config
        run: |
          cp config.example.env config.env
          python3 scripts/generate_config.py config.env

      - name: Host tests (gcc, без железа)
        run: make -C test test

      - name: Build master (ESP32-S3, Wi-Fi UDP, pioarduino core 3.x)
        run: pio run -c platformio.master.ini

      - name: Build satellites (ESP32-S3)
        run: |
          pio run -e satellite_s3_left
          pio run -e satellite_s3_right

      - name: Upload build artifacts
        uses: actions/upload-artifact@v4
        if: always()
        with:
          name: firmware-binaries
          path: |
            .pio/build/**/firmware.bin
          retention-days: 7
```

---

## 8. I2S конфигурация для PCM5102A (ESP32-S3)

### 8.1 Рекомендуемые значения (в `i2s_output.h`)

```cpp
#define I2S_DMA_DESC_NUM 16
#define I2S_DMA_FRAME_NUM 512
#define I2S_SAMPLE_RATE 48000
#define I2S_BITS_PER_SAMPLE 16
#define I2S_CHANNELS 2

// GPIO (ESP32-S3 DevKitC-1 + PCM5102A)
#define I2S_BCK_GPIO 5
#define I2S_WS_GPIO 4
#define I2S_DATA_GPIO 6
```

### 8.2 Аппаратные рекомендации

| Компонент | Рекомендация |
|---|---|
| Резисторы на линиях I2S | 33–100Ω последовательные на BCK, WS, DIN |
| Конденсаторы питания PCM5102A | 10μF + 0.1μF |
| PCM5102A перемычки | FLT=GND, DEMP=GND, XSMT=3.3V, FMT=GND |
| Питание | 3.3V от ESP32-S3 (PCM5102A работает от 3.3V) |

---

## 9. Ссылки на внешние ресурсы

| Категория | Ресурс | URL |
|---|---|---|
| ESP32-S3 и A2DP | ESP-IDF issue #16232 | https://github.com/espressif/esp-idf/issues/16232 |
| ESP-NOW документация | ESP-IDF Programming Guide | https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html |
| Secure Boot v2 | ESP-IDF Security | https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/secure-boot-v2.html |
| SimpleOTA | Ed25519 подпись | https://simpleota.com/blog/introducing-signed-firmware |
| Monocypher | Криптобиблиотека | https://github.com/loupvaillant/Monocypher |
| Linkwitz-Riley теория | RaneNote 107/160 | https://www.ranecommercial.com/legacy/note160.html |
| SOF Crossover | Документация | https://thesofproject.github.io/latest/developer_guides/tuning/crossover_tuning.html |
| arduino-audio-tools | ESP-NOW Stream | https://github.com/pschatzmann/arduino-audio-tools |
| ESP-NOW Audio (IMA ADPCM) | WillyBilly06 | https://github.com/WillyBilly06/esp-now-audio-source |
| PlatformIO CI | Документация | https://docs.platformio.org/en/stable/integration/ci/github-actions.html |
| PCM5102A + ESP32-S3 | king2552/esp32-speaker | https://github.com/king2552/esp32-speaker |
| SonicStream (мультирум) | chagoguila/SonicStream | https://github.com/chagoguila/SonicStream |
| ESP32 AirPlay 2 | Kristian8606 | https://github.com/Kristian8606/airplay-esp32 |
