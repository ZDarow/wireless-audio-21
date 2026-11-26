#include "nvs_manager.h"
#include "globals.h"
#include "esp_log.h"

static const char* TAG = "nvs_manager";

#define kNvsSaveDebounceMs 500

void nvsSaveTick(uint32_t nowMs) {
    if (g_nvsDirty) {
        bool shouldSave = g_nvsCritical || (nowMs - g_nvsLastChangeMs >= kNvsSaveDebounceMs);
        if (shouldSave) {
            esp_err_t ret = g_nvsConfig->save(g_cfg);
            if (ret == ESP_OK) {
                g_nvsDirty = false;
                g_nvsCritical = false;
                ESP_LOGD(TAG, "NVS save (%s)", g_nvsCritical ? "immediate" : "debounced");
            } else {
                ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(ret));
            }
        }
    }
}
