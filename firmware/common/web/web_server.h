// web_server.h — Web UI + REST API мастер-узла (ТЗ_Веб.md, полная реализация).
//
// Использует встроенный WebServer ESP32 Arduino (синхронный) + ArduinoJson.
// Два сервера (B9): STA-интерфейс (localIP:80) + AP-интерфейс (softAPIP:80),
// чтобы Web UI был доступен клиентам AP в APSTA-режиме.
//
// Разделы (ТЗ §4): Dashboard, Wi-Fi, Internet, Audio, Delays, Satellites,
// System, Update, Logs. REST API — ТЗ §15-17. Авторизация — §18/§23.
//
// Аудио-зависимости (pipeline, delay, espnow, статусы) — опциональные
// (указатели, дефолт nullptr): мастер ESP32-S3 (этап 1, без аудио-конвейера)
// использует тот же Web UI для настройки Wi-Fi, интернета и диагностики;
// аудио-эндпоинты при этом применяют изменения только к конфигу.
//
// Header-only. Только для мастер-узла.
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
#include "web_spa.h"

namespace audio21 {

// Хук проверки интернета для InternetChecker (ТЗ §7.4). Реализация — сырой
// HTTP/1.1 поверх WiFiClient: резолвим имя в IPv4 (WiFi.hostByName), шлём
// GET и читаем первую строку ответа (код 200/204 → online, 3xx → captive
// portal). HTTPClient core 3.x ошибочно возвращает connection refused при
// рабочем TCP-соединении (проверено на железе), поэтому обходим его.
inline HttpCheckResult httpInternetCheck(const char* url, uint32_t timeoutMs) {
    HttpCheckResult r;
    if (!url || !url[0]) { r.connectFailed = 1; return r; }

    // Разбор URL: http://host[:port]/path
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

    // --- Инъекции из main.cpp ---
    void setInternetChecker(InternetChecker* ic) { m_net = ic; }
    void setLogs(LogRing* logs) { m_logs = logs; }
    void setCpuLoadPercent(uint32_t p) { m_cpuLoadPercent = p; } // C5.5

    // Запустить серверы (Wi-Fi должен быть подключён).
    void begin() {
        // Кастомные заголовки нужно зарегистрировать ДО begin() (close() иначе
        // оставит только Authorization). Cookie — для сессии, X-CSRF-Token — для CSRF.
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
        // C5.2: сессия гаснет через kSessionTimeoutMs (ТЗ §11.4/§23.1).
        if (m_sessionActive && (millis() - m_sessionStartMs > kSessionTimeoutMs)) {
            m_sessionActive = false;
            m_sessionToken[0] = '\0';
        }
        if (m_server) m_server->handleClient();
        if (m_apServer) m_apServer->handleClient();
    }

    // Кеш найденных Wi-Fi сетей (заполняется до старта AP, B9).
    struct WifiNetInfo {
        String ssid;
        int rssi;
        bool open;
    };
    void setWifiCache(std::vector<WifiNetInfo> cache) { m_wifiCache = std::move(cache); }

    // Флаг «запрошено сохранение» — опрашивается в loop() main.cpp.
    bool saveRequested() const { return m_saveRequested; }
    void clearSaveRequested() { m_saveRequested = false; }

    // Флаг переподключения Wi-Fi (для main.cpp).
    bool reconnectRequested() const { return m_reconnectRequested; }
    void clearReconnectRequested() { m_reconnectRequested = false; }

    // Статус авторизации (для main.cpp/консоли).
    bool sessionActive() const { return m_sessionActive; }

private:
    // ------------------------------------------------------------------
    // Роуты
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
    // Авторизация (ТЗ §18, §23)
    // ------------------------------------------------------------------
    // C5.7: адрес клиента в подсети STA или AP (ТЗ_Веб §23.2).
    static bool ipInSubnet(IPAddress ip, IPAddress net, IPAddress mask) {
        for (int i = 0; i < 4; i++) {
            if ((ip[i] & mask[i]) != (net[i] & mask[i])) return false;
        }
        return true;
    }

    static bool clientIsLocal(WebServer& s) {
        IPAddress remote = s.client().remoteIP();
        if (remote == IPAddress(0, 0, 0, 0)) return false;
        bool localSta = (WiFi.status() == WL_CONNECTED) &&
                        ipInSubnet(remote, WiFi.localIP(), WiFi.subnetMask());
        bool localAp = (WiFi.getMode() & WIFI_AP) &&
                       ipInSubnet(remote, WiFi.softAPIP(), IPAddress(255, 255, 255, 0));
        return localSta || localAp;
    }

    // Авторизован только обладатель cookie текущей сессии (m_sessionActive
    // — глобальный флаг «сессия создана», но не заменяет cookie).
    bool isAuthed(WebServer& s) const {
        // C5.7: Web UI доступен только из локальной подсети (STA/AP) — блок
        // для запросов извне (например, с интернета через проброшенный порт).
        if (!clientIsLocal(s)) return false;
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
        if (!isAuthed(s)) return false; // cookie сессии обязана совпадать
        String header = s.header("X-CSRF-Token");
        if (header.length() == 0) return false;
        char expected[65];
        Auth::csrfToken(m_sessionToken, expected);
        return header == expected;
    }

    // ------------------------------------------------------------------
    // Ответы
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
    // Страницы (SPA — один HTML, разделы через hash-навигацию)
    // ------------------------------------------------------------------
    void handleRoot(WebServer& s) {
        s.send(200, "text/html", kPageHtml);
    }

    // ------------------------------------------------------------------
    // GET /api/status (ТЗ §16.1)
    // ------------------------------------------------------------------
    void handleStatus(WebServer& s) {
        JsonDocument doc;
        doc["system"]["version"] = "0.2.1";
        doc["system"]["hostname"] = m_cfg.hostname;
        doc["system"]["uptime_sec"] = millis() / 1000;
        doc["system"]["heap_free"] = ESP.getFreeHeap();
        doc["system"]["psram_free"] = ESP.getFreePsram();
        doc["system"]["cpu_load_percent"] = m_cpuLoadPercent; // C5.5
        doc["system"]["mac"] = WiFi.macAddress();              // C5.5
        if (m_cfg.ntpEnabled) {                                // C5.5: NTP-время
            time_t now = time(nullptr);
            if (now > 0) {
                struct tm tmv;
                localtime_r(&now, &tmv);
                char buf[32];
                strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
                doc["system"]["time"] = buf;
            }
        }
        doc["system"]["auth_enabled"] = m_cfg.authEnabled;
        doc["system"]["authed"] = isAuthed(s);
        // S-1: дефолтный пароль AP мастера не сменён — требование смены
        // перед публичным использованием (REPO_AUDIT V3/S-1).
        doc["system"]["default_ap_password"] =
            strcmp(m_cfg.wifiApPassword, AUDIO_WIFI_AP_PASSWORD) == 0;
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
        doc["audio"]["left_volume"] = m_cfg.leftVolume;
        doc["audio"]["right_volume"] = m_cfg.rightVolume;
        doc["audio"]["sub_volume"] = m_cfg.subVolume;
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
    // GET /api/wifi/status (ТЗ §15.1)
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
    // GET /api/wifi/scan (ТЗ §15.2) — кеш до старта AP (B9) или живой скан.
    // ------------------------------------------------------------------
    void handleWifiScan(WebServer& s) {
        // C5.3: rate limit живого сканирования — не чаще 5 с (скан отключает
        // радио и рвёт соединение; кеш (B9) отдаётся без ограничения).
        if (m_wifiCache.empty()) {
            uint32_t nowMs = millis();
            if (nowMs - m_lastScanMs < kScanMinIntervalMs) {
                s.send(429, "application/json", "{\"ok\":false,\"error\":\"scan rate limited\"}");
                return;
            }
            m_lastScanMs = nowMs;
        }
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
    // POST /api/wifi/connect (ТЗ §15.3)
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
    // POST /api/wifi/save (ТЗ §15.4) — сохранить профиль без подключения.
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
    // POST /api/wifi/forget (ТЗ §15.5)
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
    // GET /api/wifi/profiles (ТЗ §6.5) — список сохранённых сетей.
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
    // GET /api/net/internet (ТЗ §15.6)
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
    // POST /api/net/check (ТЗ §15.7) — принудительная проверка.
    // ------------------------------------------------------------------
    void handleInternetCheck(WebServer& s) {
        if (m_net) m_net->forceCheck(httpInternetCheck);
        handleInternetStatus(s);
    }

    // ------------------------------------------------------------------
    // PUT /api/volume | POST /api/mute (ТЗ §16.2, §16.5)
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
        // Покомпонентные громкости (C2.2, ТЗ §7.5): channel = master|left|right|sub.
        const char* chan = doc["channel"] | "master";
        if (strcmp(chan, "left") == 0) {
            m_cfg.leftVolume = v;
            if (m_pipeline) m_pipeline->setChannelVolumes(v, m_cfg.rightVolume, m_cfg.subVolume);
        } else if (strcmp(chan, "right") == 0) {
            m_cfg.rightVolume = v;
            if (m_pipeline) m_pipeline->setChannelVolumes(m_cfg.leftVolume, v, m_cfg.subVolume);
        } else if (strcmp(chan, "sub") == 0) {
            m_cfg.subVolume = v;
            if (m_pipeline) m_pipeline->setChannelVolumes(m_cfg.leftVolume, m_cfg.rightVolume, v);
        } else {
            m_cfg.masterVolume = v;
            if (m_pipeline) m_pipeline->setVolume(v);
        }
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
    // PUT /api/crossover (ТЗ §16.3)
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
    // PUT /api/delay (ТЗ §16.4)
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
    // POST /api/save, /api/system/* (ТЗ §17)
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
    // GET /api/logs (ТЗ §17.5) — фильтры level (0..3) и module (C5.4)
    // ------------------------------------------------------------------
    void handleLogs(WebServer& s) {
        JsonDocument doc;
        int limit = s.hasArg("limit") ? s.arg("limit").toInt() : 200;
        if (limit <= 0 || limit > 500) limit = 200;
        int minLevel = s.hasArg("level") ? s.arg("level").toInt() : 0;
        if (minLevel < 0 || minLevel > 3) minLevel = 0;
        String module = s.hasArg("module") ? s.arg("module") : "";
        module.toUpperCase();
        JsonArray arr = doc["logs"].to<JsonArray>();
        if (m_logs) {
            const char* lines[96];
            int n = m_logs->tail(lines, limit > 96 ? 96 : limit);
            for (int i = 0; i < n; i++) {
                const char* line = lines[i];
                if (parseLogSeverity(line) < minLevel) continue;
                if (module.length() > 0 && !logLineHasModule(line, module)) continue;
                arr.add(line);
            }
        }
        sendJson(s, 200, doc);
    }

    // "[ts] [LEVEL] [CAT] msg" — уровень после второго '['
    static int parseLogSeverity(const char* line) {
        const char* p = strchr(line, '[');
        if (!p) return 1;
        p = strchr(p + 1, '[');
        if (!p) return 1;
        p++;
        if (strncmp(p, "DEBUG", 5) == 0) return 0;
        if (strncmp(p, "WARN", 4) == 0) return 2;
        if (strncmp(p, "ERROR", 5) == 0) return 3;
        return 1; // INFO
    }

    // "[ts] [LEVEL] [CAT] msg" — категория после третьего '['
    static bool logLineHasModule(const char* line, const String& module) {
        const char* p = strchr(line, '[');
        if (!p) return false;
        p = strchr(p + 1, '[');
        if (!p) return false;
        p = strchr(p + 1, '[');
        if (!p) return false;
        p++;
        const char* end = strchr(p, ']');
        if (!end) return false;
        int len = (int)(end - p);
        return len == (int)module.length() && strncasecmp(p, module.c_str(), len) == 0;
    }

    // ------------------------------------------------------------------
    // GET /api/diagnostics (ТЗ §17.6)
    // ------------------------------------------------------------------
    void handleDiagnostics(WebServer& s) {
        JsonDocument doc;
        doc["heap_free"] = ESP.getFreeHeap();
        doc["heap_min_free"] = ESP.getMinFreeHeap();
        doc["psram_free"] = ESP.getFreePsram();
        doc["psram_min_free"] = ESP.getMinFreePsram();
        doc["cpu_load_percent"] = m_cpuLoadPercent; // C5.5
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["internet"] = m_net ? m_net->statusName() : "disabled";
        doc["uptime_sec"] = millis() / 1000;
        doc["wifi_mode"] = wifiModeToString(m_cfg.wifiMode);
        doc["mac"] = WiFi.macAddress();
        sendJson(s, 200, doc);
    }

    // ------------------------------------------------------------------
    // POST /api/login, /api/logout, /api/admin/setup (ТЗ §18)
    // ------------------------------------------------------------------
    void handleLogin(WebServer& s) {
        if (!m_cfg.authEnabled) { sendOk(s, "no password required"); return; }
        // C5.3: блокировка на kLoginLockMs после kMaxLoginFails неудач (ТЗ §23.1).
        if (millis() < m_loginLockUntilMs) {
            s.send(429, "application/json", "{\"ok\":false,\"error\":\"too many attempts\"}");
            return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
        const char* pass = doc["password"] | "";
        if (!Auth::checkPassword(pass, m_cfg.adminPasswordHash)) {
            m_loginFails++;
            if (m_loginFails >= kMaxLoginFails) {
                m_loginLockUntilMs = millis() + kLoginLockMs;
                m_loginFails = 0;
            }
            s.send(401, "application/json", "{\"ok\":false,\"error\":\"bad password\"}");
            return;
        }
        m_loginFails = 0;
        Auth::newSessionToken(m_sessionToken);
        m_sessionActive = true;
        m_sessionStartMs = millis(); // C5.2: старт отсчёта таймаута сессии

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
            // Пароль уже задан — сброс возможен только через авторизованную сессию.
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
        m_sessionStartMs = millis(); // C5.2

        s.sendHeader("Set-Cookie", String("session=") + m_sessionToken + "; Path=/");
        sendOk(s, "admin configured");
    }

    // ------------------------------------------------------------------
    // OTA (ТЗ §12) — POST /api/update
    // ------------------------------------------------------------------
    void handleUpdateUpload(WebServer& s) {
        HTTPUpload& up = s.upload();
        if (up.status == UPLOAD_FILE_START) {
            // Только авторизованная сессия (cookie) + CSRF-токен.
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
            // end(false): валидный образ обязателен, иначе перезагрузки нет.
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
    // Члены
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
    uint32_t m_sessionStartMs = 0;   // C5.2: момент создания сессии
    uint32_t m_loginFails = 0;       // C5.3: счётчик неудачных логинов
    uint32_t m_loginLockUntilMs = 0; // C5.3: до этого времени логин заблокирован
    uint32_t m_lastScanMs = 0;       // C5.3: rate limit живого сканирования
    uint32_t m_cpuLoadPercent = 0;   // C5.5: заполняется из main.cpp

    static constexpr uint32_t kSessionTimeoutMs = 3600 * 1000UL; // ТЗ §11.4/§23.1
    static constexpr uint32_t kMaxLoginFails = 5;                // C5.3
    static constexpr uint32_t kLoginLockMs = 60 * 1000UL;        // C5.3: 60 с
    static constexpr uint32_t kScanMinIntervalMs = 5000;         // C5.3: 5 с

    static const char* kPageHtml;
};

const char* MasterWebServer::kPageHtml = kSpaHtml;

} // namespace audio21
