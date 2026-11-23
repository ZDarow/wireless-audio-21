// nvs_config.cpp — реализация хранения конфигурации в NVS (C API).
#include "nvs_config.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

namespace audio21 {

static const char *TAG = "nvs_config";

NvsConfig::~NvsConfig()
{
    if (m_initialized && m_handle != 0) {
        nvs_close(m_handle);
        m_handle = 0;
    }
}

esp_err_t NvsConfig::init()
{
    if (m_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_open(kConfigNamespace, NVS_READWRITE, &m_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        m_handle = 0;
        return ret;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "NVS config initialized, namespace='%s'", kConfigNamespace);
    return ESP_OK;
}

esp_err_t NvsConfig::load(NodeConfig& cfg)
{
    if (!m_initialized || m_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    // Читаем версию
    uint32_t storedVersion = 0;
    esp_err_t ret = nvs_get_u32(m_handle, kKeyVersion, &storedVersion);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No config found in NVS, using defaults (%s)", esp_err_to_name(ret));
        return ESP_ERR_NOT_FOUND;
    }

    if (storedVersion != kConfigVersion) {
        ESP_LOGW(TAG, "Config version mismatch: stored=%" PRIu32 " current=%" PRIu32 ", using defaults",
                 storedVersion, kConfigVersion);
        return ESP_ERR_NOT_FOUND;
    }

    // Читаем все поля
    int32_t vol = 100;
    ret = nvs_get_i32(m_handle, kKeyVolume, &vol);
    if (ret == ESP_OK) {
        cfg.volume = (int)vol;
        if (cfg.volume < 0 || cfg.volume > 100) cfg.volume = 100;
    }

    int32_t xover = 90;
    ret = nvs_get_i32(m_handle, kKeyCrossoverHz, &xover);
    if (ret == ESP_OK) {
        cfg.crossoverHz = (int)xover;
        if (cfg.crossoverHz < 40 || cfg.crossoverHz > 500) cfg.crossoverHz = 90;
    }

    size_t len = sizeof(cfg.wifiSsid);
    ret = nvs_get_str(m_handle, kKeyWifiSsid, cfg.wifiSsid, &len);
    if (ret != ESP_OK) cfg.wifiSsid[0] = '\0';

    len = sizeof(cfg.wifiPassword);
    ret = nvs_get_str(m_handle, kKeyWifiPassword, cfg.wifiPassword, &len);
    if (ret != ESP_OK) cfg.wifiPassword[0] = '\0';

    len = sizeof(cfg.wifiApSsid);
    ret = nvs_get_str(m_handle, kKeyWifiApSsid, cfg.wifiApSsid, &len);
    if (ret != ESP_OK) strcpy(cfg.wifiApSsid, "Audio21-Master");

    len = sizeof(cfg.wifiApPassword);
    ret = nvs_get_str(m_handle, kKeyWifiApPassword, cfg.wifiApPassword, &len);
    if (ret != ESP_OK) strcpy(cfg.wifiApPassword, "1234567890");

    len = sizeof(cfg.hostname);
    ret = nvs_get_str(m_handle, kKeyHostname, cfg.hostname, &len);
    if (ret != ESP_OK) strcpy(cfg.hostname, "audio-master");

    uint8_t ntpEnabled = 1;
    ret = nvs_get_u8(m_handle, kKeyNtpEnabled, &ntpEnabled);
    cfg.ntpEnabled = ntpEnabled;

    len = sizeof(cfg.ntpServer);
    ret = nvs_get_str(m_handle, kKeyNtpServer, cfg.ntpServer, &len);
    if (ret != ESP_OK) strcpy(cfg.ntpServer, "pool.ntp.org");

    len = sizeof(cfg.timezone);
    ret = nvs_get_str(m_handle, kKeyTimezone, cfg.timezone, &len);
    if (ret != ESP_OK) strcpy(cfg.timezone, "UTC-3");

    len = sizeof(cfg.udpAuthKey);
    ret = nvs_get_str(m_handle, kKeyUdpAuthKey, cfg.udpAuthKey, &len);
    if (ret != ESP_OK) cfg.udpAuthKey[0] = '\0';

    len = sizeof(cfg.espnowPmk);
    ret = nvs_get_str(m_handle, kKeyEspNowPmk, cfg.espnowPmk, &len);
    if (ret != ESP_OK) cfg.espnowPmk[0] = '\0';

    len = sizeof(cfg.espnowLmk);
    ret = nvs_get_str(m_handle, kKeyEspNowLmk, cfg.espnowLmk, &len);
    if (ret != ESP_OK) cfg.espnowLmk[0] = '\0';

    ESP_LOGI(TAG, "Config loaded from NVS: volume=%d%%, crossover=%d Hz", cfg.volume, cfg.crossoverHz);
    return ESP_OK;
}

esp_err_t NvsConfig::save(const NodeConfig& cfg)
{
    if (!m_initialized || m_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    ret = nvs_set_u32(m_handle, kKeyVersion, cfg.version);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_i32(m_handle, kKeyVolume, cfg.volume);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_i32(m_handle, kKeyCrossoverHz, cfg.crossoverHz);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyWifiSsid, cfg.wifiSsid);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyWifiPassword, cfg.wifiPassword);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyWifiApSsid, cfg.wifiApSsid);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyWifiApPassword, cfg.wifiApPassword);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyHostname, cfg.hostname);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_u8(m_handle, kKeyNtpEnabled, cfg.ntpEnabled ? 1 : 0);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyNtpServer, cfg.ntpServer);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyTimezone, cfg.timezone);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyUdpAuthKey, cfg.udpAuthKey);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyEspNowPmk, cfg.espnowPmk);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(m_handle, kKeyEspNowLmk, cfg.espnowLmk);
    if (ret != ESP_OK) return ret;

    ret = nvs_commit(m_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Config saved to NVS: volume=%d%%, crossover=%d Hz", cfg.volume, cfg.crossoverHz);
    return ESP_OK;
}

esp_err_t NvsConfig::erase()
{
    if (!m_initialized || m_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = nvs_erase_all(m_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_all failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(m_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed after erase: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "NVS config erased (factory reset)");
    return ESP_OK;
}

} // namespace audio21
