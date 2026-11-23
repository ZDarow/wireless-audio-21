// nvs_config.h — хранение конфигурации в NVS (C API для ESP-IDF v5.3).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"

namespace audio21 {

// Версия схемы конфигурации NVS. Инкрементировать при изменении структуры.
static constexpr uint32_t kConfigVersion = 1;

// Имя NVS namespaces
static constexpr char kConfigNamespace[] = "audio21_cfg";

// Ключи конфигурации
static constexpr char kKeyVersion[] = "ver";
static constexpr char kKeyVolume[] = "vol";
static constexpr char kKeyCrossoverHz[] = "xover";
static constexpr char kKeyWifiSsid[] = "wssid";
static constexpr char kKeyWifiPassword[] = "wpwd";
static constexpr char kKeyWifiApSsid[] = "wassid";
static constexpr char kKeyWifiApPassword[] = "wapwd";
static constexpr char kKeyHostname[] = "host";
static constexpr char kKeyNtpEnabled[] = "ntp";
static constexpr char kKeyNtpServer[] = "ntpsrv";
static constexpr char kKeyTimezone[] = "tz";
static constexpr char kKeyUdpAuthKey[] = "udpk";
static constexpr char kKeyEspNowPmk[] = "pmk";
static constexpr char kKeyEspNowLmk[] = "lmk";

// Структура конфигурации (все поля должны быть фиксированного размера для NVS)
struct NodeConfig {
    uint32_t version = kConfigVersion;
    int volume = 100;           // 0..100
    int crossoverHz = 90;       // 40..500
    char wifiSsid[32] = {0};
    char wifiPassword[64] = {0};
    char wifiApSsid[32] = "Audio21-Master";
    char wifiApPassword[64] = "1234567890";
    char hostname[32] = "audio-master";
    uint8_t ntpEnabled = 1;
    char ntpServer[64] = "pool.ntp.org";
    char timezone[32] = "UTC-3";
    char udpAuthKey[64] = {0};  // HMAC-SHA256 ключ для UDP аудио (V4)
    char espnowPmk[33] = {0};   // ESP-NOW Pairwise Master Key (hex, 32 chars)
    char espnowLmk[33] = {0};   // ESP-NOW Local Master Key (hex, 32 chars)
};

class NvsConfig {
public:
    NvsConfig() = default;
    ~NvsConfig();

    // Инициализация NVS (вызывать один раз при старте)
    esp_err_t init();

    // Загрузить конфигурацию из NVS. Если не найдена — дефолтные значения.
    esp_err_t load(NodeConfig& cfg);

    // Сохранить конфигурацию в NVS
    esp_err_t save(const NodeConfig& cfg);

    // Сбросить к заводским настройкам (стереть namespace)
    esp_err_t erase();

private:
    nvs_handle m_handle = 0;
    bool m_initialized = false;
};

} // namespace audio21
