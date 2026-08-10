// main.cpp — сателлит Wireless Audio 2.1 (левый или правый).
//
// Поток: ESP-NOW/UDP RX → разбор пакета → jitter buffer → задержка → I2S → ЦАП.
// Сторона (left/right) задаётся define AUDIO_SATELLITE_SIDE=LEFT/RIGHT.
//
// Реализация MVP: приём от мастера, jitter buffer, задержка, I2S-вывод,
// serial-консоль (status/delay/save). Web UI и OLED — в следующей итерации.

#include <Arduino.h>
#include <WiFi.h>
#include <driver/i2s.h>
#include <esp_system.h>

#include "node_config.h"
#include "storage.h"
#include "logger.h"
#include "timing.h"
#include "delay_line.h"
#include "jitter_buffer.h"
#include "espnow.h"
#include "udp.h"
#include "audio_packet.h"
#include "satellite_config.h"

using namespace audio21;

// ---------------------------------------------------------------------------
// Глобальное состояние
// ---------------------------------------------------------------------------

static NodeConfig g_cfg;
static DelayLine* g_delay = nullptr;
static JitterBuffer* g_jitter = nullptr;

static EspNowTransport g_espnow;
static UdpTransport g_udp;

static volatile bool g_masterOnline = false;
static uint32_t g_lastRxMs = 0;
static uint32_t g_packetsRx = 0;

// Сторона сателлита: 0 = left, 1 = right (задаётся -DAUDIO_SATELLITE_SIDE).
#ifndef AUDIO_SATELLITE_SIDE
#define AUDIO_SATELLITE_SIDE 0
#endif

#if AUDIO_SATELLITE_SIDE == 1
static constexpr uint8_t kMyChannel = kChannelRight;
#else
static constexpr uint8_t kMyChannel = kChannelLeft;
#endif

// ---------------------------------------------------------------------------
// I2S выход сателлита (моно)
// ---------------------------------------------------------------------------
static i2s_port_t g_outPort = I2S_NUM_0;

static bool initI2S(const NodeConfig& cfg) {
    i2s_config_t conf = {};
    conf.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    conf.sample_rate = cfg.sampleRate;
    conf.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    conf.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; // моно-сателлит
    conf.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    conf.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    conf.dma_buf_count = 8;
    conf.dma_buf_len = 256;
    conf.use_apll = false;
    conf.tx_desc_auto_clear = true;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = cfg.i2sBck;
    pins.ws_io_num = cfg.i2sWs;
    pins.data_out_num = cfg.i2sDataOut;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    if (i2s_driver_install(g_outPort, &conf, 0, nullptr) != ESP_OK) return false;
    if (i2s_set_pin(g_outPort, &pins) != ESP_OK) return false;
    return true;
}

static void writeSample(int16_t sample) {
    int16_t frame[2] = {sample, sample};
    size_t written = 0;
    i2s_write(g_outPort, frame, sizeof(frame), &written, portMAX_DELAY);
}

// ---------------------------------------------------------------------------
// Приём пакетов от мастера
// ---------------------------------------------------------------------------

static void onPacket(const uint8_t* data, size_t size) {
    AudioPacketHeader hdr;
    const uint8_t* payload;
    size_t payloadSize;
    if (!parsePacket(data, size, hdr, payload, payloadSize)) return;

    // Игнорируем пакеты не нашего канала.
    if (hdr.channel != kMyChannel) return;

    g_masterOnline = true;
    g_lastRxMs = millis();
    g_packetsRx++;

    // payloadSize кратен 2 (int16). Кладём в jitter buffer.
    size_t nSamples = payloadSize / sizeof(int16_t);
    g_jitter->push(reinterpret_cast<const int16_t*>(payload), nSamples);
}

// ---------------------------------------------------------------------------
// Serial-консоль
// ---------------------------------------------------------------------------

static void handleConsoleCommand(const String& line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd == "status") {
        Serial.printf("role: satellite (%s)\n", sideToString(g_cfg.side));
        Serial.printf("transport: %s\n", transportToString(g_cfg.transport));
        Serial.printf("master_online: %s\n", g_masterOnline ? "yes" : "no");
        Serial.printf("packets_rx: %lu\n", (unsigned long)g_packetsRx);
        Serial.printf("jitter_available: %u\n", g_jitter->available());
        Serial.printf("delay_ms: %u\n", g_delay->delayMs());
        return;
    }

    if (cmd.startsWith("delay")) {
        int ms = cmd.substring(6).toInt();
        if (ms >= kMinDelayMs && ms <= kMaxDelayMs) {
            // Храним задержку в поле, соответствующем стороне сателлита.
            if (g_cfg.side == SatelliteSide::Right) g_cfg.delayRightMs = ms;
            else g_cfg.delayLeftMs = ms;
            g_delay->setDelayMs(ms);
            Serial.println("ok");
        } else Serial.println("err");
        return;
    }

    if (cmd == "save") {
        ConfigStorage::save(g_cfg);
        Serial.println("ok");
        return;
    }

    Serial.println("unknown");
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(200);
    Logger::info("satellite", "Wireless Audio 2.1 Satellite starting...");

    if (!ConfigStorage::load(g_cfg)) {
        Logger::warn("satellite", "No saved config, using defaults");
        g_cfg = defaultConfig();
        g_cfg.role = NodeRole::Satellite;
    }
    g_cfg.side = (AUDIO_SATELLITE_SIDE == 1) ? SatelliteSide::Right : SatelliteSide::Left;

    g_delay = new DelayLine(kMaxDelayMs, g_cfg.sampleRate);
    // Стартовая задержка из поля, соответствующего стороне сателлита.
    uint32_t startDelayMs = (g_cfg.side == SatelliteSide::Right)
                                ? static_cast<uint32_t>(g_cfg.delayRightMs)
                                : static_cast<uint32_t>(g_cfg.delayLeftMs);
    g_delay->setDelayMs(startDelayMs);

    g_jitter = new JitterBuffer(g_cfg.sampleRate / 20); // ~50 мс ёмкость
    g_jitter->setTargetMs(15, g_cfg.sampleRate);        // ~15 мс целевой уровень

    if (!initI2S(g_cfg)) {
        Logger::error("satellite", "I2S init failed");
    } else {
        Logger::info("satellite", "I2S ready");
    }

    if (g_cfg.transport == TransportMode::EspNow) {
        WiFi.mode(WIFI_STA);
        if (g_espnow.begin()) {
            g_espnow.setRxCallback([](const uint8_t* d, size_t s, const MacAddr&) {
                onPacket(d, s);
            });
            Logger::info("satellite", "ESP-NOW ready");
        } else {
            Logger::error("satellite", "ESP-NOW init failed");
        }
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
        Logger::info("satellite", "Connecting Wi-Fi...");
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
        if (WiFi.status() == WL_CONNECTED) {
            g_udp.begin(UdpTransport::kDefaultPort);
            g_udp.setMyChannel(kMyChannel);
            Logger::info("satellite", "Wi-Fi connected, IP: %s", WiFi.localIP().toString().c_str());
        } else {
            Logger::error("satellite", "Wi-Fi connect failed");
        }
    }

    Logger::info("satellite", "Ready. Type 'status' for info.");
}

void loop() {
    // UDP-режим: опрашиваем сокет. Discovery-запросы мастера обрабатываем
    // отдельно (ответ unicast-ом), аудио — через onPacket.
    if (g_cfg.transport == TransportMode::Udp) {
        uint8_t buf[kMaxPacketSize];
        size_t n = g_udp.receive(buf, sizeof(buf));
        if (n > 0 && !g_udp.handleDiscovery(buf, n, g_udp.lastFrom())) {
            onPacket(buf, n);
        }
    }

    // Следим за таймаутом мастера.
    if (g_masterOnline && (millis() - g_lastRxMs > 1000)) {
        g_masterOnline = false;
        Logger::warn("satellite", "master timeout");
    }

    // Выдаём поток из jitter buffer в I2S.
    int16_t sample;
    if (g_jitter->pop(sample)) {
        int16_t out = g_delay->process(sample);
        writeSample(out);
    }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        handleConsoleCommand(line);
    }

    delay(1);
}