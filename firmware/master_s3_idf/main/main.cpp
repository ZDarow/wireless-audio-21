#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

// DSP модули (header-only, без Arduino-зависимостей)
#include "pcm_pipeline.h"
#include "delay_line.h"
#include "jitter_buffer.h"
#include "volume_control.h"
#include "crossover.h"

using namespace audio21;

static const char *TAG = "master";

// Конфигурация аудио (из config.env в будущем)
static constexpr uint32_t kSampleRate = 48000;
static constexpr uint32_t kJitterTargetMs = 30;
static constexpr uint32_t kDelaySubMs = 40;

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "Audio21-Master",
            .password = "1234567890",
            .ssid_len = strlen("Audio21-Master"),
            .channel = 6,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP initialized: SSID=Audio21-Master, channel=6");
}

extern "C" void app_main(void)
{
    // Инициализация NVS (необходимо для Wi-Fi и других компонентов)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Wireless Audio 2.1 Master (ESP-IDF) starting...");
    ESP_LOGI(TAG, "Chip: ESP32-S3, FreeRTOS heap: %" PRIu32 " bytes", esp_get_free_heap_size());

    // Инициализация DSP конвейера
    VolumeControl volume(1.0f);
    ToneControl tone;
    PeakLimiter limiter;
    limiter.setThreshold(0.95f);
    Crossover crossover;
    crossover.configure(CrossoverKind::LowPass, CrossoverType::LinkwitzRiley, 4,
                        90.0f, (float)kSampleRate);
    DelayLine delaySub(kDelaySubMs, kSampleRate);
    JitterBuffer jitter((kJitterTargetMs * kSampleRate) / 1000);
    jitter.setTargetMs(kJitterTargetMs, kSampleRate);

    ESP_LOGI(TAG, "DSP pipeline initialized: volume=%d%%, crossover=%" PRIu32 " Hz, delay=%" PRIu32 " ms, jitter=%" PRIu32 " ms",
             volume.volume(), (uint32_t)crossover.freqHz(), kDelaySubMs, kJitterTargetMs);

    // Инициализация Wi-Fi AP
    wifi_init_softap();

    // Основной цикл — не блокирующий, с задержкой для кормления WDT
    uint32_t loopCounter = 0;
    while (1) {
        loopCounter++;
        if (loopCounter % 10 == 0) { // каждые 50 с (10 * 5 с)
            ESP_LOGI(TAG, "loop: heap=%" PRIu32 " bytes, counter=%" PRIu32,
                     esp_get_free_heap_size(), loopCounter);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
