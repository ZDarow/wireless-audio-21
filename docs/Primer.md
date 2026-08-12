Ниже — **рабочий skeleton-пример прошивки мастера** для ESP32-S3 с веб-интерфейсом:

- Wi-Fi настройка через браузер;
- AP fallback `Audio21-Setup`;
- подключение к домашней сети;
- проверка доступа в интернет;
- статус Wi-Fi / Internet / Audio;
- REST API для громкости, кроссовера и задержек;
- сохранение настроек в NVS;
- заглушки для последующей интеграции аудио-пайплайна.

> Это именно пример для старта. В нём пока нет полного аудио-пайплайна `arduino-audio-tools`, но подготовлены места для интеграции.

---

## 1. Структура проекта

```text
master_s3_web/
├── platformio.ini
└── src/
    └── main.cpp
```

---

## 2. `platformio.ini`

```ini
[platformio]
default_envs = master_s3_wifi

[env:master_s3_wifi]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

; Для ESP32-S3 N16R8
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB

; Безопасный вариант partition table
board_build.partitions = huge_app.csv

monitor_speed = 115200

lib_deps =
    bblanchon/ArduinoJson@^7.4.1

build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=2
```

---

## 3. `src/main.cpp`

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

// =============================================================
// Globals
// =============================================================

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

const char *AP_SSID = "Audio21-Setup";
const char *AP_PASS = "audio21master";

String cfgSsid = "";
String cfgPass = "";
String cfgHost = "audio-master";
String cfgCheckUrl = "http://connectivitycheck.gstatic.com/generate_204";

int audioVolume = 40;
int audioSubVolume = 50;
int audioCrossoverHz = 90;
int delayLeftMs = 0;
int delayRightMs = 0;
int delaySubMs = 0;

bool apActive = false;

volatile bool internetOnline = false;
volatile uint32_t lastInternetCheckMs = 0;
volatile int internetLatencyMs = 0;
String lastInternetError = "";

// =============================================================
// Web pages
// =============================================================

const char PAGE_ROOT[] = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Audio Master ESP32-S3</title>
  <style>
    body {
      font-family: system-ui, Arial, sans-serif;
      margin: 16px;
      background: #111;
      color: #eee;
    }
    h1 {
      font-size: 22px;
    }
    .card {
      background: #1c1c1c;
      border-radius: 12px;
      padding: 14px;
      margin: 12px 0;
    }
    table {
      border-collapse: collapse;
      width: 100%;
    }
    td {
      padding: 5px 8px;
      border-bottom: 1px solid #333;
    }
    td:first-child {
      color: #9e9e9e;
      width: 40%;
    }
    a {
      color: #4fc3f7;
      text-decoration: none;
    }
    .btn {
      display: inline-block;
      background: #2979ff;
      color: white;
      padding: 8px 12px;
      border-radius: 8px;
      margin-top: 8px;
    }
  </style>
</head>
<body>
  <h1>Audio Master ESP32-S3</h1>

  <div class="card">
    <h2>Статус</h2>
    <table>
      <tr><td>Wi-Fi SSID</td><td id="wifi_ssid">-</td></tr>
      <tr><td>Wi-Fi IP</td><td id="wifi_ip">-</td></tr>
      <tr><td>RSSI</td><td id="wifi_rssi">-</td></tr>
      <tr><td>Internet</td><td id="internet_status">-</td></tr>
      <tr><td>Internet latency</td><td id="internet_latency">-</td></tr>
      <tr><td>Host</td><td id="hostname">-</td></tr>
      <tr><td>Uptime</td><td id="uptime">-</td></tr>
      <tr><td>Free heap</td><td id="heap">-</td></tr>
      <tr><td>Free PSRAM</td><td id="psram">-</td></tr>
    </table>
    <a class="btn" href="/wifi">Настройка Wi-Fi</a>
  </div>

  <div class="card">
    <h2>Аудио</h2>
    <table>
      <tr><td>Volume</td><td id="volume">-</td></tr>
      <tr><td>Sub volume</td><td id="sub_volume">-</td></tr>
      <tr><td>Crossover</td><td id="crossover">-</td></tr>
      <tr><td>Delay L</td><td id="delay_left">-</td></tr>
      <tr><td>Delay R</td><td id="delay_right">-</td></tr>
      <tr><td>Delay Sub</td><td id="delay_sub">-</td></tr>
    </table>
  </div>

  <div class="card">
    <h2>API</h2>
    <p>
      <a href="/api/status">/api/status</a><br>
      <a href="/api/wifi/status">/api/wifi/status</a><br>
      <a href="/api/net/internet">/api/net/internet</a><br>
    </p>
  </div>

  <script>
    async function loadStatus() {
      try {
        const r = await fetch('/api/status');
        const j = await r.json();

        document.getElementById('wifi_ssid').textContent = j.wifi.ssid || '-';
        document.getElementById('wifi_ip').textContent = j.wifi.ip || '-';
        document.getElementById('wifi_rssi').textContent = j.wifi.rssi ?? '-';
        document.getElementById('internet_status').textContent = j.internet.status || '-';
        document.getElementById('internet_latency').textContent = j.internet.latency_ms ?? '-';
        document.getElementById('hostname').textContent = j.system.hostname || '-';
        document.getElementById('uptime').textContent = j.system.uptime_sec + ' s';
        document.getElementById('heap').textContent = j.system.heap_free;
        document.getElementById('psram').textContent = j.system.psram_free;

        document.getElementById('volume').textContent = j.audio.volume;
        document.getElementById('sub_volume').textContent = j.audio.sub_volume;
        document.getElementById('crossover').textContent = j.audio.crossover_hz + ' Hz';
        document.getElementById('delay_left').textContent = j.audio.delay_left_ms + ' ms';
        document.getElementById('delay_right').textContent = j.audio.delay_right_ms + ' ms';
        document.getElementById('delay_sub').textContent = j.audio.delay_sub_ms + ' ms';
      } catch (e) {
        console.error(e);
      }
    }

    loadStatus();
    setInterval(loadStatus, 2000);
  </script>
</body>
</html>
)rawliteral";

const char PAGE_WIFI[] = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Wi-Fi Setup</title>
  <style>
    body {
      font-family: system-ui, Arial, sans-serif;
      margin: 16px;
      background: #111;
      color: #eee;
    }
    .card {
      background: #1c1c1c;
      border-radius: 12px;
      padding: 14px;
      margin: 12px 0;
    }
    input[type=text], input[type=password] {
      width: 100%;
      padding: 10px;
      margin: 8px 0;
      border-radius: 8px;
      border: 1px solid #444;
      background: #222;
      color: #eee;
      box-sizing: border-box;
    }
    button {
      background: #2979ff;
      border: 0;
      color: white;
      padding: 10px 14px;
      border-radius: 8px;
      cursor: pointer;
    }
    ul {
      padding-left: 18px;
    }
    a {
      color: #4fc3f7;
      cursor: pointer;
    }
  </style>
</head>
<body>
  <h1>Настройка Wi-Fi</h1>

  <div class="card">
    <button onclick="scanWifi()">Сканировать сети</button>
    <div id="scan_result"></div>
  </div>

  <div class="card">
    <form method="post" action="/connect">
      <label>SSID</label>
      <input type="text" id="ssid" name="ssid" required>

      <label>Password</label>
      <input type="password" name="pass">

      <label>
        <input type="checkbox" name="save" checked>
        Сохранить сеть
      </label>

      <br><br>
      <button type="submit">Подключиться</button>
    </form>
  </div>

  <div class="card">
    <a href="/">← Dashboard</a>
  </div>

  <script>
    function setSsid(ssid) {
      document.getElementById('ssid').value = ssid;
    }

    async function scanWifi() {
      const el = document.getElementById('scan_result');
      el.innerHTML = '<p>Сканирование...</p>';

      try {
        const r = await fetch('/scan');
        const j = await r.json();

        if (!j.networks || j.networks.length === 0) {
          el.innerHTML = '<p>Сети не найдены</p>';
          return;
        }

        let html = '<ul>';
        j.networks.forEach(n => {
          html += '<li><a onclick="setSsid(\'' + n.ssid + '\')">' + n.ssid + '</a>';
          html += ' | RSSI: ' + n.rssi + ' | CH: ' + n.channel + ' | ' + n.sec;
          html += '</li>';
        });
        html += '</ul>';

        el.innerHTML = html;
      } catch (e) {
        el.innerHTML = '<p>Ошибка сканирования</p>';
      }
    }

    scanWifi();
  </script>
</body>
</html>
)rawliteral";

// =============================================================
// Function prototypes
// =============================================================

void loadSettings();
void saveAudioSettings();
void saveWifiCredentials(const String &ssid, const String &pass);
void applyAudioSettings();

void startAP();
bool connectWifi(const String &ssid, const String &pass, bool keepAp);

void checkInternet();
void internetTask(void *pv);

String getTimeStr();
void sendOk();
void sendError(const char *message);

void handleRoot();
void handleWifiPage();
void handleScan();
void handleConnect();
void handleStatusJson();
void handleWifiStatusJson();
void handleInternetJson();

void handleVolume();
void handleCrossover();
void handleDelay();

void redirectWifi();
void handleNotFound();

// =============================================================
// Settings
// =============================================================

void loadSettings() {
  prefs.begin("master", false);

  cfgSsid = prefs.getString("ssid", "");
  cfgPass = prefs.getString("pass", "");
  cfgHost = prefs.getString("host", "audio-master");
  cfgCheckUrl = prefs.getString("url", "http://connectivitycheck.gstatic.com/generate_204");

  audioVolume = prefs.getInt("vol", 40);
  audioSubVolume = prefs.getInt("subvol", 50);
  audioCrossoverHz = prefs.getInt("cross", 90);
  delayLeftMs = prefs.getInt("delay_l", 0);
  delayRightMs = prefs.getInt("delay_r", 0);
  delaySubMs = prefs.getInt("delay_s", 0);
}

void saveWifiCredentials(const String &ssid, const String &pass) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
}

void saveAudioSettings() {
  prefs.putInt("vol", audioVolume);
  prefs.putInt("subvol", audioSubVolume);
  prefs.putInt("cross", audioCrossoverHz);
  prefs.putInt("delay_l", delayLeftMs);
  prefs.putInt("delay_r", delayRightMs);
  prefs.putInt("delay_s", delaySubMs);
}

// =============================================================
// Audio placeholder
// =============================================================

void applyAudioSettings() {
  Serial.printf(
      "[AUDIO] vol=%d sub=%d crossover=%d Hz delay L/R/S=%d/%d/%d ms\n",
      audioVolume,
      audioSubVolume,
      audioCrossoverHz,
      delayLeftMs,
      delayRightMs,
      delaySubMs
  );

  // TODO:
  // Здесь нужно применить параметры к реальному аудио-пайплайну:
  // - volume stream
  // - crossover filters
  // - delay lines
  // - satellite transport
}

// =============================================================
// Network helpers
// =============================================================

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  apActive = true;

  dnsServer.start(53, "*", WiFi.softAPIP());

  Serial.println("=====================================");
  Serial.println("Started AP fallback mode");
  Serial.printf("SSID: %s\n", AP_SSID);
  Serial.printf("PASS: %s\n", AP_PASS);
  Serial.printf("IP:   %s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("Open http://192.168.4.1/wifi");
  Serial.println("=====================================");
}

bool connectWifi(const String &ssid, const String &pass, bool keepAp) {
  if (ssid.length() == 0) {
    return false;
  }

  if (keepAp && apActive) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }

  WiFi.setHostname(cfgHost.c_str());
  WiFi.disconnect(false);
  delay(100);

  if (pass.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin(ssid.c_str());
  }

  uint32_t start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
    Serial.print(".");
  }

  Serial.println();

  return WiFi.status() == WL_CONNECTED;
}

void checkInternet() {
  if (WiFi.status() != WL_CONNECTED) {
    internetOnline = false;
    return;
  }

  String url = cfgCheckUrl;
  if (url.length() == 0) {
    url = "http://connectivitycheck.gstatic.com/generate_204";
  }

  WiFiClient client;
  HTTPClient http;

  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.setFollowRedirects(false);

  uint32_t start = millis();

  if (!http.begin(client, url)) {
    internetOnline = false;
    lastInternetError = "HTTP begin failed";
    return;
  }

  int code = http.GET();
  int latency = millis() - start;

  if (code > 0) {
    internetLatencyMs = latency;
    internetOnline = (code == 204 || code == 200 || code == 301 || code == 302);
    lastInternetError = "";
  } else {
    internetOnline = false;
    internetLatencyMs = 0;
    lastInternetError = HTTPClient::errorToString(code);
  }

  http.end();
  lastInternetCheckMs = millis();

  Serial.printf("[NET] internet=%s latency=%d ms\n",
                internetOnline ? "online" : "offline",
                internetLatencyMs);
}

void internetTask(void *pv) {
  for (;;) {
    checkInternet();
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

// =============================================================
// Utility
// =============================================================

String getTimeStr() {
  struct tm t;
  if (!getLocalTime(&t)) {
    return "1970-01-01T00:00:00Z";
  }

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  return String(buf);
}

void sendOk() {
  server.send(200, "application/json", "{\"ok\":true}");
}

void sendError(const char *message) {
  String out = "{\"ok\":false,\"error\":\"";
  out += message;
  out += "\"}";
  server.send(400, "application/json", out);
}

void redirectWifi() {
  server.sendHeader("Location", "/wifi", true);
  server.send(302, "text/plain", "");
}

// =============================================================
// HTTP handlers
// =============================================================

void handleRoot() {
  server.send(200, "text/html", PAGE_ROOT);
}

void handleWifiPage() {
  server.send(200, "text/html", PAGE_WIFI);
}

void handleScan() {
  int n = WiFi.scanNetworks();

  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();

  if (n > 0) {
    for (int i = 0; i < n; i++) {
      JsonObject net = networks.add<JsonObject>();
      net["ssid"] = WiFi.SSID(i);
      net["rssi"] = WiFi.RSSI(i);
      net["channel"] = WiFi.channel(i);
      net["sec"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURE";
    }
  }

  WiFi.scanDelete();

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  bool save = server.arg("save") == "on";

  Serial.printf("[WIFI] connect request SSID=%s save=%d\n", ssid.c_str(), save ? 1 : 0);

  bool ok = connectWifi(ssid, pass, apActive);

  if (!ok) {
    server.send(
        200,
        "text/html",
        "<html><body style='background:#111;color:#eee;font-family:sans-serif;'>"
        "<h2>Wi-Fi connection failed</h2>"
        "<p>Check SSID/password and try again.</p>"
        "<a href='/wifi'>Back</a>"
        "</body></html>"
    );
    return;
  }

  if (save) {
    saveWifiCredentials(ssid, pass);

    server.send(
        200,
        "text/html",
        "<html><body style='background:#111;color:#eee;font-family:sans-serif;'>"
        "<h2>Wi-Fi saved and connected</h2>"
        "<p>Device will reboot into normal STA mode.</p>"
        "</body></html>"
    );

    delay(1500);
    ESP.restart();
    return;
  }

  String html =
      "<html><body style='background:#111;color:#eee;font-family:sans-serif;'>"
      "<h2>Wi-Fi connected</h2>"
      "<p>STA IP: <b>" + WiFi.localIP().toString() + "</b></p>"
      "<p>Credentials were not saved.</p>"
      "<a href='/'>Dashboard</a> | <a href='/wifi'>Wi-Fi</a>"
      "</body></html>";

  server.send(200, "text/html", html);
}

void handleStatusJson() {
  JsonDocument doc;

  doc["system"]["version"] = "0.1.0";
  doc["system"]["hostname"] = cfgHost;
  doc["system"]["uptime_sec"] = millis() / 1000;
  doc["system"]["heap_free"] = ESP.getFreeHeap();
  doc["system"]["psram_free"] = ESP.getFreePsram();
  doc["system"]["time"] = getTimeStr();

  doc["wifi"]["mode"] = apActive ? "AP" : "STA";
  doc["wifi"]["ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : (apActive ? String(AP_SSID) : cfgSsid);
  doc["wifi"]["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["wifi"]["rssi"] = WiFi.RSSI();
  doc["wifi"]["mac"] = WiFi.macAddress();

  doc["internet"]["status"] = internetOnline ? "online" : "offline";
  doc["internet"]["latency_ms"] = internetLatencyMs;
  doc["internet"]["last_check"] = getTimeStr();
  doc["internet"]["check_url"] = cfgCheckUrl;
  doc["internet"]["error"] = lastInternetError;

  doc["audio"]["source"] = "wifi_udp";
  doc["audio"]["playing"] = false;
  doc["audio"]["sample_rate"] = 48000;
  doc["audio"]["bits"] = 16;
  doc["audio"]["channels"] = 2;
  doc["audio"]["volume"] = audioVolume;
  doc["audio"]["sub_volume"] = audioSubVolume;
  doc["audio"]["crossover_hz"] = audioCrossoverHz;
  doc["audio"]["delay_left_ms"] = delayLeftMs;
  doc["audio"]["delay_right_ms"] = delayRightMs;
  doc["audio"]["delay_sub_ms"] = delaySubMs;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleWifiStatusJson() {
  JsonDocument doc;

  doc["mode"] = apActive ? "AP" : "STA";
  doc["state"] = (WiFi.status() == WL_CONNECTED) ? "connected" : (apActive ? "ap" : "disconnected");
  doc["ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : (apActive ? String(AP_SSID) : cfgSsid);
  doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["mac"] = WiFi.macAddress();
  doc["hostname"] = cfgHost;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleInternetJson() {
  JsonDocument doc;

  doc["status"] = internetOnline ? "online" : "offline";
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["latency_ms"] = internetLatencyMs;
  doc["last_check"] = getTimeStr();
  doc["check_url"] = cfgCheckUrl;
  doc["error"] = lastInternetError;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleVolume() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));

  if (err) {
    sendError("bad json");
    return;
  }

  if (!doc["volume"].is<int>()) {
    sendError("missing volume");
    return;
  }

  int v = doc["volume"];
  if (v < 0 || v > 100) {
    sendError("volume must be 0..100");
    return;
  }

  audioVolume = v;
  saveAudioSettings();
  applyAudioSettings();
  sendOk();
}

void handleCrossover() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));

  if (err) {
    sendError("bad json");
    return;
  }

  if (!doc["crossover_hz"].is<int>()) {
    sendError("missing crossover_hz");
    return;
  }

  int hz = doc["crossover_hz"];
  if (hz < 70 || hz > 120) {
    sendError("crossover_hz must be 70..120");
    return;
  }

  audioCrossoverHz = hz;
  saveAudioSettings();
  applyAudioSettings();
  sendOk();
}

void handleDelay() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));

  if (err) {
    sendError("bad json");
    return;
  }

  String channel = doc["channel"] | "";
  int ms = doc["delay_ms"] | -1;

  if (ms < 0 || ms > 200) {
    sendError("delay_ms must be 0..200");
    return;
  }

  if (channel == "left") {
    delayLeftMs = ms;
  } else if (channel == "right") {
    delayRightMs = ms;
  } else if (channel == "sub") {
    delaySubMs = ms;
  } else {
    sendError("channel must be left/right/sub");
    return;
  }

  saveAudioSettings();
  applyAudioSettings();
  sendOk();
}

void handleNotFound() {
  if (apActive) {
    redirectWifi();
    return;
  }

  server.send(404, "text/plain", "Not found");
}

// =============================================================
// Setup
// =============================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("=== ESP32-S3 Audio Master Web UI ===");

  loadSettings();
  WiFi.setSleep(false);
  WiFi.persistent(false);

  bool connected = false;

  if (cfgSsid.length() > 0) {
    Serial.printf("[WIFI] Connecting to saved network: %s\n", cfgSsid.c_str());
    connected = connectWifi(cfgSsid, cfgPass, false);
  }

  if (connected) {
    apActive = false;

    Serial.printf("[WIFI] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

    if (MDNS.begin(cfgHost.c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[MDNS] http://%s.local\n", cfgHost.c_str());
    }

    configTzTime("UTC0", "pool.ntp.org");
  } else {
    Serial.println("[WIFI] Failed to connect. Starting AP fallback.");
    startAP();
  }

  // Pages
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifiPage);

  // Captive portal helpers
  server.on("/generate_204", HTTP_GET, redirectWifi);
  server.on("/gen_204", HTTP_GET, redirectWifi);
  server.on("/hotspot-detect.html", HTTP_GET, redirectWifi);
  server.on("/library/test/success.html", HTTP_GET, redirectWifi);
  server.on("/fwlink", HTTP_GET, redirectWifi);

  // Wi-Fi API
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/api/wifi/status", HTTP_GET, handleWifiStatusJson);

  // Internet API
  server.on("/api/net/internet", HTTP_GET, handleInternetJson);
  server.on("/api/net/check", HTTP_POST, []() {
    checkInternet();
    handleInternetJson();
  });

  // Status API
  server.on("/api/status", HTTP_GET, handleStatusJson);

  // Audio API
  server.on("/api/volume", HTTP_POST, handleVolume);
  server.on("/api/crossover", HTTP_POST, handleCrossover);
  server.on("/api/delay", HTTP_POST, handleDelay);

  server.on("/api/save", HTTP_POST, []() {
    saveAudioSettings();
    sendOk();
  });

  server.onNotFound(handleNotFound);
  server.begin();

  xTaskCreatePinnedToCore(
      internetTask,
      "internet_task",
      6144,
      nullptr,
      1,
      nullptr,
      0
  );

  Serial.println("[HTTP] Web server started");
}

// =============================================================
// Loop
// =============================================================

void loop() {
  if (apActive) {
    dnsServer.processNextRequest();
  }

  server.handleClient();
}
```

---

## 4. Как прошить

```bash
cd master_s3_web
pio run -e master_s3_wifi -t upload
pio device monitor
```

Если монитор не показывает вывод, попробуй другой порт или измени `ARDUINO_USB_CDC_ON_BOOT` под свою плату ESP32-S3.

---

## 5. Первый запуск

Если сохранённой Wi-Fi сети нет, устройство поднимет точку:

```text
SSID: Audio21-Setup
PASS: audio21master
IP:   192.168.4.1
```

Далее:

1. Подключиться смартфоном или ПК к `Audio21-Setup`.
2. Открыть:

```text
http://192.168.4.1/wifi
```

3. Нажать `Сканировать сети`.
4. Выбрать домашнюю сеть.
5. Ввести пароль.
6. Оставить галочку `Сохранить сеть`.
7. Нажать `Подключиться`.

После сохранения устройство перезагрузится и подключится к домашней Wi-Fi сети.

---

## 6. Примеры API

Получить общий статус:

```bash
curl http://audio-master.local/api/status
```

или по IP:

```bash
curl http://192.168.1.55/api/status
```

Проверить интернет:

```bash
curl -X POST http://audio-master.local/api/net/check
```

Установить громкость:

```bash
curl -X POST http://audio-master.local/api/volume \
  -H "Content-Type: application/json" \
  -d '{"volume":45}'
```

Установить кроссовер:

```bash
curl -X POST http://audio-master.local/api/crossover \
  -H "Content-Type: application/json" \
  -d '{"crossover_hz":95}'
```

Установить задержку сабвуфера:

```bash
curl -X POST http://audio-master.local/api/delay \
  -H "Content-Type: application/json" \
  -d '{"channel":"sub","delay_ms":18}'
```

---

## 7. Что нужно добавить дальше

Этот код закрывает базовую часть веб-интерфейса и сети.

Для полной прошивки мастера нужно добавить:

1. Аутентификацию в web-интерфейсе.
2. Хранение нескольких Wi-Fi профилей.
3. Полноценный Wi-Fi UDP audio receiver.
4. Jitter buffer для входного потока.
5. DSP:
   - volume;
   - HPF/LPF crossover;
   - delay lines.
6. ESP-NOW transmitter для сателлитов.
7. Синхронизацию сателлитов.
8. OTA-обновление через браузер.
9. Страницу управления сателлитами.
10. Интеграцию OLED и энкодера.
