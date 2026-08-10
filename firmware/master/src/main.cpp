// main.cpp — мастер-узел Wireless Audio 2.1 (сабвуфер).
//
// Поток: A2DP (или Wi-Fi) → PCM int16 → DSP-конвейер (volume, tone, limiter,
// кроссовер) → 3 канала:
//   - left/right (HPF) → задержка → ESP-NOW/UDP TX на сателлиты
//   - sub (LPF, моно)  → задержка → I2S → ЦАП → усилитель сабвуфера
//
// Управление: serial-консоль (status/volume/crossover/delay/pair/save/reboot).
//
// Реализация MVP: A2DP-вход, DSP, кроссовер, задержки, ESP-NOW TX, локальный
// I2S-выход, serial-консоль. Web UI и OLED-меню — в следующей итерации.

#include <Arduino.h>
#include <WiFi.h>
#include <BluetoothA2DPSink.h>
#include <driver/i2s.h>
#include <esp_system.h>

#include "node_config.h"
#include "storage.h"
#include "logger.h"
#include "timing.h"
#include "pcm_pipeline.h"
#include "delay_line.h"
#include "espnow.h"
#include "udp.h"
#include "audio_packet.h"
#include "master_config.h"
#include "web_server.h"

using namespace audio21;

// ---------------------------------------------------------------------------
// Глобальное состояние
// ---------------------------------------------------------------------------

static NodeConfig g_cfg;
static PcmPipeline g_pipeline;
static DelayLine* g_delayLeft = nullptr;
static DelayLine* g_delayRight = nullptr;
static DelayLine* g_delaySub = nullptr;

static EspNowTransport g_espnow;
static UdpTransport g_udp;

static uint32_t g_packetId = 0;
static volatile bool g_leftOnline = false;
static volatile bool g_rightOnline = false;
static volatile bool g_a2dpConnected = false;

static BluetoothA2DPSink g_a2dp;

// Web UI + REST API (мастер). Ссылки на указатели задержек: объект создаётся
// до new DelayLine в setup(), поэтому храним ссылки на переменные-указатели.
static MasterWebServer g_webServer(g_cfg, g_pipeline,
                                   g_delayLeft, g_delayRight, g_delaySub,
                                   g_espnow,
                                   g_leftOnline, g_rightOnline, g_a2dpConnected);

// ---------------------------------------------------------------------------
// I2S выход сабвуфера (моно)
// ---------------------------------------------------------------------------
static i2s_port_t g_subPort = I2S_NUM_0;

static bool initI2SSub(const NodeConfig& cfg) {
    i2s_config_t conf = {};
    conf.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    conf.sample_rate = cfg.sampleRate;
    conf.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    conf.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; // моно-саб
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

    if (i2s_driver_install(g_subPort, &conf, 0, nullptr) != ESP_OK) return false;
    if (i2s_set_pin(g_subPort, &pins) != ESP_OK) return false;
    return true;
}

static void writeSubSample(int16_t sample) {
    int16_t frame[2] = {sample, sample};
    size_t written = 0;
    i2s_write(g_subPort, frame, sizeof(frame), &written, portMAX_DELAY);
}

// ---------------------------------------------------------------------------
// Отправка пакетов на сателлиты (батчами по kMaxInt16Samples)
// ---------------------------------------------------------------------------

static constexpr size_t kBatchSamples = kMaxInt16Samples; // 117 сэмплов/пакет
static int16_t g_leftBatch[kBatchSamples];
static int16_t g_rightBatch[kBatchSamples];
static size_t g_batchCount = 0;

static void flushSatelliteBatches(uint32_t now) {
    if (g_batchCount == 0) return;

    size_t n = g_batchCount;
    uint8_t buf[kMaxPacketSize];

    size_t szL = buildPacket(buf, sizeof(buf), kChannelLeft, kSampleFormatInt16,
                             g_leftBatch, n * sizeof(int16_t), now, g_packetId++);
    if (g_cfg.transport == TransportMode::EspNow) {
        g_espnow.sendTo(g_cfg.leftSatMac, buf, szL);
    } else {
        g_udp.broadcast(UdpTransport::kDefaultPort, buf, szL);
    }

    size_t szR = buildPacket(buf, sizeof(buf), kChannelRight, kSampleFormatInt16,
                             g_rightBatch, n * sizeof(int16_t), now, g_packetId++);
    if (g_cfg.transport == TransportMode::EspNow) {
        g_espnow.sendTo(g_cfg.rightSatMac, buf, szR);
    } else {
        g_udp.broadcast(UdpTransport::kDefaultPort, buf, szR);
    }

    g_batchCount = 0;
}

// ---------------------------------------------------------------------------
// A2DP data callback
// ---------------------------------------------------------------------------

static void a2dpDataCallback(const uint8_t* data, uint32_t samples) {
    const int16_t* pcm = reinterpret_cast<const int16_t*>(data);
    size_t frames = samples; // samples = число стерео-фреймов

    for (size_t i = 0; i < frames; i++) {
        int16_t l = pcm[i * 2];
        int16_t r = pcm[i * 2 + 1];

        PipelineOutput out = g_pipeline.process(l, r);

        g_leftBatch[g_batchCount] = g_delayLeft->process(static_cast<int16_t>(out.left * 32767.0f));
        g_rightBatch[g_batchCount] = g_delayRight->process(static_cast<int16_t>(out.right * 32767.0f));

        int16_t subOut = g_delaySub->process(static_cast<int16_t>(out.sub * 32767.0f));
        writeSubSample(subOut);

        g_batchCount++;
        if (g_batchCount >= kBatchSamples) flushSatelliteBatches(millis());
    }
}

static void a2dpConnectionState(esp_a2d_connection_state_t state, void*) {
    g_a2dpConnected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
    Logger::info("a2dp", g_a2dpConnected ? "connected" : "disconnected");
}

// ---------------------------------------------------------------------------
// Serial-консоль
// ---------------------------------------------------------------------------

static void handleConsoleCommand(const String& line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd == "status") {
        Serial.println("role: master");
        Serial.printf("source: %s\n", sourceToString(g_cfg.source));
        Serial.printf("connected: %s\n", g_a2dpConnected ? "yes" : "no");
        Serial.printf("transport: %s\n", transportToString(g_cfg.transport));
        Serial.printf("sample_rate: %lu\n", (unsigned long)g_cfg.sampleRate);
        Serial.printf("bits: %d\n", g_cfg.bitsPerSample);
        Serial.printf("channels: %d\n", g_cfg.channels);
        Serial.printf("crossover_hz: %d\n", g_cfg.crossoverHz);
        Serial.printf("delay_left_ms: %d\n", g_cfg.delayLeftMs);
        Serial.printf("delay_right_ms: %d\n", g_cfg.delayRightMs);
        Serial.printf("delay_sub_ms: %d\n", g_cfg.delaySubMs);
        Serial.printf("left_satellite: %s\n", g_leftOnline ? "online" : "offline");
        Serial.printf("right_satellite: %s\n", g_rightOnline ? "online" : "offline");
        return;
    }

    if (cmd.startsWith("volume")) {
        String arg = cmd.substring(7);
        arg.trim();
        if (arg == "mute") { g_cfg.mute = true; g_pipeline.setMute(true); Serial.println("ok"); }
        else if (arg == "unmute") { g_cfg.mute = false; g_pipeline.setMute(false); Serial.println("ok"); }
        else { int v = arg.toInt(); if (v >= 0 && v <= 100) { g_cfg.masterVolume = v; g_pipeline.setVolume(v); Serial.println("ok"); } else Serial.println("err"); }
        return;
    }

    if (cmd.startsWith("crossover")) {
        int hz = cmd.substring(9).toInt();
        if (hz >= kCrossoverMinHz && hz <= kCrossoverMaxHz) {
            g_cfg.crossoverHz = hz;
            g_pipeline.setCrossoverHz(hz);
            Serial.println("ok");
        } else Serial.println("err");
        return;
    }

    if (cmd.startsWith("delay")) {
        String rest = cmd.substring(6);
        rest.trim();
        int sp = rest.indexOf(' ');
        if (sp < 0) { Serial.println("err"); return; }
        String chan = rest.substring(0, sp);
        int ms = rest.substring(sp + 1).toInt();
        if (ms < kMinDelayMs || ms > kMaxDelayMs) { Serial.println("err"); return; }
        if (chan == "left") { g_cfg.delayLeftMs = ms; g_delayLeft->setDelayMs(ms); }
        else if (chan == "right") { g_cfg.delayRightMs = ms; g_delayRight->setDelayMs(ms); }
        else if (chan == "sub") { g_cfg.delaySubMs = ms; g_delaySub->setDelayMs(ms); }
        else { Serial.println("err"); return; }
        Serial.println("ok");
        return;
    }

    if (cmd.startsWith("transport")) {
        String t = cmd.substring(10);
        t.trim();
        if (t == "espnow") { g_cfg.transport = TransportMode::EspNow; Serial.println("ok"); }
        else if (t == "udp") { g_cfg.transport = TransportMode::Udp; Serial.println("ok"); }
        else Serial.println("err");
        return;
    }

    if (cmd.startsWith("pair")) {
        String rest = cmd.substring(5);
        rest.trim();
        int sp = rest.indexOf(' ');
        if (sp < 0) { Serial.println("err"); return; }
        String side = rest.substring(0, sp);
        String macStr = rest.substring(sp + 1);
        MacAddr mac;
        if (!MacAddr::parse(macStr.c_str(), mac)) { Serial.println("err"); return; }
        if (side == "left") { g_cfg.leftSatMac = mac; g_espnow.addPeer(mac); Serial.println("ok"); }
        else if (side == "right") { g_cfg.rightSatMac = mac; g_espnow.addPeer(mac); Serial.println("ok"); }
        else Serial.println("err");
        return;
    }

    if (cmd == "save") {
        ConfigStorage::save(g_cfg);
        Serial.println("ok");
        return;
    }

    if (cmd == "reboot") {
        Serial.println("rebooting");
        ESP.restart();
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
    Logger::info("master", "Wireless Audio 2.1 Master starting...");

    if (!ConfigStorage::load(g_cfg)) {
        Logger::warn("master", "No saved config, using defaults");
        g_cfg = defaultConfig();
        g_cfg.role = NodeRole::Master;
    }

    g_pipeline.configure(g_cfg.sampleRate);
    g_pipeline.setVolume(g_cfg.masterVolume);
    g_pipeline.setMute(g_cfg.mute);
    g_pipeline.setCrossoverHz(g_cfg.crossoverHz);

    g_delayLeft = new DelayLine(kMaxDelayMs, g_cfg.sampleRate);
    g_delayRight = new DelayLine(kMaxDelayMs, g_cfg.sampleRate);
    g_delaySub = new DelayLine(kMaxDelayMs, g_cfg.sampleRate);
    g_delayLeft->setDelayMs(g_cfg.delayLeftMs);
    g_delayRight->setDelayMs(g_cfg.delayRightMs);
    g_delaySub->setDelayMs(g_cfg.delaySubMs);

    if (!initI2SSub(g_cfg)) {
        Logger::error("master", "I2S sub init failed");
    } else {
        Logger::info("master", "I2S sub ready");
    }

    // Wi-Fi: нужен для Web UI; ESP-NOW работает в STA-режиме и без подключения
    // к точке доступа, поэтому подключаемся всегда (если сеть доступна).
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
    Logger::info("master", "Connecting Wi-Fi...");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);

    if (g_cfg.transport == TransportMode::EspNow) {
        if (g_espnow.begin()) {
            g_espnow.addPeer(g_cfg.leftSatMac);
            g_espnow.addPeer(g_cfg.rightSatMac);
            Logger::info("master", "ESP-NOW ready");
        } else {
            Logger::error("master", "ESP-NOW init failed");
        }
    } else {
        if (WiFi.status() == WL_CONNECTED) {
            g_udp.begin(UdpTransport::kDefaultPort);
            Logger::info("master", "UDP ready, IP: %s", WiFi.localIP().toString().c_str());
        } else {
            Logger::error("master", "Wi-Fi connect failed");
        }
    }

    // Web UI — только при подключённом Wi-Fi (нужен IP).
    if (WiFi.status() == WL_CONNECTED) {
        g_webServer.begin();
        Logger::info("master", "Web UI: http://%s", WiFi.localIP().toString().c_str());
    } else {
        Logger::warn("master", "Wi-Fi not connected, Web UI unavailable");
    }

    g_a2dp.set_auto_reconnect(true);
    // i2s_output=false: данные отдаются в наш callback, а не пишутся в I2S
    // библиотекой (I2S сабвуфера управляется вручную в a2dpDataCallback).
    g_a2dp.set_stream_reader(a2dpDataCallback, false);
    g_a2dp.set_on_connection_state_changed(a2dpConnectionState);
    g_a2dp.start("Audio21-Master");

    Logger::info("master", "Ready. Type 'status' for info.");
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        handleConsoleCommand(line);
    }

    // Web UI: обработка запросов и сохранение по кнопке.
    g_webServer.handleClient();
    if (g_webServer.saveRequested()) {
        ConfigStorage::save(g_cfg);
        g_webServer.clearSaveRequested();
        Logger::info("master", "config saved via Web UI");
    }

    delay(10);
}