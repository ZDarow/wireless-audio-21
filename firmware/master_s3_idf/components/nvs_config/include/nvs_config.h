// nvs_config.h — хранение конфигурации в NVS (C API для ESP-IDF v5.3).
// Использует единую структуру NodeConfig из firmware/common/config/node_config.h.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"

#include "../../../../common/config/node_config.h"

namespace audio21 {

// Имя NVS namespaces
static constexpr char kConfigNamespace[] = "audio21_cfg";

// Ключи конфигурации (совместимы с текущим NVS-форматом)
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

// Используем общую структуру NodeConfig из common/config/node_config.h.
// kConfigVersion берётся из той же структуры (version = kConfigVersion).
using NodeConfig = audio21::NodeConfig;

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
