// main.cpp — мастер-узел Wireless Audio 2.1 на ESP32-S3 (сабвуфер).
//
// Адаптация под ESP32-S3 (см. docs/PLAN.md и ТЗ): S3 не поддерживает A2DP,
// поэтому источник аудио — Wi-Fi UDP PCM со смартфона.
//
// Этап 1 (текущий): загрузка платы, стартовая диагностика (chip/flash/PSRAM),
// Wi-Fi (AP_DIRECT, STA или APSTA-репитер), Web UI + REST API (включая
// настройку Wi-Fi подключения со смартфона), UDP-listener на AUDIO_UDP_PORT,
// serial-консоль. Аудио-конвейер (jitter buffer, DSP, TX на сателлиты) —
// Этап 2+.
//
// Настройка Wi-Fi: если STA-подключение к домашней сети не удалось (сеть не
// найдена, неверный пароль), мастер поднимает AP настройки и запускает Web UI
// на http://192.168.4.1 — со смартфона выбирается сеть, вводятся SSID/пароль,
// креды сохраняются в NVS и мастер перезагружается в STA/APSTA-режиме.
//
// Стартовая диагностика (ТЗ §14.2):
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
#include <vector>

#include "node_config.h"
#include "storage.h"
#include "logger.h"
#include "master_s3_config.h"
#include "web_server.h"

using namespace audio21;

// ---------------------------------------------------------------------------
// Глобальное состояние
// ---------------------------------------------------------------------------

static NodeConfig g_cfg;
static WiFiUDP g_udp;
static uint32_t g_packetsRx = 0;
static uint32_t g_packetBytesRx = 0;

// Web UI + REST API. Аудио-конвейер (pipeline/delay/espnow) в Этапе 1 не
// реализован — передаём nullptr: аудио-эндпоинты вернут "unavailable",
// доступны настройка Wi-Fi, статус и reboot.
static MasterWebServer g_webServer(g_cfg);

// Режим настройки Wi-Fi: STA не подключился → мастер поднял AP настройки.
static bool g_setupMode = false;

// Captive portal: перехватывает DNS-запросы телефона (любой домен → softAPIP()),
// браузер открывает http://192.168.4.1/ → onNotFound → страница настройки.
static DNSServer g_dns;

// ---------------------------------------------------------------------------
// Скан Wi-Fi сетей ДО старта AP (B9)
// ---------------------------------------------------------------------------

// WiFi.scanNetworks() при активном Soft-AP отключает радио — телефон теряет
// сеть в момент POST с кредами («не сохраняет подключение»). Поэтому список
// сетей сканируется заранее (STA-режим, AP ещё не поднят) и кешируется в
// MasterWebServer; GET /api/wifi/scan отдаёт кеш.
static void scanWifiBeforeAp() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    delay(100);
    int n = WiFi.scanNetworks(); // блокирующий скан (~1-2 с)
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
// Wi-Fi: AP_DIRECT, STA или APSTA-репитер (ТЗ §6.1)
// ---------------------------------------------------------------------------

// NAPT — репитер: AP-клиенты (смартфон) получают интернет через STA (uplink).
// API есть только в Arduino core 3.x (IDF 5.1+), где lwip собран с
// CONFIG_LWIP_IP_FORWARD=y / CONFIG_LWIP_IPV4_NAPT=y (pioarduino 55.03.311).
static void enableNapt() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap != nullptr && esp_netif_napt_enable(ap) == ESP_OK) {
        Logger::info("wifi", "NAPT enabled — AP clients routed to upstream");
    } else {
        Logger::error("wifi", "NAPT enable failed (lwip без CONFIG_LWIP_IPV4_NAPT?)");
    }
#else
    Logger::warn("wifi", "NAPT requires Arduino core 3.x — skipping");
#endif
}

// Поднять AP настройки (используется при неудачном STA-подключении).
static bool startSetupAp() {
    if (WiFi.getMode() == WIFI_AP) return true; // AP уже поднят (ApSta)
    scanWifiBeforeAp(); // список сетей для Web UI — ДО старта AP (B9)
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
    WiFi.setSleep(false); // отключить power save для аудиоузлов (ТЗ §16.3)

    if (g_cfg.wifiMode == WifiMode::ApDirect) {
        scanWifiBeforeAp(); // список сетей для Web UI — ДО старта AP (B9)
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
        // Репитер: AP для смартфона + STA (домашняя сеть) + NAPT.
        scanWifiBeforeAp(); // список сетей для Web UI — ДО старта AP (B9)
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
            // Домашняя сеть недоступна — AP остаётся, включаем режим настройки.
            // Отключаем авто-реконнект STA: постоянные попытки подключения
            // мешают сканированию сетей в Web UI и нагружают радиомодуль.
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false, true);
            Logger::warn("wifi", "upstream not connected — setup mode (Web UI on AP)");
            g_setupMode = true;
        }
        return true; // AP работает в любом случае
    }

    // STA
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
    Logger::infof("wifi", "Connecting to '%s'...", g_cfg.wifiSsid);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        // Не удалось подключиться к домашней сети — поднимаем AP настройки,
        // чтобы пользователь мог задать SSID/пароль через Web UI.
        // Отключаем авто-реконнект STA (мешает скану сетей в Web UI).
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, true);
        Logger::warn("wifi", "connect failed — switching to setup AP");
        g_setupMode = true;
        return startSetupAp();
    }
    Logger::infof("wifi", "connected, IP: %s", WiFi.localIP().toString().c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Serial-консоль
// ---------------------------------------------------------------------------

// Блокирующий ping через esp_ping (IDF 5.x). Используется командой `net`
// для диагностики доступа в сеть с самого мастера.
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
    xSemaphoreTake(g_pingDone, 0); // сброс семафора
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
        Serial.printf("psram: %u MB\n", ESP.getPsramSize() / (1024 * 1024));
        return;
    }

    // Ручная настройка Wi-Fi через консоль: `wifi <ssid> <password>`
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
        // Проверка AP-интерфейса: пинг первого клиента (192.168.4.x)
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

    // mDNS (F16): доступ по http://<hostname>.local (дефолт audio-master.local).
    // В режиме настройки mDNS на AP не работает — Web UI доступен по IP.
    if (!g_setupMode && MDNS.begin(g_cfg.hostname)) {
        Logger::infof("master", "mDNS: http://%s.local", g_cfg.hostname);
        MDNS.addService("http", "tcp", 80);
    }

    // Web UI: в режиме настройки — на AP (http://192.168.4.1), иначе — по IP/mDNS.
    g_webServer.begin();
    if (g_setupMode) {
        Logger::infof("master", "Wi-Fi setup: connect to AP '%s', open http://192.168.4.1",
                      g_cfg.wifiApSsid);
    } else {
        Logger::infof("master", "Web UI: http://%s", WiFi.localIP().toString().c_str());
    }

    // Captive portal (B9): при активном AP перехватываем DNS (любой домен →
    // softAPIP()), чтобы телефон автоматически открыл страницу настройки.
    if (WiFi.getMode() & WIFI_AP) {
        if (g_dns.start(53, "*", WiFi.softAPIP())) {
            Logger::info("master", "DNS captive portal: * -> softAPIP");
        } else {
            Logger::error("master", "DNS start failed (port 53 busy?)");
        }
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

    // Web UI: обработка запросов и сохранение конфига по кнопке.
    g_webServer.handleClient();
    if (g_webServer.saveRequested()) {
        ConfigStorage::save(g_cfg);
        g_webServer.clearSaveRequested();
        Logger::info("master", "config saved via Web UI");
    }

    // Captive portal: обработать DNS-запросы телефона (no-op, если не запущен).
    g_dns.processNextRequest();

    delay(10);
}