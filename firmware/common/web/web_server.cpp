// web_server.cpp — реализация MasterWebServer (A1/T16: декомпозиция монолита).
// Вынесено из web_server.h для ускорения компиляции и разделения интерфейса/реализации.
// API-методы распределены по модулям: web_auth.cpp, web_api.cpp, web_ota.cpp, web_wifi.cpp, web_logs.cpp.
#include "web_server.h"

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
    // N3: SSE — поток событий статуса сателлитов.
    s.on("/api/events", HTTP_GET, [this, &s]() { handleSSE(s); });
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

} // namespace audio21

const char* audio21::MasterWebServer::kPageHtml = audio21::kSpaHtml;
