// web_server.cpp — реализация MasterWebServer (A1/T16: декомпозиция монолита).
// Вынесено из web_server.h для ускорения компиляции и разделения интерфейса/реализации.
#include "web_server.h"

#ifdef ESP32
#include <mbedtls/ed25519.h>
#endif

namespace audio21 {

// ---------------------------------------------------------------------------
// httpInternetCheck (ТЗ §7.4)
// ---------------------------------------------------------------------------
HttpCheckResult httpInternetCheck(const char* url, uint32_t timeoutMs) {
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

// ---------------------------------------------------------------------------
// Инъекции из main.cpp
// ---------------------------------------------------------------------------
void MasterWebServer::setInternetChecker(InternetChecker* ic) { m_net = ic; }
void MasterWebServer::setLogs(LogRing* logs) { m_logs = logs; }
void MasterWebServer::setCpuLoadPercent(uint32_t p) { m_cpuLoadPercent = p; }

void MasterWebServer::setDspLockHooks(LockFn lockFn, LockFn unlockFn) {
    m_dspLock = lockFn;
    m_dspUnlock = unlockFn;
}

// ---------------------------------------------------------------------------
// Запуск серверов / handleClient
// ---------------------------------------------------------------------------
void MasterWebServer::begin() {
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

void MasterWebServer::handleClient() {
    // C5.2: сессия гаснет через kSessionTimeoutMs (ТЗ §11.4/§23.1).
    if (m_sessionActive && (millis() - m_sessionStartMs > kSessionTimeoutMs)) {
        m_sessionActive = false;
        m_sessionToken[0] = '\0';
    }
    if (m_server) m_server->handleClient();
    if (m_apServer) m_apServer->handleClient();
}

// ---------------------------------------------------------------------------
// Кеш Wi-Fi / флаги
// ---------------------------------------------------------------------------
void MasterWebServer::setWifiCache(std::vector<WifiNetInfo> cache) {
    m_wifiCache = std::move(cache);
}

bool MasterWebServer::saveRequested() const { return m_saveRequested; }
void MasterWebServer::clearSaveRequested() { m_saveRequested = false; }

bool MasterWebServer::reconnectRequested() const { return m_reconnectRequested; }
void MasterWebServer::clearReconnectRequested() { m_reconnectRequested = false; }

bool MasterWebServer::passwordChangeRequired() const { return m_cfg.forcedPasswordChange; }

bool MasterWebServer::sendForcedPasswordChange(WebServer& s) {
    if (m_cfg.forcedPasswordChange) {
        s.send(403, "application/json",
               "{\"ok\":false,\"error\":\"forced_password_change\","
               "\"message\":\"Change default AP password first via POST /api/wifi/ap_password\"}");
        return true;
    }
    return false;
}

bool MasterWebServer::rebootRequested() const { return m_rebootRequested; }
void MasterWebServer::clearRebootRequested() { m_rebootRequested = false; }

bool MasterWebServer::factoryResetRequested() const { return m_factoryResetRequested; }
void MasterWebServer::clearFactoryResetRequested() { m_factoryResetRequested = false; }

bool MasterWebServer::updateRebootRequested() const { return m_updateRebootRequested; }
void MasterWebServer::clearUpdateRebootRequested() { m_updateRebootRequested = false; }

bool MasterWebServer::sessionActive() const { return m_sessionActive; }

// ---------------------------------------------------------------------------
// Роуты
// ---------------------------------------------------------------------------
void MasterWebServer::bindRoutes(WebServer& s) {
    s.on("/", HTTP_GET, [this, &s]() { handleRoot(s); });
    s.on("/login", HTTP_GET, [this, &s]() { handleRoot(s); });
    s.on("/api/status", HTTP_GET, [this, &s]() { handleStatus(s); });
    s.on("/api/wifi/status", HTTP_GET, [this, &s]() { handleWifiStatus(s); });
    s.on("/api/wifi/scan", HTTP_GET, [this, &s]() { handleWifiScan(s); });
    s.on("/api/wifi/connect", HTTP_POST, [this, &s]() { handleWifiConnect(s); });
    s.on("/api/wifi/save", HTTP_POST, [this, &s]() { handleWifiSave(s); });
    s.on("/api/wifi/forget", HTTP_POST, [this, &s]() { handleWifiForget(s); });
    // S-1: смена пароля AP из Web UI.
    s.on("/api/wifi/ap_password", HTTP_POST, [this, &s]() { handleApPassword(s); });
    s.on("/api/wifi/profiles", HTTP_GET, [this, &s]() { handleWifiProfiles(s); });
    // W-1: роуты, использовавшиеся SPA (sysApply/wApply).
    s.on("/api/system/hostname", HTTP_PUT, [this, &s]() { handleHostname(s); });
    s.on("/api/wifi/mode", HTTP_PUT, [this, &s]() { handleWifiMode(s); });
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

// ---------------------------------------------------------------------------
// Авторизация
// ---------------------------------------------------------------------------
bool MasterWebServer::ipInSubnet(IPAddress ip, IPAddress net, IPAddress mask) {
    for (int i = 0; i < 4; i++) {
        if ((ip[i] & mask[i]) != (net[i] & mask[i])) return false;
    }
    return true;
}

bool MasterWebServer::clientIsLocal(WebServer& s) {
    IPAddress remote = s.client().remoteIP();
    if (remote == IPAddress(0, 0, 0, 0)) return false;
    bool localSta = (WiFi.status() == WL_CONNECTED) &&
                    ipInSubnet(remote, WiFi.localIP(), WiFi.subnetMask());
    bool localAp = (WiFi.getMode() & WIFI_AP) &&
                   ipInSubnet(remote, WiFi.softAPIP(), IPAddress(255, 255, 255, 0));
    return localSta || localAp;
}

bool MasterWebServer::isAuthed(WebServer& s) const {
    // C5.7: Web UI доступен только из локальной подсети (STA/AP).
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

bool MasterWebServer::csrfOk(WebServer& s) const {
    if (!m_cfg.authEnabled) return true;
    if (!m_sessionActive) return false;
    if (!isAuthed(s)) return false;
    String header = s.header("X-CSRF-Token");
    if (header.length() == 0) return false;
    char expected[65];
    Auth::csrfToken(m_sessionToken, expected);
    return header == expected;
}

// ---------------------------------------------------------------------------
// Ответы
// ---------------------------------------------------------------------------
void MasterWebServer::sendJson(WebServer& s, int code, JsonDocument& doc) {
    String body;
    serializeJson(doc, body);
    s.sendHeader("Cache-Control", "no-store");
    s.send(code, "application/json", body);
}

void MasterWebServer::sendOk(WebServer& s, const char* msg) {
    JsonDocument doc;
    doc["ok"] = true;
    if (msg) doc["status"] = msg;
    sendJson(s, 200, doc);
}

void MasterWebServer::sendErr(WebServer& s, const char* msg) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    sendJson(s, 400, doc);
}

void MasterWebServer::redirectRoot(WebServer& s) {
    s.sendHeader("Location", "/", true);
    s.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// Страницы
// ---------------------------------------------------------------------------
void MasterWebServer::handleRoot(WebServer& s) {
    // S-1: при первом запуске блокируем Web UI до смены дефолтного пароля AP.
    if (m_cfg.forcedPasswordChange) {
        s.send(403, "application/json",
               "{\"ok\":false,\"error\":\"forced_password_change\","
               "\"message\":\"Change default AP password first via POST /api/wifi/ap_password\"}");
        return;
    }
    // C5.8: базовые security-заголовки.
    s.sendHeader("Content-Security-Policy",
                 "default-src 'self'; script-src 'self' 'unsafe-inline'; "
                 "style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                 "connect-src 'self'; frame-ancestors 'none'");
    s.sendHeader("X-Frame-Options", "DENY");
    s.sendHeader("X-Content-Type-Options", "nosniff");
    s.send(200, "text/html", kPageHtml);
}

void MasterWebServer::sendSessionCookie(WebServer& s, const char* token) {
    s.sendHeader("Set-Cookie",
                 String("session=") + token + "; Path=/; HttpOnly; SameSite=Lax; Max-Age="
                 + String(kSessionTimeoutMs / 1000UL));
}

void MasterWebServer::clearSessionCookie(WebServer& s) {
    s.sendHeader("Set-Cookie", "session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
}

// ---------------------------------------------------------------------------
// GET /api/status
// ---------------------------------------------------------------------------
void MasterWebServer::handleStatus(WebServer& s) {
    JsonDocument doc;
    doc["system"]["version"] = AUDIO_FW_VERSION;
    doc["system"]["hostname"] = m_cfg.hostname;
    doc["system"]["uptime_sec"] = millis() / 1000;
    doc["system"]["heap_free"] = ESP.getFreeHeap();
    doc["system"]["psram_free"] = ESP.getFreePsram();
    doc["system"]["cpu_load_percent"] = m_cpuLoadPercent;
    doc["system"]["mac"] = WiFi.macAddress();
    if (m_cfg.ntpEnabled) {
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
    doc["system"]["forced_password_change"] = m_cfg.forcedPasswordChange;
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

// ---------------------------------------------------------------------------
// GET /api/wifi/status
// ---------------------------------------------------------------------------
void MasterWebServer::handleWifiStatus(WebServer& s) {
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

// ---------------------------------------------------------------------------
// GET /api/wifi/scan
// ---------------------------------------------------------------------------
void MasterWebServer::handleWifiScan(WebServer& s) {
    // C5.3: rate limit живого сканирования — не чаще 5 с.
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

// ---------------------------------------------------------------------------
// POST /api/wifi/connect
// ---------------------------------------------------------------------------
void MasterWebServer::handleWifiConnect(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
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

// ---------------------------------------------------------------------------
// POST /api/wifi/save
// ---------------------------------------------------------------------------
void MasterWebServer::handleWifiSave(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
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

// ---------------------------------------------------------------------------
// POST /api/wifi/forget
// ---------------------------------------------------------------------------
void MasterWebServer::handleWifiForget(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
    const char* ssid = doc["ssid"] | "";
    if (WifiStore::forget(ssid)) sendOk(s, "forgotten");
    else sendErr(s, "not found");
}

// ---------------------------------------------------------------------------
// GET /api/wifi/profiles
// ---------------------------------------------------------------------------
void MasterWebServer::handleWifiProfiles(WebServer& s) {
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

// ---------------------------------------------------------------------------
// GET /api/net/internet
// ---------------------------------------------------------------------------
void MasterWebServer::handleInternetStatus(WebServer& s) {
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

// ---------------------------------------------------------------------------
// POST /api/net/check
// ---------------------------------------------------------------------------
void MasterWebServer::handleInternetCheck(WebServer& s) {
    if (m_net) m_net->forceCheck(httpInternetCheck);
    handleInternetStatus(s);
}

// ---------------------------------------------------------------------------
// PUT /api/volume | POST /api/mute
// ---------------------------------------------------------------------------
void MasterWebServer::handleVolume(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    DspWebLock dspGuard(*this);
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

void MasterWebServer::handleMute(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    DspWebLock dspGuard(*this);
    JsonDocument doc;
    if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
    bool m = doc["mute"] | true;
    m_cfg.mute = m;
    if (m_pipeline) m_pipeline->setMute(m);
    sendOk(s);
}

// ---------------------------------------------------------------------------
// PUT /api/crossover
// ---------------------------------------------------------------------------
void MasterWebServer::handleCrossover(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    DspWebLock dspGuard(*this);
    JsonDocument doc;
    if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
    int hz = doc["crossover_hz"] | doc["hz"] | -1;
    if (hz < kCrossoverMinHz || hz > kCrossoverMaxHz) { sendErr(s, "hz out of range"); return; }
    m_cfg.crossoverHz = hz;
    if (m_pipeline) m_pipeline->setCrossoverHz(hz);
    sendOk(s);
}

// ---------------------------------------------------------------------------
// PUT /api/delay
// ---------------------------------------------------------------------------
void MasterWebServer::handleDelay(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    DspWebLock dspGuard(*this);
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

// ---------------------------------------------------------------------------
// POST /api/transport, /api/pair
// ---------------------------------------------------------------------------
void MasterWebServer::handleTransport(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
    const char* mode = doc["mode"] | "";
    if (strcmp(mode, "espnow") == 0) m_cfg.transport = TransportMode::EspNow;
    else if (strcmp(mode, "udp") == 0) m_cfg.transport = TransportMode::Udp;
    else { sendErr(s, "bad mode"); return; }
    sendOk(s);
}

void MasterWebServer::handlePair(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
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

// ---------------------------------------------------------------------------
// POST /api/save, /api/system/*
// ---------------------------------------------------------------------------
void MasterWebServer::handleSave(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    m_saveRequested = true;
    sendOk(s);
}

void MasterWebServer::handleHostname(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
    const char* h = doc["hostname"] | "";
    if (h[0] == '\0' || strlen(h) >= sizeof(m_cfg.hostname)) { sendErr(s, "bad hostname"); return; }
    strlcpy(m_cfg.hostname, h, sizeof(m_cfg.hostname));
    m_saveRequested = true;
    sendOk(s);
}

void MasterWebServer::handleWifiMode(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
    const char* mode = doc["mode"] | "";
    WifiMode m;
    if (strcmp(mode, "AP_DIRECT") == 0) m = WifiMode::ApDirect;
    else if (strcmp(mode, "STA") == 0) m = WifiMode::Sta;
    else if (strcmp(mode, "APSTA") == 0) m = WifiMode::ApSta;
    else { sendErr(s, "bad mode"); return; }
    m_cfg.wifiMode = m;
    m_saveRequested = true;
    m_reconnectRequested = true;
    sendOk(s);
}

void MasterWebServer::handleApPassword(WebServer& s) {
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
    const char* pass = doc["password"] | "";
    const size_t len = strlen(pass);
    if (len < 8 || len >= sizeof(m_cfg.wifiApPassword)) { sendErr(s, "password must be 8..63 chars"); return; }
    if (strcmp(pass, AUDIO_WIFI_AP_PASSWORD) == 0) { sendErr(s, "must differ from default"); return; }
    strlcpy(m_cfg.wifiApPassword, pass, sizeof(m_cfg.wifiApPassword));
    m_cfg.forcedPasswordChange = false; // S-1: пароль изменён — флаг сброшен
    m_saveRequested = true;
    m_reconnectRequested = true;
    sendOk(s, "ap password updated");
}

void MasterWebServer::handleReboot(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    sendOk(s, "reboot requested");
    m_rebootRequested = true;
}

void MasterWebServer::handleFactoryReset(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return; }
    sendOk(s, "factory reset requested");
    m_factoryResetRequested = true;
}

void MasterWebServer::handleConfigExport(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
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

String MasterWebServer::macToStr(const MacAddr& mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac.bytes[0], mac.bytes[1], mac.bytes[2],
             mac.bytes[3], mac.bytes[4], mac.bytes[5]);
    return String(buf);
}

void MasterWebServer::handleConfigImport(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
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

// ---------------------------------------------------------------------------
// GET /api/logs
// ---------------------------------------------------------------------------
void MasterWebServer::handleLogs(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
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

int MasterWebServer::parseLogSeverity(const char* line) {
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

bool MasterWebServer::logLineHasModule(const char* line, const String& module) {
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

// ---------------------------------------------------------------------------
// GET /api/diagnostics
// ---------------------------------------------------------------------------
void MasterWebServer::handleDiagnostics(WebServer& s) {
    if (sendForcedPasswordChange(s)) return;
    JsonDocument doc;
    doc["heap_free"] = ESP.getFreeHeap();
    doc["heap_min_free"] = ESP.getMinFreeHeap();
    doc["psram_free"] = ESP.getFreePsram();
    doc["psram_min_free"] = ESP.getMinFreePsram();
    doc["cpu_load_percent"] = m_cpuLoadPercent;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["internet"] = m_net ? m_net->statusName() : "disabled";
    doc["uptime_sec"] = millis() / 1000;
    doc["wifi_mode"] = wifiModeToString(m_cfg.wifiMode);
    doc["mac"] = WiFi.macAddress();
    sendJson(s, 200, doc);
}

// ---------------------------------------------------------------------------
// POST /api/login, /api/logout, /api/admin/setup
// ---------------------------------------------------------------------------
void MasterWebServer::handleLogin(WebServer& s) {
    if (!m_cfg.authEnabled) { sendOk(s, "no password required"); return; }
    // C5.3: блокировка на kLoginLockMs после kMaxLoginFails неудач.
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
    m_sessionStartMs = millis();

    sendSessionCookie(s, m_sessionToken);
    sendOk(s, "logged in");
}

void MasterWebServer::handleLogout(WebServer& s) {
    m_sessionActive = false;
    m_sessionToken[0] = '\0';
    clearSessionCookie(s);
    sendOk(s, "logged out");
}

void MasterWebServer::handleAdminSetup(WebServer& s) {
    if (m_cfg.authEnabled) {
        s.send(403, "application/json", "{\"ok\":false,\"error\":\"already configured\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, s.arg("plain"))) { sendErr(s, "bad json"); return; }
    const char* pass = doc["password"] | "";
    const char* confirm = doc["confirm"] | "";
    if (strlen(pass) < 8) { sendErr(s, "password too short (min 8)"); return; }
    if (strcmp(pass, confirm) != 0) { sendErr(s, "password mismatch"); return; }
    Auth::hashPassword(pass, m_cfg.adminPasswordHash);
    m_cfg.authEnabled = true;
    ConfigStorage::save(m_cfg);
    Auth::newSessionToken(m_sessionToken);
    m_sessionActive = true;
    m_sessionStartMs = millis();

    sendSessionCookie(s, m_sessionToken);
    sendOk(s, "admin configured");
}

// ---------------------------------------------------------------------------
// OTA — POST /api/update
// ---------------------------------------------------------------------------
void MasterWebServer::handleUpdateUpload(WebServer& s) {
    HTTPUpload& up = s.upload();
    if (up.status == UPLOAD_FILE_START) {
        if (sendForcedPasswordChange(s)) {
            s.send(403, "application/json",
                   "{\"ok\":false,\"error\":\"forced_password_change\"}");
            return;
        }
        if (!csrfOk(s) || !isAuthed(s)) {
            s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
            return;
        }
        // V5: если настроен открытый ключ Ed25519, требуем заголовок X-OTA-Signature.
        if (m_cfg.otaPublicKey[0] != '\0') {
            const char* sig = s.header("X-OTA-Signature");
            if (!sig || strlen(sig) == 0) {
                s.send(401, "application/json",
                       "{\"ok\":false,\"error\":\"ota_signature_required\"}");
                return;
            }
            strncpy(m_otaSignatureHex, sig, sizeof(m_otaSignatureHex) - 1);
            m_otaSignatureHex[sizeof(m_otaSignatureHex) - 1] = '\0';
        } else {
            m_otaSignatureHex[0] = '\0';
        }
        m_updateActive = false;
        m_otaHashing = false;
        memset(m_otaHash, 0, sizeof(m_otaHash));
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            s.send(500, "application/json", "{\"ok\":false,\"error\":\"update begin failed\"}");
            return;
        }
        m_updateActive = true;
        m_otaHashing = true;
        m_otaSha256Ctx.init();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!m_updateActive) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}"); return; }
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            Update.abort();
            m_updateActive = false;
            s.send(500, "application/json", "{\"ok\":false,\"error\":\"write failed\"}");
        }
        // V5: накапливаем SHA-256 загружаемых данных.
        if (m_otaHashing) {
            m_otaSha256Ctx.update(up.buf, up.currentSize);
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (!m_updateActive) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}"); return; }
        m_updateActive = false;
        m_otaHashing = false;

        // V5: завершаем SHA-256 и проверяем подпись Ed25519.
        if (m_cfg.otaPublicKey[0] != '\0') {
            m_otaSha256Ctx.final(m_otaHash);
            if (!verifyOtaSignature(m_otaHash, m_otaSignatureHex)) {
                Update.abort();
                s.send(401, "application/json",
                       "{\"ok\":false,\"error\":\"ota_signature_invalid\"}");
                return;
            }
        }

        if (Update.end(false)) {
            s.send(200, "application/json", "{\"ok\":true,\"status\":\"update ok, rebooting\"}");
            m_updateRebootRequested = true;
        } else {
            Update.abort();
            Update.printError(Serial);
            s.send(500, "application/json", "{\"ok\":false,\"error\":\"update failed\"}");
        }
    }
}

void MasterWebServer::handleUpdateEnd(WebServer& s) {
    s.send(200, "application/json", "{\"ok\":true}");
}

// V5: проверка Ed25519 подписи OTA-образа.
// hash32 — SHA-256 образа (32 байта), signatureHex — hex-подпись (128 символов).
// Возвращает true если подпись верна (или ключ не настроен).
bool MasterWebServer::verifyOtaSignature(const uint8_t* hash32, const char* signatureHex) {
    if (!hash32 || !signatureHex) return false;
    if (strlen(signatureHex) != 128) return false;
    if (strlen(m_cfg.otaPublicKey) != 64) return false;

    // Парсим hex-подпись (64 байта).
    uint8_t signature[64];
    for (int i = 0; i < 64; i++) {
        unsigned int v;
        if (sscanf(signatureHex + i * 2, "%2x", &v) != 1) return false;
        signature[i] = static_cast<uint8_t>(v);
    }

    // Парсим hex-открытый ключ (32 байта).
    uint8_t publicKey[32];
    for (int i = 0; i < 32; i++) {
        unsigned int v;
        if (sscanf(m_cfg.otaPublicKey + i * 2, "%2x", &v) != 1) return false;
        publicKey[i] = static_cast<uint8_t>(v);
    }

#ifdef ESP32
    mbedtls_ed25519_context ctx;
    mbedtls_ed25519_init(&ctx);
    int ret = mbedtls_ed25519_read_public_key(&ctx, publicKey, 32);
    if (ret != 0) {
        mbedtls_ed25519_free(&ctx);
        return false;
    }
    ret = mbedtls_ed25519_verify(&ctx, hash32, 32, signature);
    mbedtls_ed25519_free(&ctx);
    return ret == 0;
#else
    // Не-ESP32 (хост): веб-сервер не компилируется, заглушка.
    return true;
#endif
}

} // namespace audio21

const char* audio21::MasterWebServer::kPageHtml = audio21::kSpaHtml;
