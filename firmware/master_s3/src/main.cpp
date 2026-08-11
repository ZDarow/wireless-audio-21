// main.cpp вЂ” РјР°СЃС‚РµСЂ-СѓР·РµР» Wireless Audio 2.1 РЅР° ESP32-S3 (СЃР°Р±РІСѓС„РµСЂ).
//
// РђРґР°РїС‚Р°С†РёСЏ РїРѕРґ ESP32-S3 (СЃРј. docs/PLAN.md Рё РўР—): S3 РЅРµ РїРѕРґРґРµСЂР¶РёРІР°РµС‚ A2DP,
// РїРѕСЌС‚РѕРјСѓ РёСЃС‚РѕС‡РЅРёРє Р°СѓРґРёРѕ вЂ” Wi-Fi UDP PCM СЃРѕ СЃРјР°СЂС‚С„РѕРЅР°.
//
// Р­С‚Р°Рї 1 (С‚РµРєСѓС‰РёР№): Р·Р°РіСЂСѓР·РєР° РїР»Р°С‚С‹, СЃС‚Р°СЂС‚РѕРІР°СЏ РґРёР°РіРЅРѕСЃС‚РёРєР° (chip/flash/PSRAM),
// Wi-Fi (AP_DIRECT, STA РёР»Рё APSTA-СЂРµРїРёС‚РµСЂ), Web UI + REST API (РІРєР»СЋС‡Р°СЏ
// РЅР°СЃС‚СЂРѕР№РєСѓ Wi-Fi РїРѕРґРєР»СЋС‡РµРЅРёСЏ СЃРѕ СЃРјР°СЂС‚С„РѕРЅР°), UDP-listener РЅР° AUDIO_UDP_PORT,
// serial-РєРѕРЅСЃРѕР»СЊ. РђСѓРґРёРѕ-РєРѕРЅРІРµР№РµСЂ (jitter buffer, DSP, TX РЅР° СЃР°С‚РµР»Р»РёС‚С‹) вЂ”
// Р­С‚Р°Рї 2+.
//
// РќР°СЃС‚СЂРѕР№РєР° Wi-Fi: РµСЃР»Рё STA-РїРѕРґРєР»СЋС‡РµРЅРёРµ Рє РґРѕРјР°С€РЅРµР№ СЃРµС‚Рё РЅРµ СѓРґР°Р»РѕСЃСЊ (СЃРµС‚СЊ РЅРµ
// РЅР°Р№РґРµРЅР°, РЅРµРІРµСЂРЅС‹Р№ РїР°СЂРѕР»СЊ), РјР°СЃС‚РµСЂ РїРѕРґРЅРёРјР°РµС‚ AP РЅР°СЃС‚СЂРѕР№РєРё Рё Р·Р°РїСѓСЃРєР°РµС‚ Web UI
// РЅР° http://192.168.4.1 вЂ” СЃРѕ СЃРјР°СЂС‚С„РѕРЅР° РІС‹Р±РёСЂР°РµС‚СЃСЏ СЃРµС‚СЊ, РІРІРѕРґСЏС‚СЃСЏ SSID/РїР°СЂРѕР»СЊ,
// РєСЂРµРґС‹ СЃРѕС…СЂР°РЅСЏСЋС‚СЃСЏ РІ NVS Рё РјР°СЃС‚РµСЂ РїРµСЂРµР·Р°РіСЂСѓР¶Р°РµС‚СЃСЏ РІ STA/APSTA-СЂРµР¶РёРјРµ.
//
// РЎС‚Р°СЂС‚РѕРІР°СЏ РґРёР°РіРЅРѕСЃС‚РёРєР° (РўР— В§14.2):
//   chip model, flash size, PSRAM size, Wi-Fi mode, IP, UDP audio port,
//   I2S pins, satellite MAC, transport mode, crossover, delays.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <ping/ping_sock.h>
#include <freertos/semphr.h>
#include <time.h>
#include <vector>
#include <math.h>

#include "node_config.h"
#include "storage.h"
#include "logger.h"
#include "master_s3_config.h"
#include "web_server.h"
#include "logs.h"
#include "internet_check.h"
#include "udp_audio_packet.h"
#include "udp_audio_receiver.h"
#include "jitter_buffer.h"
#include "i2s_output.h"

using namespace audio21;

// ---------------------------------------------------------------------------
// Аудио-конвейер мастера (Этап 2, §9/§18): UDP-пакет со смартфона →
// UdpAudioReceiver (sequence/concealment) → JitterBuffer (PSRAM) → I2S (sub).
// Стерео PCM складывается в моно (сабвуфер), задержки/DSP — Этап 3.
// ---------------------------------------------------------------------------

// Буфер PCM для приёма: максимальный UDP-пакет §9.2 (< MTU).
static uint8_t g_udpBuf[sizeof(UdpAudioHeader) + kUdpMaxPayload];
static UdpAudioReceiver g_audioRecv;
// JitterBuffer в PSRAM: 20–60 мс при 48 кГц (§7.6, §16.2 — B13). Моно.
static constexpr uint32_t kMasterJitterCapacity = 60 * 48000 / 1000;
static JitterBuffer* g_jitter = nullptr;   // ps_malloc в setup
static volatile bool g_audioActive = false; // статус для Web UI

// ---------------------------------------------------------------------------
// Р“Р»РѕР±Р°Р»СЊРЅРѕРµ СЃРѕСЃС‚РѕСЏРЅРёРµ
// ---------------------------------------------------------------------------

static NodeConfig g_cfg;
static WiFiUDP g_udp;
static uint32_t g_packetsRx = 0;
static uint32_t g_packetBytesRx = 0;

// I2S-выход (C1.4): сабвуфер — моно (L=R), пины BCK=4/WS=5/DATA=6.
static I2sOutput g_i2sOut;
static bool g_i2sOn = false;

// Тестовый тон для проверки I2S: `tone <freq>` — синус в loop(), не блокируя Wi-Fi/Web UI.
static constexpr uint32_t kToneDurationMs = 2000;
static uint32_t g_toneUntilMs = 0;
static uint32_t g_toneFreq = 440;
static uint32_t g_tonePhase = 0;
static constexpr float kToneAmp = 0.2f;

// ESP-NOW: РїСЂРёС‘Рј heartbeat РѕС‚ СЃР°С‚РµР»Р»РёС‚РѕРІ (discovery-response) РґР»СЏ СЃС‚Р°С‚СѓСЃР°
// online РґР°Р¶Рµ Р±РµР· Р°СѓРґРёРѕ-РїРѕС‚РѕРєР°. РђСѓРґРёРѕ-РєРѕРЅРІРµР№РµСЂ вЂ” Р­С‚Р°Рї 2+.
static EspNowTransport g_espnow;
static volatile bool g_leftOnline = false;
static volatile bool g_rightOnline = false;
static uint32_t g_leftLastSeenMs = 0;
static uint32_t g_rightLastSeenMs = 0;
static uint32_t g_heartbeatsRx = 0;

// Web UI + REST API. РђСѓРґРёРѕ-РєРѕРЅРІРµР№РµСЂ (pipeline/delay/espnow) РІ Р­С‚Р°РїРµ 1 РЅРµ
// СЂРµР°Р»РёР·РѕРІР°РЅ вЂ” РїРµСЂРµРґР°С‘Рј nullptr: Р°СѓРґРёРѕ-СЌРЅРґРїРѕРёРЅС‚С‹ РїСЂРёРјРµРЅСЏСЋС‚ РЅР°СЃС‚СЂРѕР№РєРё Рє
// РєРѕРЅС„РёРіСѓ, РґРѕСЃС‚СѓРїРЅС‹ РЅР°СЃС‚СЂРѕР№РєР° Wi-Fi, РёРЅС‚РµСЂРЅРµС‚Р°, РґРёР°РіРЅРѕСЃС‚РёРєР° Рё reboot.
static MasterWebServer g_webServer(g_cfg, nullptr, nullptr, nullptr, nullptr,
                                   &g_espnow, &g_leftOnline, &g_rightOnline);// Р РµР¶РёРј РЅР°СЃС‚СЂРѕР№РєРё Wi-Fi: STA РЅРµ РїРѕРґРєР»СЋС‡РёР»СЃСЏ в†’ РјР°СЃС‚РµСЂ РїРѕРґРЅСЏР» AP РЅР°СЃС‚СЂРѕР№РєРё.
static bool g_setupMode = false;

// Captive portal: РїРµСЂРµС…РІР°С‚С‹РІР°РµС‚ DNS-Р·Р°РїСЂРѕСЃС‹ С‚РµР»РµС„РѕРЅР° (Р»СЋР±РѕР№ РґРѕРјРµРЅ в†’ softAPIP()),
// Р±СЂР°СѓР·РµСЂ РѕС‚РєСЂС‹РІР°РµС‚ http://192.168.4.1/ в†’ onNotFound в†’ СЃС‚СЂР°РЅРёС†Р° РЅР°СЃС‚СЂРѕР№РєРё.
static DNSServer g_dns;

// РџСЂРѕРІРµСЂРєР° РёРЅС‚РµСЂРЅРµС‚Р° (РўР—_Р’РµР± В§7): HTTP GET РґРѕ connectivitycheck.gstatic.com.
// Р‘Р»РѕРєРёСЂСѓСЋС‰РёР№ HTTP (DNS+connect+read) РІС‹РїРѕР»РЅСЏРµС‚СЃСЏ РІ РѕС‚РґРµР»СЊРЅРѕР№ Р·Р°РґР°С‡Рµ
// internetCheckTask, С‡С‚РѕР±С‹ РЅРµ Р·Р°РјРѕСЂР°Р¶РёРІР°С‚СЊ loop/Web UI.
static InternetChecker g_internet;
static uint32_t g_lastNetTick = 0;

static void internetCheckTask(void*) {
    for (;;) {
        uint32_t now = millis();
        if (g_internet.tick(httpInternetCheck, now)) {
            g_lastNetTick = now;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// РљРѕР»СЊС†РµРІРѕР№ Р±СѓС„РµСЂ Р»РѕРіРѕРІ РґР»СЏ Web UI (РўР—_Р’РµР± В§13).
static char g_logStorage[32][LogRing::kLineSize];
static LogRing g_logs(g_logStorage[0], 32);

// ---------------------------------------------------------------------------
// ESP-NOW: РїСЂРёС‘Рј heartbeat/discovery-response РѕС‚ СЃР°С‚РµР»Р»РёС‚РѕРІ (СЃС‚Р°С‚СѓСЃ online)
// ---------------------------------------------------------------------------

// РЎР°С‚РµР»Р»РёС‚ С€Р»С‘С‚ discovery-response (kFlagDiscoveryResponse) РєР°Р¶РґС‹Рµ ~2 СЃ
// (heartbeat) СЃРѕ СЃРІРѕРёРј РєР°РЅР°Р»РѕРј вЂ” РјР°СЃС‚РµСЂ РїРѕРјРµС‡Р°РµС‚ РµРіРѕ online. РўР°Р№РјР°СѓС‚ вЂ” 6 СЃ.
// РРЅС‚РµСЂРІР°Р» Рё С‚Р°Р№РјР°СѓС‚ вЂ” РѕР±С‰РёРµ (audio_packet.h).
static constexpr uint32_t kDiscoveryIntervalMs = kHeartbeatIntervalMs;

// РџРµСЂРµРїРѕРґРєР»СЋС‡РµРЅРёРµ STA РїРѕ Р·Р°РїСЂРѕСЃСѓ Web UI: РЅРµ СѓР±РёРІР°РµРј AP (ApSta-СЂРµРїРёС‚РµСЂ) Рё РЅРµ
// Р±Р»РѕРєРёСЂСѓРµРј loop вЂ” Р¶РґС‘Рј РёСЃС…РѕРґ РІ loop() СЃ Р»РёРјРёС‚РѕРј, РїСЂРё РЅРµСѓРґР°С‡Рµ РїРѕРґРЅРёРјР°РµРј setup AP.
static constexpr uint32_t kReconnectTimeoutMs = 20000;
static uint32_t g_reconnectAtMs = 0;
static bool g_reconnectPending = false;

// РџРµСЂРёРѕРґРёС‡РµСЃРєРёР№ discovery-Р·Р°РїСЂРѕСЃ: СЃР°С‚РµР»Р»РёС‚ Р·Р°РїРѕРјРёРЅР°РµС‚ MAC РјР°СЃС‚РµСЂР° Рё РѕС‚РІРµС‡Р°РµС‚
// unicast-heartbeat-РѕРј; РјР°СЃС‚РµСЂ С‚Р°РєР¶Рµ РїРѕР»СѓС‡Р°РµС‚ broadcast-heartbeat РЅР°РїСЂСЏРјСѓСЋ.
static uint32_t g_lastDiscoveryMs = 0;
static void sendDiscoveryRequest() {
    uint8_t buf[kMaxPacketSize];
    size_t n = buildPacket(buf, sizeof(buf), 0x00, kSampleFormatInt16,
                           nullptr, 0, (uint32_t)millis(), 0, kFlagDiscoveryRequest);
    g_espnow.broadcast(buf, n);
}

static void onEspNowPacket(const uint8_t* data, size_t size, const MacAddr& from) {
    AudioPacketHeader hdr;
    const uint8_t* payload;
    size_t payloadSize;
    if (!parsePacket(data, size, hdr, payload, payloadSize)) return;

    if (hdr.flags & kFlagDiscoveryResponse) {
        g_heartbeatsRx++;
        uint32_t now = millis();
        if (hdr.channel == kChannelLeft) {
            g_leftOnline = true;
            g_leftLastSeenMs = now;
        } else if (hdr.channel == kChannelRight) {
            g_rightOnline = true;
            g_rightLastSeenMs = now;
        }
        return;
    }
    (void)from; // MAC РёСЃС‚РѕС‡РЅРёРєР° РЅРµ РЅСѓР¶РµРЅ РґР»СЏ heartbeat
}

static bool initEspNow() {
    if (!g_espnow.begin()) return false;
    g_espnow.setRxCallback(onEspNowPacket);
    return true;
}

// ---------------------------------------------------------------------------
// РЎРєР°РЅ Wi-Fi СЃРµС‚РµР№ Р”Рћ СЃС‚Р°СЂС‚Р° AP (B9)
// ---------------------------------------------------------------------------

// WiFi.scanNetworks() РїСЂРё Р°РєС‚РёРІРЅРѕРј Soft-AP РѕС‚РєР»СЋС‡Р°РµС‚ СЂР°РґРёРѕ вЂ” С‚РµР»РµС„РѕРЅ С‚РµСЂСЏРµС‚
// СЃРµС‚СЊ РІ РјРѕРјРµРЅС‚ POST СЃ РєСЂРµРґР°РјРё (В«РЅРµ СЃРѕС…СЂР°РЅСЏРµС‚ РїРѕРґРєР»СЋС‡РµРЅРёРµВ»). РџРѕСЌС‚РѕРјСѓ СЃРїРёСЃРѕРє
// СЃРµС‚РµР№ СЃРєР°РЅРёСЂСѓРµС‚СЃСЏ Р·Р°СЂР°РЅРµРµ (STA-СЂРµР¶РёРј, AP РµС‰С‘ РЅРµ РїРѕРґРЅСЏС‚) Рё РєРµС€РёСЂСѓРµС‚СЃСЏ РІ
// MasterWebServer; GET /api/wifi/scan РѕС‚РґР°С‘С‚ РєРµС€.
static void scanWifiBeforeAp() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    delay(100);
    int n = WiFi.scanNetworks(); // Р±Р»РѕРєРёСЂСѓСЋС‰РёР№ СЃРєР°РЅ (~1-2 СЃ)
    std::vector<MasterWebServer::WifiNetInfo> cache;
    if (n > 0) cache.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
        MasterWebServer::WifiNetInfo info;
        info.ssid = WiFi.SSID(i);
        info.rssi = WiFi.RSSI(i);
        info.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        cache.push_back(std::move(info));
    }
    WiFi.scanDelete();
    g_webServer.setWifiCache(std::move(cache));
    Logger::infof("wifi", "pre-AP scan: %d networks cached", n);
}

// ---------------------------------------------------------------------------
// РЎС‚Р°СЂС‚РѕРІР°СЏ РґРёР°РіРЅРѕСЃС‚РёРєР° (РўР— В§14.2)
// ---------------------------------------------------------------------------

static void printDiagnostics() {
    Logger::info("diag", "--- ESP32-S3 master ---");
    Logger::infof("diag", "chip: %s, rev %d, cores %d",
                  ESP.getChipModel(), (int)ESP.getChipRevision(), (int)ESP.getChipCores());
    Logger::infof("diag", "flash: %u MB", ESP.getFlashChipSize() / (1024 * 1024));
    Logger::infof("diag", "psram: %u MB", ESP.getPsramSize() / (1024 * 1024));
    Logger::infof("diag", "heap free: %lu", (unsigned long)ESP.getFreeHeap());
    Logger::infof("diag", "wifi mode: %s", wifiModeToString(g_cfg.wifiMode));
    Logger::infof("diag", "udp audio port: %u", g_cfg.udpAudioPort);
    Logger::infof("diag", "i2s pins: bck=%u ws=%u data=%u",
                  g_cfg.i2sBck, g_cfg.i2sWs, g_cfg.i2sDataOut);
    Logger::infof("diag", "left sat MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  g_cfg.leftSatMac.bytes[0], g_cfg.leftSatMac.bytes[1],
                  g_cfg.leftSatMac.bytes[2], g_cfg.leftSatMac.bytes[3],
                  g_cfg.leftSatMac.bytes[4], g_cfg.leftSatMac.bytes[5]);
    Logger::infof("diag", "right sat MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  g_cfg.rightSatMac.bytes[0], g_cfg.rightSatMac.bytes[1],
                  g_cfg.rightSatMac.bytes[2], g_cfg.rightSatMac.bytes[3],
                  g_cfg.rightSatMac.bytes[4], g_cfg.rightSatMac.bytes[5]);
    Logger::infof("diag", "transport: %s", transportToString(g_cfg.transport));
    Logger::infof("diag", "crossover: %d Hz", g_cfg.crossoverHz);
    Logger::infof("diag", "delays ms: L=%d R=%d Sub=%d",
                  g_cfg.delayLeftMs, g_cfg.delayRightMs, g_cfg.delaySubMs);

    // РљСЂРёС‚РµСЂРёР№ РєРѕСЂСЂРµРєС‚РЅРѕСЃС‚Рё PSRAM (РўР— В§14.3).
    if (ESP.getPsramSize() == 0) {
        Logger::error("diag", "PSRAM size = 0 вЂ” РЅРµРІРµСЂРЅР°СЏ РїР»Р°С‚Р° РёР»Рё board_build.arduino.memory_type");
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi: AP_DIRECT, STA РёР»Рё APSTA-СЂРµРїРёС‚РµСЂ (РўР— В§6.1)
// ---------------------------------------------------------------------------

// NAPT вЂ” СЂРµРїРёС‚РµСЂ: AP-РєР»РёРµРЅС‚С‹ (СЃРјР°СЂС‚С„РѕРЅ) РїРѕР»СѓС‡Р°СЋС‚ РёРЅС‚РµСЂРЅРµС‚ С‡РµСЂРµР· STA (uplink).
// API РµСЃС‚СЊ С‚РѕР»СЊРєРѕ РІ Arduino core 3.x (IDF 5.1+), РіРґРµ lwip СЃРѕР±СЂР°РЅ СЃ
// CONFIG_LWIP_IP_FORWARD=y / CONFIG_LWIP_IPV4_NAPT=y (pioarduino 55.03.311).
static void enableNapt() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap != nullptr && esp_netif_napt_enable(ap) == ESP_OK) {
        Logger::info("wifi", "NAPT enabled вЂ” AP clients routed to upstream");
    } else {
        Logger::error("wifi", "NAPT enable failed (lwip Р±РµР· CONFIG_LWIP_IPV4_NAPT?)");
    }
#else
    Logger::warn("wifi", "NAPT requires Arduino core 3.x вЂ” skipping");
#endif
}

// РџРѕРґРЅСЏС‚СЊ AP РЅР°СЃС‚СЂРѕР№РєРё (РёСЃРїРѕР»СЊР·СѓРµС‚СЃСЏ РїСЂРё РЅРµСѓРґР°С‡РЅРѕРј STA-РїРѕРґРєР»СЋС‡РµРЅРёРё).
static bool startSetupAp() {
    if (WiFi.getMode() == WIFI_AP) return true; // AP СѓР¶Рµ РїРѕРґРЅСЏС‚ (ApSta)
    scanWifiBeforeAp(); // СЃРїРёСЃРѕРє СЃРµС‚РµР№ РґР»СЏ Web UI вЂ” Р”Рћ СЃС‚Р°СЂС‚Р° AP (B9)
    WiFi.mode(WIFI_AP_STA);
    bool ok = WiFi.softAP(g_cfg.wifiApSsid, g_cfg.wifiApPassword, kDefaultWifiChannel);
    if (!ok) {
        Logger::error("wifi", "setup softAP failed");
        return false;
    }
    delay(200);
    Logger::infof("wifi", "setup AP '%s' on channel %d, IP: %s",
                  g_cfg.wifiApSsid, kDefaultWifiChannel,
                  WiFi.softAPIP().toString().c_str());
    return true;
}

static bool initWifi() {
    WiFi.disconnect(true);
    WiFi.setSleep(false); // РѕС‚РєР»СЋС‡РёС‚СЊ power save РґР»СЏ Р°СѓРґРёРѕСѓР·Р»РѕРІ (РўР— В§16.3)

    if (g_cfg.wifiMode == WifiMode::ApDirect) {
        scanWifiBeforeAp(); // СЃРїРёСЃРѕРє СЃРµС‚РµР№ РґР»СЏ Web UI вЂ” Р”Рћ СЃС‚Р°СЂС‚Р° AP (B9)
        WiFi.mode(WIFI_AP);
        bool ok = WiFi.softAP(g_cfg.wifiApSsid, g_cfg.wifiApPassword, kDefaultWifiChannel);
        if (!ok) {
            Logger::error("wifi", "softAP failed");
            return false;
        }
        delay(200);
        Logger::infof("wifi", "AP '%s' on channel %d, IP: %s",
                      g_cfg.wifiApSsid, kDefaultWifiChannel,
                      WiFi.softAPIP().toString().c_str());
        return true;
    }

    if (g_cfg.wifiMode == WifiMode::ApSta) {
        // Р РµРїРёС‚РµСЂ: AP РґР»СЏ СЃРјР°СЂС‚С„РѕРЅР° + STA (РґРѕРјР°С€РЅСЏСЏ СЃРµС‚СЊ) + NAPT.
        scanWifiBeforeAp(); // СЃРїРёСЃРѕРє СЃРµС‚РµР№ РґР»СЏ Web UI вЂ” Р”Рћ СЃС‚Р°СЂС‚Р° AP (B9)
        WiFi.mode(WIFI_AP_STA);
        bool ok = WiFi.softAP(g_cfg.wifiApSsid, g_cfg.wifiApPassword, kDefaultWifiChannel);
        if (!ok) {
            Logger::error("wifi", "softAP failed");
            return false;
        }
        delay(200);
        Logger::infof("wifi", "AP '%s' on channel %d, IP: %s",
                      g_cfg.wifiApSsid, kDefaultWifiChannel,
                      WiFi.softAPIP().toString().c_str());

        WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
        Logger::infof("wifi", "Connecting to upstream '%s'...", g_cfg.wifiSsid);
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
        if (WiFi.status() == WL_CONNECTED) {
            Logger::infof("wifi", "upstream connected, IP: %s",
                          WiFi.localIP().toString().c_str());
            enableNapt();
        } else {
            // Р”РѕРјР°С€РЅСЏСЏ СЃРµС‚СЊ РЅРµРґРѕСЃС‚СѓРїРЅР° вЂ” AP РѕСЃС‚Р°С‘С‚СЃСЏ, РІРєР»СЋС‡Р°РµРј СЂРµР¶РёРј РЅР°СЃС‚СЂРѕР№РєРё.
            // РћС‚РєР»СЋС‡Р°РµРј Р°РІС‚Рѕ-СЂРµРєРѕРЅРЅРµРєС‚ STA: РїРѕСЃС‚РѕСЏРЅРЅС‹Рµ РїРѕРїС‹С‚РєРё РїРѕРґРєР»СЋС‡РµРЅРёСЏ
            // РјРµС€Р°СЋС‚ СЃРєР°РЅРёСЂРѕРІР°РЅРёСЋ СЃРµС‚РµР№ РІ Web UI Рё РЅР°РіСЂСѓР¶Р°СЋС‚ СЂР°РґРёРѕРјРѕРґСѓР»СЊ.
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false, true);
            Logger::warn("wifi", "upstream not connected вЂ” setup mode (Web UI on AP)");
            g_setupMode = true;
        }
        return true; // AP СЂР°Р±РѕС‚Р°РµС‚ РІ Р»СЋР±РѕРј СЃР»СѓС‡Р°Рµ
    }

    // STA
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
    Logger::infof("wifi", "Connecting to '%s'...", g_cfg.wifiSsid);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        // РќРµ СѓРґР°Р»РѕСЃСЊ РїРѕРґРєР»СЋС‡РёС‚СЊСЃСЏ Рє РґРѕРјР°С€РЅРµР№ СЃРµС‚Рё вЂ” РїРѕРґРЅРёРјР°РµРј AP РЅР°СЃС‚СЂРѕР№РєРё,
        // С‡С‚РѕР±С‹ РїРѕР»СЊР·РѕРІР°С‚РµР»СЊ РјРѕРі Р·Р°РґР°С‚СЊ SSID/РїР°СЂРѕР»СЊ С‡РµСЂРµР· Web UI.
        // РћС‚РєР»СЋС‡Р°РµРј Р°РІС‚Рѕ-СЂРµРєРѕРЅРЅРµРєС‚ STA (РјРµС€Р°РµС‚ СЃРєР°РЅСѓ СЃРµС‚РµР№ РІ Web UI).
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, true);
        Logger::warn("wifi", "connect failed вЂ” switching to setup AP");
        g_setupMode = true;
        return startSetupAp();
    }
    Logger::infof("wifi", "connected, IP: %s", WiFi.localIP().toString().c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Serial-РєРѕРЅСЃРѕР»СЊ
// ---------------------------------------------------------------------------

// Р‘Р»РѕРєРёСЂСѓСЋС‰РёР№ ping С‡РµСЂРµР· esp_ping (IDF 5.x). РСЃРїРѕР»СЊР·СѓРµС‚СЃСЏ РєРѕРјР°РЅРґРѕР№ `net`
// РґР»СЏ РґРёР°РіРЅРѕСЃС‚РёРєРё РґРѕСЃС‚СѓРїР° РІ СЃРµС‚СЊ СЃ СЃР°РјРѕРіРѕ РјР°СЃС‚РµСЂР°.
static SemaphoreHandle_t g_pingDone = nullptr;
static uint32_t g_pingRecv = 0;

static void onPingEnd(esp_ping_handle_t h, void* /*arg*/) {
    uint32_t recv = 0;
    esp_ping_get_profile(h, ESP_PING_PROF_REPLY, &recv, sizeof(recv));
    g_pingRecv = recv;
    esp_ping_delete_session(h);
    if (g_pingDone) xSemaphoreGive(g_pingDone);
}

static bool pingHost(const IPAddress& ip, uint32_t count, uint32_t timeoutMs) {
    if (!g_pingDone) g_pingDone = xSemaphoreCreateBinary();
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = count;
    cfg.timeout_ms = timeoutMs;
    cfg.interval_ms = 300;
    cfg.target_addr.type = IPADDR_TYPE_V4;
    cfg.target_addr.u_addr.ip4.addr = (uint32_t)ip;
    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_end = onPingEnd;
    esp_ping_handle_t h;
    if (esp_ping_new_session(&cfg, &cbs, &h) != ESP_OK) return false;
    xSemaphoreTake(g_pingDone, 0); // СЃР±СЂРѕСЃ СЃРµРјР°С„РѕСЂР°
    g_pingRecv = 0;
    esp_ping_start(h);
    uint32_t waitMs = count * (timeoutMs + 300) + 500;
    if (xSemaphoreTake(g_pingDone, pdMS_TO_TICKS(waitMs)) == pdTRUE) {
        return g_pingRecv > 0;
    }
    esp_ping_delete_session(h);
    return false;
}

static void handleConsoleCommand(const String& line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd == "status") {
        Serial.println("role: master (s3)");
        Serial.printf("source: %s\n", sourceToString(g_cfg.source));
        Serial.printf("wifi_mode: %s\n", wifiModeToString(g_cfg.wifiMode));
        Serial.printf("wifi_ssid: %s\n", g_cfg.wifiSsid);
        Serial.printf("wifi_ip: %s\n", WiFi.localIP().toString().c_str());
        if (g_cfg.wifiMode == WifiMode::ApDirect || g_cfg.wifiMode == WifiMode::ApSta || g_setupMode) {
            Serial.printf("wifi_ap_ip: %s\n", WiFi.softAPIP().toString().c_str());
        }
        Serial.printf("setup_mode: %s\n", g_setupMode ? "yes" : "no");
        Serial.printf("udp_port: %u\n", g_cfg.udpAudioPort);
        Serial.printf("packets_rx: %lu\n", (unsigned long)g_packetsRx);
        Serial.printf("bytes_rx: %lu\n", (unsigned long)g_packetBytesRx);
        Serial.printf("i2s: %s\n", g_i2sOn ? "on" : "off");
        Serial.printf("sats: L=%s R=%s heartbeats=%lu\n",
                      g_leftOnline ? "online" : "offline",
                      g_rightOnline ? "online" : "offline",
                      (unsigned long)g_heartbeatsRx);
        Serial.printf("psram: %u MB\n", ESP.getPsramSize() / (1024 * 1024));
        return;
    }

    // Р СѓС‡РЅР°СЏ РЅР°СЃС‚СЂРѕР№РєР° Wi-Fi С‡РµСЂРµР· РєРѕРЅСЃРѕР»СЊ: `wifi <ssid> <password>`
    if (cmd.startsWith("wifi ")) {
        String rest = cmd.substring(5);
        rest.trim();
        int sp = rest.indexOf(' ');
        if (sp <= 0) { Serial.println("usage: wifi <ssid> <password>"); return; }
        String ssid = rest.substring(0, sp);
        String pass = rest.substring(sp + 1);
        ssid.trim();
        pass.trim();
        if (ssid.length() == 0 || ssid.length() >= sizeof(g_cfg.wifiSsid)) { Serial.println("err: bad ssid"); return; }
        if (pass.length() >= sizeof(g_cfg.wifiPassword)) { Serial.println("err: bad password"); return; }
        strlcpy(g_cfg.wifiSsid, ssid.c_str(), sizeof(g_cfg.wifiSsid));
        strlcpy(g_cfg.wifiPassword, pass.c_str(), sizeof(g_cfg.wifiPassword));
        ConfigStorage::save(g_cfg);
        Serial.println("saved, rebooting...");
        delay(200);
        ESP.restart();
        return;
    }

    if (cmd == "net") {
        Serial.printf("sta_ip: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("ap_ip: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("ap_stations: %u\n", WiFi.softAPgetStationNum());
        Serial.printf("gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("netmask: %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("dns: %s\n", WiFi.dnsIP().toString().c_str());
        // РџСЂРѕРІРµСЂРєР° AP-РёРЅС‚РµСЂС„РµР№СЃР°: РїРёРЅРі РїРµСЂРІРѕРіРѕ РєР»РёРµРЅС‚Р° (192.168.4.x)
        {
            esp_netif_ip_info_t ip;
            esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            if (ap && esp_netif_get_ip_info(ap, &ip) == ESP_OK) {
                IPAddress apIp(ip.ip.addr);
                Serial.printf("ap_netif: %s\n", apIp.toString().c_str());
                IPAddress apGw(ip.gw.addr);
                Serial.printf("ping ap_gw: %s\n", pingHost(apGw, 2, 1500) ? "OK" : "FAIL");
            } else {
                Serial.println("ap_netif: not found");
            }
        }
        Serial.printf("ping gateway: %s\n", pingHost(WiFi.gatewayIP(), 3, 2000) ? "OK" : "FAIL");
        Serial.printf("ping 8.8.8.8: %s\n", pingHost(IPAddress(8, 8, 8, 8), 3, 2000) ? "OK" : "FAIL");
        Serial.printf("ping 1.1.1.1: %s\n", pingHost(IPAddress(1, 1, 1, 1), 3, 2000) ? "OK" : "FAIL");
        return;
    }

    if (cmd == "save") {
        ConfigStorage::save(g_cfg);
        Serial.println("ok");
        return;
    }

    if (cmd == "erase") {
        ConfigStorage::erase();
        Serial.println("config erased, rebooting...");
        delay(200);
        ESP.restart();
        return;
    }

    if (cmd == "reboot") {
        Serial.println("rebooting");
        ESP.restart();
        return;
    }

    // Тестовый тон на I2S (C1.4): `tone 440`, `tone off`. Проверка PCM5102A без смартфона.
    if (cmd.startsWith("tone")) {
        String rest = cmd.substring(4);
        rest.trim();
        if (rest.length() == 0 || rest == "off") {
            g_toneUntilMs = 0;
            Serial.println("tone: off");
            return;
        }
        long freq = rest.toInt();
        if (freq <= 0 || !g_i2sOn) {
            Serial.println("usage: tone <freq> | tone off  (i2s must be on)");
            return;
        }
        g_toneFreq = (uint32_t)freq;
        g_tonePhase = 0;
        g_toneUntilMs = millis() + kToneDurationMs;
        Serial.printf("tone: %lu Hz, %u s\n", g_toneFreq, kToneDurationMs / 1000);
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
    Logger::info("master", "Wireless Audio 2.1 Master (ESP32-S3) starting...");

    if (!ConfigStorage::load(g_cfg)) {
        Logger::warn("master", "No saved config, using defaults");
        g_cfg = defaultConfig();
        g_cfg.role = NodeRole::Master;
    }

    printDiagnostics();

    if (!initWifi()) {
        Logger::error("master", "Wi-Fi init failed");
    }

    // ESP-NOW: heartbeat РѕС‚ СЃР°С‚РµР»Р»РёС‚РѕРІ в†’ СЃС‚Р°С‚СѓСЃ online Р±РµР· Р°СѓРґРёРѕ-РїРѕС‚РѕРєР°.
    if (initEspNow()) {
        Logger::info("master", "ESP-NOW ready (satellite heartbeat RX)");
    } else {
        Logger::error("master", "ESP-NOW init failed");
    }

    // mDNS (F16): РґРѕСЃС‚СѓРї РїРѕ http://<hostname>.local (РґРµС„РѕР»С‚ audio-master.local).
    // Р’ СЂРµР¶РёРјРµ РЅР°СЃС‚СЂРѕР№РєРё mDNS РЅР° AP РЅРµ СЂР°Р±РѕС‚Р°РµС‚ вЂ” Web UI РґРѕСЃС‚СѓРїРµРЅ РїРѕ IP.
    if (!g_setupMode && MDNS.begin(g_cfg.hostname)) {
        Logger::infof("master", "mDNS: http://%s.local", g_cfg.hostname);
        MDNS.addService("http", "tcp", 80);
    }

    // NTP (РўР—_Р’РµР± В§7.5): СЃРёРЅС…СЂРѕРЅРёР·Р°С†РёСЏ РІСЂРµРјРµРЅРё РёР· STA-СЃРµС‚Рё.
    if (!g_setupMode && g_cfg.ntpEnabled && WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, g_cfg.ntpServer);
        setenv("TZ", g_cfg.timezone, 1);
        tzset();
        Logger::infof("master", "NTP: %s (tz %s)", g_cfg.ntpServer, g_cfg.timezone);
    }

    // РџСЂРѕРІРµСЂРєР° РёРЅС‚РµСЂРЅРµС‚Р° (РўР—_Р’РµР± В§7): Р°РєС‚РёРІРЅР° РїСЂРё STA-РїРѕРґРєР»СЋС‡РµРЅРёРё.
    g_internet.configure(g_cfg.netCheckUrl, g_cfg.netCheckIntervalSec,
                         g_cfg.netCheckTimeoutMs, g_cfg.netCheckEnabled && !g_setupMode);
    g_internet.start(httpInternetCheck, millis());
    g_webServer.setInternetChecker(&g_internet);
    g_webServer.setLogs(&g_logs);
    // Р—Р°РґР°С‡Р° РёРЅС‚РµСЂРЅРµС‚-С‡РµРєР°: Р±Р»РѕРєРёСЂСѓСЋС‰РёР№ HTTP РІС‹РїРѕР»РЅСЏРµС‚СЃСЏ РІРЅРµ loop.
    xTaskCreate(internetCheckTask, "netcheck", 4096, nullptr, 1, nullptr);
    g_logs.addf(LogCat::Boot, 1, "master booted, mode %s", wifiModeToString(g_cfg.wifiMode));

    // Web UI: РІ СЂРµР¶РёРјРµ РЅР°СЃС‚СЂРѕР№РєРё вЂ” РЅР° AP (http://192.168.4.1), РёРЅР°С‡Рµ вЂ” РїРѕ IP/mDNS.
    g_webServer.begin();
    if (g_setupMode) {
        Logger::infof("master", "Wi-Fi setup: connect to AP '%s', open http://192.168.4.1",
                      g_cfg.wifiApSsid);
    } else {
        Logger::infof("master", "Web UI: http://%s", WiFi.localIP().toString().c_str());
    }

    // Captive portal (B9): РїСЂРё Р°РєС‚РёРІРЅРѕРј AP РїРµСЂРµС…РІР°С‚С‹РІР°РµРј DNS (Р»СЋР±РѕР№ РґРѕРјРµРЅ в†’
    // softAPIP()), С‡С‚РѕР±С‹ С‚РµР»РµС„РѕРЅ Р°РІС‚РѕРјР°С‚РёС‡РµСЃРєРё РѕС‚РєСЂС‹Р» СЃС‚СЂР°РЅРёС†Сѓ РЅР°СЃС‚СЂРѕР№РєРё.
    if (WiFi.getMode() & WIFI_AP) {
        if (g_dns.start(53, "*", WiFi.softAPIP())) {
            Logger::info("master", "DNS captive portal: * -> softAPIP");
        } else {
            Logger::error("master", "DNS start failed (port 53 busy?)");
        }
    }

    // UDP-listener Р°СѓРґРёРѕ РѕС‚ СЃРјР°СЂС‚С„РѕРЅР° (Р­С‚Р°Рї 2: РїСЂРёС‘Рј PCM-РїР°РєРµС‚РѕРІ).
    if (g_udp.begin(g_cfg.udpAudioPort)) {
        Logger::infof("master", "UDP audio listener on port %u", g_cfg.udpAudioPort);
    } else {
        Logger::error("master", "UDP begin failed");
    }

    // I2S-выход (C1.4): PCM5102A на пинах BCK/WS/DATA, сабвуфер — моно (L=R).
    I2sOutputPins i2sPins = {(int)g_cfg.i2sBck, (int)g_cfg.i2sWs, (int)g_cfg.i2sDataOut};
    if (g_i2sOut.init(i2sPins, g_cfg.sampleRate, /*mono=*/true)) {
        g_i2sOn = true;
        Logger::infof("audio", "I2S out: %u Hz, mono (L=R), pins %u/%u/%u",
                      (unsigned)g_cfg.sampleRate, g_cfg.i2sBck, g_cfg.i2sWs, g_cfg.i2sDataOut);
    } else {
        Logger::error("audio", "I2S init failed");
    }

    // Jitter-буфер в PSRAM (C1.3, §7.6): 60 мс ёмкость, целевая задержка 30 мс.
    g_jitter = new (ps_malloc(kMasterJitterCapacity * sizeof(int16_t))) JitterBuffer(kMasterJitterCapacity);
    if (g_jitter) {
        g_jitter->setTargetMs(30, g_cfg.sampleRate);
        Logger::infof("audio", "Jitter buffer: cap=%u samples (%u ms), target=30 ms",
                      (unsigned)kMasterJitterCapacity, (unsigned)(kMasterJitterCapacity * 1000 / g_cfg.sampleRate));
    } else {
        Logger::error("audio", "Jitter buffer alloc failed");
    }

    // Приёмник UDP-аудио (C1.2): 5 мс/пакет при 48 кГц стерео 16 бит (§9.3).
    g_audioRecv.configure(g_cfg.sampleRate, 5.0f);

    Logger::info("master", "Ready. Type 'status' for info.");
}

// Генерация тестового тона (C1.4): вызывается из loop(), не блокирует Wi-Fi/Web UI.
static void toneTick() {
    if (g_toneUntilMs == 0) return;
    if (millis() >= g_toneUntilMs) {
        g_toneUntilMs = 0;
        Logger::info("audio", "tone stopped");
        return;
    }
    if (!g_i2sOn) {
        g_toneUntilMs = 0;
        return;
    }
    const size_t kChunk = 128;
    int16_t buf[kChunk];
    const uint32_t phaseStep =
        (uint32_t)((g_toneFreq * 65536.0f) / (float)g_cfg.sampleRate);
    for (size_t i = 0; i < kChunk; i++) {
        g_tonePhase += phaseStep;
        float ph = (float)(g_tonePhase >> 16) * (2.0f * PI) / 65536.0f;
        buf[i] = (int16_t)(kToneAmp * 32767.0f * sinf(ph));
    }
    g_i2sOut.write(buf, kChunk);
}

// Драйвер аудио-выхода (C1.3/C1.5): вычитывает моно-семплы из jitter-буфера
// в I2S. Задержка конфигурируется через setTargetMs (30 мс). Пока буфер не
// накоплен до целевого уровня — выдаём тишину (плавный старт без щелчков).
static void audioOutTick() {
    if (!g_i2sOn || !g_jitter) return;
    constexpr size_t kChunk = 128;
    int16_t buf[kChunk];
    for (size_t i = 0; i < kChunk; i++) {
        int16_t s;
        buf[i] = g_jitter->pop(s) ? s : 0;
    }
    g_i2sOut.write(buf, kChunk);
}

void loop() {
    // Приём UDP-аудио со смартфона (C1.5, §9): разбор пакета → UdpAudioReceiver
    // (sequence/concealment) → JitterBuffer (PSRAM) → I2S (sub). Стерео→моно.
    int packetSize = g_udp.parsePacket();
    if (packetSize > 0) {
        int n = g_udp.read(g_udpBuf, sizeof(g_udpBuf));
        g_packetsRx++;
        g_packetBytesRx += static_cast<uint32_t>(n);

        UdpAudioHeader hdr;
        const uint8_t* payload;
        size_t payloadSize;
        if (n > 0 && parseUdpPacket(g_udpBuf, static_cast<size_t>(n), hdr, payload, payloadSize)) {
            // Стерео PCM → моно (сабвуфер): усредняем пары {L, R}.
            size_t nSamples = payloadSize / sizeof(int16_t);
            const int16_t* pcm = reinterpret_cast<const int16_t*>(payload);
            static int16_t s_mono[sizeof(g_udpBuf) / sizeof(int16_t)];
            for (size_t i = 0, o = 0; i + 1 < nSamples; i += 2, o++) {
                s_mono[o] = static_cast<int16_t>((static_cast<int32_t>(pcm[i]) + pcm[i + 1]) / 2);
            }
            size_t nMono = nSamples / 2;

            StreamState st = g_audioRecv.feed(hdr.sequence, hdr.timestampSamples,
                                              s_mono, nMono, millis());
            if (st == StreamState::Active || st == StreamState::Conceal) {
                g_jitter->push(s_mono, nMono);
            }
            g_audioActive = (st != StreamState::Standby);
        }
    }

    // Драйвер аудио-выхода: вычитываем из jitter-буфера в I2S.
    audioOutTick();

    // Тестовый тон (C1.4) — не блокирует loop.
    toneTick();

    // РЎС‚Р°С‚СѓСЃ СЃР°С‚РµР»Р»РёС‚РѕРІ: online, РїРѕРєР° РїСЂРёС…РѕРґРёС‚ heartbeat (discovery-response).
    uint32_t now = millis();
    if (g_leftOnline && (now - g_leftLastSeenMs > kSatelliteTimeoutMs)) g_leftOnline = false;
    if (g_rightOnline && (now - g_rightLastSeenMs > kSatelliteTimeoutMs)) g_rightOnline = false;

    // Discovery-Р·Р°РїСЂРѕСЃ СЃР°С‚РµР»Р»РёС‚Р°Рј: РїРѕРєР° РєС‚Рѕ-С‚Рѕ offline вЂ” РєР°Р¶РґС‹Рµ 2 СЃ, С‡С‚РѕР±С‹
    // РѕРЅРё РїРµСЂРµС€Р»Рё РЅР° unicast-heartbeat. РљРѕРіРґР° РѕР±Р° online, СЌС„РёСЂ РЅРµ РјСѓСЃРѕСЂРёРј
    // (heartbeat РїСЂРёС…РѕРґРёС‚ Рё С‚Р°Рє); РЅРѕРІС‹Р№/РїРµСЂРµР·Р°РіСЂСѓР¶РµРЅРЅС‹Р№ СЃР°С‚РµР»Р»РёС‚ СЃР°Рј РїРѕС€Р»С‘С‚
    // broadcast-heartbeat, РїРѕ РєРѕС‚РѕСЂРѕРјСѓ РјР°СЃС‚РµСЂ РІРѕСЃСЃС‚Р°РЅРѕРІРёС‚ СЃС‚Р°С‚СѓСЃ.
    if (now - g_lastDiscoveryMs >= kDiscoveryIntervalMs && (!g_leftOnline || !g_rightOnline)) {
        g_lastDiscoveryMs = now;
        sendDiscoveryRequest();
    }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        handleConsoleCommand(line);
    }

    // Web UI: РѕР±СЂР°Р±РѕС‚РєР° Р·Р°РїСЂРѕСЃРѕРІ Рё СЃРѕС…СЂР°РЅРµРЅРёРµ РєРѕРЅС„РёРіР° РїРѕ РєРЅРѕРїРєРµ.
    g_webServer.handleClient();
    if (g_webServer.saveRequested()) {
        ConfigStorage::save(g_cfg);
        g_webServer.clearSaveRequested();
        Logger::info("master", "config saved via Web UI");
        g_logs.addf(LogCat::Config, 1, "config saved via Web UI");
    }

    // РРЅС‚РµСЂРЅРµС‚-С‡РµРє (РўР—_Р’РµР± В§7.4): РІС‹РїРѕР»РЅСЏРµС‚СЃСЏ РІ РѕС‚РґРµР»СЊРЅРѕР№ Р·Р°РґР°С‡Рµ, С‡С‚РѕР±С‹
    // Р±Р»РѕРєРёСЂСѓСЋС‰РёР№ HTTP (DNS + connect + read) РЅРµ Р·Р°РјРѕСЂР°Р¶РёРІР°Р» loop Рё Web UI.
    static NetStatus lastLoggedNet = NetStatus::Disabled;
    if (g_internet.status() != lastLoggedNet) {
        lastLoggedNet = g_internet.status();
        g_logs.addf(LogCat::Internet, 1, "internet check: %s (%lu ms)",
                    g_internet.statusName(), (unsigned long)g_internet.latencyMs());
    }

    // РџРµСЂРµРїРѕРґРєР»СЋС‡РµРЅРёРµ Wi-Fi РїРѕ Р·Р°РїСЂРѕСЃСѓ Web UI (POST /api/wifi/connect).
    if (g_webServer.reconnectRequested()) {
        g_webServer.clearReconnectRequested();
        Logger::infof("wifi", "reconnect requested via Web UI (ssid %s)", g_cfg.wifiSsid);
        g_logs.addf(LogCat::Wifi, 1, "reconnect requested: %s", g_cfg.wifiSsid);
        bool keepAp = g_cfg.wifiMode == WifiMode::ApSta || g_cfg.wifiMode == WifiMode::ApDirect;
        if (keepAp && WiFi.getMode() == WIFI_AP) {
            // AP СѓР¶Рµ РїРѕРґРЅСЏС‚ вЂ” РїРµСЂРµР·Р°РїСѓСЃРєР°РµРј С‚РѕР»СЊРєРѕ STA-С‡Р°СЃС‚СЊ, С‡С‚РѕР±С‹ РЅРµ С‚РµСЂСЏС‚СЊ
            // РєР°РЅР°Р» ESP-NOW (СЃРІСЏР·СЊ СЃ СЃР°С‚РµР»Р»РёС‚Р°РјРё Р¶РёРІС‘С‚ Р±РµР· Р°СѓРґРёРѕ-РїР°РєРµС‚РѕРІ).
            WiFi.mode(WIFI_AP_STA);
        } else if (!keepAp) {
            WiFi.mode(WIFI_STA);
        }
        WiFi.disconnect(false, true);
        delay(200);
        WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
        g_reconnectPending = true;
        g_reconnectAtMs = millis();
    }

    // Fallback: reconnect STA РЅРµ СѓРґР°Р»СЃСЏ Р·Р° Р»РёРјРёС‚ вЂ” РїРѕРґРЅСЏС‚СЊ setup AP, С‡С‚РѕР±С‹
    // Web UI РѕСЃС‚Р°РІР°Р»СЃСЏ РґРѕСЃС‚СѓРїРЅС‹Рј (Рё СЃРІСЏР·СЊ СЃ СЃР°С‚РµР»Р»РёС‚Р°РјРё РЅРµ РїРѕС‚РµСЂСЏР»Р°СЃСЊ).
    if (g_reconnectPending) {
        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            g_reconnectPending = false;
            Logger::infof("wifi", "reconnect OK, IP: %s", WiFi.localIP().toString().c_str());
        } else if (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED ||
                   st == WL_CONNECTION_LOST || (millis() - g_reconnectAtMs > kReconnectTimeoutMs)) {
            g_reconnectPending = false;
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false, true);
            if (g_cfg.wifiMode != WifiMode::ApDirect) {
                Logger::warn("wifi", "reconnect failed вЂ” setup AP");
                g_setupMode = true;
                startSetupAp();
            } else {
                Logger::warn("wifi", "reconnect failed вЂ” AP remains, setup mode");
                g_setupMode = true;
            }
        }
    }

    // Captive portal: РѕР±СЂР°Р±РѕС‚Р°С‚СЊ DNS-Р·Р°РїСЂРѕСЃС‹ С‚РµР»РµС„РѕРЅР° (no-op, РµСЃР»Рё РЅРµ Р·Р°РїСѓС‰РµРЅ).
    g_dns.processNextRequest();

    delay(10);
}
