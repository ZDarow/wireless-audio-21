#include "ota_update.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_crt_bundle.h"

#include "esp_crt_bundle.h"

static const char *TAG = "ota_update";

esp_err_t ota_update_start(const char *url)
{
    if (url == nullptr) {
        ESP_LOGE(TAG, "OTA URL is null");
        return ESP_ERR_INVALID_ARG;
    }

    // Только HTTPS — запрещаем HTTP для предотвращения MITM атак
    if (strncmp(url, "https://", 8) != 0) {
        ESP_LOGE(TAG, "OTA URL must use HTTPS, got: %s", url);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting OTA update from: %s", url);

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };
    http_config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful, marking as valid and rebooting...");
        esp_ota_mark_app_valid_cancel_rollback();
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
