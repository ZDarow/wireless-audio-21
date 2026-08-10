// main.cpp — мастер-узел Wireless Audio 2.1 на ESP32-S3 (сабвуфер).
//
// Адаптация под ESP32-S3 (см. docs/PLAN.md и ТЗ): S3 не поддерживает A2DP,
// поэтому источник аудио — Wi-Fi UDP PCM со смартфона.
//
// Этап 1 (текущий): загрузка платы, стартовая диагностика (chip/flash/PSRAM),
// Wi-Fi (AP_DIRECT или STA), UDP-listener на AUDIO_UDP_PORT, serial-консоль.
// Аудио-конвейер (jitter buffer, DSP, TX на сателлиты) — Этап 2+.
//
// Стартовая диагностика (ТЗ §14.2):
//   chip model, flash size, PSRAM size, Wi-Fi mode, IP, UDP audio port,
//   I2S pins, satellite MAC, transport mode, crossover, delays.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>

#include "node_config.h"
#include "storage.h"
#include "logger.h"
#include "master_s3_config.h"

using namespace audio21;

// ---------------------------------------------------------------------------
// Глобальное состояние
// ---------------------------------------------------------------------------

static NodeConfig g_cfg;
static WiFiUDP g_udp;
static uint32_t g_packetsRx = 0;
static uint32_t g_packetBytesRx = 0;

// ---------------------------------------------------------------------------
// Стартовая диагностика (ТЗ §14.2)
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

    // Критерий корректности PSRAM (ТЗ §14.3).
    if (ESP.getPsramSize() == 0) {
        Logger::error("diag", "PSRAM size = 0 — неверная плата или board_build.arduino.memory_type");
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi: AP_DIRECT или STA (ТЗ §6.1)
// ---------------------------------------------------------------------------

static bool initWifi() {
    WiFi.disconnect(true);
    WiFi.setSleep(false); // отключить power save для аудиоузлов (ТЗ §16.3)

    if (g_cfg.wifiMode == WifiMode::ApDirect) {
        WiFi.mode(WIFI_AP);
        bool ok = WiFi.softAP(g_cfg.wifiSsid, g_cfg.wifiPassword, kDefaultWifiChannel);
        if (!ok) {
            Logger::error("wifi", "softAP failed");
            return false;
        }
        delay(200);
        Logger::infof("wifi", "AP '%s' on channel %d, IP: %s",
                      g_cfg.wifiSsid, kDefaultWifiChannel,
                      WiFi.softAPIP().toString().c_str());
        return true;
    }

    // STA
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
    Logger::infof("wifi", "Connecting to '%s'...", g_cfg.wifiSsid);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        Logger::error("wifi", "connect failed");
        return false;
    }
    Logger::infof("wifi", "connected, IP: %s", WiFi.localIP().toString().c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Serial-консоль
// ---------------------------------------------------------------------------

static void handleConsoleCommand(const String& line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd == "status") {
        Serial.println("role: master (s3)");
        Serial.printf("source: %s\n", sourceToString(g_cfg.source));
        Serial.printf("wifi_mode: %s\n", wifiModeToString(g_cfg.wifiMode));
        Serial.printf("wifi_ip: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("udp_port: %u\n", g_cfg.udpAudioPort);
        Serial.printf("packets_rx: %lu\n", (unsigned long)g_packetsRx);
        Serial.printf("bytes_rx: %lu\n", (unsigned long)g_packetBytesRx);
        Serial.printf("psram: %u MB\n", ESP.getPsramSize() / (1024 * 1024));
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

    // UDP-listener аудио от смартфона (Этап 2: приём PCM-пакетов).
    if (g_udp.begin(g_cfg.udpAudioPort)) {
        Logger::infof("master", "UDP audio listener on port %u", g_cfg.udpAudioPort);
    } else {
        Logger::error("master", "UDP begin failed");
    }

    Logger::info("master", "Ready. Type 'status' for info.");
}

void loop() {
    // Этап 1: считаем пакеты, разбор — в Этапе 2 (udp_audio_receiver).
    int packetSize = g_udp.parsePacket();
    if (packetSize > 0) {
        g_packetsRx++;
        g_packetBytesRx += static_cast<uint32_t>(packetSize);
        char discard[64];
        while (g_udp.available()) g_udp.read(discard, sizeof(discard));
    }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        handleConsoleCommand(line);
    }

    delay(10);
}
