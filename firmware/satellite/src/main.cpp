// main.cpp — сателлит Wireless Audio 2.1 (левый или правый).
//
// Поток: ESP-NOW/UDP RX → разбор пакета → jitter buffer → задержка → I2S → ЦАП.
// Сторона (left/right) задаётся define AUDIO_SATELLITE_SIDE=LEFT/RIGHT.
//
// Реализация MVP: приём от мастера, jitter buffer, задержка, I2S-вывод,
// serial-консоль (status/delay/save). Web UI и OLED — в следующей итерации.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/queue.h>

#include "node_config.h"
#include "storage.h"
#include "logger.h"
#include "timing.h"
#include "delay_line.h"
#include "jitter_buffer.h"
#include "volume_control.h"
#include "drift_correction.h"
#include "espnow.h"
#include "udp_transport.h"
#include "audio_packet.h"
#include "i2s_output.h"
#include "console.h"

using namespace audio21;

// ---------------------------------------------------------------------------
// Глобальное состояние
// ---------------------------------------------------------------------------

static NodeConfig g_cfg;
static DelayLine* g_delay = nullptr;
static JitterBuffer* g_jitter = nullptr;
static VolumeControl g_vol; // C3.5: громкость сателлита (§8.6) + fade-in/out

static EspNowTransport g_espnow;
static UdpTransport g_udp;

static volatile bool g_masterOnline = false;
static uint32_t g_lastRxMs = 0;
static uint32_t g_packetsRx = 0;
static uint32_t g_lastHeartbeatMs = 0;
static uint32_t g_heartbeatsSent = 0;

// C3.4: дрейф-коррекция по timestampMs из пакетов мастера.
static DriftCorrector g_drift;

// A9: очередь FreeRTOS для RX ESP-NOW — коллбек только копирует пакет
// в пул и ставит индекс в очередь; обработка (parsePacket + jitter push)
// происходит в loop, чтобы не держать Wi-Fi task.
static constexpr size_t kRxPoolSize = 8;
struct RxPacket {
    uint16_t size;
    uint8_t data[250];
};
static QueueHandle_t g_rxQueue = nullptr;
static RxPacket g_rxPool[kRxPoolSize];
static uint8_t g_rxPoolIdx = 0;

// Период heartbeat (discovery-response) мастеру — статус online даже без аудио.
// Общая константа в audio_packet.h (интервал < таймаут мастера).

// Сторона сателлита: 0 = left, 1 = right (задаётся -DAUDIO_SATELLITE_SIDE).
#ifndef AUDIO_SATELLITE_SIDE
#define AUDIO_SATELLITE_SIDE 0
#endif

#if AUDIO_SATELLITE_SIDE == 1
static constexpr uint8_t kMyChannel = kChannelRight;
#else
static constexpr uint8_t kMyChannel = kChannelLeft;
#endif

// RF-канал приёма: сателлит работает без ассоциации с AP (ESP-NOW STA),
// Wi-Fi по умолчанию встаёт на канал 1 — переводим на канал мастера
// (AUDIO_ESPNOW_CHANNEL из config.env).
static constexpr uint8_t kEspNowChannel = AUDIO_ESPNOW_CHANNEL;

// ---------------------------------------------------------------------------
// I2S выход сателлита (моно, L=R)
// ---------------------------------------------------------------------------
static I2sOutput g_i2sOut;
static bool g_i2sReady = false; // guard: не писать в неинициализированный I2S (B14)

static bool initI2S(const NodeConfig& cfg) {
    I2sOutputPins pins = {(int)cfg.i2sBck, (int)cfg.i2sWs, (int)cfg.i2sDataOut};
    bool ok = g_i2sOut.init(pins, cfg.sampleRate, /*mono=*/true);
    g_i2sReady = ok;
    return ok;
}

static void writeSample(int16_t sample) {
    if (!g_i2sReady) return; // B14: риск зависания i2s_write при провале init
    g_i2sOut.write(&sample, 1);
}

// ---------------------------------------------------------------------------
// A9: коллбек ESP-NOW — только копирует пакет в пул и ставит индекс в очередь.
// Обработка (parsePacket + jitter push) происходит в loop, чтобы не держать
// Wi-Fi task.
static void onPacketFromIsr(const uint8_t* data, size_t size) {
    if (!g_rxQueue || size > sizeof(g_rxPool[0].data)) return;
    uint8_t idx = g_rxPoolIdx++;
    if (g_rxPoolIdx == kRxPoolSize) g_rxPoolIdx = 0;
    g_rxPool[idx].size = (uint16_t)size;
    memcpy(g_rxPool[idx].data, data, size);
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(g_rxQueue, &idx, &hpw);
    if (hpw) portYIELD_FROM_ISR();
}

// Обработка принятого пакета (вызывается из loop, не из ISR).
static void onPacketProcess(const uint8_t* data, size_t size) {
    AudioPacketHeader hdr;
    const uint8_t* payload;
    size_t payloadSize;
    if (!parsePacket(data, size, hdr, payload, payloadSize)) return;

    if (hdr.channel != kMyChannel) return;

    g_masterOnline = true;
    g_lastRxMs = millis();
    g_packetsRx++;

    if (hdr.timestampMs != 0) {
        int32_t suggested = g_drift.process(hdr.timestampMs, millis());
        if (suggested > 0) {
            g_jitter->setTargetMs((uint32_t)suggested, g_cfg.sampleRate);
        }
    }

    size_t nSamples = payloadSize / sizeof(int16_t);
    g_jitter->push(reinterpret_cast<const int16_t*>(payload), nSamples);
}

// ---------------------------------------------------------------------------
// Heartbeat мастеру: discovery-response (broadcast) — мастер помечает online.
// Если известен MAC мастера (после его discovery-запроса) — шлём unicast-ом.
// ---------------------------------------------------------------------------
static MacAddr g_masterMac;
static bool g_hasMasterMac = false;

static void sendHeartbeat() {
    // Heartbeat возможен только по ESP-NOW: мастер не слушает UDP-порт
    // heartbeat (его UDP-listener — аудио от смартфона на 5004).
    if (g_cfg.transport != TransportMode::EspNow) return;
    uint8_t buf[kMaxPacketSize];
    size_t n = buildPacket(buf, sizeof(buf), kMyChannel, kSampleFormatInt16,
                           nullptr, 0, (uint32_t)millis(), 0, kFlagDiscoveryResponse);
    esp_err_t err;
    if (g_hasMasterMac) {
        err = g_espnow.sendTo(g_masterMac, buf, n);
    } else {
        err = g_espnow.broadcast(buf, n);
    }
    if (err == ESP_OK) {
        g_heartbeatsSent++;
    } else if (err != ESP_ERR_ESPNOW_FULL) {
        Logger::errorf("satellite", "heartbeat send failed: 0x%X", (unsigned)err);
    }
}

// Discovery-запрос мастера (broadcast): запоминаем MAC мастера, регистрируем
// пир и отвечаем unicast-ом (связь «без аудио-пакетов» — присутствие).
static void onDiscoveryRequest(const uint8_t* data, size_t size, const MacAddr& from) {
    if (g_cfg.transport != TransportMode::EspNow) return;
    g_masterMac = from;
    g_hasMasterMac = true;
    g_espnow.addPeer(g_masterMac);
    sendHeartbeat();
    (void)data; (void)size;
}

// ---------------------------------------------------------------------------
// Serial-консоль
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Serial-консоль сателлита (T17).
// ---------------------------------------------------------------------------

class SatelliteConsole : public Console {
public:
    using Console::Console;

protected:
    void cmdStatus() override {
        Serial.printf("role: satellite (%s)\n", sideToString(g_cfg.side));
        Serial.printf("transport: %s\n", transportToString(g_cfg.transport));
        uint8_t ch = 0; wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
        esp_wifi_get_channel(&ch, &sec);
        Serial.printf("wifi_channel: %u\n", (unsigned)ch);
        Serial.printf("master_online: %s\n", g_masterOnline ? "yes" : "no");
        Serial.printf("packets_rx: %lu\n", (unsigned long)g_packetsRx);
        Serial.printf("heartbeats_sent: %lu\n", (unsigned long)g_heartbeatsSent);
        Serial.printf("master_mac: ");
        if (g_hasMasterMac) {
            for (int i = 0; i < 6; i++) {
                if (i) Serial.print(":");
                Serial.printf("%02X", g_masterMac.bytes[i]);
            }
            Serial.println();
        } else {
            Serial.println("unknown");
        }
        Serial.printf("jitter_available: %u\n", g_jitter->available());
        Serial.printf("delay_ms: %u\n", g_delay->delayMs());
        Serial.printf("drift_median_ms: %d\n", (int)g_drift.medianMs());
    }

    bool handleCommand(const String& cmd) override {
        if (cmd.startsWith("delay")) {
            int ms = cmd.substring(6).toInt();
            if (ms >= kMinDelayMs && ms <= kMaxDelayMs) {
                if (g_cfg.side == SatelliteSide::Right) g_cfg.delayRightMs = ms;
                else g_cfg.delayLeftMs = ms;
                g_delay->setDelayMs(ms);
                Serial.println("ok");
            } else Serial.println("err");
            return true;
        }
        return false;
    }
};

static SatelliteConsole g_console{g_cfg};

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

    // C3.5: громкость сателлита — из покомпонентной (left/right) или общей.
    int satVolume = (g_cfg.side == SatelliteSide::Right) ? g_cfg.rightVolume
                                                         : g_cfg.leftVolume;
    g_vol.setVolume(satVolume);

    g_jitter = new JitterBuffer(g_cfg.sampleRate / 20); // ~50 мс ёмкость
    // Целевой уровень §10.3: 20 мс базовый (40/80 — для нестабильных сетей).
    g_jitter->setTargetMs(20, g_cfg.sampleRate);

    if (!initI2S(g_cfg)) {
        Logger::error("satellite", "I2S init failed");
    } else {
        Logger::info("satellite", "I2S ready");
    }

    if (g_cfg.transport == TransportMode::EspNow) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false); // power save выключает приём ESP-NOW
        esp_wifi_set_channel(kEspNowChannel, WIFI_SECOND_CHAN_NONE);
        if (g_espnow.begin()) {
            g_rxQueue = xQueueCreate(kRxPoolSize, sizeof(uint8_t));
            if (!g_rxQueue) {
                Logger::error("satellite", "rx queue create failed");
            }
            g_espnow.setRxCallback([](const uint8_t* d, size_t s, const MacAddr& from) {
                AudioPacketHeader hdr;
                const uint8_t* payload;
                size_t payloadSize;
                if (parsePacket(d, s, hdr, payload, payloadSize) &&
                    (hdr.flags & kFlagDiscoveryRequest)) {
                    onDiscoveryRequest(d, s, from);
                    return;
                }
                onPacketFromIsr(d, s);
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
    // Heartbeat мастеру (discovery-response broadcast) — присутствие без аудио.
    uint32_t nowMs = millis();
    if (nowMs - g_lastHeartbeatMs >= kHeartbeatIntervalMs) {
        g_lastHeartbeatMs = nowMs;
        sendHeartbeat();
    }

    // A9: drain очереди RX ESP-NOW (обработка из loop, не из коллбека).
    if (g_cfg.transport == TransportMode::EspNow && g_rxQueue) {
        uint8_t idx;
        while (xQueueReceive(g_rxQueue, &idx, 0) == pdTRUE) {
            const RxPacket& pkt = g_rxPool[idx];
            onPacketProcess(pkt.data, pkt.size);
        }
    }

    // UDP-режим: опрашиваем сокет. Discovery-запросы мастера обрабатываем
    // отдельно (ответ unicast-ом), аудио — через onPacketProcess.
    if (g_cfg.transport == TransportMode::Udp) {
        uint8_t buf[kMaxPacketSize];
        size_t n = g_udp.receive(buf, sizeof(buf));
        if (n > 0 && !g_udp.handleDiscovery(buf, n, g_udp.lastFrom())) {
            onPacketProcess(buf, n);
        }
    }

    // Следим за таймаутом мастера.
    if (g_masterOnline && (millis() - g_lastRxMs > 1000)) {
        g_masterOnline = false;
        Logger::warn("satellite", "master timeout");
    }

    // Выдаём поток из jitter buffer в I2S. C3.3: на старте ждём накопления
    // целевого уровня (ready()) — иначе щелчки; после входа в потоковый режим
    // выдаём всё, при истощении буфера снова ждём накопления.
    static bool g_streaming = false;
    if (g_i2sReady) {
        if (!g_streaming && g_jitter->ready()) g_streaming = true;
        int16_t sample;
        if (g_streaming && g_jitter->pop(sample)) {
            int16_t out = g_delay->process(sample);
            // C3.5: плавный fade-in/out + громкость канала сателлита.
            out = (int16_t)(g_vol.process(out / 32768.0f) * 32768.0f);
            writeSample(out);
        } else if (g_jitter->available() == 0) {
            g_streaming = false;
        }
    }

    g_console.update();

    delay(1);
}