// web_api.cpp — REST API эндпоинты (модуль A1).
// Вынесено из web_server.cpp при декомпозиции монолита.
#include "web_server.h"

namespace audio21 {

// ---------------------------------------------------------------------------
// GET /api/status (ТЗ §16.1)
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
// GET /api/wifi/status (ТЗ §15.1)
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
// GET /api/wifi/scan (ТЗ §15.2)
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
// POST /api/wifi/connect (ТЗ §15.3)
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
// POST /api/wifi/save (ТЗ §15.4)
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
// POST /api/wifi/forget (ТЗ §15.5)
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
// GET /api/wifi/profiles (ТЗ §6.5)
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
// GET /api/net/internet (ТЗ §15.6)
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
// POST /api/net/check (ТЗ §15.7)
// ---------------------------------------------------------------------------
void MasterWebServer::handleInternetCheck(WebServer& s) {
    if (m_net) m_net->forceCheck(httpInternetCheck);
    handleInternetStatus(s);
}

// ---------------------------------------------------------------------------
// PUT /api/volume | POST /api/mute (ТЗ §16.2, §16.5)
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
// PUT /api/crossover (ТЗ §16.3)
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
// PUT /api/delay (ТЗ §16.4)
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
// POST /api/save, /api/system/* (ТЗ §17)
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
// GET /api/logs (ТЗ §17.5)
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
// GET /api/diagnostics (ТЗ §17.6)
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

} // namespace audio21
