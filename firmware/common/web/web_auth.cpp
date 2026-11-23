// web_auth.cpp — авторизация, сессии, CSRF (модуль A1).
// Вынесено из web_server.cpp при декомпозиции монолита.
#include "web_server.h"

namespace audio21 {

bool MasterWebServer::sessionActive() const { return m_sessionActive; }

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

// P19: объединённая проверка авторизации — forcedPasswordChange + CSRF.
// Возвращает true если запрос разрешён, false — если нужно вернуть ошибку
// (обработчик уже отправил odpowiedź). Уменьшает дублирование кода в
// 20+ обработчиках web_api.cpp.
bool MasterWebServer::checkAuth(WebServer& s) {
    if (sendForcedPasswordChange(s)) return false;
    if (!csrfOk(s)) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"csrf\"}"); return false; }
    return true;
}

void MasterWebServer::sendSessionCookie(WebServer& s, const char* token) {
    s.sendHeader("Set-Cookie",
                 String("session=") + token + "; Path=/; HttpOnly; SameSite=Lax; Max-Age="
                 + String(kSessionTimeoutMs / 1000UL));
}

void MasterWebServer::clearSessionCookie(WebServer& s) {
    s.sendHeader("Set-Cookie", "session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
}

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

} // namespace audio21
