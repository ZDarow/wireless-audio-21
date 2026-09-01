// main.cpp — мастер-узел Wireless Audio 2.1 на ESP32-S3 (сабвуфер).
//
// Адаптация под ESP32-S3 (см. docs/PLAN.md и ТЗ): S3 не поддерживает A2DP,
// поэтому источник аудио — Wi-Fi UDP PCM со смартфона.
//
// Этап 2 (текущий): загрузка платы, стартовая диагностика (chip/flash/PSRAM),
// Wi-Fi (AP_DIRECT, STA или APSTA-репитер), Web UI + REST API (включая
// настройку Wi-Fi подключения со смартфона), UDP-listener на AUDIO_UDP_PORT,
// serial-консоль, аудио-конвейер: UDP → jitter buffer (PSRAM) → DSP
// (volume → tone → limiter → LR4 crossover) → sub → DelayLine → I2S.
// TX на сателлиты (left/right HPF-каналы) — Этап 3.
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
#include <esp_task_wdt.h>
#include <ping/ping_sock.h>
#include <freertos/semphr.h>
#include <time.h>
#include <vector>
#include <math.h>

#include "node_config.h"
#include "storage.h"
#include "logger.h"
#include "master_s3_config.h"
#include "web_server.h"
#include "logs.h"
#include "wifi_store.h"
#include "internet_check.h"
#include "udp_audio_packet.h"
#include "udp_audio_receiver.h"
#include "udp_transport.h"
#include "jitter_buffer.h"
#include "pcm_pipeline.h"
#include "delay_line.h"
#include "i2s_output.h"
#include "console.h"

using namespace audio21;

// ---------------------------------------------------------------------------
// Аудио-конвейер мастера (Этап 2, §9/§18): UDP-пакет со смартфона →
// UdpAudioReceiver (sequence/concealment) → JitterBuffer (PSRAM) → DSP
// (volume → tone → limiter → LR4 crossover) → sub → DelayLine → I2S.
// left/right (HPF) — для сателлитов, TX — Этап 3.
// ---------------------------------------------------------------------------

// Буфер PCM для приёма: максимальный UDP-пакет §9.2 (< MTU).
static uint8_t g_udpBuf[sizeof(UdpAudioHeader) + kUdpMaxPayload];
static UdpAudioReceiver g_audioRecv;
// JitterBuffer в PSRAM: 20–60 мс при 48 кГц (§7.6, §16.2 — B13). Моно (sub).
static constexpr uint32_t kMasterJitterCapacity = 60 * 48000 / 1000;
static JitterBuffer* g_jitter = nullptr;   // ps_malloc в setup
static volatile bool g_audioActive = false; // статус для Web UI

// TX аудио на сателлиты (C3.1/C3.2, §10.2): батч 117 семплов ≈ 2.4 мс @48 кГц
// → payload 234 байта (лимит ESP-NOW). Отправка — по заполнению батча.
static constexpr size_t kBatchSamples = 117;
static int16_t g_txLeft[kBatchSamples];
static int16_t g_txRight[kBatchSamples];
static size_t g_txCount = 0;
static uint32_t g_txPacketId = 0;
static uint32_t g_txPackets = 0;

// DSP-конвейер (C2.1): volume → tone → limiter → LR4 crossover → L/R/Sub.
static PcmPipeline g_pipeline;

// Линии задержки L/R/Sub (C2.3): ёмкость kMaxDelayMs, буферы в PSRAM.
// Указатели передаются в MasterWebServer — настройки /api/delay применяются
// к живым объектам, а не только к конфигу.
static DelayLine* g_delayLeft = nullptr;
static DelayLine* g_delayRight = nullptr;
static DelayLine* g_delaySub = nullptr;

// ---------------------------------------------------------------------------
// Глобальное состояние
// ---------------------------------------------------------------------------

static NodeConfig g_cfg;
static WiFiUDP g_udp;            // приём аудио от смартфона (порт 5004)
static UdpTransport g_udpTx;     // C3.1: TX аудио на сателлиты (порт 4210)
static uint32_t g_packetsRx = 0;
static uint32_t g_packetBytesRx = 0;

static MasterS3Console g_console{g_cfg};

// I2S-выход (C1.4): сабвуфер — моно (L=R), пины BCK=4/WS=5/DATA=6.
static I2sOutput g_i2sOut;
static bool g_i2sOn = false;

// Тестовый тон для проверки I2S: `tone <freq>` — синус в loop(), не блокируя Wi-Fi/Web UI.
static constexpr uint32_t kToneDurationMs = 2000;
static uint32_t g_toneUntilMs = 0;
static uint32_t g_toneFreq = 440;
static uint32_t g_tonePhase = 0;
static constexpr float kToneAmp = 0.2f;

// ESP-NOW: приём heartbeat от сателлитов (discovery-response) для статуса
// online даже без аудио-потока. Аудио-конвейер — Этап 2+.
static EspNowTransport g_espnow;
static volatile bool g_leftOnline = false;
static volatile bool g_rightOnline = false;
static uint32_t g_leftLastSeenMs = 0;
static uint32_t g_rightLastSeenMs = 0;
static uint32_t g_heartbeatsRx = 0;

// Web UI + REST API. Аудио-конвейер (pipeline/delay/espnow) подключён:
// /api/volume, /api/mute, /api/crossover и /api/delay применяют настройки
// к живым объектам DSP (C2.1–C2.4), а не только к конфигу.
static MasterWebServer g_webServer(g_cfg, &g_pipeline,
                                   &g_delayLeft, &g_delayRight, &g_delaySub,
                                   &g_espnow, &g_leftOnline, &g_rightOnline);// Режим настройки Wi-Fi: STA не подключился → мастер поднял AP настройки.
static bool g_setupMode = false;

// Captive portal: перехватывает DNS-запросы телефона (любой домен → softAPIP()),
// браузер открывает http://192.168.4.1/ → onNotFound → страница настройки.
static DNSServer g_dns;

// Проверка интернета (ТЗ_Веб §7): HTTP GET до connectivitycheck.gstatic.com.
// Блокирующий HTTP (DNS+connect+read) выполняется в отдельной задаче
// internetCheckTask, чтобы не замораживать loop/Web UI.
static InternetChecker g_internet;
static uint32_t g_lastNetTick = 0;

static void internetCheckTask(void*) {
    for (;;) {
        uint32_t now = millis();
        if (g_internet.tick(httpInternetCheck, now)) {
            g_lastNetTick = now;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// Кольцевой буфер логов для Web UI (ТЗ_Веб §13).
// Лог-буфер для Web UI: 96 слотов × 192 Б ≈ 18 КБ (ТЗ §13.4: 16–64 КБ, C5.4).
static char g_logStorage[96][LogRing::kLineSize];
static LogRing g_logs(g_logStorage[0], 96);

// CPU load (C5.5): доля времени loop() вне delay, усредняется за 2 с.
static uint32_t g_cpuBusyUs = 0;
static uint32_t g_cpuTotalUs = 0;
static uint32_t g_cpuLastReportMs = 0;
static uint32_t g_cpuLoadPercent = 0;

// ---------------------------------------------------------------------------
// ESP-NOW: приём heartbeat/discovery-response от сателлитов (статус online)
// ---------------------------------------------------------------------------

// Сателлит шлёт discovery-response (kFlagDiscoveryResponse) каждые ~2 с
// (heartbeat) со своим каналом — мастер помечает его online. Таймаут — 6 с.
// Интервал и таймаут — общие (audio_packet.h).
static constexpr uint32_t kDiscoveryIntervalMs = kHeartbeatIntervalMs;

// Переподключение STA по запросу Web UI: не убиваем AP (ApSta-репитер) и не
// блокируем loop — ждём исход в loop() с лимитом, при неудаче поднимаем setup AP.
static constexpr uint32_t kReconnectTimeoutMs = 20000;
static uint32_t g_reconnectAtMs = 0;
static bool g_reconnectPending = false;
static bool g_wifiWasConnected = false; // C6.2: был ли STA-линк (для авто-реконнекта)

// Периодический discovery-запрос: сателлит запоминает MAC мастера и отвечает
// unicast-heartbeat-ом; мастер также получает broadcast-heartbeat напрямую.
static uint32_t g_lastDiscoveryMs = 0;
static void sendDiscoveryRequest() {
    uint8_t buf[kMaxPacketSize];
    size_t n = buildPacket(buf, sizeof(buf), 0x00, kSampleFormatInt16,
                           nullptr, 0, (uint32_t)millis(), 0, kFlagDiscoveryRequest);
    g_espnow.broadcast(buf, n);
}

static void onEspNowPacket(const uint8_t* data, size_t size, const MacAddr& from) {
    AudioPacketHeader hdr;
    const uint8_t* payload;
    size_t payloadSize;
    if (!parsePacket(data, size, hdr, payload, payloadSize)) return;

    if (hdr.flags & kFlagDiscoveryResponse) {
        g_heartbeatsRx++;
        uint32_t now = millis();
        if (hdr.channel == kChannelLeft) {
            g_leftOnline = true;
            g_leftLastSeenMs = now;
        } else if (hdr.channel == kChannelRight) {
            g_rightOnline = true;
            g_rightLastSeenMs = now;
        }
        return;
    }
    (void)from; // MAC источника не нужен для heartbeat
}

static bool initEspNow() {
    if (!g_espnow.begin()) return false;
    g_espnow.setRxCallback(onEspNowPacket);
    // C3.1: пиры сателлитов для unicast-отправки аудио (по MAC из конфига).
    if (g_cfg.leftSatMac != MacAddr{}) g_espnow.addPeer(g_cfg.leftSatMac);
    if (g_cfg.rightSatMac != MacAddr{}) g_espnow.addPeer(g_cfg.rightSatMac);
    return true;
}

// ---------------------------------------------------------------------------
// TX аудио на сателлиты (C3.1/C3.2, §10): батч 117 семплов → пакет 234 байта.
// ESP-NOW — unicast по MAC сателлита; UDP — unicast на запомненный IP канала
// (fallback broadcast, пока discovery не завершён).
// ---------------------------------------------------------------------------
static void sendAudioToSatellite(uint8_t channel, const int16_t* samples, size_t n) {
    if (n == 0) return;
    uint8_t buf[kMaxPacketSize];
    size_t len = buildPacket(buf, sizeof(buf), channel, kSampleFormatInt16,
                             samples, (uint16_t)(n * sizeof(int16_t)),
                             (uint32_t)millis(), g_txPacketId++);
    if (g_cfg.transport == TransportMode::EspNow) {
        MacAddr mac = (channel == kChannelLeft) ? g_cfg.leftSatMac : g_cfg.rightSatMac;
        if (mac != MacAddr{}) g_espnow.sendTo(mac, buf, len);
    } else {
        g_udpTx.sendToChannel(channel, UdpTransport::kDefaultPort, buf, len);
    }
}

static void flushTxBatch() {
    if (g_txCount == 0) return;
    sendAudioToSatellite(kChannelLeft, g_txLeft, g_txCount);
    sendAudioToSatellite(kChannelRight, g_txRight, g_txCount);
    g_txPackets += 2;
    g_txCount = 0;
}

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

// S-1 (REPO_AUDIT V3): предупреждение, пока AP мастера использует заводской
// пароль из дефолтов — сменить через Web UI (Wi-Fi → AP) или config.env.
static void logDefaultPasswordWarning() {
    if (strcmp(g_cfg.wifiApPassword, AUDIO_WIFI_AP_PASSWORD) == 0) {
        Logger::warn("wifi",
                     "AP password is DEFAULT ('audio21master') — change it via Web UI / config.env!");
    }
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
    logDefaultPasswordWarning();
    return true;
}

// C5.1: применить статический IP из сохранённого профиля (ТЗ_Веб §6.3, §21.2).
// Вызывать ПЕРЕД WiFi.begin() — иначе адрес из DHCP перезапишет конфигурацию.
static void applyStaticIpFromProfile() {
    WifiProfile prof;
    if (!WifiStore::loadProfile(g_cfg.wifiSsid, prof)) return;
    if (!prof.staticIp || prof.ip[0] == '\0') return;
    IPAddress ip, gw, mask, dns;
    if (ip.fromString(prof.ip) && gw.fromString(prof.gateway) &&
        mask.fromString(prof.netmask) && dns.fromString(prof.dns)) {
        WiFi.config(ip, gw, mask, dns);
        Logger::infof("wifi", "static IP from profile: %s", prof.ip);
    } else {
        Logger::warn("wifi", "invalid static IP in profile — using DHCP");
    }
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
        logDefaultPasswordWarning();
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

        applyStaticIpFromProfile(); // C5.1: статический IP из профиля
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
    applyStaticIpFromProfile(); // C5.1: статический IP из профиля
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

// ---------------------------------------------------------------------------
// Serial-консоль S3-мастера (T17).
// ---------------------------------------------------------------------------

class MasterS3Console : public Console {
public:
    using Console::Console;

protected:
    void cmdStatus() override {
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
        Serial.printf("tx_packets: %lu\n", (unsigned long)g_txPackets);
        Serial.printf("i2s: %s\n", g_i2sOn ? "on" : "off");
        Serial.printf("dsp: volume=%d mute=%s crossover=%d Hz\n",
                      g_cfg.masterVolume, g_cfg.mute ? "on" : "off", g_cfg.crossoverHz);
        Serial.printf("delays: L=%d R=%d Sub=%d ms\n",
                      g_cfg.delayLeftMs, g_cfg.delayRightMs, g_cfg.delaySubMs);
        Serial.printf("sats: L=%s R=%s heartbeats=%lu\n",
                      g_leftOnline ? "online" : "offline",
                      g_rightOnline ? "online" : "offline",
                      (unsigned long)g_heartbeatsRx);
        Serial.printf("psram: %u MB\n", ESP.getPsramSize() / (1024 * 1024));
    }

    bool handleCommand(const String& cmd) override {
        if (cmd.startsWith("wifi ")) {
            String rest = cmd.substring(5);
            rest.trim();
            int sp = rest.indexOf(' ');
            if (sp <= 0) { Serial.println("usage: wifi <ssid> <password>"); return true; }
            String ssid = rest.substring(0, sp);
            String pass = rest.substring(sp + 1);
            ssid.trim();
            pass.trim();
            if (ssid.length() == 0 || ssid.length() >= sizeof(g_cfg.wifiSsid)) { Serial.println("err: bad ssid"); return true; }
            if (pass.length() >= sizeof(g_cfg.wifiPassword)) { Serial.println("err: bad password"); return true; }
            strlcpy(g_cfg.wifiSsid, ssid.c_str(), sizeof(g_cfg.wifiSsid));
            strlcpy(g_cfg.wifiPassword, pass.c_str(), sizeof(g_cfg.wifiPassword));
            ConfigStorage::save(g_cfg);
            Serial.println("saved, rebooting...");
            delay(200);
            ESP.restart();
            return true;
        }
        if (cmd.startsWith("setsat ")) {
            String rest = cmd.substring(7);
            rest.trim();
            int sp = rest.indexOf(' ');
            if (sp <= 0) { Serial.println("usage: setsat <left|right> <MAC>"); return true; }
            String side = rest.substring(0, sp);
            String macStr = rest.substring(sp + 1);
            macStr.trim();
            MacAddr mac;
            if (!MacAddr::parse(macStr.c_str(), mac)) { Serial.println("err: bad mac"); return true; }
            if (side == "left") { g_cfg.leftSatMac = mac; Serial.println("left sat mac set"); }
            else if (side == "right") { g_cfg.rightSatMac = mac; Serial.println("right sat mac set"); }
            else Serial.println("err: side must be left or right");
            return true;
        }
        if (cmd == "net") {
            Serial.printf("sta_ip: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("ap_ip: %s\n", WiFi.softAPIP().toString().c_str());
            Serial.printf("ap_stations: %u\n", WiFi.softAPgetStationNum());
            Serial.printf("gateway: %s\n", WiFi.gatewayIP().toString().c_str());
            Serial.printf("netmask: %s\n", WiFi.subnetMask().toString().c_str());
            Serial.printf("dns: %s\n", WiFi.dnsIP().toString().c_str());
            Serial.printf("wifi_channel: %u\n", (unsigned)WiFi.channel());
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
            return true;
        }
        if (cmd == "erase") {
            ConfigStorage::erase();
            Serial.println("config erased, rebooting...");
            delay(200);
            ESP.restart();
            return true;
        }
        if (cmd.startsWith("tone")) {
            String rest = cmd.substring(4);
            rest.trim();
            if (rest.length() == 0 || rest == "off") {
                g_toneUntilMs = 0;
                Serial.println("tone: off");
                return true;
            }
            long freq = rest.toInt();
            if (freq <= 0 || !g_i2sOn) {
                Serial.println("usage: tone <freq> | tone off  (i2s must be on)");
                return true;
            }
            g_toneFreq = (uint32_t)freq;
            g_tonePhase = 0;
            g_toneUntilMs = millis() + kToneDurationMs;
            Serial.printf("tone: %lu Hz, %u s\n", g_toneFreq, kToneDurationMs / 1000);
            return true;
        }
        return false;
    }
};
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

// Создать DelayLine с буфером в PSRAM (C2.3). Возвращает nullptr при нехватке
// памяти — в этом случае задержка канала не применяется (web-хендлеры
// /api/delay работают только с конфигом).
static DelayLine* createDelayLinePsram(uint32_t capacityMs, uint32_t sampleRate) {
    uint32_t samples = (capacityMs * sampleRate) / 1000;
    if (samples < 1) samples = 1;
    int16_t* buf = nullptr;
    if (ESP.getPsramSize() > 0) {
        buf = static_cast<int16_t*>(ps_malloc(samples * sizeof(int16_t)));
    }
    if (!buf) {
        buf = static_cast<int16_t*>(malloc(samples * sizeof(int16_t)));
    }
    if (!buf) {
        Logger::error("audio", "DelayLine alloc failed (PSRAM=%u)",
                      (unsigned)(ESP.getPsramSize() / (1024 * 1024)));
        return nullptr;
    }
    return new DelayLine(capacityMs, sampleRate, buf);
}

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

    // ESP-NOW: heartbeat от сателлитов → статус online без аудио-потока.
    if (initEspNow()) {
        Logger::info("master", "ESP-NOW ready (satellite heartbeat RX)");
    } else {
        Logger::error("master", "ESP-NOW init failed");
    }

    // mDNS (F16): доступ по http://<hostname>.local (дефолт audio-master.local).
    // В режиме настройки mDNS на AP не работает — Web UI доступен по IP.
    if (!g_setupMode && MDNS.begin(g_cfg.hostname)) {
        Logger::infof("master", "mDNS: http://%s.local", g_cfg.hostname);
        MDNS.addService("http", "tcp", 80);
    }

    // NTP (ТЗ_Веб §7.5): синхронизация времени из STA-сети.
    if (!g_setupMode && g_cfg.ntpEnabled && WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, g_cfg.ntpServer);
        setenv("TZ", g_cfg.timezone, 1);
        tzset();
        Logger::infof("master", "NTP: %s (tz %s)", g_cfg.ntpServer, g_cfg.timezone);
    }

    // Проверка интернета (ТЗ_Веб §7): активна при STA-подключении.
    g_internet.configure(g_cfg.netCheckUrl, g_cfg.netCheckIntervalSec,
                         g_cfg.netCheckTimeoutMs, g_cfg.netCheckEnabled && !g_setupMode);
    g_internet.start(httpInternetCheck, millis());
    g_webServer.setInternetChecker(&g_internet);
    g_webServer.setLogs(&g_logs);
    // Задача интернет-чека: блокирующий HTTP выполняется вне loop.
    xTaskCreate(internetCheckTask, "netcheck", 4096, nullptr, 1, nullptr);
    g_logs.addf(LogCat::Boot, 1, "master booted, mode %s", wifiModeToString(g_cfg.wifiMode));

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

    // C3.1: UDP-транспорт TX на сателлиты (порт 4210, discovery/аудио).
    if (g_cfg.transport == TransportMode::Udp) {
        if (g_udpTx.begin()) {
            Logger::infof("master", "UDP TX to satellites on port %u", UdpTransport::kDefaultPort);
        } else {
            Logger::error("master", "UDP TX begin failed");
        }
    }

    // I2S-выход (C1.4): PCM5102A на пинах BCK/WS/DATA, сабвуфер — моно (L=R).
    I2sOutputPins i2sPins = {(int)g_cfg.i2sBck, (int)g_cfg.i2sWs, (int)g_cfg.i2sDataOut};
    if (g_i2sOut.init(i2sPins, g_cfg.sampleRate, /*mono=*/true)) {
        g_i2sOn = true;
        Logger::infof("audio", "I2S out: %u Hz, mono (L=R), pins %u/%u/%u",
                      (unsigned)g_cfg.sampleRate, g_cfg.i2sBck, g_cfg.i2sWs, g_cfg.i2sDataOut);
    } else {
        Logger::error("audio", "I2S init failed");
    }

    // Jitter-буфер (C1.3, §7.6): 60 мс ёмкость, целевая задержка 30 мс.
    // PSRAM на этой плате не обнаружен — fallback на обычный heap.
    g_jitter = new JitterBuffer(kMasterJitterCapacity);
    if (g_jitter) {
        g_jitter->setTargetMs(30, g_cfg.sampleRate);
        Logger::infof("audio", "Jitter buffer: cap=%u samples (%u ms), target=30 ms",
                      (unsigned)kMasterJitterCapacity, (unsigned)(kMasterJitterCapacity * 1000 / g_cfg.sampleRate));
    } else {
        Logger::error("audio", "Jitter buffer alloc failed");
    }

    // Приёмник UDP-аудио (C1.2): 5 мс/пакет при 48 кГц стерео 16 бит (§9.3).
    g_audioRecv.configure(g_cfg.sampleRate, 5.0f);

    // DSP-конвейер (C2.1): volume → tone → limiter → LR4 crossover.
    // Настройки из конфига; далее меняются через Web UI /api/volume,
    // /api/mute, /api/crossover (применяются к живым объектам).
    g_pipeline.configure(g_cfg.sampleRate);
    g_pipeline.setVolume(g_cfg.masterVolume);
    g_pipeline.setMute(g_cfg.mute);
    g_pipeline.setCrossoverHz(g_cfg.crossoverHz);
    g_pipeline.setChannelVolumes(g_cfg.leftVolume, g_cfg.rightVolume, g_cfg.subVolume);
    Logger::infof("audio", "DSP: volume=%d mute=%s crossover=%d Hz, chan L=%d R=%d Sub=%d",
                  g_cfg.masterVolume, g_cfg.mute ? "on" : "off", g_cfg.crossoverHz,
                  g_cfg.leftVolume, g_cfg.rightVolume, g_cfg.subVolume);

    // Линии задержки L/R/Sub в PSRAM (C2.3): ёмкость kMaxDelayMs (200 мс),
    // текущие задержки — из конфига (ТЗ §6.9). Настраиваются через /api/delay.
    g_delayLeft = createDelayLinePsram(kMaxDelayMs, g_cfg.sampleRate);
    g_delayRight = createDelayLinePsram(kMaxDelayMs, g_cfg.sampleRate);
    g_delaySub = createDelayLinePsram(kMaxDelayMs, g_cfg.sampleRate);
    if (g_delayLeft) g_delayLeft->setDelayMs(static_cast<uint32_t>(g_cfg.delayLeftMs));
    if (g_delayRight) g_delayRight->setDelayMs(static_cast<uint32_t>(g_cfg.delayRightMs));
    if (g_delaySub) g_delaySub->setDelayMs(static_cast<uint32_t>(g_cfg.delaySubMs));
    Logger::infof("audio", "DelayLines (PSRAM): L=%d R=%d Sub=%d ms",
                  g_delayLeft ? g_cfg.delayLeftMs : -1,
                  g_delayRight ? g_cfg.delayRightMs : -1,
                  g_delaySub ? g_cfg.delaySubMs : -1);

    Logger::info("master", "Ready. Type 'status' for info.");

    // C6.1: watchdog задачи loop (таймаут 30 с — с запасом на блокирующие
    // Wi-Fi-операции в setup; сброс — в начале loop()). API IDF 5.x (core 3.x):
    // esp_task_wdt_init принимает esp_task_wdt_config_t.
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdtCfg);
    esp_task_wdt_add(nullptr);
}

// Генерация тестового тона (C1.4): вызывается из loop(), не блокирует Wi-Fi/Web UI.
static void toneTick() {
    if (g_toneUntilMs == 0) return;
    if (millis() >= g_toneUntilMs) {
        g_toneUntilMs = 0;
        Logger::info("audio", "tone stopped");
        return;
    }
    if (!g_i2sOn) {
        g_toneUntilMs = 0;
        return;
    }
    const size_t kChunk = 128;
    int16_t buf[kChunk];
    const uint32_t phaseStep =
        (uint32_t)((g_toneFreq * 65536.0f) / (float)g_cfg.sampleRate);
    for (size_t i = 0; i < kChunk; i++) {
        g_tonePhase += phaseStep;
        float ph = (float)(g_tonePhase >> 16) * (2.0f * PI) / 65536.0f;
        buf[i] = (int16_t)(kToneAmp * 32767.0f * sinf(ph));
    }
    g_i2sOut.write(buf, kChunk);
}

// Драйвер аудио-выхода (C1.3/C1.5): вычитывает моно-семплы из jitter-буфера
// в I2S. Задержка конфигурируется через setTargetMs (30 мс). Пока буфер не
// накоплен до целевого уровня — выдаём тишину (плавный старт без щелчков).
static void audioOutTick() {
    if (!g_i2sOn || !g_jitter) return;
    constexpr size_t kChunk = 128;
    int16_t buf[kChunk];
    for (size_t i = 0; i < kChunk; i++) {
        int16_t s;
        buf[i] = g_jitter->pop(s) ? s : 0;
    }
    g_i2sOut.write(buf, kChunk);
}

void loop() {
    // C6.1: сброс watchdog задачи loop.
    esp_task_wdt_reset();
    // C5.5: замер занятости loop() (без учёта delay).
    uint32_t t0 = micros();
    // Приём UDP-аудио со смартфона (C2.1, §9): разбор пакета → UdpAudioReceiver
    // (sequence/concealment) → DSP (volume → tone → limiter → LR4 crossover) →
    // sub → DelayLine → JitterBuffer (PSRAM) → I2S. left/right (HPF) — Этап 3.
    int packetSize = g_udp.parsePacket();
    if (packetSize > 0) {
        int n = g_udp.read(g_udpBuf, sizeof(g_udpBuf));
        g_packetsRx++;
        g_packetBytesRx += static_cast<uint32_t>(n);

        UdpAudioHeader hdr;
        const uint8_t* payload;
        size_t payloadSize;
        if (n > 0 && parseUdpPacket(g_udpBuf, static_cast<size_t>(n), hdr, payload, payloadSize)) {
            size_t nSamples = payloadSize / sizeof(int16_t);
            const int16_t* pcm = reinterpret_cast<const int16_t*>(payload);
            size_t nMono = nSamples / 2;

            // Сначала feed(): обновляет состояние потока, чтобы concealGain()
            // ниже вернул актуальный множитель маскирования потерь.
            StreamState st = g_audioRecv.feed(hdr.sequence, hdr.timestampSamples,
                                              pcm, nSamples, millis());
            if (st == StreamState::Active || st == StreamState::Conceal) {
            // DSP: стерео → sub (моно-микс → LPF) + left/right (HPF) для TX.
            // При потерях — плавное затухание (concealGain), затем задержка сабвуфера.
                float gain = g_audioRecv.concealGain();
                static int16_t s_mono[sizeof(g_udpBuf) / sizeof(int16_t)];
                for (size_t i = 0, o = 0; i + 1 < nSamples; i += 2, o++) {
                    PipelineOutput out = g_pipeline.process(pcm[i], pcm[i + 1]);
                    float sub = out.sub * gain;
                    if (sub > 1.0f) sub = 1.0f;
                    else if (sub < -1.0f) sub = -1.0f;
                    int16_t s = static_cast<int16_t>(sub * 32767.0f);
                    if (g_delaySub) s = g_delaySub->process(s);
                    s_mono[o] = s;

                    // C3.1: left/right каналы (HPF) → батч → пакеты на сателлиты.
                    float lf = out.left * gain;
                    float rf = out.right * gain;
                    if (lf > 1.0f) lf = 1.0f; else if (lf < -1.0f) lf = -1.0f;
                    if (rf > 1.0f) rf = 1.0f; else if (rf < -1.0f) rf = -1.0f;
                    g_txLeft[g_txCount] = static_cast<int16_t>(lf * 32767.0f);
                    g_txRight[g_txCount] = static_cast<int16_t>(rf * 32767.0f);
                    g_txCount++;
                    if (g_txCount >= kBatchSamples) flushTxBatch();
                }
                if (g_jitter) g_jitter->push(s_mono, nMono);
            }
            g_audioActive = (st != StreamState::Standby);
        }
    }

    // Драйвер аудио-выхода: вычитываем из jitter-буфера в I2S.
    audioOutTick();

    // C3.1: UDP-режим — discovery-ответы сателлитов (запоминаем их IP для
    // unicast-отправки аудио; fallback — broadcast).
    if (g_cfg.transport == TransportMode::Udp) {
        uint8_t discBuf[kMaxPacketSize];
        size_t dn = g_udpTx.receive(discBuf, sizeof(discBuf));
        if (dn > 0) g_udpTx.handleDiscovery(discBuf, dn, g_udpTx.lastFrom());
    }

    // Тестовый тон (C1.4) — не блокирует loop.
    toneTick();

    // Статус сателлитов: online, пока приходит heartbeat (discovery-response).
    uint32_t now = millis();
    if (g_leftOnline && (now - g_leftLastSeenMs > kSatelliteTimeoutMs)) g_leftOnline = false;
    if (g_rightOnline && (now - g_rightLastSeenMs > kSatelliteTimeoutMs)) g_rightOnline = false;

    // Discovery-запрос сателлитам: пока кто-то offline — каждые 2 с, чтобы
    // они перешли на unicast-heartbeat. Когда оба online, эфир не мусорим
    // (heartbeat приходит и так); новый/перезагруженный сателлит сам пошлёт
    // broadcast-heartbeat, по которому мастер восстановит статус.
    if (now - g_lastDiscoveryMs >= kDiscoveryIntervalMs && (!g_leftOnline || !g_rightOnline)) {
        g_lastDiscoveryMs = now;
        sendDiscoveryRequest();
    }

    g_console.update();

    // Web UI: обработка запросов и сохранение конфига по кнопке.
    g_webServer.handleClient();
    if (g_webServer.saveRequested()) {
        ConfigStorage::save(g_cfg);
        g_webServer.clearSaveRequested();
        Logger::info("master", "config saved via Web UI");
        g_logs.addf(LogCat::Config, 1, "config saved via Web UI");
    }

    // C6.2: авто-переподключение STA при обрыве в рантайме (ТЗ §16.3,
    // ТЗ_Веб §21). Fallback на setup AP — в существующей логике ниже.
    if (g_cfg.wifiMode != WifiMode::ApDirect && !g_setupMode) {
        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            g_wifiWasConnected = true;
        } else if (g_wifiWasConnected && !g_reconnectPending) {
            Logger::warn("wifi", "STA lost — auto reconnect");
            g_logs.addf(LogCat::Wifi, 2, "STA lost — auto reconnect");
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false, true);
            delay(200);
            applyStaticIpFromProfile(); // C5.1
            WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
            g_reconnectPending = true;
            g_reconnectAtMs = millis();
        }
    }

    // Интернет-чек (ТЗ_Веб §7.4): выполняется в отдельной задаче, чтобы
    // блокирующий HTTP (DNS + connect + read) не замораживал loop и Web UI.
    static NetStatus lastLoggedNet = NetStatus::Disabled;
    if (g_internet.status() != lastLoggedNet) {
        lastLoggedNet = g_internet.status();
        g_logs.addf(LogCat::Internet, 1, "internet check: %s (%lu ms)",
                    g_internet.statusName(), (unsigned long)g_internet.latencyMs());
    }

    // Переподключение Wi-Fi по запросу Web UI (POST /api/wifi/connect).
    if (g_webServer.reconnectRequested()) {
        g_webServer.clearReconnectRequested();
        Logger::infof("wifi", "reconnect requested via Web UI (ssid %s)", g_cfg.wifiSsid);
        g_logs.addf(LogCat::Wifi, 1, "reconnect requested: %s", g_cfg.wifiSsid);
        bool keepAp = g_cfg.wifiMode == WifiMode::ApSta || g_cfg.wifiMode == WifiMode::ApDirect;
        if (keepAp && WiFi.getMode() == WIFI_AP) {
            // AP уже поднят — перезапускаем только STA-часть, чтобы не терять
            // канал ESP-NOW (связь с сателлитами живёт без аудио-пакетов).
            WiFi.mode(WIFI_AP_STA);
        } else if (!keepAp) {
            WiFi.mode(WIFI_STA);
        }
        WiFi.disconnect(false, true);
        delay(200);
        WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPassword);
        g_reconnectPending = true;
        g_reconnectAtMs = millis();
    }

    // Fallback: reconnect STA не удался за лимит — поднять setup AP, чтобы
    // Web UI оставался доступным (и связь с сателлитами не потерялась).
    if (g_reconnectPending) {
        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            g_reconnectPending = false;
            Logger::infof("wifi", "reconnect OK, IP: %s", WiFi.localIP().toString().c_str());
        } else if (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED ||
                   st == WL_CONNECTION_LOST || (millis() - g_reconnectAtMs > kReconnectTimeoutMs)) {
            g_reconnectPending = false;
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false, true);
            if (g_cfg.wifiMode != WifiMode::ApDirect) {
                Logger::warn("wifi", "reconnect failed — setup AP");
                g_setupMode = true;
                startSetupAp();
            } else {
                Logger::warn("wifi", "reconnect failed — AP remains, setup mode");
                g_setupMode = true;
            }
        }
    }

    // Captive portal: обработать DNS-запросы телефона (no-op, если не запущен).
    g_dns.processNextRequest();

    // C5.5: занятость = время до delay; усреднение за 2 с → Web UI.
    g_cpuBusyUs += (micros() - t0);
    delay(10);
    g_cpuTotalUs += (micros() - t0);
    uint32_t cpuNow = millis();
    if (cpuNow - g_cpuLastReportMs >= 2000) {
        g_cpuLastReportMs = cpuNow;
        uint32_t total = g_cpuTotalUs;
        g_cpuLoadPercent = total ? (uint32_t)((uint64_t)g_cpuBusyUs * 100 / total) : 0;
        g_cpuBusyUs = 0;
        g_cpuTotalUs = 0;
        g_webServer.setCpuLoadPercent(g_cpuLoadPercent);
    }
}
