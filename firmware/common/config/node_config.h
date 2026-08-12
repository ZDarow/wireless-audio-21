// node_config.h — структуры конфигурации узла и дефолты.
// Header-only. Загружает значения из сгенерированного generated_config.h,
// если он есть, иначе использует дефолты.
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#ifdef GENERATED_CONFIG_H
#include "generated_config.h"
#endif

#ifndef AUDIO_NODE_ROLE_MASTER
#define AUDIO_NODE_ROLE_MASTER 1
#endif
#ifndef AUDIO_SOURCE_MODE_A2DP
#define AUDIO_SOURCE_MODE_A2DP 1
#endif
#ifndef AUDIO_TRANSPORT_MODE_ESPNOW
#define AUDIO_TRANSPORT_MODE_ESPNOW 1
#endif
#ifndef AUDIO_WIFI_MODE_AP
#define AUDIO_WIFI_MODE_AP 1
#endif
#ifndef AUDIO_WIFI_MODE_APSTA
#define AUDIO_WIFI_MODE_APSTA 0
#endif

#ifndef AUDIO_WIFI_SSID
#define AUDIO_WIFI_SSID "MyNetwork"
#endif
#ifndef AUDIO_WIFI_PASSWORD
#define AUDIO_WIFI_PASSWORD "MyPassword"
#endif
// AP мастера (ТЗ §6.3: Audio21-Master / audio21master).
// В режиме APSTA/AP используется этот SSID; AUDIO_WIFI_SSID — домашняя сеть.
#ifndef AUDIO_WIFI_AP_SSID
#define AUDIO_WIFI_AP_SSID "Audio21-Master"
#endif
#ifndef AUDIO_WIFI_AP_PASSWORD
#define AUDIO_WIFI_AP_PASSWORD "audio21master"
#endif
#ifndef AUDIO_HOSTNAME
#define AUDIO_HOSTNAME "audio-master"
#endif
#ifndef AUDIO_UDP_PORT
#define AUDIO_UDP_PORT 5004
#endif
#ifndef AUDIO_ESPNOW_CHANNEL
#define AUDIO_ESPNOW_CHANNEL 6
#endif

// Проверка интернета (ТЗ_Веб §7)
#ifndef AUDIO_NET_CHECK_ENABLED
#define AUDIO_NET_CHECK_ENABLED 1
#endif
#ifndef AUDIO_NET_CHECK_URL
#define AUDIO_NET_CHECK_URL "http://connectivitycheck.gstatic.com/generate_204"
#endif
#ifndef AUDIO_NTP_SERVER
#define AUDIO_NTP_SERVER "pool.ntp.org"
#endif
#ifndef AUDIO_TIMEZONE
#define AUDIO_TIMEZONE "UTC0"
#endif

#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE 48000
#endif
#ifndef AUDIO_BITS_PER_SAMPLE
#define AUDIO_BITS_PER_SAMPLE 16
#endif
#ifndef AUDIO_CHANNELS
#define AUDIO_CHANNELS 2
#endif

#ifndef AUDIO_CROSSOVER_HZ
#define AUDIO_CROSSOVER_HZ 90
#endif
#ifndef AUDIO_DELAY_LEFT_MS
#define AUDIO_DELAY_LEFT_MS 0
#endif
#ifndef AUDIO_DELAY_RIGHT_MS
#define AUDIO_DELAY_RIGHT_MS 0
#endif
#ifndef AUDIO_DELAY_SUB_MS
#define AUDIO_DELAY_SUB_MS 0
#endif

// ESP32-S3: GPIO22-25 невалидны (внутренний SPI-флеш) — дефолты 4/5/6.
#ifndef AUDIO_I2S_BCK
#define AUDIO_I2S_BCK 4
#endif
#ifndef AUDIO_I2S_WS
#define AUDIO_I2S_WS 5
#endif
#ifndef AUDIO_I2S_DATA_OUT
#define AUDIO_I2S_DATA_OUT 6
#endif

#ifndef AUDIO_LEFT_SAT_MAC
#define AUDIO_LEFT_SAT_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01}
#endif
#ifndef AUDIO_RIGHT_SAT_MAC
#define AUDIO_RIGHT_SAT_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02}
#endif

namespace audio21 {

// Источник аудио и транспорт
enum class AudioSource : uint8_t { A2DP = 0, WiFi = 1 };
enum class TransportMode : uint8_t { EspNow = 0, Udp = 1 };

// Роль узла
enum class NodeRole : uint8_t { Master = 0, Satellite = 1 };
enum class SatelliteSide : uint8_t { Left = 0, Right = 1 };

// Режим Wi-Fi мастера
enum class WifiMode : uint8_t { ApDirect = 1, Sta = 0, ApSta = 2 };

// MAC-адрес (6 байт)
struct MacAddr {
    uint8_t bytes[6];

    bool operator==(const MacAddr& o) const { return memcmp(bytes, o.bytes, 6) == 0; }
    bool operator!=(const MacAddr& o) const { return !(*this == o); }

    static bool parse(const char* str, MacAddr& out) {
        int vals[6];
        if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x",
                   &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6) {
            return false;
        }
        for (int i = 0; i < 6; i++) out.bytes[i] = static_cast<uint8_t>(vals[i]);
        return true;
    }
};

// Диапазоны задержек (мс) — по спецификации §6.9
constexpr int kMaxDelayMs = 200;
constexpr int kMinDelayMs = 0;

// Диапазоны кроссовера (Гц) — по спецификации §6.10
constexpr int kCrossoverMinHz = 70;
constexpr int kCrossoverMaxHz = 120;
constexpr int kCrossoverDefaultHz = 90;

// Диапазоны громкости (0..100)
constexpr int kVolumeMin = 0;
constexpr int kVolumeMax = 100;

// Текущая настройка узла (что хранится в NVS и отдаётся через API)
struct NodeConfig {
    // --- Общие ---
    NodeRole role = NodeRole::Master;
    SatelliteSide side = SatelliteSide::Left;

    // --- Источник и транспорт ---
    AudioSource source = AudioSource::A2DP;
    TransportMode transport = TransportMode::EspNow;
    WifiMode wifiMode = AUDIO_WIFI_MODE_APSTA ? WifiMode::ApSta
                       : (AUDIO_WIFI_MODE_AP ? WifiMode::ApDirect : WifiMode::Sta);

    // --- PCM ---
    uint32_t sampleRate = AUDIO_SAMPLE_RATE;
    uint8_t bitsPerSample = AUDIO_BITS_PER_SAMPLE;
    uint8_t channels = AUDIO_CHANNELS;

    // --- DSP ---
    int masterVolume = 50;        // 0..100 (общая громкость)
    bool mute = false;
    // Покомпонентные громкости (C2.2, ТЗ §7.5): 0..100, множители к master.
    int leftVolume = 50;
    int rightVolume = 50;
    int subVolume = 50;
    int crossoverHz = kCrossoverDefaultHz;
    int delayLeftMs = AUDIO_DELAY_LEFT_MS;
    int delayRightMs = AUDIO_DELAY_RIGHT_MS;
    int delaySubMs = AUDIO_DELAY_SUB_MS;

    // --- Сеть ---
    char wifiSsid[33] = AUDIO_WIFI_SSID;            // STA: домашняя сеть
    char wifiPassword[65] = AUDIO_WIFI_PASSWORD;
    char wifiApSsid[33] = AUDIO_WIFI_AP_SSID;       // AP мастера
    char wifiApPassword[65] = AUDIO_WIFI_AP_PASSWORD;
    char hostname[33] = AUDIO_HOSTNAME;
    uint16_t udpAudioPort = AUDIO_UDP_PORT;

    // --- Проверка интернета (ТЗ_Веб §7) ---
    bool netCheckEnabled = AUDIO_NET_CHECK_ENABLED;
    uint16_t netCheckIntervalSec = 30;              // интервал проверки, с
    uint16_t netCheckTimeoutMs = 5000;              // таймаут проверки, мс
    char netCheckUrl[96] = AUDIO_NET_CHECK_URL;     // endpoint проверки

    // --- NTP (ТЗ_Веб §7.5) ---
    bool ntpEnabled = true;
    char ntpServer[64] = AUDIO_NTP_SERVER;
    char timezone[24] = AUDIO_TIMEZONE;

    // --- Безопасность (ТЗ_Веб §18) ---
    // SHA-256(password + salt) администратора, hex-строка (64 символа).
    // Пустая строка = пароль не задан (первый запуск → /admin/setup).
    char adminPasswordHash[65] = "";
    bool authEnabled = false;   // пароль задан → требуется вход

    // --- Сателлиты ---
    MacAddr leftSatMac = MacAddr{AUDIO_LEFT_SAT_MAC};
    MacAddr rightSatMac = MacAddr{AUDIO_RIGHT_SAT_MAC};

    // --- GPIO ---
    uint8_t i2sBck = AUDIO_I2S_BCK;
    uint8_t i2sWs = AUDIO_I2S_WS;
    uint8_t i2sDataOut = AUDIO_I2S_DATA_OUT;

    // Ограничитель диапазонов (idempotent)
    void clamp() {
        if (masterVolume < kVolumeMin) masterVolume = kVolumeMin;
        if (masterVolume > kVolumeMax) masterVolume = kVolumeMax;
        if (leftVolume < kVolumeMin) leftVolume = kVolumeMin;
        if (leftVolume > kVolumeMax) leftVolume = kVolumeMax;
        if (rightVolume < kVolumeMin) rightVolume = kVolumeMin;
        if (rightVolume > kVolumeMax) rightVolume = kVolumeMax;
        if (subVolume < kVolumeMin) subVolume = kVolumeMin;
        if (subVolume > kVolumeMax) subVolume = kVolumeMax;
        if (crossoverHz < kCrossoverMinHz) crossoverHz = kCrossoverMinHz;
        if (crossoverHz > kCrossoverMaxHz) crossoverHz = kCrossoverMaxHz;
        if (delayLeftMs < kMinDelayMs) delayLeftMs = kMinDelayMs;
        if (delayLeftMs > kMaxDelayMs) delayLeftMs = kMaxDelayMs;
        if (delayRightMs < kMinDelayMs) delayRightMs = kMinDelayMs;
        if (delayRightMs > kMaxDelayMs) delayRightMs = kMaxDelayMs;
        if (delaySubMs < kMinDelayMs) delaySubMs = kMinDelayMs;
        if (delaySubMs > kMaxDelayMs) delaySubMs = kMaxDelayMs;
    }
};

// Дефолтная конфигурация из макросов (генерируются из config.env).
// Роль/источник/транспорт берутся из AUDIO_NODE_ROLE_MASTER,
// AUDIO_SOURCE_MODE_A2DP, AUDIO_TRANSPORT_MODE_ESPNOW.
inline NodeConfig defaultConfig() {
    NodeConfig cfg;
#if AUDIO_NODE_ROLE_MASTER
    cfg.role = NodeRole::Master;
#else
    cfg.role = NodeRole::Satellite;
#endif
#if AUDIO_SOURCE_MODE_A2DP
    cfg.source = AudioSource::A2DP;
#else
    cfg.source = AudioSource::WiFi;
#endif
#if AUDIO_TRANSPORT_MODE_ESPNOW
    cfg.transport = TransportMode::EspNow;
#else
    cfg.transport = TransportMode::Udp;
#endif
#if AUDIO_WIFI_MODE_APSTA
    cfg.wifiMode = WifiMode::ApSta;
#elif AUDIO_WIFI_MODE_AP
    cfg.wifiMode = WifiMode::ApDirect;
#else
    cfg.wifiMode = WifiMode::Sta;
#endif
    cfg.clamp();
    return cfg;
}

// Вспомогательные строковые конвертации для консоли/API
inline const char* sourceToString(AudioSource s) { return s == AudioSource::A2DP ? "a2dp" : "wifi"; }
inline const char* transportToString(TransportMode t) { return t == TransportMode::EspNow ? "espnow" : "udp"; }
inline const char* sideToString(SatelliteSide s) { return s == SatelliteSide::Left ? "left" : "right"; }
inline const char* wifiModeToString(WifiMode m) {
    switch (m) {
        case WifiMode::ApDirect: return "ap_direct";
        case WifiMode::ApSta: return "apsta_repeater";
        default: return "sta";
    }
}

} // namespace audio21