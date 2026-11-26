// main.cpp — сателлит Wireless Audio 2.1 на ESP-IDF v5.3.1 (левый/правый).
//
// Поток: ESP-NOW RX → разбор пакета → jitter buffer → задержка → громкость → I2S.
// Сторона задаётся макросом AUDIO_SATELLITE_SIDE (0 = left, 1 = right).

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/uart.h"

#include "i2s_output.h"
#include "espnow_transport.h"
#include "nvs_config.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "delay_line.h"
#include "jitter_buffer.h"
#include "volume_control.h"
#include "drift_correction.h"

using namespace audio21;

#ifndef AUDIO_SATELLITE_SIDE
#define AUDIO_SATELLITE_SIDE 0
#endif

#if AUDIO_SATELLITE_SIDE == 1
static constexpr uint8_t kMyChannel = 0x02; // правый канал
#else
static constexpr uint8_t kMyChannel = 0x01; // левый канал
#endif

static constexpr uint32_t kSampleRate = 48000;
static constexpr uint32_t kJitterTargetMs = 20;
static constexpr uint8_t kEspNowChannel = AUDIO_ESPNOW_CHANNEL;
static constexpr uint32_t kHeartbeatIntervalMs = 2000;

static const char *TAG = "satellite";

// Конфигурация (NVS)
static NodeConfig g_cfg;
static NvsConfig g_nvs;

// Аудио-объекты
static DelayLine* g_delay = nullptr;
static JitterBuffer* g_jitter = nullptr;
static VolumeControl g_vol;
static DriftCorrector g_drift;
static I2sOutput g_i2sOut;
static bool g_i2sReady = false;

// ESP-NOW транспорт
static EspNowTransport g_espnow;
static bool g_masterOnline = false;
static uint32_t g_lastRxMs = 0;
static uint32_t g_packetsRx = 0;
static uint16_t g_lastSequence = 0;  // anti-replay: последний принятый sequence
static bool g_hasLastSequence = false;
static uint32_t g_lastHeartbeatMs = 0;
static uint32_t g_heartbeatsSent = 0;
static MacAddr g_masterMac;
static bool g_hasMasterMac = false;

// Коллбек приёма ESP-NOW (адаптер от C-стиля к объекту EspNowRxCallback).
static void onEspNowPacket(const uint8_t* srcMac, const uint8_t* data, size_t len);
class SatRxCallback : public EspNowRxCallback {
public:
    void onPacket(const uint8_t* srcMac, const uint8_t* data, size_t len) override {
        onEspNowPacket(srcMac, data, len);
    }
};
static SatRxCallback g_rxCb;

// A9: очередь RX ESP-NOW (коллбек только копирует, обработка в loop).
static constexpr size_t kRxPoolSize = 8;
struct RxPacket {
    uint16_t size;
    uint8_t srcMac[6];
    uint8_t data[250];
};
static QueueHandle_t g_rxQueue = nullptr;
static RxPacket g_rxPool[kRxPoolSize];
static uint8_t g_rxPoolIdx = 0;

// UDP сокет для приёма аудио (альтернатива ESP-NOW, порт 5004).
static int g_udpSock = -1;
static bool g_udpReady = false;
static uint32_t g_udpPacketsRx = 0;

// ---------------------------------------------------------------------------
// I2S вывод (моно, L=R)
// ---------------------------------------------------------------------------
static void writeSample(int16_t sample) {
    if (!g_i2sReady) return;
    g_i2sOut.write(&sample, 1, 100);
}

static bool initI2S() {
    I2sOutputPins pins = {(int)g_cfg.i2sBck, (int)g_cfg.i2sWs, (int)g_cfg.i2sDataOut};
    esp_err_t ret = g_i2sOut.init(pins, kSampleRate, /*mono=*/true);
    g_i2sReady = (ret == ESP_OK);
    if (g_i2sReady) {
        ESP_LOGI(TAG, "I2S ready (BCK=%d WS=%d DATA=%d)", pins.bck, pins.ws, pins.data);
    } else {
        ESP_LOGE(TAG, "I2S init failed");
    }
    return g_i2sReady;
}

// ---------------------------------------------------------------------------
// UDP аудио-приём (порт 5004, альтернатива ESP-NOW).
// ---------------------------------------------------------------------------
static bool initUdp() {
    struct sockaddr_in addr;
    g_udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udpSock < 0) {
        ESP_LOGE(TAG, "UDP socket create failed");
        return false;
    }

    // Non-blocking
    int flags = fcntl(g_udpSock, F_GETFL, 0);
    fcntl(g_udpSock, F_SETFL, flags | O_NONBLOCK);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(5004);

    if (bind(g_udpSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "UDP bind failed");
        close(g_udpSock);
        g_udpSock = -1;
        return false;
    }

    g_udpReady = true;
    ESP_LOGI(TAG, "UDP audio listener on port 5004");
    return true;
}

static void udpReceiveTick() {
    if (!g_udpReady) return;
    static int16_t buf[256];
    struct sockaddr_in src;
    socklen_t srcLen = sizeof(src);
    int n = recvfrom(g_udpSock, buf, sizeof(buf), 0, (struct sockaddr*)&src, &srcLen);
    if (n > 0 && n >= (int)sizeof(AudioPacketHeader)) {
        AudioPacketHeader hdr;
        memcpy(&hdr, buf, sizeof(AudioPacketHeader));
        const int16_t* payload = (const int16_t*)(buf + sizeof(AudioPacketHeader));
        size_t nSamples = (n - sizeof(AudioPacketHeader)) / sizeof(int16_t);

        // Проверяем канал.
        uint8_t channel = (hdr.magic == kMagicAudioL) ? 0x01 : 0x02;
        if (channel != kMyChannel) return;

        g_masterOnline = true;
        g_lastRxMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        g_packetsRx++;
        g_udpPacketsRx++;

        if (g_jitter) g_jitter->push(payload, nSamples);
    }
}

// ---------------------------------------------------------------------------
// ESP-NOW коллбек приёма — только копирует пакет в пул + очередь.
// ---------------------------------------------------------------------------
static void onEspNowPacket(const uint8_t* srcMac, const uint8_t* data, size_t len) {
    if (!g_rxQueue || len > sizeof(g_rxPool[0].data)) return;
    uint8_t idx = g_rxPoolIdx++;
    if (g_rxPoolIdx == kRxPoolSize) g_rxPoolIdx = 0;
    g_rxPool[idx].size = (uint16_t)len;
    memcpy(g_rxPool[idx].srcMac, srcMac, 6);
    memcpy(g_rxPool[idx].data, data, len);
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(g_rxQueue, &idx, &hpw);
    if (hpw) portYIELD_FROM_ISR();
}

// ---------------------------------------------------------------------------
// Обработка пакета (из loop, не из ISR). Формат: AudioPacketHeader (espnow_transport.h)
// + payload int16 PCM.
// ---------------------------------------------------------------------------
static void onPacketProcess(const uint8_t* srcMac, const uint8_t* data, size_t size) {
    if (size < sizeof(AudioPacketHeader)) return;

    AudioPacketHeader hdr;
    memcpy(&hdr, data, sizeof(AudioPacketHeader));
    const uint8_t* payload = data + sizeof(AudioPacketHeader);
    size_t payloadSize = size - sizeof(AudioPacketHeader);

    // Discovery-запрос мастера (kMagicDiscovery, broadcast) → запоминаем MAC, отвечаем unicast.
    if (hdr.magic == kMagicDiscovery) {
        memcpy(g_masterMac.bytes, srcMac, 6);
        g_hasMasterMac = true;
        // sendUnicast сама добавляет пира при первой отправке.
        ESP_LOGI(TAG, "master discovered: %02X:%02X:%02X:%02X:%02X:%02X",
                 (unsigned)g_masterMac.bytes[0], (unsigned)g_masterMac.bytes[1],
                 (unsigned)g_masterMac.bytes[2], (unsigned)g_masterMac.bytes[3],
                 (unsigned)g_masterMac.bytes[4], (unsigned)g_masterMac.bytes[5]);
        return;
    }

    // Аудио-пакет: только свой канал.
    uint8_t channel = (hdr.magic == kMagicAudioL) ? 0x01 : 0x02;
    if (channel != kMyChannel) return;

    // Anti-replay (P6): отклоняем пакеты с sequence <= последнего принятого.
    // Обработка wraparound: если разница > 32768, считаем что это новый цикл.
    if (g_hasLastSequence) {
        int16_t diff = (int16_t)(hdr.sequence - g_lastSequence);
        if (diff <= 0) {
            // Пакет устаревший или дубликат — отклоняем
            return;
        }
    }
    g_lastSequence = hdr.sequence;
    g_hasLastSequence = true;

    g_masterOnline = true;
    g_lastRxMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    g_packetsRx++;

    // C3.4: дрейф-коррекция по timestampMs мастера.
    if (hdr.timestampMs != 0) {
        int32_t suggested = g_drift.process(hdr.timestampMs, g_lastRxMs);
        if (suggested > 0) {
            g_jitter->setTargetMs((uint32_t)suggested, kSampleRate);
        }
    }

    size_t nSamples = payloadSize / sizeof(int16_t);
    g_jitter->push(reinterpret_cast<const int16_t*>(payload), nSamples);
}

// ---------------------------------------------------------------------------
// Heartbeat (discovery-response) мастеру.
// ---------------------------------------------------------------------------
static void sendHeartbeat() {
    uint8_t buf[16];
    AudioPacketHeader hdr = {};
    hdr.magic = kMagicDiscovery;
    hdr.sequence = 0;
    hdr.timestampMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    memcpy(buf, &hdr, sizeof(hdr));

    esp_err_t err;
    if (g_hasMasterMac) {
        err = g_espnow.sendUnicast(g_masterMac.bytes, buf, sizeof(AudioPacketHeader));
    } else {
        err = g_espnow.sendBroadcast(buf, sizeof(AudioPacketHeader));
    }
    if (err == ESP_OK) {
        g_heartbeatsSent++;
    } else if (err != ESP_ERR_ESPNOW_FULL) {
        ESP_LOGE(TAG, "heartbeat send failed: %s", esp_err_to_name(err));
    }
}

// ---------------------------------------------------------------------------
// Serial-консоль (упрощённая, UART0): status, delay <ms>.
// ---------------------------------------------------------------------------
static void consoleProcessLine(char* line) {
    char* end = line + strlen(line) - 1;
    while (end >= line && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
    char* start = line;
    while (*start == ' ') start++;
    if (start[0] == '\0') return;

    if (strcmp(start, "status") == 0) {
        printf("role: satellite (%s)\n", sideToString(g_cfg.side));
        printf("transport: espnow+udp\n");
        printf("master_online: %s\n", g_masterOnline ? "yes" : "no");
        printf("packets_rx: %lu (espnow) %lu (udp)\n", (unsigned long)g_packetsRx, (unsigned long)g_udpPacketsRx);
        printf("heartbeats_sent: %lu\n", (unsigned long)g_heartbeatsSent);
        printf("jitter_available: %u\n", (unsigned int)g_jitter->available());
        printf("delay_ms: %u\n", (unsigned int)g_delay->delayMs());
        printf("volume: %d\n", (g_cfg.side == SatelliteSide::Right) ? g_cfg.rightVolume : g_cfg.leftVolume);
        printf("drift_median_ms: %d\n", (int)g_drift.medianMs());
        if (g_hasMasterMac) {
            printf("master_mac: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   (unsigned)g_masterMac.bytes[0], (unsigned)g_masterMac.bytes[1],
                   (unsigned)g_masterMac.bytes[2], (unsigned)g_masterMac.bytes[3],
                   (unsigned)g_masterMac.bytes[4], (unsigned)g_masterMac.bytes[5]);
        } else {
            printf("master_mac: unknown\n");
        }
        printf("i2s: %s\n", g_i2sReady ? "ready" : "not ready");
        printf("udp: %s\n", g_udpReady ? "ready" : "not ready");
    } else if (strncmp(start, "delay", 5) == 0) {
        int ms = atoi(start + 5);
        if (ms >= 0 && ms <= 200) {
            if (g_cfg.side == SatelliteSide::Right) g_cfg.delayRightMs = ms;
            else g_cfg.delayLeftMs = ms;
            g_delay->setDelayMs((uint32_t)ms);
            printf("ok\n");
        } else {
            printf("err\n");
        }
    } else if (strcmp(start, "mute") == 0) {
        bool mute = !g_vol.isMuted();
        g_vol.setMute(mute);
        printf("mute: %s\n", mute ? "on" : "off");
    } else if (strcmp(start, "save") == 0) {
        esp_err_t ret = g_nvs.save(g_cfg);
        printf(ret == ESP_OK ? "saved to NVS\n" : "save failed\n");
    } else {
        printf("unknown\n");
    }
}

static void consoleTick() {
    static char buf[64];
    static int pos = 0;
    uint8_t c;
    int len = uart_read_bytes(UART_NUM_0, &c, 1, 0);
    if (len <= 0) return;
    if (c == '\r' || c == '\n') {
        buf[pos] = '\0';
        consoleProcessLine(buf);
        pos = 0;
        printf("> ");
    } else if (c >= 32 && c < 127 && pos < (int)sizeof(buf) - 1) {
        buf[pos++] = (char)c;
    } else if (c == 0x7F || c == 0x08) {
        if (pos > 0) pos--;
    }
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------
extern "C" void app_main(void) {
    // NVS конфигурация
    g_nvs.init();
    g_nvs.load(g_cfg);
    g_cfg.side = (AUDIO_SATELLITE_SIDE == 1) ? SatelliteSide::Right : SatelliteSide::Left;
    g_cfg.clamp();

    // Аудио-объекты
    g_delay = new DelayLine(200, kSampleRate);
    uint32_t startDelayMs = (g_cfg.side == SatelliteSide::Right)
                                ? static_cast<uint32_t>(g_cfg.delayRightMs)
                                : static_cast<uint32_t>(g_cfg.delayLeftMs);
    g_delay->setDelayMs(startDelayMs);

    int satVolume = (g_cfg.side == SatelliteSide::Right) ? g_cfg.rightVolume : g_cfg.leftVolume;
    g_vol.setVolume(satVolume);

    g_jitter = new JitterBuffer(kSampleRate / 20); // ~50 мс ёмкость
    g_jitter->setTargetMs(kJitterTargetMs, kSampleRate);
    g_drift.reset();

    // I2S
    initI2S();

    // UDP аудио-приём
    initUdp();

    // RX очередь
    g_rxQueue = xQueueCreate(kRxPoolSize, sizeof(uint8_t));
    if (!g_rxQueue) {
        ESP_LOGE(TAG, "rx queue create failed");
    }

    // Wi-Fi STA + ESP-NOW
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifiCfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifiCfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_channel(kEspNowChannel, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    esp_err_t ret = g_espnow.init(false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
    } else {
        g_espnow.setRxCallback(&g_rxCb);
        ESP_LOGI(TAG, "ESP-NOW ready on channel %u", (unsigned int)kEspNowChannel);
    }

    // UART консоль
    uart_config_t uartCfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM_0, &uartCfg);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);

    ESP_LOGI(TAG, "Wireless Audio 2.1 Satellite (%s) ready. Type 'status'.",
             sideToString(g_cfg.side));

    // Главный цикл ~100 Гц
    TickType_t lastWakeTime = xTaskGetTickCount();
    while (1) {
        uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // Heartbeat мастеру.
        if (nowMs - g_lastHeartbeatMs >= kHeartbeatIntervalMs) {
            g_lastHeartbeatMs = nowMs;
            sendHeartbeat();
        }

        // Приём UDP аудио.
        udpReceiveTick();

        // Drain RX очереди ESP-NOW.
        if (g_rxQueue) {
            uint8_t idx;
            while (xQueueReceive(g_rxQueue, &idx, 0) == pdTRUE) {
                onPacketProcess(g_rxPool[idx].srcMac, g_rxPool[idx].data, g_rxPool[idx].size);
            }
        }

        // Таймаут мастера.
        if (g_masterOnline && (nowMs - g_lastRxMs > 1000)) {
            g_masterOnline = false;
            ESP_LOGW(TAG, "master timeout");
        }

        // Аудио-вывод: джиттер → задержка → громкость → I2S.
        if (g_i2sReady) {
            static bool streaming = false;
            if (!streaming && g_jitter->ready()) streaming = true;
            int16_t sample = 0;
            if (streaming && g_jitter->pop(sample)) {
                sample = g_delay->process(sample);
                sample = g_vol.process(sample);
                writeSample(sample);
            } else if (g_jitter->available() == 0) {
                streaming = false;
            }
        }

        consoleTick();

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10));
    }
}
