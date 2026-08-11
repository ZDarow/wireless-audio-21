// web_server.h — Web UI + REST API мастер-узла.
//
// Использует встроенный WebServer ESP32 Arduino (синхронный) + ArduinoJson.
// Эндпоинты (спецификация docs/PLAN.md §6.4):
//   GET  /                    — панель управления (HTML+JS)
//   GET  /api/status          — JSON-состояние узла
//   PUT  /api/volume          — {"volume":0..100} | {"mute":true|false}
//   PUT  /api/crossover       — {"hz":70..120}
//   PUT  /api/delay           — {"channel":"left|right|sub","ms":0..200}
//   POST /api/transport       — {"mode":"espnow"|"udp"} (действие)
//   POST /api/pair            — {"side":"left|right","mac":"AA:BB:CC:DD:EE:01"} (действие)
//   POST /api/save            — сохранить конфиг в NVS (действие)
//   POST /api/reboot          — перезагрузка (действие)
//   GET  /api/wifi/scan       — список найденных Wi-Fi сетей (настройка подключения)
//   POST /api/wifi            — {"ssid":"...","password":"..."} → сохранить и reboot
//
// Аудио-зависимости (pipeline, delay, espnow, статусы) — опциональные
// (указатели, дефолт nullptr): мастер ESP32-S3 (этап 1, без аудио-конвейера)
// использует тот же Web UI только для настройки Wi-Fi и диагностики;
// аудио-эндпоинты в этом случае возвращают "unavailable".
//
// Header-only. Только для мастер-узла (Web UI на сателлитах не нужен).
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "node_config.h"
#include "pcm_pipeline.h"
#include "delay_line.h"
#include "espnow.h"
#include "storage.h"

namespace audio21 {

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

    // Запустить сервер (Wi-Fi должен быть подключён).
    void begin() {
        m_server.on("/", HTTP_GET, [this]() { handleRoot(); });
        m_server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
        m_server.on("/api/volume", HTTP_PUT, [this]() { handleVolume(); });
        m_server.on("/api/crossover", HTTP_PUT, [this]() { handleCrossover(); });
        m_server.on("/api/delay", HTTP_PUT, [this]() { handleDelay(); });
        m_server.on("/api/transport", HTTP_POST, [this]() { handleTransport(); });
        m_server.on("/api/pair", HTTP_POST, [this]() { handlePair(); });
        m_server.on("/api/save", HTTP_POST, [this]() { handleSave(); });
        m_server.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
        m_server.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
        m_server.on("/api/wifi", HTTP_POST, [this]() { handleWifi(); });
        m_server.begin();
    }

    // Вызывать в loop().
    void handleClient() { m_server.handleClient(); }

private:
    // --- Ответы ---
    static void sendJson(WebServer& s, int code, JsonDocument& doc) {
        String body;
        serializeJson(doc, body);
        s.send(code, "application/json", body);
    }

    static void sendOk(WebServer& s, const char* msg = "ok") {
        JsonDocument doc;
        doc["status"] = msg;
        sendJson(s, 200, doc);
    }

    static void sendErr(WebServer& s, const char* msg = "err") {
        JsonDocument doc;
        doc["status"] = msg;
        sendJson(s, 400, doc);
    }

    // --- Страница управления ---
    void handleRoot() {
        m_server.send(200, "text/html", kPageHtml);
    }

    // --- GET /api/status ---
    void handleStatus() {
        JsonDocument doc;
        doc["role"] = "master";
        doc["source"] = sourceToString(m_cfg.source);
        doc["connected"] = m_a2dpConnected ? *m_a2dpConnected : false;
        doc["transport"] = transportToString(m_cfg.transport);
        doc["sample_rate"] = m_cfg.sampleRate;
        doc["bits"] = m_cfg.bitsPerSample;
        doc["channels"] = m_cfg.channels;
        doc["volume"] = m_cfg.masterVolume;
        doc["mute"] = m_cfg.mute;
        doc["crossover_hz"] = m_cfg.crossoverHz;
        doc["delay_left_ms"] = m_cfg.delayLeftMs;
        doc["delay_right_ms"] = m_cfg.delayRightMs;
        doc["delay_sub_ms"] = m_cfg.delaySubMs;
        // Wi-Fi-диагностика (настройка подключения).
        doc["wifi_mode"] = wifiModeToString(m_cfg.wifiMode);
        doc["wifi_ssid"] = m_cfg.wifiSsid;
        doc["wifi_ip"] = WiFi.localIP().toString();
        doc["wifi_ap_ip"] = WiFi.softAPIP().toString();
        JsonObject satellites = doc["satellites"].to<JsonObject>();
        satellites["left"] = (m_leftOnline && *m_leftOnline) ? "online" : "offline";
        satellites["right"] = (m_rightOnline && *m_rightOnline) ? "online" : "offline";
        sendJson(m_server, 200, doc);
    }

    // --- PUT /api/volume ---
    void handleVolume() {
        if (!m_pipeline) { sendErr(m_server, "audio unavailable"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, m_server.arg("plain"))) { sendErr(m_server, "bad json"); return; }

        if (doc["mute"].is<bool>()) {
            bool m = doc["mute"].as<bool>();
            m_cfg.mute = m;
            m_pipeline->setMute(m);
            sendOk(m_server);
            return;
        }
        // Нет ни volume, ни mute — ошибка (не молча ставим 0).
        if (!doc["volume"].is<int>()) { sendErr(m_server, "missing volume|mute"); return; }
        int v = doc["volume"].as<int>();
        if (v < kVolumeMin || v > kVolumeMax) { sendErr(m_server, "volume out of range"); return; }
        m_cfg.masterVolume = v;
        m_pipeline->setVolume(v);
        sendOk(m_server);
    }

    // --- PUT /api/crossover ---
    void handleCrossover() {
        if (!m_pipeline) { sendErr(m_server, "audio unavailable"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, m_server.arg("plain"))) { sendErr(m_server, "bad json"); return; }
        int hz = doc["hz"].as<int>();
        if (hz < kCrossoverMinHz || hz > kCrossoverMaxHz) { sendErr(m_server, "hz out of range"); return; }
        m_cfg.crossoverHz = hz;
        m_pipeline->setCrossoverHz(hz);
        sendOk(m_server);
    }

    // --- PUT /api/delay ---
    void handleDelay() {
        if (!m_delayLeft || !m_delayRight || !m_delaySub) { sendErr(m_server, "audio unavailable"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, m_server.arg("plain"))) { sendErr(m_server, "bad json"); return; }
        const char* chan = doc["channel"] | "";
        int ms = doc["ms"].as<int>();
        if (ms < kMinDelayMs || ms > kMaxDelayMs) { sendErr(m_server, "ms out of range"); return; }

        if (strcmp(chan, "left") == 0) { m_cfg.delayLeftMs = ms; (*m_delayLeft)->setDelayMs(ms); }
        else if (strcmp(chan, "right") == 0) { m_cfg.delayRightMs = ms; (*m_delayRight)->setDelayMs(ms); }
        else if (strcmp(chan, "sub") == 0) { m_cfg.delaySubMs = ms; (*m_delaySub)->setDelayMs(ms); }
        else { sendErr(m_server, "bad channel"); return; }
        sendOk(m_server);
    }

    // --- POST /api/transport ---
    void handleTransport() {
        JsonDocument doc;
        if (deserializeJson(doc, m_server.arg("plain"))) { sendErr(m_server, "bad json"); return; }
        const char* mode = doc["mode"] | "";
        if (strcmp(mode, "espnow") == 0) m_cfg.transport = TransportMode::EspNow;
        else if (strcmp(mode, "udp") == 0) m_cfg.transport = TransportMode::Udp;
        else { sendErr(m_server, "bad mode"); return; }
        sendOk(m_server);
    }

    // --- POST /api/pair ---
    void handlePair() {
        if (!m_espnow) { sendErr(m_server, "espnow unavailable"); return; }
        JsonDocument doc;
        if (deserializeJson(doc, m_server.arg("plain"))) { sendErr(m_server, "bad json"); return; }
        const char* side = doc["side"] | "";
        const char* macStr = doc["mac"] | "";
        MacAddr mac;
        if (!MacAddr::parse(macStr, mac)) { sendErr(m_server, "bad mac"); return; }

        if (strcmp(side, "left") == 0) { m_cfg.leftSatMac = mac; m_espnow->addPeer(mac); }
        else if (strcmp(side, "right") == 0) { m_cfg.rightSatMac = mac; m_espnow->addPeer(mac); }
        else { sendErr(m_server, "bad side"); return; }
        sendOk(m_server);
    }

    // --- POST /api/save ---
    void handleSave() {
        // ConfigStorage подключается в main.cpp; здесь только ответ.
        m_saveRequested = true;
        sendOk(m_server);
    }

    // --- POST /api/reboot ---
    void handleReboot() {
        m_server.send(200, "application/json", "{\"status\":\"rebooting\"}");
        delay(100);
        ESP.restart();
    }

    // --- GET /api/wifi/scan : список найденных сетей ---
    void handleWifiScan() {
        // В setup mode STA-драйвер может активно переподключаться (авто-реконнект
        // после неудачного WiFi.begin) и мешать сканированию — останавливаем
        // попытки и отключаем автоподключение, чтобы скан был чистым.
        WiFi.setAutoReconnect(false);
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect(false, true);
            delay(100);
        }
        int n = WiFi.scanNetworks(); // блокирующий скан (~1-2 с)
        JsonDocument doc;
        JsonArray nets = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject o = nets.add<JsonObject>();
            o["ssid"] = WiFi.SSID(i);
            o["rssi"] = WiFi.RSSI(i);
            o["enc"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "wpa";
        }
        WiFi.scanDelete();
        sendJson(m_server, 200, doc);
    }

    // --- POST /api/wifi : сохранить SSID/пароль и перезагрузиться ---
    void handleWifi() {
        JsonDocument doc;
        if (deserializeJson(doc, m_server.arg("plain"))) { sendErr(m_server, "bad json"); return; }
        const char* ssid = doc["ssid"] | "";
        const char* pass = doc["password"] | "";
        if (ssid == nullptr || strlen(ssid) == 0 || strlen(ssid) >= sizeof(m_cfg.wifiSsid)) {
            sendErr(m_server, "bad ssid");
            return;
        }
        if (pass != nullptr && strlen(pass) >= sizeof(m_cfg.wifiPassword)) {
            sendErr(m_server, "bad password");
            return;
        }
        strlcpy(m_cfg.wifiSsid, ssid, sizeof(m_cfg.wifiSsid));
        strlcpy(m_cfg.wifiPassword, pass != nullptr ? pass : "", sizeof(m_cfg.wifiPassword));
        if (!ConfigStorage::save(m_cfg)) { sendErr(m_server, "nvs save failed"); return; }
        m_server.send(200, "application/json", "{\"status\":\"saved, rebooting\"}");
        delay(200);
        ESP.restart();
    }

public:
    // Флаг «запрошено сохранение» — опрашивается в loop() main.cpp.
    bool saveRequested() const { return m_saveRequested; }
    void clearSaveRequested() { m_saveRequested = false; }

private:
    NodeConfig& m_cfg;
    PcmPipeline* m_pipeline;
    DelayLine** m_delayLeft;
    DelayLine** m_delayRight;
    DelayLine** m_delaySub;
    EspNowTransport* m_espnow;
    volatile bool* m_leftOnline;
    volatile bool* m_rightOnline;
    volatile bool* m_a2dpConnected;
    WebServer m_server;
    bool m_saveRequested = false;

    // Панель управления (HTML+JS, без внешних ресурсов).
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
  body { font-family: sans-serif; max-width: 480px; margin: 16px auto; padding: 0 12px; background:#111; color:#eee; }
  h1 { font-size: 20px; }
  .card { background:#1c1c1c; border:1px solid #333; border-radius:8px; padding:12px; margin:10px 0; }
  label { display:block; margin:6px 0 2px; font-size:13px; color:#aaa; }
  input[type=range] { width:100%; }
  input[type=text], input[type=password], select { width:100%; padding:6px; box-sizing:border-box; background:#222; color:#eee; border:1px solid #444; border-radius:4px; }
  button { margin-top:8px; padding:8px 14px; background:#2a6; color:#fff; border:none; border-radius:4px; cursor:pointer; }
  button.danger { background:#a33; }
  .row { display:flex; gap:8px; align-items:center; }
  .val { color:#2a6; font-weight:bold; }
  .net { padding:8px; border:1px solid #333; border-radius:4px; margin:4px 0; cursor:pointer; font-size:14px; }
  .net:active { background:#2a6; }
  #status { font-size:13px; color:#aaa; white-space:pre-line; }
  #wifiMsg { font-size:13px; color:#aaa; white-space:pre-line; }
</style>
</head>
<body>
<h1>Audio 2.1 Master</h1>

<div class="card">
  <label>Wi-Fi подключение (домашняя сеть)</label>
  <input type="text" id="wifiSsid" placeholder="SSID сети" autocomplete="off">
  <input type="password" id="wifiPass" placeholder="Пароль сети">
  <div class="row">
    <button id="wifiScanBtn">Сканировать</button>
    <button id="wifiSaveBtn">Сохранить и перезагрузить</button>
  </div>
  <div id="wifiNets"></div>
  <div id="wifiMsg"></div>
</div>

<div class="card">
  <label>Громкость: <span id="volLabel" class="val">50</span></label>
  <input type="range" id="volume" min="0" max="100" value="50">
  <div class="row">
    <button id="muteBtn">Mute</button>
    <button id="unmuteBtn">Unmute</button>
  </div>
</div>

<div class="card">
  <label>Кроссовер: <span id="xoLabel" class="val">90</span> Гц</label>
  <input type="range" id="crossover" min="70" max="120" value="90">
</div>

<div class="card">
  <label>Задержка канала, мс</label>
  <div class="row">
    <select id="delayChan">
      <option value="left">Left</option>
      <option value="right">Right</option>
      <option value="sub">Sub</option>
    </select>
    <input type="number" id="delayMs" min="0" max="200" value="0" style="width:70px">
    <button id="delayBtn">Set</button>
  </div>
</div>

<div class="card">
  <label>Транспорт</label>
  <div class="row">
    <select id="transport">
      <option value="espnow">ESP-NOW</option>
      <option value="udp">UDP</option>
    </select>
    <button id="transportBtn">Set</button>
  </div>
</div>

<div class="card">
  <label>Привязка сателлита</label>
  <div class="row">
    <select id="pairSide">
      <option value="left">Left</option>
      <option value="right">Right</option>
    </select>
    <input type="text" id="pairMac" placeholder="AA:BB:CC:DD:EE:01">
    <button id="pairBtn">Pair</button>
  </div>
</div>

<div class="card">
  <div class="row">
    <button id="saveBtn">Сохранить (NVS)</button>
    <button id="rebootBtn" class="danger">Reboot</button>
  </div>
</div>

<div class="card" id="status">Загрузка...</div>

<script>
const $ = id => document.getElementById(id);
async function api(path, body, method) {
  const opts = body ? { method: method || 'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body) } : {};
  const r = await fetch(path, opts);
  return r.json();
}
async function refresh() {
  try {
    const s = await api('/api/status');
    $('volLabel').textContent = s.volume;
    $('volume').value = s.volume;
    $('xoLabel').textContent = s.crossover_hz;
    $('crossover').value = s.crossover_hz;
    $('transport').value = s.transport;
    // Поле SSID НЕ перезаписываем в refresh(): пользователь может вводить своё
    // значение. Заполнение из сохранённого конфига — один раз при загрузке
    // страницы (initWifiField ниже).
    $('status').textContent =
      'Источник: ' + s.source + (s.connected ? ' (A2DP подключён)' : '') + '\n' +
      'Wi-Fi: ' + s.wifi_mode + ' ' + (s.wifi_ip || '-') + '\n' +
      'Транспорт: ' + s.transport + '\n' +
      'Кроссовер: ' + s.crossover_hz + ' Гц\n' +
      'Задержки: L ' + s.delay_left_ms + ' / R ' + s.delay_right_ms + ' / Sub ' + s.delay_sub_ms + ' мс\n' +
      'Сателлиты: L ' + s.satellites.left + ' | R ' + s.satellites.right;
  } catch (e) { $('status').textContent = 'Ошибка связи'; }
}
async function scanWifi() {
  $('wifiMsg').textContent = 'Сканирование...';
  try {
    const d = await api('/api/wifi/scan');
    $('wifiNets').innerHTML = '';
    (d.networks || []).forEach(n => {
      const el = document.createElement('div');
      el.className = 'net';
      el.textContent = n.ssid + '  (' + n.rssi + ' dBm' + (n.enc === 'open' ? ', open' : '') + ')';
      el.onclick = () => { $('wifiSsid').value = n.ssid; };
      $('wifiNets').appendChild(el);
    });
    $('wifiMsg').textContent = 'Найдено сетей: ' + (d.networks || []).length;
  } catch (e) { $('wifiMsg').textContent = 'Ошибка сканирования'; }
}
// Заполнить поле SSID сохранённым значением один раз при загрузке страницы
// (не перезаписываем в refresh(), чтобы не мешать вводу пользователя).
async function initWifiField() {
  try {
    const s = await api('/api/status');
    if (s.wifi_ssid) $('wifiSsid').value = s.wifi_ssid;
  } catch (e) { /* сеть ещё не готова — оставляем поле пустым */ }
}
$('volume').oninput = async e => { $('volLabel').textContent = e.target.value; await api('/api/volume', {volume: +e.target.value}, 'PUT'); };
$('muteBtn').onclick = async () => api('/api/volume', {mute:true}, 'PUT');
$('unmuteBtn').onclick = async () => api('/api/volume', {mute:false}, 'PUT');
$('crossover').oninput = async e => { $('xoLabel').textContent = e.target.value; await api('/api/crossover', {hz: +e.target.value}, 'PUT'); };
$('delayBtn').onclick = async () => api('/api/delay', {channel: $('delayChan').value, ms: +$('delayMs').value}, 'PUT');
$('transportBtn').onclick = async () => api('/api/transport', {mode: $('transport').value});
$('pairBtn').onclick = async () => api('/api/pair', {side: $('pairSide').value, mac: $('pairMac').value.trim()});
$('saveBtn').onclick = async () => api('/api/save');
$('rebootBtn').onclick = async () => { await api('/api/reboot'); setTimeout(refresh, 3000); };
$('wifiScanBtn').onclick = scanWifi;
$('wifiSaveBtn').onclick = async () => {
  const ssid = $('wifiSsid').value.trim();
  if (!ssid) { $('wifiMsg').textContent = 'Введите SSID'; return; }
  $('wifiMsg').textContent = 'Сохранение...';
  try {
    const r = await fetch('/api/wifi', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ssid: ssid, password: $('wifiPass').value})
    });
    const d = await r.json();
    $('wifiMsg').textContent = d.status || 'ok';
  } catch (e) { $('wifiMsg').textContent = 'Ошибка сохранения'; }
};
refresh();
initWifiField();
setInterval(refresh, 2000);
</script>
</body>
</html>
)rawliteral";

} // namespace audio21