// web_server.h вЂ” Web UI + REST API РјР°СЃС‚РµСЂ-СѓР·Р»Р° (РўР—_Р’РµР±.md, РїРѕР»РЅР°СЏ СЂРµР°Р»РёР·Р°С†РёСЏ).
//
// РСЃРїРѕР»СЊР·СѓРµС‚ РІСЃС‚СЂРѕРµРЅРЅС‹Р№ WebServer ESP32 Arduino (СЃРёРЅС…СЂРѕРЅРЅС‹Р№) + ArduinoJson.
// Р”РІР° СЃРµСЂРІРµСЂР° (B9): STA-РёРЅС‚РµСЂС„РµР№СЃ (localIP:80) + AP-РёРЅС‚РµСЂС„РµР№СЃ (softAPIP:80),
// С‡С‚РѕР±С‹ Web UI Р±С‹Р» РґРѕСЃС‚СѓРїРµРЅ РєР»РёРµРЅС‚Р°Рј AP РІ APSTA-СЂРµР¶РёРјРµ.
//
// Р Р°Р·РґРµР»С‹ (РўР— В§4): Dashboard, Wi-Fi, Internet, Audio, Delays, Satellites,
// System, Update, Logs. REST API вЂ” РўР— В§15-17. РђРІС‚РѕСЂРёР·Р°С†РёСЏ вЂ” В§18/В§23.
//
// РђСѓРґРёРѕ-Р·Р°РІРёСЃРёРјРѕСЃС‚Рё (pipeline, delay, espnow, СЃС‚Р°С‚СѓСЃС‹) вЂ” РѕРїС†РёРѕРЅР°Р»СЊРЅС‹Рµ
// (СѓРєР°Р·Р°С‚РµР»Рё, РґРµС„РѕР»С‚ nullptr): РјР°СЃС‚РµСЂ ESP32-S3 (СЌС‚Р°Рї 1, Р±РµР· Р°СѓРґРёРѕ-РєРѕРЅРІРµР№РµСЂР°)
// РёСЃРїРѕР»СЊР·СѓРµС‚ С‚РѕС‚ Р¶Рµ Web UI РґР»СЏ РЅР°СЃС‚СЂРѕР№РєРё Wi-Fi, РёРЅС‚РµСЂРЅРµС‚Р° Рё РґРёР°РіРЅРѕСЃС‚РёРєРё;
// Р°СѓРґРёРѕ-СЌРЅРґРїРѕРёРЅС‚С‹ РїСЂРё СЌС‚РѕРј РїСЂРёРјРµРЅСЏСЋС‚ РёР·РјРµРЅРµРЅРёСЏ С‚РѕР»СЊРєРѕ Рє РєРѕРЅС„РёРіСѓ.
//
// Header-only. РўРѕР»СЊРєРѕ РґР»СЏ РјР°СЃС‚РµСЂ-СѓР·Р»Р°.
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <time.h>
#include <vector>
#include <utility>

#include "node_config.h"
#include "pcm_pipeline.h"
#include "delay_line.h"
#include "espnow.h"
#include "storage.h"
#include "logs.h"
#include "internet_check.h"
#include "auth.h"
#include "wifi_store.h"

namespace audio21 {

// РҐСѓРє РїСЂРѕРІРµСЂРєРё РёРЅС‚РµСЂРЅРµС‚Р° РґР»СЏ InternetChecker (РўР— В§7.4). Р РµР°Р»РёР·Р°С†РёСЏ вЂ” СЃС‹СЂРѕР№
// HTTP/1.1 РїРѕРІРµСЂС… WiFiClient: СЂРµР·РѕР»РІРёРј РёРјСЏ РІ IPv4 (WiFi.hostByName), С€Р»С‘Рј
// GET Рё С‡РёС‚Р°РµРј РїРµСЂРІСѓСЋ СЃС‚СЂРѕРєСѓ РѕС‚РІРµС‚Р° (РєРѕРґ 200/204 в†’ online, 3xx в†’ captive
// portal). HTTPClient core 3.x РѕС€РёР±РѕС‡РЅРѕ РІРѕР·РІСЂР°С‰Р°РµС‚ connection refused РїСЂРё
// СЂР°Р±РѕС‡РµРј TCP-СЃРѕРµРґРёРЅРµРЅРёРё (РїСЂРѕРІРµСЂРµРЅРѕ РЅР° Р¶РµР»РµР·Рµ), РїРѕСЌС‚РѕРјСѓ РѕР±С…РѕРґРёРј РµРіРѕ.
inline HttpCheckResult httpInternetCheck(const char* url, uint32_t timeoutMs) {
    HttpCheckResult r;
    if (!url || !url[0]) { r.connectFailed = 1; return r; }

    // Р Р°Р·Р±РѕСЂ URL: http://host[:port]/path
    String u(url);
    String host;
    uint16_t port = 80;
    String uri = "/";
    int scheme = u.indexOf("://");
    String rest = (scheme >= 0) ? u.substring(scheme + 3) : u;
    int slash = rest.indexOf('/');
    String hostport = (slash >= 0) ? rest.substring(0, slash) : rest;
    if (slash >= 0) uri = rest.substring(slash);
    int colon = hostport.indexOf(':');
    if (colon >= 0) {
        host = hostport.substring(0, colon);
        port = (uint16_t)hostport.substring(colon + 1).toInt();
    } else {
        host = hostport;
    }

    IPAddress ip;
    if (!WiFi.hostByName(host.c_str(), ip) || ip == IPAddress(0, 0, 0, 0)) {
        r.connectFailed = 1;
        r.statusCode = -1;
        return r;
    }

    uint32_t t0 = millis();
    WiFiClient client;
    if (!client.connect(ip, port, timeoutMs)) {
        r.latencyMs = millis() - t0;
        r.connectFailed = 1;
        return r;
    }

    client.setTimeout((int)(timeoutMs / 1000 < 1 ? 1 : timeoutMs / 1000));
    client.print(String("GET ") + uri + " HTTP/1.1\r\n"
                 "Host: " + host + "\r\n"
                 "User-Agent: audio21-master/0.2\r\n"
                 "Connection: close\r\n\r\n");

    int code = -1;
    unsigned long deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            code = line.substring(9).toInt();
            break;
        }
        if (line.length() == 0) continue;
    }
    r.latencyMs = millis() - t0;
    r.statusCode = code;
    r.httpConnected = (code != -1);
    if (code == -1) r.connectFailed = 1;
    client.stop();
    return r;
}

class MasterWebServer {
public:
    MasterWebServer(NodeConfig& cfg,
                    PcmPipeline* pipeline = nullptr,
                    DelayLine** delayLeft = nullptr,
                    DelayLine** delayRight = nullptr,
                    DelayLine** delaySub = nullptr,
                    EspNowTransport* espnow = nullptr,
                    volatile bool* leftOnline = nullptr,
                    volatile bool* rightOnline = nullptr,
                    volatile bool* a2dpConnected = nullptr)
        : m_cfg(cfg), m_pipeline(pipeline),
          m_delayLeft(delayLeft), m_delayRight(delayRight), m_delaySub(delaySub),
          m_espnow(espnow),
          m_leftOnline(leftOnline), m_rightOnline(rightOnline),
          m_a2dpConnected(a2dpConnected) {}

    // --- РРЅСЉРµРєС†РёРё РёР· main.cpp ---
    void setInternetChecker(InternetChecker* ic) { m_net = ic; }
    void setLogs(LogRing* logs) { m_logs = logs; }

    // Р—Р°РїСѓСЃС‚РёС‚СЊ СЃРµСЂРІРµСЂС‹ (Wi-Fi РґРѕР»Р¶РµРЅ Р±С‹С‚СЊ РїРѕРґРєР»СЋС‡С‘РЅ).
    void begin() {
        // РљР°СЃС‚РѕРјРЅС‹Рµ Р·Р°РіРѕР»РѕРІРєРё РЅСѓР¶РЅРѕ Р·Р°СЂРµРіРёСЃС‚СЂРёСЂРѕРІР°С‚СЊ Р”Рћ begin() (close() РёРЅР°С‡Рµ
        // РѕСЃС‚Р°РІРёС‚ С‚РѕР»СЊРєРѕ Authorization). Cookie вЂ” РґР»СЏ СЃРµСЃСЃРёРё, X-CSRF-Token вЂ” РґР»СЏ CSRF.
        const char* hdrKeys[] = { "Cookie", "X-CSRF-Token" };
        if (WiFi.status() == WL_CONNECTED) {
            m_server = new WebServer(WiFi.localIP(), 80);
            m_server->collectHeaders(hdrKeys, 2);
            bindRoutes(*m_server);
            m_server->begin();
        }
        if (WiFi.getMode() & WIFI_AP) {
            m_apServer = new WebServer(WiFi.softAPIP(), 80);
            m_apServer->collectHeaders(hdrKeys, 2);
            bindRoutes(*m_apServer);
            m_apServer->begin();
        }
    }

    void handleClient() {
        if (m_server) m_server->handleClient();
        if (m_apServer) m_apServer->handleClient();
    }

    // РљРµС€ РЅР°Р№РґРµРЅРЅС‹С… Wi-Fi СЃРµС‚РµР№ (Р·Р°РїРѕР»РЅСЏРµС‚СЃСЏ РґРѕ СЃС‚Р°СЂС‚Р° AP, B9).
    struct WifiNetInfo {
        String ssid;
        int rssi;
        bool open;
    };
    void setWifiCache(std::vector<WifiNetInfo> cache) { m_wifiCache = std::move(cache); }

    // Р¤Р»Р°Рі В«Р·Р°РїСЂРѕС€РµРЅРѕ СЃРѕС…СЂР°РЅРµРЅРёРµВ» вЂ” РѕРїСЂР°С€РёРІР°РµС‚СЃСЏ РІ loop() main.cpp.
    bool saveRequested() const { return m_saveRequested; }
    void clearSaveRequested() { m_saveRequested = false; }

    // Р¤Р»Р°Рі РїРµСЂРµРїРѕРґРєР»СЋС‡РµРЅРёСЏ Wi-Fi (РґР»СЏ main.cpp).
    bool reconnectRequested() const { return m_reconnectRequested; }
    void clearReconnectRequested() { m_reconnectRequested = false; }

    // РЎС‚Р°С‚СѓСЃ Р°РІС‚РѕСЂРёР·Р°С†РёРё (РґР»СЏ main.cpp/РєРѕРЅСЃРѕР»Рё).
    bool sessionActive() const { return m_sessionActive; }

private:
    // ------------------------------------------------------------------
    // Р РѕСѓС‚С‹
    // ------------------------------------------------------------------
    void bindRoutes(WebServer& s) {
        s.on("/", HTTP_GET, [this, &s]() { handleRoot(s); });
        s.on("/login", HTTP_GET, [this, &s]() { handleRoot(s); });
        s.on("/api/status", HTTP_GET, [this, &s]() { handleStatus(s); });
        s.on("/api/wifi/status", HTTP_GET, [this, &s]() { handleWifiStatus(s); });
        s.on("/api/wifi/scan", HTTP_GET, [this, &s]() { handleWifiScan(s); });
        s.on("/api/wifi/connect", HTTP_POST, [this, &s]() { handleWifiConnect(s); });
        s.on("/api/wifi/save", HTTP_POST, [this, &s]() { handleWifiSave(s); });
        s.on("/api/wifi/forget", HTTP_POST, [this, &s]() { handleWifiForget(s); });
        s.on("/api/wifi/profiles", HTTP_GET, [this, &s]() { handleWifiProfiles(s); });
        s.on("/api/net/internet", HTTP_GET, [this, &s]() { handleInternetStatus(s); });
        s.on("/api/net/check", HTTP_POST, [this, &s]() { handleInternetCheck(s); });
        s.on("/api/volume", HTTP_PUT, [this, &s]() { handleVolume(s); });
        s.on("/api/volume", HTTP_POST, [this, &s]() { handleVolume(s); });
        s.on("/api/crossover", HTTP_PUT, [this, &s]() { handleCrossover(s); });
        s.on("/api/crossover", HTTP_POST, [this, &s]() { handleCrossover(s); });
        s.on("/api/delay", HTTP_PUT, [this, &s]() { handleDelay(s); });
        s.on("/api/delay", HTTP_POST, [this, &s]() { handleDelay(s); });
        s.on("/api/mute", HTTP_POST, [this, &s]() { handleMute(s); });
        s.on("/api/transport", HTTP_POST, [this, &s]() { handleTransport(s); });
        s.on("/api/pair", HTTP_POST, [this, &s]() { handlePair(s); });
        s.on("/api/save", HTTP_POST, [this, &s]() { handleSave(s); });
        s.on("/api/system/reboot", HTTP_POST, [this, &s]() { handleReboot(s); });
        s.on("/api/system/factory_reset", HTTP_POST, [this, &s]() { handleFactoryReset(s); });
        s.on("/api/system/config/export", HTTP_GET, [this, &s]() { handleConfigExport(s); });
        s.on("/api/system/config/import", HTTP_POST, [this, &s]() { handleConfigImport(s); });
        s.on("/api/logs", HTTP_GET, [this, &s]() { handleLogs(s); });
        s.on("/api/diagnostics", HTTP_GET, [this, &s]() { handleDiagnostics(s); });
        s.on("/api/login", HTTP_POST, [this, &s]() { handleLogin(s); });
        s.on("/api/logout", HTTP_POST, [this, &s]() { handleLogout(s); });
        s.on("/api/admin/setup", HTTP_POST, [this, &s]() { handleAdminSetup(s); });
        s.on("/api/update", HTTP_POST,
             [this, &s]() { handleUpdateEnd(s); },
             [this, &s]() { handleUpdateUpload(s); });
        // Captive portal probes (B9).
        s.on("/generate_204", HTTP_GET, [this, &s]() { redirectRoot(s); });
        s.on("/hotspot-detect.html", HTTP_GET, [this, &s]() { redirectRoot(s); });
        s.on("/ncsi.txt", HTTP_GET, [this, &s]() { s.send(200, "text/plain", "Microsoft NCSI"); });
        s.on("/connecttest.txt", HTTP_GET, [this, &s]() { s.send(200, "text/plain", ""); });
        s.on("/fwlink", HTTP_GET, [this, &s]() { redirectRoot(s); });
        s.onNotFound([this, &s]() { redirectRoot(s); });
    }

    // ------------------------------------------------------------------
    // РђРІС‚РѕСЂРёР·Р°С†РёСЏ (РўР— В§18, В§23)
    // ------------------------------------------------------------------
    // РђРІС‚РѕСЂРёР·РѕРІР°РЅ С‚РѕР»СЊРєРѕ РѕР±Р»Р°РґР°С‚РµР»СЊ cookie С‚РµРєСѓС‰РµР№ СЃРµСЃСЃРёРё (m_sessionActive
    // вЂ” РіР»РѕР±Р°Р»СЊРЅС‹Р№ С„Р»Р°Рі В«СЃРµСЃСЃРёСЏ СЃРѕР·РґР°РЅР°В», РЅРѕ РЅРµ Р·Р°РјРµРЅСЏРµС‚ cookie).
    bool isAuthed(WebServer& s) const {
        if (!m_cfg.authEnabled) return true;
        if (!m_sessionActive) return false;
        String cookie = s.header("Cookie");
        if (cookie.length() == 0) return false;
        int pos = cookie.indexOf("session=");
        if (pos < 0) return false;
        int end = cookie.indexOf(';', pos);
        String token = cookie.substring(pos + 8, end < 0 ? cookie.length() : end);
        token.trim();
        if (token.length() == 0) return false;
        return strcmp(token.c_str(), m_sessionToken) == 0;
    }

    bool csrfOk(WebServer& s) const {
        if (!m_cfg.authEnabled) return true;
        if (!m_sessionActive) return false;
        if (!isAuthed(s)) return false; // cookie СЃРµСЃСЃРёРё РѕР±СЏР·Р°РЅР° СЃРѕРІРїР°РґР°С‚СЊ
        String header = s.header("X-CSRF-Token");
        if (header.length() == 0) return false;
        char expected[65];
        Auth::csrfToken(m_sessionToken, expected);
        return header == expected;
    }

    // ------------------------------------------------------------------
    // РћС‚РІРµС‚С‹
    // ------------------------------------------------------------------
    static void sendJson(WebServer& s, int code, JsonDocument& doc) {
        String body;
        serializeJson(doc, body);
        s.sendHeader("Cache-Control", "no-store");
        s.send(code, "application/json", body);
    }

    static void sendOk(WebServer& s, const char* msg = "ok") {
        JsonDocument doc;
        doc["ok"] = true;
        if (msg) doc["status"] = msg;
        sendJson(s, 200, doc);
    }

    static void sendErr(WebServer& s, const char* msg = "err") {
        JsonDocument doc;
        doc["ok"] = false;
        doc["error"] = msg;
        sendJson(s, 400, doc);
    }

    static void redirectRoot(WebServer& s) {
        s.send(200, "text/html", "<html><body><script>location.href='/'</script></body></html>");
    }

    // ------------------------------------------------------------------
    // РЎС‚СЂР°РЅРёС†С‹ (SPA вЂ” РѕРґРёРЅ HTML, СЂР°Р·РґРµР»С‹ С‡РµСЂРµР· hash-РЅР°РІРёРіР°С†РёСЋ)
    // ------------------------------------------------------------------
    void handleRoot(WebServer& s) {
        s.send(200, "text/html", kPageHtml);
    }

    // ------------------------------------------------------------------
    // GET /api/status (РўР— В§16.1)
    // ------------------------------------------------------------------
    void handleStatus(WebServer& s) {
        JsonDocument doc;
        doc["system"]["version"] = "0.2.0";
        doc["system"]["hostname"] = m_cfg.hostname;
        doc["system"]["uptime_sec"] = millis() / 1000;
        doc["system"]["heap_free"] = ESP.getFreeHeap();
        doc["system"]["psram_free"] = ESP.getFreePsram();
        doc["system"]["auth_enabled"] = m_cfg.authEnabled;
        doc["system"]["authed"] = isAuthed(s);
        if (m_cfg.authEnabled && isAuthed(s)) {
            char csrf[65];
            Auth::csrfToken(m_sessionToken, csrf);
            doc["system"]["csrf"] = csrf;
        }

        doc["wifi"]["ssid"] = m_cfg.wifiSsid;
        doc["wifi"]["rssi"] = WiFi.RSSI();
        doc["wifi"]["ip"] = WiFi.localIP().toString();
        doc["wifi"]["ap_ip"] = WiFi.softAPIP().toString();
        doc["wifi"]["mode"] = wifiModeToString(m_cfg.wifiMode);
        doc["wifi"]["state"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
        doc["wifi"]["internet"] = m_net ? m_net->statusName() : "unknown";

        doc["audio"]["source"] = sourceToString(m_cfg.source);
        doc["audio"]["playing"] = m_a2dpConnected ? *m_a2dpConnected : false;
        doc["audio"]["sample_rate"] = m_cfg.sampleRate;
        doc["audio"]["bits"] = m_cfg.bitsPerSample;
        doc["audio"]["channels"] = m_cfg.channels;
        doc["audio"]["volume"] = m_cfg.masterVolume;
        doc["audio"]["mute"] = m_cfg.mute;
        doc["audio"]["crossover_hz"] = m_cfg.crossoverHz;

        doc["delays"]["left_ms"] = m_cfg.delayLeftMs;
        doc["delays"]["right_ms"] = m_cfg.delayRightMs;
        doc["delays"]["sub_ms"] = m_cfg.delaySubMs;

        JsonObject sats = doc["satellites"].to<JsonObject>();
        sats["left"] = (m_leftOnline && *m_leftOnline) ? "online" : "offline";
        sats["right"] = (m_rightOnline && *m_rightOnline) ? "online" : "offline";
        sendJson(s, 200, doc);
    }

    // ------------------------------------------------------------------
    // GET /api/wifi/status (РўР— В§15.1)
    // ------------------------------------------------------------------
    void handleWifiStatus(WebServer& s) {
        JsonDocument doc;
        doc["mode"] = wifiModeToString(m_cfg.wifiMode);
        doc["state"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
        doc["ssid"] = WiFi.SSID();
        doc["bssid"] = WiFi.BSSIDstr();
        doc["rssi"] = WiFi.RSSI();
        doc["channel"] = WiFi.channel();
        doc["ip"] = WiFi.localIP().toString();
        doc["netmask"] = WiFi.subnetMask().toString();
        doc["gateway"] = WiFi.gatewayIP().toString();
        doc["dns"] = WiFi.dnsIP().toString();
        doc["mac"] = WiFi.macAddress();
        doc["hostname"] = m_cfg.hostname;
        doc["mdns"] = String(m_cfg.hostname) + ".local";
        sendJson(s, 200, doc);
    }

    // ------------------------------------------------------------------
    // GET /api/wifi/scan (РўР— В§15.2) вЂ” РєРµС€ РґРѕ СЃС‚Р°СЂС‚Р° AP (B9) РёР»Рё Р¶РёРІРѕР№ СЃРєР°РЅ.
    // ------------------------------------------------------------------
    void handleWifiScan(WebServer& s) {
        JsonDocument doc;
        JsonArray nets = doc["networks"].to<JsonArray>();
        if (!m_wifiCache.empty()) {
            for (const WifiNetInfo& n : m_wifiCache) {
                JsonObject o = nets.add<JsonObject>();
                o["ssid"] = n.ssid;
                o["rssi"] = n.rssi;
                o["channel"] = -1;
                o["security"] = n.open ? "OPEN" : "WPA";
                o["hidden"] = false;
            }
            doc["ok"] = true;
            sendJson(s, 200, doc);
            return;
        }
        WiFi.setAutoReconnect(false);
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect(false, true);
            delay(100);
        }
        int n = WiFi.scanNetworks();
        for (int i = 0; i < n; i++) {
            JsonObject o = nets.add<JsonObject>();
            o["ssid"] = WiFi.SSID(i);
            o["rssi"] = WiFi.RSSI(i);
            o["channel"] = WiFi.channel(i);
            o["security"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "WPA";
            o["hidden"] = false;
        }
        WiFi.scanDelete();
        doc["ok"] = true;
        sendJson(s, 200, doc);
    }

    // ------------------------------------------------------------------
    // POST /api/wifi/connect (РўР— В§15.3)
    // ------------------------------------------------------------------
    void handleWifiConnect(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* ssid = doc["ssid"] | "";
        const char* pass = doc["password"] | "";
        bool save = doc["save"] | false;
        if (ssid[0] == '\0' || strlen(ssid) >= sizeof(m_cfg.wifiSsid)) { sendErr(s, "bad ssid"); return; }

        strlcpy(m_cfg.wifiSsid, ssid, sizeof(m_cfg.wifiSsid));
        if (pass) strlcpy(m_cfg.wifiPassword, pass, sizeof(m_cfg.wifiPassword));
        else m_cfg.wifiPassword[0] = '\0';

        if (save) {
            WifiProfile p;
            strlcpy(p.ssid, ssid, sizeof(p.ssid));
            strlcpy(p.password, m_cfg.wifiPassword, sizeof(p.password));
            p.hidden = doc["hidden"] | false;
            p.staticIp = strcmp(doc["ip_mode"] | "dhcp", "static") == 0;
            strlcpy(p.ip, doc["ip"] | "", sizeof(p.ip));
            strlcpy(p.netmask, doc["netmask"] | "", sizeof(p.netmask));
            strlcpy(p.gateway, doc["gateway"] | "", sizeof(p.gateway));
            strlcpy(p.dns, doc["dns"] | "", sizeof(p.dns));
            p.autoReconnect = doc["auto_reconnect"] | true;
            p.priority = (uint8_t)(doc["priority"] | 0);
            WifiStore::saveProfile(p);
            ConfigStorage::save(m_cfg);
        }

        JsonDocument out;
        out["ok"] = true;
        out["state"] = "connecting";
        sendJson(s, 200, out);
        m_reconnectRequested = true;
    }

    // ------------------------------------------------------------------
    // POST /api/wifi/save (РўР— В§15.4) вЂ” СЃРѕС…СЂР°РЅРёС‚СЊ РїСЂРѕС„РёР»СЊ Р±РµР· РїРѕРґРєР»СЋС‡РµРЅРёСЏ.
    // ------------------------------------------------------------------
    void handleWifiSave(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* ssid = doc["ssid"] | "";
        if (ssid[0] == '\0' || strlen(ssid) >= sizeof(m_cfg.wifiSsid)) { sendErr(s, "bad ssid"); return; }
        WifiProfile p;
        strlcpy(p.ssid, ssid, sizeof(p.ssid));
        const char* pass = doc["password"] | "";
        strlcpy(p.password, pass, sizeof(p.password));
        p.hidden = doc["hidden"] | false;
        p.staticIp = strcmp(doc["ip_mode"] | "dhcp", "static") == 0;
        strlcpy(p.ip, doc["ip"] | "", sizeof(p.ip));
        strlcpy(p.netmask, doc["netmask"] | "", sizeof(p.netmask));
        strlcpy(p.gateway, doc["gateway"] | "", sizeof(p.gateway));
        strlcpy(p.dns, doc["dns"] | "", sizeof(p.dns));
        p.autoReconnect = doc["auto_reconnect"] | true;
        p.priority = (uint8_t)(doc["priority"] | 0);
        if (WifiStore::saveProfile(p)) sendOk(s, "saved");
        else sendErr(s, "nvs save failed");
    }

    // ------------------------------------------------------------------
    // POST /api/wifi/forget (РўР— В§15.5)
    // ------------------------------------------------------------------
    void handleWifiForget(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* ssid = doc["ssid"] | "";
        if (WifiStore::forget(ssid)) sendOk(s, "forgotten");
        else sendErr(s, "not found");
    }

    // ------------------------------------------------------------------
    // GET /api/wifi/profiles (РўР— В§6.5) вЂ” СЃРїРёСЃРѕРє СЃРѕС…СЂР°РЅС‘РЅРЅС‹С… СЃРµС‚РµР№.
    // ------------------------------------------------------------------
    void handleWifiProfiles(WebServer& s) {
        JsonDocument doc;
        JsonArray arr = doc["profiles"].to<JsonArray>();
        WifiProfile profs[WifiStore::kMaxProfiles];
        int n = WifiStore::loadAll(profs);
        for (int i = 0; i < n; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = profs[i].ssid;
            o["hidden"] = profs[i].hidden;
            o["ip_mode"] = profs[i].staticIp ? "static" : "dhcp";
            o["priority"] = profs[i].priority;
            o["auto_reconnect"] = profs[i].autoReconnect;
        }
        sendJson(s, 200, doc);
    }

    // ------------------------------------------------------------------
    // GET /api/net/internet (РўР— В§15.6)
    // ------------------------------------------------------------------
    void handleInternetStatus(WebServer& s) {
        JsonDocument doc;
        NetStatus st = m_net ? m_net->status() : NetStatus::Disabled;
        doc["status"] = netStatusName(st);
        doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
        if (m_net) {
            doc["latency_ms"] = m_net->latencyMs();
            doc["last_check"] = m_net->lastCheckMs();
            doc["check_url"] = m_net->checkUrl();
            doc["dns_ok"] = m_net->dnsOk();
            doc["http_ok"] = m_net->httpOk();
        } else {
            doc["latency_ms"] = 0;
            doc["last_check"] = 0;
            doc["check_url"] = "";
            doc["dns_ok"] = false;
            doc["http_ok"] = false;
        }
        sendJson(s, 200, doc);
    }

    // ------------------------------------------------------------------
    // POST /api/net/check (РўР— В§15.7) вЂ” РїСЂРёРЅСѓРґРёС‚РµР»СЊРЅР°СЏ РїСЂРѕРІРµСЂРєР°.
    // ------------------------------------------------------------------
    void handleInternetCheck(WebServer& s) {
        if (m_net) m_net->forceCheck(httpInternetCheck);
        handleInternetStatus(s);
    }

    // ------------------------------------------------------------------
    // PUT /api/volume | POST /api/mute (РўР— В§16.2, В§16.5)
    // ------------------------------------------------------------------
    void handleVolume(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        if (doc["mute"].is<bool>()) {
            bool m = doc["mute"].as<bool>();
            m_cfg.mute = m;
            if (m_pipeline) m_pipeline->setMute(m);
            sendOk(s);
            return;
        }
        if (!doc["volume"].is<int>()) { sendErr(s, "missing volume|mute"); return; }
        int v = doc["volume"].as<int>();
        if (v < kVolumeMin || v > kVolumeMax) { sendErr(s, "volume out of range"); return; }
        m_cfg.masterVolume = v;
        if (m_pipeline) m_pipeline->setVolume(v);
        sendOk(s);
    }

    void handleMute(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        bool m = doc["mute"] | true;
        m_cfg.mute = m;
        if (m_pipeline) m_pipeline->setMute(m);
        sendOk(s);
    }

    // ------------------------------------------------------------------
    // PUT /api/crossover (РўР— В§16.3)
    // ------------------------------------------------------------------
    void handleCrossover(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        int hz = doc["crossover_hz"] | doc["hz"] | -1;
        if (hz < kCrossoverMinHz || hz > kCrossoverMaxHz) { sendErr(s, "hz out of range"); return; }
        m_cfg.crossoverHz = hz;
        if (m_pipeline) m_pipeline->setCrossoverHz(hz);
        sendOk(s);
    }

    // ------------------------------------------------------------------
    // PUT /api/delay (РўР— В§16.4)
    // ------------------------------------------------------------------
    void handleDelay(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* chan = doc["channel"] | "";
        int ms = doc["delay_ms"] | doc["ms"] | -1;
        if (ms < kMinDelayMs || ms > kMaxDelayMs) { sendErr(s, "ms out of range"); return; }

        if (strcmp(chan, "left") == 0) {
            m_cfg.delayLeftMs = ms;
            if (m_delayLeft && *m_delayLeft) (*m_delayLeft)->setDelayMs(ms);
        } else if (strcmp(chan, "right") == 0) {
            m_cfg.delayRightMs = ms;
            if (m_delayRight && *m_delayRight) (*m_delayRight)->setDelayMs(ms);
        } else if (strcmp(chan, "sub") == 0) {
            m_cfg.delaySubMs = ms;
            if (m_delaySub && *m_delaySub) (*m_delaySub)->setDelayMs(ms);
        } else { sendErr(s, "bad channel"); return; }
        sendOk(s);
    }

    // ------------------------------------------------------------------
    // POST /api/transport, /api/pair
    // ------------------------------------------------------------------
    void handleTransport(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* mode = doc["mode"] | "";
        if (strcmp(mode, "espnow") == 0) m_cfg.transport = TransportMode::EspNow;
        else if (strcmp(mode, "udp") == 0) m_cfg.transport = TransportMode::Udp;
        else { sendErr(s, "bad mode"); return; }
        sendOk(s);
    }

    void handlePair(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* side = doc["side"] | "";
        const char* macStr = doc["mac"] | "";
        MacAddr mac;
        if (!MacAddr::parse(macStr, mac)) { sendErr(s, "bad mac"); return; }
        if (strcmp(side, "left") == 0) {
            m_cfg.leftSatMac = mac;
            if (m_espnow) m_espnow->addPeer(mac);
        } else if (strcmp(side, "right") == 0) {
            m_cfg.rightSatMac = mac;
            if (m_espnow) m_espnow->addPeer(mac);
        } else { sendErr(s, "bad side"); return; }
        sendOk(s);
    }

    // ------------------------------------------------------------------
    // POST /api/save, /api/system/* (РўР— В§17)
    // ------------------------------------------------------------------
    void handleSave(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        m_saveRequested = true;
        sendOk(s);
    }

    void handleReboot(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        s.send(200, "application/json", "{\"ok\":true,\"status\":\"rebooting\"}");
        delay(100);
        ESP.restart();
    }

    void handleFactoryReset(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        ConfigStorage::erase();
        WifiStore::clearAll();
        sendOk(s, "erased");
        delay(200);
        ESP.restart();
    }

    void handleConfigExport(WebServer& s) {
        if (m_cfg.authEnabled && !isAuthed(s)) {
            s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
            return;
        }
        JsonDocument doc;
        doc["ok"] = true;
        doc["config"]["hostname"] = m_cfg.hostname;
        doc["config"]["wifi_ssid"] = m_cfg.wifiSsid;
        doc["config"]["wifi_ap_ssid"] = m_cfg.wifiApSsid;
        doc["config"]["volume"] = m_cfg.masterVolume;
        doc["config"]["crossover_hz"] = m_cfg.crossoverHz;
        doc["config"]["delay_left_ms"] = m_cfg.delayLeftMs;
        doc["config"]["delay_right_ms"] = m_cfg.delayRightMs;
        doc["config"]["delay_sub_ms"] = m_cfg.delaySubMs;
        doc["config"]["net_check_enabled"] = m_cfg.netCheckEnabled;
        doc["config"]["net_check_interval_sec"] = m_cfg.netCheckIntervalSec;
        doc["config"]["net_check_timeout_ms"] = m_cfg.netCheckTimeoutMs;
        doc["config"]["net_check_url"] = m_cfg.netCheckUrl;
        doc["config"]["ntp_enabled"] = m_cfg.ntpEnabled;
        doc["config"]["ntp_server"] = m_cfg.ntpServer;
        doc["config"]["timezone"] = m_cfg.timezone;
        doc["config"]["left_sat_mac"] = macToStr(m_cfg.leftSatMac);
        doc["config"]["right_sat_mac"] = macToStr(m_cfg.rightSatMac);
        sendJson(s, 200, doc);
    }

    static String macToStr(const MacAddr& mac) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac.bytes[0], mac.bytes[1], mac.bytes[2],
                 mac.bytes[3], mac.bytes[4], mac.bytes[5]);
        return String(buf);
    }

    void handleConfigImport(WebServer& s) {
        if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
        if (m_cfg.authEnabled && !isAuthed(s)) {
            s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
            return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        JsonObject cfg = doc["config"];
        if (!cfg.isNull()) {
            const char* v = cfg["hostname"] | "";
            if (v[0] && strlen(v) < sizeof(m_cfg.hostname)) strlcpy(m_cfg.hostname, v, sizeof(m_cfg.hostname));
            v = cfg["wifi_ssid"] | "";
            if (v[0] && strlen(v) < sizeof(m_cfg.wifiSsid)) strlcpy(m_cfg.wifiSsid, v, sizeof(m_cfg.wifiSsid));
            v = cfg["wifi_ap_ssid"] | "";
            if (v[0] && strlen(v) < sizeof(m_cfg.wifiApSsid)) strlcpy(m_cfg.wifiApSsid, v, sizeof(m_cfg.wifiApSsid));
            if (cfg["volume"].is<int>()) m_cfg.masterVolume = cfg["volume"];
            if (cfg["crossover_hz"].is<int>()) m_cfg.crossoverHz = cfg["crossover_hz"];
            if (cfg["delay_left_ms"].is<int>()) m_cfg.delayLeftMs = cfg["delay_left_ms"];
            if (cfg["delay_right_ms"].is<int>()) m_cfg.delayRightMs = cfg["delay_right_ms"];
            if (cfg["delay_sub_ms"].is<int>()) m_cfg.delaySubMs = cfg["delay_sub_ms"];
            if (cfg["net_check_enabled"].is<bool>()) m_cfg.netCheckEnabled = cfg["net_check_enabled"];
            if (cfg["net_check_interval_sec"].is<int>()) m_cfg.netCheckIntervalSec = cfg["net_check_interval_sec"];
            if (cfg["net_check_timeout_ms"].is<int>()) m_cfg.netCheckTimeoutMs = cfg["net_check_timeout_ms"];
            v = cfg["net_check_url"] | "";
            if (v[0] && strlen(v) < sizeof(m_cfg.netCheckUrl)) strlcpy(m_cfg.netCheckUrl, v, sizeof(m_cfg.netCheckUrl));
            if (cfg["ntp_enabled"].is<bool>()) m_cfg.ntpEnabled = cfg["ntp_enabled"];
            v = cfg["ntp_server"] | "";
            if (v[0] && strlen(v) < sizeof(m_cfg.ntpServer)) strlcpy(m_cfg.ntpServer, v, sizeof(m_cfg.ntpServer));
            v = cfg["timezone"] | "";
            if (v[0] && strlen(v) < sizeof(m_cfg.timezone)) strlcpy(m_cfg.timezone, v, sizeof(m_cfg.timezone));
            const char* lm = cfg["left_sat_mac"] | "";
            if (lm[0]) MacAddr::parse(lm, m_cfg.leftSatMac);
            const char* rm = cfg["right_sat_mac"] | "";
            if (rm[0]) MacAddr::parse(rm, m_cfg.rightSatMac);
            m_cfg.clamp();
        }
        ConfigStorage::save(m_cfg);
        sendOk(s, "imported");
    }

    // ------------------------------------------------------------------
    // GET /api/logs (РўР— В§17.5)
    // ------------------------------------------------------------------
    void handleLogs(WebServer& s) {
        JsonDocument doc;
        int limit = s.hasArg("limit") ? s.arg("limit").toInt() : 200;
        if (limit <= 0 || limit > 500) limit = 200;
        JsonArray arr = doc["logs"].to<JsonArray>();
        if (m_logs) {
            const char* lines[64];
            int n = m_logs->tail(lines, limit > 64 ? 64 : limit);
            for (int i = 0; i < n; i++) arr.add(lines[i]);
        }
        sendJson(s, 200, doc);
    }

    // ------------------------------------------------------------------
    // GET /api/diagnostics (РўР— В§17.6)
    // ------------------------------------------------------------------
    void handleDiagnostics(WebServer& s) {
        JsonDocument doc;
        doc["heap_free"] = ESP.getFreeHeap();
        doc["heap_min_free"] = ESP.getMinFreeHeap();
        doc["psram_free"] = ESP.getFreePsram();
        doc["psram_min_free"] = ESP.getMinFreePsram();
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["internet"] = m_net ? m_net->statusName() : "disabled";
        doc["uptime_sec"] = millis() / 1000;
        doc["wifi_mode"] = wifiModeToString(m_cfg.wifiMode);
        doc["mac"] = WiFi.macAddress();
        sendJson(s, 200, doc);
    }

    // ------------------------------------------------------------------
    // POST /api/login, /api/logout, /api/admin/setup (РўР— В§18)
    // ------------------------------------------------------------------
    void handleLogin(WebServer& s) {
        if (!m_cfg.authEnabled) { sendOk(s, "no password required"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* pass = doc["password"] | "";
        if (!Auth::checkPassword(pass, m_cfg.adminPasswordHash)) {
            s.send(401, "application/json", "{\"ok\":false,\"error\":\"bad password\"}");
            return;
        }
        Auth::newSessionToken(m_sessionToken);
        m_sessionActive = true;

        s.sendHeader("Set-Cookie", String("session=") + m_sessionToken + "; Path=/");
        sendOk(s, "logged in");
    }

    void handleLogout(WebServer& s) {
        m_sessionActive = false;
        m_sessionToken[0] = '\0';
        s.sendHeader("Set-Cookie", "session=; Path=/; Max-Age=0");
        sendOk(s, "logged out");
    }

    void handleAdminSetup(WebServer& s) {
        if (m_cfg.authEnabled) {
            // РџР°СЂРѕР»СЊ СѓР¶Рµ Р·Р°РґР°РЅ вЂ” СЃР±СЂРѕСЃ РІРѕР·РјРѕР¶РµРЅ С‚РѕР»СЊРєРѕ С‡РµСЂРµР· Р°РІС‚РѕСЂРёР·РѕРІР°РЅРЅСѓСЋ СЃРµСЃСЃРёСЋ.
            s.send(403, "application/json", "{\"ok\":false,\"error\":\"already configured\"}");
            return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* pass = doc["password"] | "";
        const char* confirm = doc["confirm"] | "";
        if (strlen(pass) < 4) { sendErr(s, "password too short"); return; }
        if (strcmp(pass, confirm) != 0) { sendErr(s, "password mismatch"); return; }
        Auth::hashPassword(pass, m_cfg.adminPasswordHash);
        m_cfg.authEnabled = true;
        ConfigStorage::save(m_cfg);
        Auth::newSessionToken(m_sessionToken);
        m_sessionActive = true;

        s.sendHeader("Set-Cookie", String("session=") + m_sessionToken + "; Path=/");
        sendOk(s, "admin configured");
    }

    // ------------------------------------------------------------------
    // OTA (РўР— В§12) вЂ” POST /api/update
    // ------------------------------------------------------------------
    void handleUpdateUpload(WebServer& s) {
        HTTPUpload& up = s.upload();
        if (up.status == UPLOAD_FILE_START) {
            // РўРѕР»СЊРєРѕ Р°РІС‚РѕСЂРёР·РѕРІР°РЅРЅР°СЏ СЃРµСЃСЃРёСЏ (cookie) + CSRF-С‚РѕРєРµРЅ.
            if (!csrfOk(s) || !isAuthed(s)) {
                s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
                return;
            }
            m_updateActive = false;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                s.send(500, "application/json", "{\"ok\":false,\"error\":\"update begin failed\"}");
                return;
            }
            m_updateActive = true;
        } else if (up.status == UPLOAD_FILE_WRITE) {
            if (!m_updateActive) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}"); return; }
            if (Update.write(up.buf, up.currentSize) != up.currentSize) {
                Update.abort();
                m_updateActive = false;
                s.send(500, "application/json", "{\"ok\":false,\"error\":\"write failed\"}");
            }
        } else if (up.status == UPLOAD_FILE_END) {
            if (!m_updateActive) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}"); return; }
            m_updateActive = false;
            // end(false): РІР°Р»РёРґРЅС‹Р№ РѕР±СЂР°Р· РѕР±СЏР·Р°С‚РµР»РµРЅ, РёРЅР°С‡Рµ РїРµСЂРµР·Р°РіСЂСѓР·РєРё РЅРµС‚.
            if (Update.end(false)) {
                s.send(200, "application/json", "{\"ok\":true,\"status\":\"update ok, rebooting\"}");
                delay(200);
                ESP.restart();
            } else {
                Update.abort();
                Update.printError(Serial);
                s.send(500, "application/json", "{\"ok\":false,\"error\":\"update failed\"}");
            }
        }
    }

    void handleUpdateEnd(WebServer& s) {
        s.send(200, "application/json", "{\"ok\":true}");
    }

    // ------------------------------------------------------------------
    // Р§Р»РµРЅС‹
    // ------------------------------------------------------------------
    NodeConfig& m_cfg;
    PcmPipeline* m_pipeline;
    DelayLine** m_delayLeft;
    DelayLine** m_delayRight;
    DelayLine** m_delaySub;
    EspNowTransport* m_espnow;
    volatile bool* m_leftOnline;
    volatile bool* m_rightOnline;
    volatile bool* m_a2dpConnected;
    WebServer* m_server = nullptr;
    WebServer* m_apServer = nullptr;
    std::vector<WifiNetInfo> m_wifiCache;
    bool m_saveRequested = false;
    bool m_reconnectRequested = false;

    InternetChecker* m_net = nullptr;
    LogRing* m_logs = nullptr;

    char m_sessionToken[Auth::kTokenLen + 1] = "";
    bool m_sessionActive = false;
    bool m_updateActive = false;

    static const char kPageHtml[];
};

const char MasterWebServer::kPageHtml[] = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Audio 2.1 Master</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: system-ui, sans-serif; max-width: 720px; margin: 0 auto; padding: 0 12px 40px; background:#111; color:#eee; }
  h1 { font-size: 20px; }
  h2 { font-size: 16px; margin: 4px 0; }
  nav { display:flex; flex-wrap:wrap; gap:4px; margin:10px 0; }
  nav button { background:#222; color:#ccc; border:1px solid #333; border-radius:6px; padding:6px 10px; cursor:pointer; font-size:13px; }
  nav button.active { background:#2a6; color:#fff; border-color:#2a6; }
  .card { background:#1c1c1c; border:1px solid #333; border-radius:8px; padding:12px; margin:10px 0; }
  label { display:block; margin:6px 0 2px; font-size:13px; color:#aaa; }
  input[type=text], input[type=password], input[type=number], select { width:100%; padding:6px; background:#222; color:#eee; border:1px solid #444; border-radius:4px; }
  input[type=range] { width:100%; }
  button { margin-top:8px; padding:8px 14px; background:#2a6; color:#fff; border:none; border-radius:4px; cursor:pointer; }
  button.danger { background:#a33; }
  button.ghost { background:#333; }
  .row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
  .val { color:#2a6; font-weight:bold; }
  .page { display:none; }
  .page.active { display:block; }
  .net { padding:8px; border:1px solid #333; border-radius:4px; margin:4px 0; cursor:pointer; font-size:14px; }
  .net:active { background:#2a6; }
  table { border-collapse:collapse; width:100%; }
  td { padding:5px 8px; border-bottom:1px solid #333; font-size:13px; }
  td:first-child { color:#9e9e9e; width:40%; }
  .mono { font-family: monospace; font-size:12px; white-space:pre-wrap; word-break:break-all; }
  #toast { position:fixed; bottom:16px; left:50%; transform:translateX(-50%); background:#2a6; color:#fff; padding:8px 14px; border-radius:6px; display:none; z-index:10; }
  .banner { background:#1c1c1c; border:1px solid #a33; border-radius:8px; padding:14px; margin:14px 0; }
</style>
</head>
<body>
<h1>Audio 2.1 Master</h1>
<div id="loginBanner" class="banner" style="display:none">
  <h2>РўСЂРµР±СѓРµС‚СЃСЏ РІС…РѕРґ</h2>
  <label>РџР°СЂРѕР»СЊ Р°РґРјРёРЅРёСЃС‚СЂР°С‚РѕСЂР°</label>
  <div class="row">
    <input type="password" id="loginPass" style="flex:1">
    <button id="loginBtn">Р’РѕР№С‚Рё</button>
  </div>
</div>
<nav id="nav">
  <button data-p="dashboard" class="active">Dashboard</button>
  <button data-p="wifi">Wi-Fi</button>
  <button data-p="internet">Internet</button>
  <button data-p="audio">Audio</button>
  <button data-p="delays">Delays</button>
  <button data-p="satellites">Satellites</button>
  <button data-p="system">System</button>
  <button data-p="update">Update</button>
  <button data-p="logs">Logs</button>
</nav>

<div id="p-dashboard" class="page active">
  <div class="card">
    <h2>РЎС‚Р°С‚СѓСЃ</h2>
    <table>
      <tr><td>РЎРёСЃС‚РµРјР°</td><td id="d_system">-</td></tr>
      <tr><td>Wi-Fi</td><td id="d_wifi">-</td></tr>
      <tr><td>РРЅС‚РµСЂРЅРµС‚</td><td id="d_internet">-</td></tr>
      <tr><td>IP-Р°РґСЂРµСЃ</td><td id="d_ip">-</td></tr>
      <tr><td>Hostname</td><td id="d_host">-</td></tr>
      <tr><td>РСЃС‚РѕС‡РЅРёРє Р°СѓРґРёРѕ</td><td id="d_source">-</td></tr>
      <tr><td>Р“СЂРѕРјРєРѕСЃС‚СЊ</td><td id="d_volume">-</td></tr>
      <tr><td>РљСЂРѕСЃСЃРѕРІРµСЂ</td><td id="d_crossover">-</td></tr>
      <tr><td>Р—Р°РґРµСЂР¶РєРё</td><td id="d_delays">-</td></tr>
      <tr><td>РЎР°С‚РµР»Р»РёС‚С‹</td><td id="d_sats">-</td></tr>
      <tr><td>РЎРІРѕР±РѕРґРЅС‹Р№ heap</td><td id="d_heap">-</td></tr>
      <tr><td>РЎРІРѕР±РѕРґРЅС‹Р№ PSRAM</td><td id="d_psram">-</td></tr>
      <tr><td>Р’РµСЂСЃРёСЏ</td><td id="d_ver">-</td></tr>
    </table>
    <div class="row">
      <button id="dMuteBtn">Mute</button>
      <button id="dSaveBtn" class="ghost">РЎРѕС…СЂР°РЅРёС‚СЊ (NVS)</button>
      <button id="dRebootBtn" class="danger">Reboot</button>
      <button id="dLogoutBtn" class="ghost">Р’С‹Р№С‚Рё</button>
    </div>
  </div>
</div>

<div id="p-wifi" class="page">
  <div class="card">
    <h2>РЎРєР°РЅРёСЂРѕРІР°РЅРёРµ СЃРµС‚РµР№</h2>
    <div class="row">
      <button id="wifiScanBtn">РЎРєР°РЅРёСЂРѕРІР°С‚СЊ</button>
      <label style="display:inline; margin:0">РЎРєСЂС‹С‚Р°СЏ СЃРµС‚СЊ <input type="checkbox" id="wifiHidden" style="width:auto"></label>
    </div>
    <div id="wifiNets"></div>
    <div id="wifiMsg" style="font-size:13px;color:#aaa"></div>
  </div>
  <div class="card">
    <h2>РџРѕРґРєР»СЋС‡РµРЅРёРµ</h2>
    <label>SSID</label>
    <input type="text" id="wifiSsid" placeholder="SSID СЃРµС‚Рё" autocomplete="off">
    <label>РџР°СЂРѕР»СЊ</label>
    <input type="password" id="wifiPass" placeholder="РџР°СЂРѕР»СЊ СЃРµС‚Рё">
    <div class="row">
      <button id="wifiConnectBtn">РџРѕРґРєР»СЋС‡РёС‚СЊСЃСЏ Рё СЃРѕС…СЂР°РЅРёС‚СЊ</button>
      <button id="wifiSaveOnlyBtn" class="ghost">РўРѕР»СЊРєРѕ СЃРѕС…СЂР°РЅРёС‚СЊ</button>
    </div>
  </div>
  <div class="card">
    <h2>РЎРѕС…СЂР°РЅС‘РЅРЅС‹Рµ СЃРµС‚Рё</h2>
    <div id="wifiProfiles"></div>
  </div>
</div>

<div id="p-internet" class="page">
  <div class="card">
    <h2>РЎС‚Р°С‚СѓСЃ РёРЅС‚РµСЂРЅРµС‚Р°</h2>
    <table>
      <tr><td>РЎС‚Р°С‚СѓСЃ</td><td id="i_status">-</td></tr>
      <tr><td>Р—Р°РґРµСЂР¶РєР°</td><td id="i_latency">-</td></tr>
      <tr><td>РџРѕСЃР»РµРґРЅСЏСЏ РїСЂРѕРІРµСЂРєР°</td><td id="i_last">-</td></tr>
      <tr><td>URL РїСЂРѕРІРµСЂРєРё</td><td id="i_url">-</td></tr>
      <tr><td>DNS</td><td id="i_dns">-</td></tr>
      <tr><td>HTTP</td><td id="i_http">-</td></tr>
    </table>
    <button id="iCheckBtn">РџСЂРѕРІРµСЂРёС‚СЊ СЃРµР№С‡Р°СЃ</button>
  </div>
  <div class="card">
    <h2>РќР°СЃС‚СЂРѕР№РєРё</h2>
    <label>URL РїСЂРѕРІРµСЂРєРё</label>
    <input type="text" id="iUrlInput">
    <div class="row">
      <button id="iUrlSaveBtn">РЎРѕС…СЂР°РЅРёС‚СЊ</button>
    </div>
  </div>
</div>

<div id="p-audio" class="page">
  <div class="card">
    <h2>Р“СЂРѕРјРєРѕСЃС‚СЊ</h2>
    <label>Р“СЂРѕРјРєРѕСЃС‚СЊ: <span id="a_volLabel" class="val">50</span></label>
    <input type="range" id="a_volume" min="0" max="100" value="50">
    <div class="row">
      <button id="aMuteBtn">Mute</button>
      <button id="aUnmuteBtn">Unmute</button>
    </div>
  </div>
  <div class="card">
    <h2>РљСЂРѕСЃСЃРѕРІРµСЂ</h2>
    <label>Р§Р°СЃС‚РѕС‚Р°: <span id="a_xoLabel" class="val">90</span> Р“С†</label>
    <input type="range" id="a_crossover" min="70" max="120" value="90">
  </div>
  <div class="card">
    <h2>РђСѓРґРёРѕ-СЃС‚Р°С‚СѓСЃ</h2>
    <table>
      <tr><td>РСЃС‚РѕС‡РЅРёРє</td><td id="a_source">-</td></tr>
      <tr><td>Р¤РѕСЂРјР°С‚</td><td id="a_format">-</td></tr>
      <tr><td>Р’РѕСЃРїСЂРѕРёР·РІРµРґРµРЅРёРµ</td><td id="a_playing">-</td></tr>
    </table>
    <button id="aSaveBtn" class="ghost">РЎРѕС…СЂР°РЅРёС‚СЊ (NVS)</button>
  </div>
</div>

<div id="p-delays" class="page">
  <div class="card">
    <h2>Р—Р°РґРµСЂР¶РєРё РєР°РЅР°Р»РѕРІ, РјСЃ</h2>
    <label>Left: <span id="dly_l" class="val">0</span> РјСЃ</label>
    <input type="range" id="dlyLeft" min="0" max="200" value="0">
    <label>Right: <span id="dly_r" class="val">0</span> РјСЃ</label>
    <input type="range" id="dlyRight" min="0" max="200" value="0">
    <label>Sub: <span id="dly_s" class="val">0</span> РјСЃ</label>
    <input type="range" id="dlySub" min="0" max="200" value="0">
    <p style="font-size:12px;color:#777">РџРѕРґСЃРєР°Р·РєР°: delay_ms = distance_m / 0.343 (СЃРєРѕСЂРѕСЃС‚СЊ Р·РІСѓРєР°)</p>
  </div>
</div>

<div id="p-satellites" class="page">
  <div class="card">
    <h2>РЎС‚Р°С‚СѓСЃ СЃР°С‚РµР»Р»РёС‚РѕРІ</h2>
    <table>
      <tr><td>Р›РµРІС‹Р№</td><td id="s_left">-</td></tr>
      <tr><td>РџСЂР°РІС‹Р№</td><td id="s_right">-</td></tr>
      <tr><td>РўСЂР°РЅСЃРїРѕСЂС‚</td><td id="s_transport">-</td></tr>
    </table>
  </div>
  <div class="card">
    <h2>РџСЂРёРІСЏР·РєР°</h2>
    <div class="row">
      <select id="pairSide">
        <option value="left">Left</option>
        <option value="right">Right</option>
      </select>
      <input type="text" id="pairMac" placeholder="AA:BB:CC:DD:EE:01" style="flex:1">
      <button id="pairBtn">РџСЂРёРІСЏР·Р°С‚СЊ</button>
    </div>
  </div>
  <div class="card">
    <h2>РўСЂР°РЅСЃРїРѕСЂС‚</h2>
    <div class="row">
      <select id="transport">
        <option value="espnow">ESP-NOW</option>
        <option value="udp">UDP</option>
      </select>
      <button id="transportBtn">Set</button>
    </div>
  </div>
</div>

<div id="p-system" class="page">
  <div class="card">
    <h2>РЈСЃС‚СЂРѕР№СЃС‚РІРѕ</h2>
    <label>Hostname</label>
    <input type="text" id="sysHostname">
    <label>URL РїСЂРѕРІРµСЂРєРё РёРЅС‚РµСЂРЅРµС‚Р°</label>
    <input type="text" id="sysNetUrl">
    <label>NTP СЃРµСЂРІРµСЂ</label>
    <input type="text" id="sysNtp">
    <label>Р§Р°СЃРѕРІРѕР№ РїРѕСЏСЃ</label>
    <input type="text" id="sysTz">
    <button id="sysSaveBtn">РЎРѕС…СЂР°РЅРёС‚СЊ</button>
  </div>
  <div class="card">
    <h2>РћР±СЃР»СѓР¶РёРІР°РЅРёРµ</h2>
    <div class="row">
      <button id="sysRebootBtn">Reboot</button>
      <button id="sysFactoryBtn" class="danger">Factory reset</button>
      <button id="sysExportBtn" class="ghost">Р­РєСЃРїРѕСЂС‚ РєРѕРЅС„РёРіР°</button>
      <button id="sysDiagBtn" class="ghost">Р”РёР°РіРЅРѕСЃС‚РёРєР°</button>
    </div>
    <pre id="sysDiagOut" class="mono"></pre>
  </div>
  <div class="card">
    <h2>РРјРїРѕСЂС‚ РєРѕРЅС„РёРіР° (JSON)</h2>
    <textarea id="sysImportArea" rows="5" style="width:100%;background:#222;color:#eee;border:1px solid #444;border-radius:4px"></textarea>
    <button id="sysImportBtn" class="ghost">РРјРїРѕСЂС‚РёСЂРѕРІР°С‚СЊ</button>
  </div>
</div>

<div id="p-update" class="page">
  <div class="card">
    <h2>РћР±РЅРѕРІР»РµРЅРёРµ РїСЂРѕС€РёРІРєРё</h2>
    <table>
      <tr><td>Р’РµСЂСЃРёСЏ</td><td id="u_ver">-</td></tr>
    </table>
    <label>Р¤Р°Р№Р» .bin</label>
    <input type="file" id="uFile" accept=".bin">
    <button id="uBtn">РћР±РЅРѕРІРёС‚СЊ</button>
    <div id="uMsg" style="font-size:13px;color:#aaa"></div>
    <p style="font-size:12px;color:#777">РќРµ РїСЂРµСЂС‹РІР°Р№С‚Рµ РїРёС‚Р°РЅРёРµ РІРѕ РІСЂРµРјСЏ РѕР±РЅРѕРІР»РµРЅРёСЏ. РЈСЃС‚СЂРѕР№СЃС‚РІРѕ РїРµСЂРµР·Р°РіСЂСѓР·РёС‚СЃСЏ Р°РІС‚РѕРјР°С‚РёС‡РµСЃРєРё.</p>
  </div>
</div>

<div id="p-logs" class="page">
  <div class="card">
    <h2>Р›РѕРіРё</h2>
    <div class="row">
      <button id="lRefreshBtn">РћР±РЅРѕРІРёС‚СЊ</button>
      <button id="lClearBtn" class="ghost">РћС‡РёСЃС‚РёС‚СЊ</button>
      <label style="display:inline;margin:0">Р›РёРјРёС‚ <select id="lLimit" style="width:80px"><option>20</option><option selected>64</option><option>200</option></select></label>
    </div>
    <pre id="lOut" class="mono" style="max-height:60vh;overflow:auto"></pre>
  </div>
</div>

<div id="toast"></div>

<script>
const $ = id => document.getElementById(id);
let csrf = '';
async function api(path, body, method) {
  const opts = { headers: {} };
  if (body) { opts.method = method || 'POST'; opts.headers['Content-Type'] = 'application/json'; opts.body = JSON.stringify(body); }
  if (csrf) opts.headers['X-CSRF-Token'] = csrf;
  const r = await fetch(path, opts);
  if (r.status === 401) { showLogin(); throw new Error('unauthorized'); }
  const j = await r.json();
  if (j.system && j.system.csrf) csrf = j.system.csrf;
  return j;
}
function showLogin() {
  $('loginBanner').style.display = 'block';
  document.querySelectorAll('#nav button').forEach(b => b.disabled = true);
}
function hideLogin() {
  $('loginBanner').style.display = 'none';
  document.querySelectorAll('#nav button').forEach(b => b.disabled = false);
}
let toastTimer = null;
function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.style.display = 'block';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.style.display = 'none', 2500);
}

function navPage(p) {
  document.querySelectorAll('.page').forEach(el => el.classList.remove('active'));
  $('p-' + p).classList.add('active');
  document.querySelectorAll('#nav button').forEach(b => b.classList.toggle('active', b.dataset.p === p));
  if (p === 'dashboard') refresh();
  if (p === 'wifi') { loadProfiles(); }
  if (p === 'internet') refreshInternet();
  if (p === 'audio') refresh();
  if (p === 'delays') refresh();
  if (p === 'satellites') refresh();
  if (p === 'system') loadSystem();
  if (p === 'logs') loadLogs();
}
document.querySelectorAll('#nav button').forEach(b => b.onclick = () => navPage(b.dataset.p));

async function refresh() {
  try {
    const s = await api('/api/status');
    if (!s.system.authed) { showLogin(); return; }
    hideLogin();
    $('d_system').textContent = 'OK';
    $('d_wifi').textContent = s.wifi.mode + ' ' + (s.wifi.ip || '-');
    $('d_internet').textContent = s.wifi.internet;
    $('d_ip').textContent = s.wifi.ip || '-';
    $('d_host').textContent = s.system.hostname;
    $('d_source').textContent = s.audio.source;
    $('d_volume').textContent = s.audio.volume + (s.audio.mute ? ' (mute)' : '');
    $('d_crossover').textContent = s.audio.crossover_hz + ' Р“С†';
    $('d_delays').textContent = 'L ' + s.delays.left_ms + ' / R ' + s.delays.right_ms + ' / Sub ' + s.delays.sub_ms + ' РјСЃ';
    $('d_sats').textContent = 'L ' + s.satellites.left + ' | R ' + s.satellites.right;
    $('d_heap').textContent = s.system.heap_free;
    $('d_psram').textContent = s.system.psram_free;
    $('d_ver').textContent = s.system.version;
    $('a_volLabel').textContent = s.audio.volume;
    $('a_volume').value = s.audio.volume;
    $('a_xoLabel').textContent = s.audio.crossover_hz;
    $('a_crossover').value = s.audio.crossover_hz;
    $('a_source').textContent = s.audio.source;
    $('a_format').textContent = s.audio.sample_rate + '/' + s.audio.bits + '/' + s.audio.channels;
    $('a_playing').textContent = s.audio.playing ? 'Active' : 'Stopped';
    $('dly_l').textContent = s.delays.left_ms; $('dlyLeft').value = s.delays.left_ms;
    $('dly_r').textContent = s.delays.right_ms; $('dlyRight').value = s.delays.right_ms;
    $('dly_s').textContent = s.delays.sub_ms; $('dlySub').value = s.delays.sub_ms;
    $('s_left').textContent = s.satellites.left;
    $('s_right').textContent = s.satellites.right;
    $('s_transport').textContent = s.audio.source;
    $('u_ver').textContent = s.system.version;
  } catch (e) {}
}

async function scanWifi() {
  $('wifiMsg').textContent = 'РЎРєР°РЅРёСЂРѕРІР°РЅРёРµ...';
  try {
    const d = await api('/api/wifi/scan');
    $('wifiNets').innerHTML = '';
    (d.networks || []).forEach(n => {
      const el = document.createElement('div');
      el.className = 'net';
      el.textContent = n.ssid + '  (' + n.rssi + ' dBm' + (n.security === 'OPEN' ? ', open' : '') + ', ch ' + n.channel + ')';
      el.onclick = () => { $('wifiSsid').value = n.ssid; };
      $('wifiNets').appendChild(el);
    });
    $('wifiMsg').textContent = 'РќР°Р№РґРµРЅРѕ СЃРµС‚РµР№: ' + (d.networks || []).length;
  } catch (e) { $('wifiMsg').textContent = 'РћС€РёР±РєР° СЃРєР°РЅРёСЂРѕРІР°РЅРёСЏ'; }
}

async function loadProfiles() {
  try {
    const d = await api('/api/wifi/profiles');
    const el = $('wifiProfiles');
    el.innerHTML = '';
    (d.profiles || []).forEach(p => {
      const row = document.createElement('div');
      row.className = 'row';
      row.style.justifyContent = 'space-between';
      const span = document.createElement('span');
      span.appendChild(document.createTextNode(p.ssid + ' '));
      const small = document.createElement('small');
      small.style.color = '#777';
      small.textContent = '(' + p.ip_mode + (p.hidden ? ', hidden' : '') + ')';
      span.appendChild(small);
      row.appendChild(span);
      const btn = document.createElement('button');
      btn.className = 'danger';
      btn.style.margin = '0';
      btn.textContent = 'Forget';
      btn.onclick = async () => { await api('/api/wifi/forget', {ssid: p.ssid}); loadProfiles(); };
      row.appendChild(btn);
      el.appendChild(row);
    });
  } catch (e) {}
}

async function connectWifi() {
  const ssid = $('wifiSsid').value.trim();
  if (!ssid) { $('wifiMsg').textContent = 'Р’РІРµРґРёС‚Рµ SSID'; return; }
  $('wifiMsg').textContent = 'РџРѕРґРєР»СЋС‡РµРЅРёРµ...';
  try {
    const r = await fetch('/api/wifi/connect', {
      method: 'POST',
      headers: {'Content-Type': 'application/json', ...(csrf ? {'X-CSRF-Token': csrf} : {})},
      body: JSON.stringify({ssid: ssid, password: $('wifiPass').value, hidden: $('wifiHidden').checked, save: true, auto_reconnect: true})
    });
    const d = await r.json();
    $('wifiMsg').textContent = d.state || d.error || 'ok';
  } catch (e) { $('wifiMsg').textContent = 'РћС€РёР±РєР° РїРѕРґРєР»СЋС‡РµРЅРёСЏ'; }
}

async function refreshInternet() {
  try {
    const d = await api('/api/net/internet');
    $('i_status').textContent = d.status;
    $('i_latency').textContent = (d.latency_ms ? d.latency_ms + ' ms' : '-');
    $('i_last').textContent = d.last_check ? d.last_check + ' ms' : '-';
    $('i_url').textContent = d.check_url;
    $('i_dns').textContent = d.dns_ok ? 'OK' : '-';
    $('i_http').textContent = d.http_ok ? 'OK' : '-';
  } catch (e) {}
}

async function loadSystem() {
  try {
    const d = await api('/api/wifi/status');
    $('sysHostname').value = d.hostname || '';
    const n = await api('/api/net/internet');
    $('sysNetUrl').value = n.check_url || '';
    const s = await api('/api/status');
  } catch (e) {}
}

async function loadLogs() {
  try {
    const limit = $('lLimit').value;
    const d = await api('/api/logs?limit=' + limit);
    $('lOut').textContent = (d.logs || []).join('\n');
  } catch (e) { $('lOut').textContent = 'РћС€РёР±РєР°'; }
}

async function sysSave() {
  try {
    const r = await fetch('/api/system/config/import', {
      method: 'POST',
      headers: {'Content-Type': 'application/json', ...(csrf ? {'X-CSRF-Token': csrf} : {})},
      body: JSON.stringify({config: {
        hostname: $('sysHostname').value.trim(),
        net_check_url: $('sysNetUrl').value.trim(),
        ntp_server: $('sysNtp').value.trim(),
        timezone: $('sysTz').value.trim()
      }})
    });
    const d = await r.json();
    toast(d.ok ? 'РЎРѕС…СЂР°РЅРµРЅРѕ' : (d.error || 'РћС€РёР±РєР°'));
  } catch (e) { toast('РћС€РёР±РєР°'); }
}

$('dMuteBtn').onclick = async () => api('/api/mute', {mute: true});
$('dSaveBtn').onclick = async () => { await api('/api/save', {}); toast('РЎРѕС…СЂР°РЅРµРЅРѕ'); };
$('dRebootBtn').onclick = async () => { await api('/api/system/reboot', {}); toast('РџРµСЂРµР·Р°РіСЂСѓР·РєР°...'); };
$('dLogoutBtn').onclick = async () => { await api('/api/logout', {}); location.reload(); };
$('aMuteBtn').onclick = async () => api('/api/mute', {mute: true});
$('aUnmuteBtn').onclick = async () => api('/api/mute', {mute: false});
$('aSaveBtn').onclick = async () => { await api('/api/save', {}); toast('РЎРѕС…СЂР°РЅРµРЅРѕ'); };
$('a_volume').oninput = async e => { $('a_volLabel').textContent = e.target.value; await api('/api/volume', {volume: +e.target.value}, 'PUT'); };
$('a_crossover').oninput = async e => { $('a_xoLabel').textContent = e.target.value; await api('/api/crossover', {crossover_hz: +e.target.value}, 'PUT'); };
$('dlyLeft').oninput = async e => { $('dly_l').textContent = e.target.value; await api('/api/delay', {channel: 'left', delay_ms: +e.target.value}, 'PUT'); };
$('dlyRight').oninput = async e => { $('dly_r').textContent = e.target.value; await api('/api/delay', {channel: 'right', delay_ms: +e.target.value}, 'PUT'); };
$('dlySub').oninput = async e => { $('dly_s').textContent = e.target.value; await api('/api/delay', {channel: 'sub', delay_ms: +e.target.value}, 'PUT'); };
$('pairBtn').onclick = async () => api('/api/pair', {side: $('pairSide').value, mac: $('pairMac').value.trim()});
$('transportBtn').onclick = async () => api('/api/transport', {mode: $('transport').value});
$('sysSaveBtn').onclick = sysSave;
$('sysRebootBtn').onclick = async () => { await api('/api/system/reboot', {}); toast('РџРµСЂРµР·Р°РіСЂСѓР·РєР°...'); };
$('sysFactoryBtn').onclick = async () => { if (confirm('РЎР±СЂРѕСЃРёС‚СЊ РІСЃРµ РЅР°СЃС‚СЂРѕР№РєРё?')) { await api('/api/system/factory_reset', {}); } };
$('sysExportBtn').onclick = async () => { const d = await api('/api/system/config/export'); $('sysImportArea').value = JSON.stringify(d, null, 2); };
$('sysDiagBtn').onclick = async () => { const d = await api('/api/diagnostics'); $('sysDiagOut').textContent = JSON.stringify(d, null, 2); };
$('sysImportBtn').onclick = async () => {
  try {
    const obj = JSON.parse($('sysImportArea').value);
    const r = await fetch('/api/system/config/import', {
      method: 'POST',
      headers: {'Content-Type': 'application/json', ...(csrf ? {'X-CSRF-Token': csrf} : {})},
      body: JSON.stringify(obj)
    });
    toast((await r.json()).ok ? 'РРјРїРѕСЂС‚РёСЂРѕРІР°РЅРѕ' : 'РћС€РёР±РєР°');
  } catch (e) { toast('РќРµРІРµСЂРЅС‹Р№ JSON'); }
};
$('iCheckBtn').onclick = async () => { toast('РџСЂРѕРІРµСЂРєР°...'); await api('/api/net/check', {}); refreshInternet(); };
$('iUrlSaveBtn').onclick = async () => {
  await fetch('/api/system/config/import', {
    method: 'POST',
    headers: {'Content-Type': 'application/json', ...(csrf ? {'X-CSRF-Token': csrf} : {})},
    body: JSON.stringify({config: {net_check_url: $('iUrlInput').value.trim()}})
  });
  toast('РЎРѕС…СЂР°РЅРµРЅРѕ');
};
$('wifiScanBtn').onclick = scanWifi;
$('wifiConnectBtn').onclick = connectWifi;
$('wifiSaveOnlyBtn').onclick = async () => {
  const ssid = $('wifiSsid').value.trim();
  if (!ssid) return;
  await api('/api/wifi/save', {ssid: ssid, password: $('wifiPass').value, hidden: $('wifiHidden').checked, auto_reconnect: true});
  toast('РЎРѕС…СЂР°РЅРµРЅРѕ'); loadProfiles();
};
$('lRefreshBtn').onclick = loadLogs;
$('lClearBtn').onclick = () => { $('lOut').textContent = ''; };
$('lLimit').onchange = loadLogs;
$('loginBtn').onclick = async () => {
  try {
    const r = await fetch('/api/login', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({password: $('loginPass').value})
    });
    if (r.status === 401) { toast('РќРµРІРµСЂРЅС‹Р№ РїР°СЂРѕР»СЊ'); return; }
    const d = await r.json();
    hideLogin();
    location.reload();
  } catch (e) { toast('РћС€РёР±РєР°'); }
};
$('uBtn').onclick = async () => {
  const f = $('uFile').files[0];
  if (!f) { $('uMsg').textContent = 'Р’С‹Р±РµСЂРёС‚Рµ С„Р°Р№Р» .bin'; return; }
  $('uMsg').textContent = 'Р—Р°РіСЂСѓР·РєР°...';
  const form = new FormData();
  form.append('firmware', f);
  try {
    const r = await fetch('/api/update', {method: 'POST', body: form});
    const d = await r.json();
    $('uMsg').textContent = d.status || d.error || 'Р“РѕС‚РѕРІРѕ';
  } catch (e) { $('uMsg').textContent = 'РћС€РёР±РєР° Р·Р°РіСЂСѓР·РєРё'; }
};

// РџРµСЂРІС‹Р№ Р·Р°РїСѓСЃРє: РЅРµС‚ РїР°СЂРѕР»СЏ вЂ” РїРѕРєР°Р·Р°С‚СЊ РЅР°СЃС‚СЂРѕР№РєСѓ Р°РґРјРёРЅРёСЃС‚СЂР°С‚РѕСЂР°.
async function bootCheck() {
  try {
    const s = await api('/api/status');
    if (s.system.authed) { hideLogin(); }
    else if (!s.system.auth_enabled) {
      const pass = prompt('РџРµСЂРІС‹Р№ Р·Р°РїСѓСЃРє. Р—Р°РґР°Р№С‚Рµ РїР°СЂРѕР»СЊ Р°РґРјРёРЅРёСЃС‚СЂР°С‚РѕСЂР° (РјРёРЅ 4 СЃРёРјРІРѕР»Р°):');
      if (pass) {
        const c = prompt('РџРѕРІС‚РѕСЂРёС‚Рµ РїР°СЂРѕР»СЊ:');
        if (pass === c) {
          await fetch('/api/admin/setup', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({password: pass, confirm: c})
          });
          hideLogin();
        }
      }
    } else {
      showLogin();
    }
    refresh();
  } catch (e) {}
}
bootCheck();
setInterval(() => { if ($('p-dashboard').classList.contains('active')) refresh(); }, 2000);
</script>
</body>
</html>
)rawliteral";

} // namespace audio21
