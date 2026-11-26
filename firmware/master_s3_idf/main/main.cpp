#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <memory>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/uart.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"

// DSP модули
#include "pcm_pipeline.h"
#include "../../../common/web/auth.h"
#include "delay_line.h"
#include "jitter_buffer.h"
#include "volume_control.h"
#include "crossover.h"

// Новые компоненты Этапа 3
#include "driver/i2s_std.h"
#include "espnow_transport.h"
#include "i2s_output.h"
#include "udp_audio.h"
#include "web_server.h"

// Этап 4: NVS конфигурация
#include "console_handler.h"
#include "nvs_manager.h"
#include "audio_pipeline.h"
#include "wifi_manager.h"
#include "nvs_config.h"

// Этап 4: Captive DNS — все DNS-запросы отвечают softAP IP (192.168.4.1)
#include "dns_captive.h"

// Этап 5: mDNS через сырой UDP (порт 5353) — доступ по http://<hostname>.local
#include "mdns_raw.h"

// Этап 5: OTA обновление
#include "ota_update.h"

using namespace audio21;

// Внешние функции CSRF токена из web_server.cpp
namespace audio21 {
    extern const char* web_server_get_csrf_token(void);
    extern bool web_server_validate_csrf_token(const char* token);
}

static const char *TAG = "master";

// Конфигурация аудио
static constexpr uint32_t kSampleRate = 48000;
static constexpr uint32_t kJitterTargetMs = 30;
static constexpr uint32_t kDelaySubMs = 40;
static constexpr uint16_t kUdpAudioPort = 5004;

// PSRAM аллокатор для больших аудиобуферов (jitter buffer, delay lines).
// Возвращает nullptr если PSRAM недоступен или аллокация не удалась.
static int16_t* psramAllocSamples(size_t samples) {
    size_t size = samples * sizeof(int16_t);
    int16_t* buf = (int16_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
        ESP_LOGI(TAG, "PSRAM: allocated %zu bytes for %u samples", size, samples);
    } else {
        ESP_LOGW(TAG, "PSRAM: allocation failed for %u samples, falling back to DRAM", samples);
        buf = (int16_t*)malloc(size);
        if (buf) {
            ESP_LOGI(TAG, "DRAM: allocated %zu bytes for %u samples", size, samples);
        } else {
            ESP_LOGE(TAG, "DRAM: allocation failed for %u samples", samples);
        }
    }
    return buf;
}

// Глобальные объекты аудио конвейера (создаются в app_main() после инициализации кучи)
std::unique_ptr<DelayLine> g_delaySub;
std::unique_ptr<DelayLine> g_delayLeft;
std::unique_ptr<DelayLine> g_delayRight;
std::unique_ptr<JitterBuffer> g_jitter;
PcmPipeline g_pipeline; // без аллокации в куче — можно оставить глобальным

// TX аудио на сателлиты (C3.1/C3.2): батч 117 семплов → пакет 234 байта.
// TX аудио на сателлиты (C3.1/C3.2): батч 117 семплов → пакет 234 байта.
static constexpr size_t kBatchSamples = 117;
int16_t g_txLeft[kBatchSamples];
int16_t g_txRight[kBatchSamples];
std::atomic<size_t> g_txCount{0};
uint32_t g_txPacketId = 0;
uint32_t g_txPackets = 0;

// Глобальные компоненты транспорта (создаются в app_main())
std::unique_ptr<EspNowTransport> g_espnow;
std::unique_ptr<I2sOutput> g_i2sOut;
std::unique_ptr<UdpAudio> g_udpAudio;
std::unique_ptr<WebServer> g_webServer;

// Этап 4: NVS конфигурация
std::unique_ptr<NvsConfig> g_nvsConfig;
NodeConfig g_cfg;

// Состояние
bool g_i2sOn = false;
int g_volumePercent = 100;
int g_crossoverHz = 90;
char g_staIpAddr[16] = "";
bool g_wifi_inited;
std::atomic<bool> g_wifi_reconfiguring{false};
bool g_nvsDirty = false;
bool g_nvsCritical = false; // немедленное сохранение для критических изменений
uint32_t g_nvsLastChangeMs = 0;
static constexpr uint32_t kNvsSaveDebounceMs = 500;
uint32_t g_sta_connect_start_ms = 0;
bool g_sta_connecting = false;
static constexpr uint32_t kStaConnectTimeoutMs = 30000;

// Статус сателлитов (heartbeat через ESP-NOW)
bool g_leftOnline = false;
bool g_rightOnline = false;
uint32_t g_lastLeftSeenMs = 0;
uint32_t g_lastRightSeenMs = 0;
std::atomic<uint32_t> g_heartbeatsRx{0};
std::atomic<uint32_t> g_heartbeatsTx{0};
// Анти-replay на мастере: последний принятый sequence от каждого сателлита
static uint16_t g_lastLeftSeq = 0;
static uint16_t g_lastRightSeq = 0;
static bool g_hasLeftSeq = false;
static bool g_hasRightSeq = false;
uint32_t g_lastDiscoveryMs = 0;
static constexpr uint32_t kSatelliteTimeoutMs = 6000;
static constexpr uint32_t kDiscoveryIntervalMs = 3000;

// Реализация WebServerCallbacks



char g_adminSalt[33] = {0}; // per-device соль для PBKDF2

// Генерация per-device соли из MAC-адреса (детерминированная, не требует NVS)
static void initAdminSalt(void) {
    if (g_adminSalt[0] != '\0') return; // уже инициализирована
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    // Если MAC нулевой (Wi-Fi не инициализирован), используем статическую соль
    bool allZero = true;
    for (int i = 0; i < 6; i++) if (mac[i] != 0) { allZero = false; break; }
    if (allZero) {
        strcpy(g_adminSalt, audio21::Auth::kSalt);
        return;
    }
    // Hex-кодируем MAC как соль
    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        g_adminSalt[i * 2] = hex[mac[i] >> 4];
        g_adminSalt[i * 2 + 1] = hex[mac[i] & 0x0f];
    }
    g_adminSalt[12] = '\0';
}


int netInternetStatusCallback(char* buf, size_t bufSize)
{
    // Проверяем интернет-соединение через STA интерфейс
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool connected = false;
    bool hasIp = false;
    if (sta) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            connected = true;
            hasIp = true;
        }
    }
    int len = snprintf(buf, bufSize,
        "{\"connected\":%s,\"hasIp\":%s,\"interface\":\"%s\"}",
        connected ? "true" : "false",
        hasIp ? "true" : "false",
        connected ? "sta" : "none");
    return len;
}

static esp_err_t netInternetCheckCallback(void)
{
    // Простая проверка: есть ли STA соединение с IP-адресом
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) return ESP_FAIL;
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

int configExportCallback(char* buf, size_t bufSize)
{
    // Экспортируем конфигурацию как JSON
    int len = snprintf(buf, bufSize,
        "{\"version\":%" PRIu32 ",\"masterVolume\":%d,\"crossoverHz\":%d,\"mute\":%s,"
        "\"leftVolume\":%d,\"rightVolume\":%d,\"subVolume\":%d,"
        "\"delayLeftMs\":%d,\"delayRightMs\":%d,\"delaySubMs\":%d,"
        "\"wifiMode\":%d,\"wifiApSsid\":\"%s\",\"wifiApPassword\":\"%s\","
        "\"wifiSsid\":\"%s\",\"wifiPassword\":\"%s\","
        "\"hostname\":\"%s\",\"udpAuthKey\":\"%s\","
        "\"leftSatMac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
        "\"rightSatMac\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}",
        g_cfg.version,
        g_cfg.masterVolume, g_cfg.crossoverHz, g_cfg.mute ? "true" : "false",
        g_cfg.leftVolume, g_cfg.rightVolume, g_cfg.subVolume,
        g_cfg.delayLeftMs, g_cfg.delayRightMs, g_cfg.delaySubMs,
        (int)g_cfg.wifiMode,
        g_cfg.wifiApSsid, g_cfg.wifiApPassword,
        g_cfg.wifiSsid, g_cfg.wifiPassword,
        g_cfg.hostname, g_cfg.udpAuthKey,
        g_cfg.leftSatMac.bytes[0], g_cfg.leftSatMac.bytes[1], g_cfg.leftSatMac.bytes[2],
        g_cfg.leftSatMac.bytes[3], g_cfg.leftSatMac.bytes[4], g_cfg.leftSatMac.bytes[5],
        g_cfg.rightSatMac.bytes[0], g_cfg.rightSatMac.bytes[1], g_cfg.rightSatMac.bytes[2],
        g_cfg.rightSatMac.bytes[3], g_cfg.rightSatMac.bytes[4], g_cfg.rightSatMac.bytes[5]);
    return len;
}

static esp_err_t configImportCallback(const char* json)
{
    // Простой импорт: пароль JSON и применим поля
    cJSON* j = cJSON_Parse(json);
    if (!j) return ESP_ERR_INVALID_ARG;
    cJSON* vol = cJSON_GetObjectItemCaseSensitive(j, "masterVolume");
    if (vol && cJSON_IsNumber(vol)) {
        g_cfg.masterVolume = vol->valueint;
        g_pipeline.setVolume(g_cfg.masterVolume);
    }
    cJSON* xover = cJSON_GetObjectItemCaseSensitive(j, "crossoverHz");
    if (xover && cJSON_IsNumber(xover)) {
        g_cfg.crossoverHz = xover->valueint;
        g_pipeline.setCrossoverHz(g_cfg.crossoverHz);
    }
    cJSON* mute = cJSON_GetObjectItemCaseSensitive(j, "mute");
    if (mute && cJSON_IsBool(mute)) {
        g_cfg.mute = cJSON_IsTrue(mute);
        g_pipeline.setMute(g_cfg.mute);
    }
    cJSON* lv = cJSON_GetObjectItemCaseSensitive(j, "leftVolume");
    if (lv && cJSON_IsNumber(lv)) g_cfg.leftVolume = lv->valueint;
    cJSON* rv = cJSON_GetObjectItemCaseSensitive(j, "rightVolume");
    if (rv && cJSON_IsNumber(rv)) g_cfg.rightVolume = rv->valueint;
    cJSON* sv = cJSON_GetObjectItemCaseSensitive(j, "subVolume");
    if (sv && cJSON_IsNumber(sv)) g_cfg.subVolume = sv->valueint;
    cJSON_Delete(j);
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Config imported and saved to NVS");
    return ESP_OK;
}

int logsGetCallback(char* buf, size_t bufSize)
{
    // Простой лог-буфер: возвращаем системную информацию
    int len = snprintf(buf, bufSize,
        "{\"logs\":["
        "{\"t\":\"system\",\"m\":\"Wireless Audio 2.1 Master started\"},"
        "{\"t\":\"audio\",\"m\":\"DSP pipeline initialized\"},"
        "{\"t\":\"wifi\",\"m\":\"Wi-Fi %s mode active\"}"
        "]}",
        g_wifi_inited ? (g_staIpAddr[0] ? "STA" : "AP") : "off");
    return len;
}

int diagnosticsGetCallback(char* buf, size_t bufSize)
{
    // Диагностика системы
    uint32_t freeHeap = esp_get_free_heap_size();
    uint32_t minHeap = esp_get_minimum_free_heap_size();
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool staConnected = false;
    if (sta) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            staConnected = true;
        }
    }
    int len = snprintf(buf, bufSize,
        "{\"freeHeap\":%" PRIu32 ",\"minHeap\":%" PRIu32 ",\"uptimeMs\":%" PRIu32 ","
        "\"wifiConnected\":%s,\"satLeft\":\"%s\",\"satRight\":\"%s\","
        "\"jitterAvailable\":%" PRIu32 ",\"heartbeatsRx\":%" PRIu32 "}",
        freeHeap, minHeap, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
        staConnected ? "true" : "false",
        g_leftOnline ? "online" : "offline",
        g_rightOnline ? "online" : "offline",
        g_jitter ? g_jitter->available() : 0,
        g_heartbeatsRx.load());
    return len;
}

// Агент: простая проверка живости (без CSRF)
int agentHealthCallback(char* buf, size_t bufSize)
{
    uint32_t freeHeap = esp_get_free_heap_size();
    uint32_t minHeap = esp_get_minimum_free_heap_size();
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool staConnected = false;
    if (sta) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            staConnected = true;
        }
    }
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    bool apActive = ap != nullptr;
    int len = snprintf(buf, bufSize,
        "{\"ok\":true,\"uptimeMs\":%" PRIu32 ",\"freeHeap\":%" PRIu32 ","
        "\"minHeap\":%" PRIu32 ",\"wifiConnected\":%s,\"apActive\":%s,"
        "\"satLeft\":\"%s\",\"satRight\":\"%s\"}",
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
        freeHeap, minHeap,
        staConnected ? "true" : "false",
        apActive ? "true" : "false",
        g_leftOnline ? "online" : "offline",
        g_rightOnline ? "online" : "offline");
    return len;
}

// Агент: детальная диагностика (требует CSRF)
int agentDiagnosticsCallback(char* buf, size_t bufSize)
{
    uint32_t freeHeap = esp_get_free_heap_size();
    uint32_t minHeap = esp_get_minimum_free_heap_size();
    size_t freePsram = 0;
    size_t totalPsram = 0;
#if CONFIG_SPIRAM
    freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    totalPsram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
#endif
    // Детальный статус сателлитов (MAC, RSSI, время последнего heartbeat).
    int8_t leftRssi = -128, rightRssi = -128;
    uint32_t leftLastSeen = 0, rightLastSeen = 0;
    if (g_espnow) {
        const auto& satL = g_espnow->satellite(0);
        const auto& satR = g_espnow->satellite(1);
        leftRssi = satL.rssi;
        rightRssi = satR.rssi;
        leftLastSeen = g_leftOnline ? g_lastLeftSeenMs : 0;
        rightLastSeen = g_rightOnline ? g_lastRightSeenMs : 0;
    }
    int len = snprintf(buf, bufSize,
        "{\"uptimeMs\":%" PRIu32 ",\"freeHeap\":%" PRIu32 ",\"minHeap\":%" PRIu32 ","
        "\"freePsram\":%" PRIu32 ",\"totalPsram\":%" PRIu32 ","
        "\"satLeft\":\"%s\",\"satRight\":\"%s\","
        "\"leftSatMac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
        "\"rightSatMac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
        "\"leftRssi\":%d,\"rightRssi\":%d,"
        "\"leftLastSeenMs\":%" PRIu32 ",\"rightLastSeenMs\":%" PRIu32 ","
        "\"heartbeatsRx\":%" PRIu32 ",\"heartbeatsTx\":%" PRIu32 "}",
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
        freeHeap, minHeap,
        (uint32_t)freePsram, (uint32_t)totalPsram,
        g_leftOnline ? "online" : "offline",
        g_rightOnline ? "online" : "offline",
        g_cfg.leftSatMac.bytes[0], g_cfg.leftSatMac.bytes[1], g_cfg.leftSatMac.bytes[2],
        g_cfg.leftSatMac.bytes[3], g_cfg.leftSatMac.bytes[4], g_cfg.leftSatMac.bytes[5],
        g_cfg.rightSatMac.bytes[0], g_cfg.rightSatMac.bytes[1], g_cfg.rightSatMac.bytes[2],
        g_cfg.rightSatMac.bytes[3], g_cfg.rightSatMac.bytes[4], g_cfg.rightSatMac.bytes[5],
        leftRssi, rightRssi,
        leftLastSeen, rightLastSeen,
        g_heartbeatsRx.load(), g_heartbeatsTx.load());
    return len;
}

// Агент: эхо-проверка связности (без CSRF)
int agentPingCallback(char* buf, size_t bufSize)
{
    int len = snprintf(buf, bufSize, "{\"pong\":true,\"uptimeMs\":%" PRIu32 "}",
                       (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    return len;
}

// --- Авторизация (сессии + SHA-256 пароли) ---
#include "mbedtls/md.h"

static constexpr size_t kMaxSessions = 4;
static constexpr uint32_t kSessionTtlMs = 30 * 60 * 1000; // 30 минут

struct Session {
    char token[65];
    uint32_t createdMs;
    bool active;
};

Session g_sessions[kMaxSessions];
char g_adminUsername[32] = "admin";
char g_adminPasswordHash[65] = {0}; // PBKDF2 hash string or SHA-256 hex
bool g_adminSetupDone = false;

static void sha256Hex(const char* input, char* outputHex)
{
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const uint8_t*)input, strlen(input));
    uint8_t hash[32];
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    for (int i = 0; i < 32; i++) {
        sprintf(outputHex + i * 2, "%02x", hash[i]);
    }
    outputHex[64] = '\0';
}

static void cleanupExpiredSessions(uint32_t nowMs)
{
    for (size_t i = 0; i < kMaxSessions; i++) {
        if (g_sessions[i].active && nowMs - g_sessions[i].createdMs > kSessionTtlMs) {
            g_sessions[i].active = false;
        }
    }
}

int loginCallback(const char* username, const char* password, char* tokenOut, size_t tokenOutSize)
{
    if (!g_adminSetupDone) {
        // Админ не настроен — разрешаем вход с дефолтными учётными данными (предупреждение)
        if (strcmp(username, "admin") == 0 && strcmp(password, "admin") == 0) {
            // Генерируем токен с esp_random()
            char tokenBuf[65];
            audio21::Auth::newSessionToken(tokenBuf);
            cleanupExpiredSessions((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
            // Находим свободный слот
            for (size_t i = 0; i < kMaxSessions; i++) {
                if (!g_sessions[i].active) {
                    strcpy(g_sessions[i].token, tokenBuf);
                    g_sessions[i].createdMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                    g_sessions[i].active = true;
                    strncpy(tokenOut, tokenBuf, tokenOutSize - 1);
                    tokenOut[tokenOutSize - 1] = '\0';
                    return (int)strlen(tokenOut);
                }
            }
        }
        return -1;
    }
    // Проверяем пароль через Auth::checkPassword (PBKDF2 + backward compat SHA-256)
    if (!audio21::Auth::checkPassword(password, g_adminPasswordHash, g_adminSalt)) {
        return -1;
    }
    // Проверяем username
    if (strcmp(username, g_adminUsername) != 0) {
        return -1;
    }
    cleanupExpiredSessions((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    for (size_t i = 0; i < kMaxSessions; i++) {
        if (!g_sessions[i].active) {
            char tokenBuf[65];
            audio21::Auth::newSessionToken(tokenBuf);
            strcpy(g_sessions[i].token, tokenBuf);
            g_sessions[i].createdMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            g_sessions[i].active = true;
            strncpy(tokenOut, tokenBuf, tokenOutSize - 1);
            tokenOut[tokenOutSize - 1] = '\0';
            return (int)strlen(tokenOut);
        }
    }
    return -1;
}

static esp_err_t logoutCallback(const char* token)
{
    for (size_t i = 0; i < kMaxSessions; i++) {
        if (g_sessions[i].active && strcmp(g_sessions[i].token, token) == 0) {
            g_sessions[i].active = false;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t adminSetupCallback(const char* username, const char* password)
{
    strncpy(g_adminUsername, username, sizeof(g_adminUsername) - 1);
    g_adminUsername[sizeof(g_adminUsername) - 1] = '\0';
    // Хешируем пароль через PBKDF2-HMAC-SHA256 с per-device солью
    audio21::Auth::hashPassword(password, g_adminPasswordHash, g_adminSalt);
    g_adminSetupDone = true;
    ESP_LOGI(TAG, "Admin setup done: user=%s", g_adminUsername);
    return ESP_OK;
}

static esp_err_t rebootCallback(void)
{
    ESP_LOGW(TAG, "reboot requested via Web UI");
    esp_restart();
    return ESP_OK;
}

static esp_err_t factoryResetCallback(void)
{
    ESP_LOGW(TAG, "factory reset requested via Web UI");
    esp_err_t ret = g_nvsConfig->erase();
    if (ret == ESP_OK) {
        // Загружаем дефолтные значения после сброса
        g_cfg = NodeConfig();
        g_volumePercent = g_cfg.masterVolume;
        g_crossoverHz = g_cfg.crossoverHz;
        g_pipeline.setVolume(g_cfg.masterVolume);
        g_pipeline.setCrossoverHz(g_cfg.crossoverHz);
    }
    return ret;
}

static esp_err_t otaUpdateCallback(const char* url)
{
    ESP_LOGW(TAG, "OTA update requested via Web UI: %s", url);
    return ota_update_start(url);
}

static esp_err_t otaUpdateFileCallback(const uint8_t* data, size_t size, const char* signatureHex)
{
    // Проверка подписи HMAC-SHA256, если публичный ключ настроен
    if (g_cfg.otaPublicKey[0] != '\0') {
        if (signatureHex == nullptr || strlen(signatureHex) != 64) {
            ESP_LOGE(TAG, "OTA signature required but not provided or invalid length (expected 64 hex chars)");
            return ESP_ERR_INVALID_ARG;
        }

        // Парсим HMAC ключ (hex, до 32 байта = 64 hex символов)
        uint8_t hmacKey[32];
        size_t keyLen = 0;
        for (size_t i = 0; i < 32 && g_cfg.otaPublicKey[i * 2] != '\0'; i++) {
            unsigned int byte;
            if (sscanf(g_cfg.otaPublicKey + i * 2, "%02x", &byte) != 1) {
                ESP_LOGE(TAG, "Invalid otaPublicKey hex at position %zu", i);
                return ESP_ERR_INVALID_ARG;
            }
            hmacKey[i] = (uint8_t)byte;
            keyLen++;
        }

        // Парсим подпись HMAC-SHA256 (hex, 32 байта = 64 hex символов)
        uint8_t signature[32];
        for (size_t i = 0; i < 32; i++) {
            unsigned int byte;
            if (sscanf(signatureHex + i * 2, "%02x", &byte) != 1) {
                ESP_LOGE(TAG, "Invalid signature hex at position %zu", i);
                return ESP_ERR_INVALID_ARG;
            }
            signature[i] = (uint8_t)byte;
        }

        // Проверка подписи через mbedTLS HMAC-SHA256
        unsigned char computed[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
        mbedtls_md_hmac_starts(&ctx, hmacKey, keyLen);
        mbedtls_md_hmac_update(&ctx, data, size);
        mbedtls_md_hmac_finish(&ctx, computed);
        mbedtls_md_free(&ctx);

        // Постоянное время сравнения
        int diff = 0;
        for (size_t i = 0; i < 32; i++) {
            diff |= computed[i] ^ signature[i];
        }

        if (diff != 0) {
            ESP_LOGE(TAG, "OTA signature verification failed");
            return ESP_ERR_INVALID_ARG;
        }

        ESP_LOGI(TAG, "OTA signature verified successfully");
    }

    const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(nullptr);
    if (!ota_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        return ESP_ERR_NOT_FOUND;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t ret = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ota_write(ota_handle, data, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
        esp_ota_abort(ota_handle);
        return ret;
    }

    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ota_set_boot_partition(ota_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "OTA update from file succeeded, partition=%s, size=%zu",
             ota_partition->label, size);
    return ESP_OK;
}


static const char* getCsrfTokenCallback(void)
{
    // Возвращаем CSRF токен из веб-сервера
    return web_server_get_csrf_token();
}

bool validateCsrfTokenCallback(const char* token)
{
    // Проверяем токен против значения из веб-сервера
    return web_server_validate_csrf_token(token);
}

// Предварительное объявление для переконфигурации Wi-Fi


static const WebServerCallbacks g_webCallbacks = {
    .getStatus = getStatusCallback,
    .setVolume = setVolumeCallback,
    .getVolume = getVolumeCallback,
    .setCrossoverHz = setCrossoverHzCallback,
    .getCrossoverHz = getCrossoverHzCallback,
    .setMute = setMuteCallback,
    .getMute = getMuteCallback,
    .setTone = setToneCallback,
    .getTone = getToneCallback,
    .setChannelVolumes = setChannelVolumesCallback,
    .getChannelVolumes = getChannelVolumesCallback,
    .setDelay = setDelayCallback,
    .getDelay = getDelayCallback,
    .wifiScan = wifiScanCallback,
    .wifiSetMode = wifiSetModeCallback,
    .wifiSetApPassword = wifiSetApPasswordCallback,
    .setHostname = setHostnameCallback,
    .netInternetStatus = netInternetStatusCallback,
    .netInternetCheck = netInternetCheckCallback,
    .configExport = configExportCallback,
    .configImport = configImportCallback,
    .logsGet = logsGetCallback,
    .diagnosticsGet = diagnosticsGetCallback,
    .agentHealth = agentHealthCallback,
    .agentDiagnostics = agentDiagnosticsCallback,
    .agentPing = agentPingCallback,
    .reboot = rebootCallback,
    .factoryReset = factoryResetCallback,
    .otaUpdate = otaUpdateCallback,
    .otaUpdateFile = otaUpdateFileCallback,
    .getWifiConfig = getWifiConfigCallback,
    .setWifiConfig = setWifiConfigCallback,
    .getCsrfToken = getCsrfTokenCallback,
    .validateCsrfToken = validateCsrfTokenCallback,
    .login = loginCallback,
    .logout = logoutCallback,
    .adminSetup = adminSetupCallback,
};

// Предварительное объявление (вызывается из onPcm до определения flushTxBatch).
static void flushTxBatch();

// Коллбек приёма UDP аудио
class UdpAudioHandler : public UdpAudioRxCallback {
public:
    void onPcm(const int16_t* data, size_t nSamplesPerChannel) override
    {
        // Обрабатываем стерео PCM через DSP конвейер.
        // out.sub → джиттер-буфер → I2S (сабвуфер).
        // out.left / out.right → батчи для отправки на сателлиты (ESP-NOW).
        for (size_t i = 0; i < nSamplesPerChannel; i++) {
            int16_t l = data[i * 2];
            int16_t r = data[i * 2 + 1];

            PipelineOutput out = g_pipeline.process(l, r);

            // Сабвуфер — моно, в джиттер-буфер (выдача ровным потоком в audioOutTick).
            int16_t subSample = static_cast<int16_t>(out.sub * 32767.0f);
            subSample = g_delaySub->process(subSample);
            if (g_jitter) g_jitter->push(&subSample, 1);

            // TX на сателлиты: задержка каналов → накапливаем HPF-каналы L/R в батчи.
            int16_t leftSample = static_cast<int16_t>(out.left * 32767.0f);
            int16_t rightSample = static_cast<int16_t>(out.right * 32767.0f);
            g_txLeft[g_txCount.load()] = g_delayLeft->process(leftSample);
            g_txRight[g_txCount.load()] = g_delayRight->process(rightSample);
            g_txCount.fetch_add(1);
            if (g_txCount.load() >= kBatchSamples) {
                flushTxBatch();
            }
        }
    }
};

UdpAudioHandler g_udpHandler;

// Коллбек приёма ESP-NOW на мастере — обрабатывает heartbeat (discovery-response)
// от сателлитов и помечает их онлайн.
class EspNowMasterRxCallback : public EspNowRxCallback {
public:
    void onPacket(const uint8_t* srcMac, const uint8_t* data, size_t len) override {
        if (len < sizeof(AudioPacketHeader)) return;
        AudioPacketHeader hdr;
        memcpy(&hdr, data, sizeof(AudioPacketHeader));

        if (hdr.magic != kMagicDiscovery) return;

        // Проверяем MAC только если настроен (не все нули)
        bool leftMacSet = memcmp(g_cfg.leftSatMac.bytes, "\0\0\0\0\0\0", 6) != 0;
        bool rightMacSet = memcmp(g_cfg.rightSatMac.bytes, "\0\0\0\0\0\0", 6) != 0;
        bool isLeft = leftMacSet && memcmp(srcMac, g_cfg.leftSatMac.bytes, 6) == 0;
        bool isRight = rightMacSet && memcmp(srcMac, g_cfg.rightSatMac.bytes, 6) == 0;

        // Анти-replay: отклоняем дубликаты/устаревшие пакеты
        int16_t seqDiff = 0;
        bool& hasSeq = isLeft ? g_hasLeftSeq : g_hasRightSeq;
        uint16_t& lastSeq = isLeft ? g_lastLeftSeq : g_lastRightSeq;
        if (hasSeq) {
            seqDiff = (int16_t)(hdr.sequence - lastSeq);
            if (seqDiff <= 0) return; // дубликат или устаревший пакет
        }
        lastSeq = hdr.sequence;
        hasSeq = true;

        uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        g_heartbeatsRx++;

        if (isLeft) {
            if (!g_leftOnline) {
                g_leftOnline = true;
                ESP_LOGI(TAG, "satellite left online");
                char sse[128];
                snprintf(sse, sizeof(sse), "{\"left\":\"online\",\"right\":\"%s\"}",
                         g_rightOnline ? "online" : "offline");
                g_webServer->sseBroadcast("satellite", sse);
            }
            g_lastLeftSeenMs = nowMs;
        } else if (isRight) {
            if (!g_rightOnline) {
                g_rightOnline = true;
                ESP_LOGI(TAG, "satellite right online");
                char sse[128];
                snprintf(sse, sizeof(sse), "{\"left\":\"%s\",\"right\":\"online\"}",
                         g_leftOnline ? "online" : "offline");
                g_webServer->sseBroadcast("satellite", sse);
            }
            g_lastRightSeenMs = nowMs;
        }
    }
};

static EspNowMasterRxCallback g_espnowMasterRxCb;

// Отправить накопленный батч аудио на сателлиты (ESP-NOW unicast по MAC).
// Формат пакета: AudioPacketHeader (16 байт, espnow_transport.h) + payload int16.
static void flushTxBatch()
{
    size_t txCount = g_txCount.exchange(0);
    if (txCount == 0) return;
    uint8_t buf[16 + 234];
    AudioPacketHeader hdr = {};
    hdr.magic = kMagicAudioL;
    hdr.sequence = g_txPacketId++;
    hdr.timestampMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    hdr.volumeL = (int16_t)g_cfg.masterVolume;
    hdr.volumeR = (int16_t)g_cfg.masterVolume;
    hdr.delaySubMs = (int16_t)g_cfg.delaySubMs;
    size_t payloadBytes = txCount * sizeof(int16_t);
    size_t n = sizeof(hdr) + payloadBytes;
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), g_txLeft, payloadBytes);

    bool hasLeft = g_cfg.leftSatMac.bytes[0] || g_cfg.leftSatMac.bytes[1] ||
                   g_cfg.leftSatMac.bytes[2] || g_cfg.leftSatMac.bytes[3] ||
                   g_cfg.leftSatMac.bytes[4] || g_cfg.leftSatMac.bytes[5];
    if (hasLeft) {
        g_espnow->sendUnicast(g_cfg.leftSatMac.bytes, buf, n);
    } else {
        g_espnow->sendBroadcast(buf, n);
    }

    // Правый канал
    hdr.magic = kMagicAudioR;
    hdr.sequence = g_txPacketId++;
    n = sizeof(hdr) + payloadBytes;
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), g_txRight, payloadBytes);

    bool hasRight = g_cfg.rightSatMac.bytes[0] || g_cfg.rightSatMac.bytes[1] ||
                    g_cfg.rightSatMac.bytes[2] || g_cfg.rightSatMac.bytes[3] ||
                    g_cfg.rightSatMac.bytes[4] || g_cfg.rightSatMac.bytes[5];
    if (hasRight) {
        g_espnow->sendUnicast(g_cfg.rightSatMac.bytes, buf, n);
    } else {
        g_espnow->sendBroadcast(buf, n);
    }
    g_txPackets += 2;
}

// Драйвер аудио-вывода: вычитывает семплы из джиттер-буфера в I2S.
// Вызывается из loop() с бюджетом семплов, привязанным к реальному времени.
static void audioOutTick(uint32_t samplesBudget)
{
    if (!g_i2sOn) return;
    int16_t buf[128];
    while (samplesBudget > 0) {
        size_t chunk = samplesBudget > 128 ? 128 : samplesBudget;
        for (size_t i = 0; i < chunk; i++) {
            int16_t s = 0;
            if (g_jitter && g_jitter->pop(s)) {
                s = g_delaySub->process(s);
            }
            buf[i] = s;
        }
        g_i2sOut->write(buf, chunk, 100);
        samplesBudget -= chunk;
    }
}

// Состояние Wi-Fi

// Предварительное объявление

// Сканирование Wi-Fi сетей — выводит список с параметрами безопасности.
static void wifi_scan_and_log(void)
{
    uint16_t ap_count = 0;
    esp_err_t ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK || ap_count == 0) {
        ESP_LOGI(TAG, "Wi-Fi scan: no APs found");
        return;
    }
    wifi_ap_record_t ap_list[8];
    uint16_t count = ap_count > 8 ? 8 : ap_count;
    ret = esp_wifi_scan_get_ap_records(&count, ap_list);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi scan get_ap_records failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi scan found %u APs:", ap_count);
    for (uint16_t i = 0; i < count; i++) {
        const char *auth_str;
        switch (ap_list[i].authmode) {
            case WIFI_AUTH_OPEN:          auth_str = "OPEN"; break;
            case WIFI_AUTH_WEP:           auth_str = "WEP"; break;
            case WIFI_AUTH_WPA_PSK:       auth_str = "WPA-PSK"; break;
            case WIFI_AUTH_WPA2_PSK:      auth_str = "WPA2-PSK"; break;
            case WIFI_AUTH_WPA_WPA2_PSK:  auth_str = "WPA/WPA2-PSK"; break;
            case WIFI_AUTH_WPA3_PSK:      auth_str = "WPA3-PSK"; break;
            case WIFI_AUTH_WPA2_WPA3_PSK: auth_str = "WPA2/WPA3-PSK"; break;
            default:                      auth_str = "UNKNOWN"; break;
        }
        ESP_LOGI(TAG, "  [%u] %-32s RSSI=%-4d ch=%-3d auth=%-16s",
                 i, ap_list[i].ssid, ap_list[i].rssi, ap_list[i].primary, auth_str);
    }
}

// Инициализация Wi-Fi с поддержкой AP / STA / APSTA режимов.

// Переконфигурация Wi-Fi без перезагрузки (применение новых SSID/пароля/режима)

extern "C" void app_main(void)
{
    // Загрузка конфигурации из NVS
    g_nvsConfig = std::make_unique<NvsConfig>();
    esp_err_t ret = g_nvsConfig->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS config init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = g_nvsConfig->load(g_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Using default config (no saved config in NVS)");
        g_cfg = NodeConfig();
    }
    g_volumePercent = g_cfg.masterVolume;
    g_crossoverHz = g_cfg.crossoverHz;

    // Инициализация аудио-объектов (после инициализации кучи)
    // Пытаемся выделить большие буферы в PSRAM, если доступна.
    int16_t* jitterBufPsram = psramAllocSamples((kJitterTargetMs * kSampleRate) / 1000);
    int16_t* delayLeftBufPsram = psramAllocSamples((kMaxDelayMs * kSampleRate) / 1000);
    int16_t* delayRightBufPsram = psramAllocSamples((kMaxDelayMs * kSampleRate) / 1000);
    int16_t* delaySubBufPsram = psramAllocSamples((kDelaySubMs * kSampleRate) / 1000);

    g_delaySub = std::make_unique<DelayLine>(kDelaySubMs, kSampleRate, delaySubBufPsram);
    g_delayLeft = std::make_unique<DelayLine>(kMaxDelayMs, kSampleRate, delayLeftBufPsram);
    g_delayRight = std::make_unique<DelayLine>(kMaxDelayMs, kSampleRate, delayRightBufPsram);
    g_jitter = std::make_unique<JitterBuffer>(
        (kJitterTargetMs * kSampleRate) / 1000,
        jitterBufPsram ? psramAllocSamples : nullptr);
    if (!g_jitter || !g_jitter->allocated()) {
        ESP_LOGE(TAG, "JitterBuffer allocation failed");
        return;
    }

    ESP_LOGI(TAG, "Wireless Audio 2.1 Master (ESP-IDF) starting...");
    ESP_LOGI(TAG, "Chip: ESP32-S3, FreeRTOS heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Loaded config: masterVolume=%d%%, crossover=%d Hz, hostname=%s",
             g_cfg.masterVolume, g_cfg.crossoverHz, g_cfg.hostname);

    // Инициализация DSP конвейера
    ESP_LOGI(TAG, "Configuring pipeline...");
    g_pipeline.configure((float)kSampleRate);
    g_pipeline.setCrossoverHz(g_cfg.crossoverHz);
    g_pipeline.setVolume(g_cfg.masterVolume);
    g_pipeline.setMute(g_cfg.mute);
    g_pipeline.setTone(0.0f, 0.0f);
    g_pipeline.setChannelVolumes(g_cfg.leftVolume, g_cfg.rightVolume, g_cfg.subVolume);
    g_pipeline.reset();

    ESP_LOGI(TAG, "DSP pipeline initialized: sampleRate=%" PRIu32 " Hz, crossover=%d Hz, masterVolume=%d%%, mute=%s",
             kSampleRate, g_cfg.crossoverHz, g_cfg.masterVolume,
             g_cfg.mute ? "on" : "off");

    // Инициализация Wi-Fi (AP / STA / APSTA по конфигу)
    ESP_LOGI(TAG, "Calling wifi_init()...");
    wifi_init();
    ESP_LOGI(TAG, "After wifi_init, g_wifi_inited=%d", g_wifi_inited ? 1 : 0);

    // Per-device соль для PBKDF2 (из MAC-адреса AP)
    initAdminSalt();
    ESP_LOGI(TAG, "Admin salt initialized: %s", g_adminSalt);

    // Этап 4/5: mDNS — доступ по http://<hostname>.local (STA приоритет, иначе AP).
    esp_netif_t *mdns_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const char* netif_name = "STA";
    if (!mdns_netif) {
        mdns_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        netif_name = "AP";
    }
    if (mdns_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(mdns_netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "%s IP: " IPSTR, netif_name, IP2STR(&ip_info.ip));
        }

        ret = mdns_raw_init(mdns_netif, g_cfg.hostname);
        if (ret == ESP_OK) {
            ret = mdns_raw_add_service("_http", "tcp", 80,
                                       "Wireless Audio 2.1 Master");
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "mDNS: http://%s.local (%s)", g_cfg.hostname, netif_name);
            }
        } else {
            ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "DNS/mDNS: could not get netif handle");
    }

    // Инициализация ESP-NOW транспорта
    g_espnow = std::make_unique<EspNowTransport>();
    ret = g_espnow->init(false); // без шифрования пока
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
    } else {
        g_espnow->setRxCallback(&g_espnowMasterRxCb);
        ESP_LOGI(TAG, "ESP-NOW transport initialized with RX callback");
    }

    // Инициализация I2S вывода (пины из конфигурации)
    I2sOutputPins i2sPins = {(int)g_cfg.i2sBck, (int)g_cfg.i2sWs, (int)g_cfg.i2sDataOut};
    g_i2sOut = std::make_unique<I2sOutput>();
    ret = g_i2sOut->init(i2sPins, kSampleRate, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(ret));
    } else {
        g_i2sOn = true;
        ESP_LOGI(TAG, "I2S output initialized");
    }

    // Инициализация UDP аудио приёма
    esp_err_t netif_ret = esp_netif_init();
    if (netif_ret == ESP_OK || netif_ret == ESP_ERR_INVALID_STATE) {
        g_udpAudio = std::make_unique<UdpAudio>();
        ret = g_udpAudio->init(kUdpAudioPort, g_cfg.udpAuthKey[0] ? g_cfg.udpAuthKey : nullptr);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "UDP audio init failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "UDP audio listener on port %u", (unsigned int)kUdpAudioPort);
        }
    } else {
        ESP_LOGE(TAG, "LwIP not initialized, skipping UDP audio: %s", esp_err_to_name(netif_ret));
    }

    // Инициализация Web сервера
    g_webServer = std::make_unique<WebServer>();
    g_webServer->setCallbacks(&g_webCallbacks);
    if (netif_ret == ESP_OK || netif_ret == ESP_ERR_INVALID_STATE) {
        ret = g_webServer->init(80);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Web server init failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Web server started on port 80");
            // HTTPS сервер на порту 443 (самоподписанный сертификат)
            esp_err_t https_ret = g_webServer->initHttps(443);
            if (https_ret != ESP_OK) {
                ESP_LOGW(TAG, "HTTPS server not started: %s", esp_err_to_name(https_ret));
            } else {
                ESP_LOGI(TAG, "HTTPS server started on port 443");
            }
        }
    } else {
        ESP_LOGE(TAG, "LwIP not initialized, skipping Web server");
    }

    ESP_LOGI(TAG, "setup complete, entering main loop");

    // Основной цикл
    uint32_t loopCounter = 0;
    TickType_t lastWakeTime = xTaskGetTickCount();
    uint32_t audioOutLastMs = 0;

    while (1) {
        loopCounter++;

        // Если Wi-Fi переконфигурируется — пропускаем сетевые операции
        if (!g_wifi_reconfiguring.load()) {
            // Приём UDP аудио (неблокирующий, таймаут 1 мс) — пишет в джиттер-буфер и TX батчи.
            g_udpAudio->receive(1, &g_udpHandler);

            // Отправка накопленного TX батча на сателлиты (когда заполнен).
            if (g_txCount.load() >= kBatchSamples) {
                flushTxBatch();
            }
        }

        // Аудио-вывод: бюджет семплов привязан к реальному времени (как в Arduino-версии).
        uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint32_t elapsed = nowMs - audioOutLastMs;
        if (elapsed > 100) elapsed = 100; // лимит догона
        audioOutLastMs = nowMs;
        uint32_t samplesBudget = elapsed * kSampleRate / 1000u;
        audioOutTick(samplesBudget);

        // Каждые 5 секунд логируем статус и проверяем сателлиты
        if (loopCounter % 500 == 0) {
            ESP_LOGI(TAG, "loop: heap=%" PRIu32 " bytes, counter=%" PRIu32 ", tx_packets=%" PRIu32,
                     esp_get_free_heap_size(), loopCounter, g_txPackets);

            // Проверяем таймаут сателлитов (6 секунд без heartbeat = оффлайн)
            uint32_t nowMs2 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (g_leftOnline && nowMs2 - g_lastLeftSeenMs > kSatelliteTimeoutMs) {
                g_leftOnline = false;
                ESP_LOGW(TAG, "satellite left offline (no heartbeat for %" PRIu32 " ms)",
                         nowMs2 - g_lastLeftSeenMs);
                char sse[128];
                snprintf(sse, sizeof(sse), "{\"left\":\"offline\",\"right\":\"%s\"}",
                         g_rightOnline ? "online" : "offline");
                g_webServer->sseBroadcast("satellite", sse);
            }
            if (g_rightOnline && nowMs2 - g_lastRightSeenMs > kSatelliteTimeoutMs) {
                g_rightOnline = false;
                ESP_LOGW(TAG, "satellite right offline (no heartbeat for %" PRIu32 " ms)",
                         nowMs2 - g_lastRightSeenMs);
                char sse[128];
                snprintf(sse, sizeof(sse), "{\"left\":\"%s\",\"right\":\"offline\"}",
                         g_leftOnline ? "online" : "offline");
                g_webServer->sseBroadcast("satellite", sse);
            }
        }

        // Периодический discovery-запрос каждые 3 с (сателлиты отвечают heartbeat-ом).
        if (nowMs - g_lastDiscoveryMs >= kDiscoveryIntervalMs) {
            g_lastDiscoveryMs = nowMs;
            uint8_t buf[16];
            AudioPacketHeader hdr = {};
            hdr.magic = kMagicDiscovery;
            hdr.sequence = 0;
            hdr.timestampMs = nowMs;
            memcpy(buf, &hdr, sizeof(hdr));
            g_espnow->sendBroadcast(buf, sizeof(AudioPacketHeader));
            g_heartbeatsTx++;
        }

        // Fallback: если STA не подключился за kStaConnectTimeoutMs — переходим в AP режим,
        // чтобы можно было настроить Wi-Fi через веб-интерфейс.
        if (g_sta_connecting && g_staIpAddr[0] == '\0' &&
            nowMs - g_sta_connect_start_ms > kStaConnectTimeoutMs) {
            g_sta_connecting = false;
            g_cfg.wifiMode = WifiMode::ApDirect;
            g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            ESP_LOGW(TAG, "STA connect timeout — falling back to AP mode");
            wifi_reconfigure();
        }

        // Цикл примерно 10 мс (100 Гц)
        // Отложенная запись в NVS (дебаунс 500 мс, немедленная для критических изменений)
        nvsSaveTick(nowMs);
        consoleTick();
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10));
    }
}
