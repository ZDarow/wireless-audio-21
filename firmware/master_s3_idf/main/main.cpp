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
#include "esp_task_wdt.h"

// DSP модули
#include "pcm_pipeline.h"
#include "delay_line.h"
#include "jitter_buffer.h"
#include "volume_control.h"
#include "crossover.h"

// Новые компоненты Этапа 3
#include "driver/i2s_std.h"
#include "espnow_transport.h"
#include "../components/i2s_output/include/i2s_output.h"
#include "udp_audio.h"
#include "web_server.h"

// Этап 4: NVS конфигурация
#include "nvs_config.h"

// Этап 4: Captive DNS — все DNS-запросы отвечают softAP IP (192.168.4.1)
#include "dns_captive.h"

// Этап 5: mDNS через сырой UDP (порт 5353) — доступ по http://<hostname>.local
#include "mdns_raw.h"

// Этап 5: OTA обновление
#include "ota_update.h"

using namespace audio21;

static const char *TAG = "master";

// Конфигурация аудио
static constexpr uint32_t kSampleRate = 48000;
static constexpr uint32_t kJitterTargetMs = 30;
static constexpr uint32_t kDelaySubMs = 40;
static constexpr uint16_t kUdpAudioPort = 5004;

// Глобальные объекты аудио конвейера
static VolumeControl g_volume(1.0f);
static ToneControl g_tone;
static PeakLimiter g_limiter;
static Crossover g_crossover;
static DelayLine g_delaySub(kDelaySubMs, kSampleRate);
static JitterBuffer g_jitter((kJitterTargetMs * kSampleRate) / 1000);
static PcmPipeline g_pipeline;

// Глобальные компоненты транспорта
static EspNowTransport g_espnow;
static I2sOutput g_i2sOut;
static UdpAudio g_udpAudio;
static WebServer g_webServer;

// Этап 4: NVS конфигурация
static NvsConfig g_nvsConfig;
static NodeConfig g_cfg;

// Состояние
static bool g_i2sOn = false;
static int g_volumePercent = 100;
static int g_crossoverHz = 90;

// Реализация WebServerCallbacks
static int getStatusCallback(char* buf, size_t bufSize)
{
    int len = snprintf(buf, bufSize,
        "{\"sampleRate\":%" PRIu32 ",\"volume\":%d,\"crossoverHz\":%d,"
        "\"i2sOn\":%s,\"jitterAvailable\":%" PRIu32 ",\"espNowSatellites\":2,"
        "\"hostname\":\"%s\",\"wifiApSsid\":\"%s\",\"wifiMode\":\"ap\"}",
        kSampleRate, g_cfg.volume, g_cfg.crossoverHz,
        g_i2sOn ? "true" : "false", g_jitter.available(),
        g_cfg.hostname, g_cfg.wifiApSsid);
    return len;
}

static esp_err_t setVolumeCallback(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    g_cfg.volume = volume;
    g_volumePercent = volume;
    g_volume.setVolume(volume);
    // Сохраняем в NVS
    g_nvsConfig.save(g_cfg);
    ESP_LOGI(TAG, "volume set to %d%% (saved to NVS)", volume);
    return ESP_OK;
}

static int getVolumeCallback(void)
{
    return g_cfg.volume;
}

static esp_err_t setCrossoverHzCallback(int hz)
{
    if (hz < 40) hz = 40;
    if (hz > 500) hz = 500;
    g_cfg.crossoverHz = hz;
    g_crossoverHz = hz;
    g_crossover.configure(CrossoverKind::LowPass, CrossoverType::LinkwitzRiley, 4,
                          (float)hz, (float)kSampleRate);
    // Сохраняем в NVS
    g_nvsConfig.save(g_cfg);
    ESP_LOGI(TAG, "crossover set to %d Hz (saved to NVS)", hz);
    return ESP_OK;
}

static int getCrossoverHzCallback(void)
{
    return g_crossoverHz;
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
    esp_err_t ret = g_nvsConfig.erase();
    if (ret == ESP_OK) {
        // Загружаем дефолтные значения после сброса
        g_cfg = NodeConfig();
        g_volumePercent = g_cfg.volume;
        g_crossoverHz = g_cfg.crossoverHz;
        g_volume.setVolume(g_cfg.volume);
        g_crossover.configure(CrossoverKind::LowPass, CrossoverType::LinkwitzRiley, 4,
                              (float)g_cfg.crossoverHz, (float)kSampleRate);
    }
    return ret;
}

static esp_err_t otaUpdateCallback(const char* url)
{
    ESP_LOGW(TAG, "OTA update requested via Web UI: %s", url);
    return ota_update_start(url);
}

static int getWifiConfigCallback(char* buf, size_t bufSize)
{
    int len = snprintf(buf, bufSize,
        "{\"mode\":\"ap\",\"apSsid\":\"%s\",\"apPassword\":\"%s\","
        "\"staSsid\":\"%s\",\"staPassword\":\"%s\"}",
        g_cfg.wifiApSsid, g_cfg.wifiApPassword,
        g_cfg.wifiSsid, g_cfg.wifiPassword);
    return len;
}

// Предварительное объявление для переконфигурации Wi-Fi
static esp_err_t wifi_reconfigure(void);

static esp_err_t setWifiConfigCallback(const char* mode,
                                       const char* apSsid,
                                       const char* apPassword,
                                       const char* staSsid,
                                       const char* staPassword)
{
    ESP_LOGW(TAG, "Wi-Fi config requested: mode=%s", mode ? mode : "ap");

    if (apSsid) {
        strncpy(g_cfg.wifiApSsid, apSsid, sizeof(g_cfg.wifiApSsid) - 1);
        g_cfg.wifiApSsid[sizeof(g_cfg.wifiApSsid) - 1] = '\0';
    }
    if (apPassword) {
        strncpy(g_cfg.wifiApPassword, apPassword, sizeof(g_cfg.wifiApPassword) - 1);
        g_cfg.wifiApPassword[sizeof(g_cfg.wifiApPassword) - 1] = '\0';
    }
    if (staSsid) {
        strncpy(g_cfg.wifiSsid, staSsid, sizeof(g_cfg.wifiSsid) - 1);
        g_cfg.wifiSsid[sizeof(g_cfg.wifiSsid) - 1] = '\0';
    }
    if (staPassword) {
        strncpy(g_cfg.wifiPassword, staPassword, sizeof(g_cfg.wifiPassword) - 1);
        g_cfg.wifiPassword[sizeof(g_cfg.wifiPassword) - 1] = '\0';
    }

    // Сохраняем в NVS
    esp_err_t ret = g_nvsConfig.save(g_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wi-Fi config to NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Применяем без перезагрузки
    ret = wifi_reconfigure();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi config saved and applied: apSsid=%s", g_cfg.wifiApSsid);
    return ESP_OK;
}

static const WebServerCallbacks g_webCallbacks = {
    .getStatus = getStatusCallback,
    .setVolume = setVolumeCallback,
    .getVolume = getVolumeCallback,
    .setCrossoverHz = setCrossoverHzCallback,
    .getCrossoverHz = getCrossoverHzCallback,
    .reboot = rebootCallback,
    .factoryReset = factoryResetCallback,
    .otaUpdate = otaUpdateCallback,
    .getWifiConfig = getWifiConfigCallback,
    .setWifiConfig = setWifiConfigCallback,
};

// Коллбек приёма UDP аудио
class UdpAudioHandler : public UdpAudioRxCallback {
public:
    void onPcm(const int16_t* data, size_t nSamplesPerChannel) override
    {
        // Обрабатываем стерео PCM через DSP конвейер
        for (size_t i = 0; i < nSamplesPerChannel; i++) {
            int16_t l = data[i * 2];
            int16_t r = data[i * 2 + 1];

            PipelineOutput out = g_pipeline.process(l, r);

            // Сабвуфер — моно, через DelayLine
            int16_t subSample = static_cast<int16_t>(out.sub * 32767.0f);
            // DelayLine.process принимает int16_t и возвращает задержанный семпл
            subSample = g_delaySub.process(subSample);

            // I2S вывод (моно сабвуфер)
            if (g_i2sOn) {
                int16_t outBuf[1] = { subSample };
                g_i2sOut.write(outBuf, 1, 10);
            }
        }
    }
};

static UdpAudioHandler g_udpHandler;

// Состояние Wi-Fi
static bool g_wifi_inited = false;

// Предварительное объявление
static esp_err_t wifi_reconfigure(void);

// Инициализация Wi-Fi AP (вызывается один раз при старте)
static esp_err_t wifi_init_softap(void)
{
    if (!g_wifi_inited) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_ap();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        g_wifi_inited = true;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = {},
            .password = {},
            .ssid_len = (uint8_t)strlen(g_cfg.wifiApSsid),
            .channel = 6,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };
    memcpy(wifi_config.ap.ssid, g_cfg.wifiApSsid, strlen(g_cfg.wifiApSsid));
    memcpy(wifi_config.ap.password, g_cfg.wifiApPassword, strlen(g_cfg.wifiApPassword));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP initialized: SSID=%s, channel=6", g_cfg.wifiApSsid);
    return ESP_OK;
}

// Переконфигурация Wi-Fi без перезагрузки (применение новых SSID/пароля)
static esp_err_t wifi_reconfigure(void)
{
    ESP_LOGW(TAG, "Wi-Fi reconfiguration requested");

    // Останавливаем Wi-Fi
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Останавливаем captive DNS и mDNS перед изменением IP
    dns_captive_stop();
    mdns_raw_deinit();

    // Применяем новую конфигурацию
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = {},
            .password = {},
            .ssid_len = (uint8_t)strlen(g_cfg.wifiApSsid),
            .channel = 6,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };
    memcpy(wifi_config.ap.ssid, g_cfg.wifiApSsid, strlen(g_cfg.wifiApSsid));
    memcpy(wifi_config.ap.password, g_cfg.wifiApPassword, strlen(g_cfg.wifiApPassword));

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Перезапускаем captive DNS с новым IP
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            dns_captive_start(ip_info.ip.addr);
            ESP_LOGI(TAG, "Captive DNS restarted, responding with " IPSTR, IP2STR(&ip_info.ip));
        }

        // Перезапускаем mDNS
        ret = mdns_raw_init(ap_netif, g_cfg.hostname);
        if (ret == ESP_OK) {
            ret = mdns_raw_add_service(ap_netif, "_http", "tcp", 80,
                                       "Wireless Audio 2.1 Master");
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "mDNS restarted: http://%s.local", g_cfg.hostname);
            }
        }
    }

    ESP_LOGI(TAG, "Wi-Fi reconfigured: SSID=%s", g_cfg.wifiApSsid);
    return ESP_OK;
}

extern "C" void app_main(void)
{
    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализация NVS конфигурации
    ret = g_nvsConfig.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS config init failed: %s", esp_err_to_name(ret));
    }

    // Загрузка конфигурации из NVS
    ret = g_nvsConfig.load(g_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Using default config (no saved config in NVS)");
        g_cfg = NodeConfig();
    }
    g_volumePercent = g_cfg.volume;
    g_crossoverHz = g_cfg.crossoverHz;

    ESP_LOGI(TAG, "Wireless Audio 2.1 Master (ESP-IDF) starting...");
    ESP_LOGI(TAG, "Chip: ESP32-S3, FreeRTOS heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Loaded config: volume=%d%%, crossover=%d Hz, hostname=%s",
             g_cfg.volume, g_cfg.crossoverHz, g_cfg.hostname);

    // Инициализация DSP конвейера
    g_pipeline.configure((float)kSampleRate);
    g_pipeline.setCrossoverHz(g_cfg.crossoverHz);
    g_pipeline.setVolume(g_cfg.volume);
    g_jitter.setTargetMs(kJitterTargetMs, kSampleRate);
    g_limiter.setThreshold(0.95f);

    ESP_LOGI(TAG, "DSP pipeline initialized: sampleRate=%" PRIu32 " Hz, crossover=%d Hz, volume=%d%%",
             kSampleRate, g_cfg.crossoverHz, g_cfg.volume);

    // Инициализация Wi-Fi AP
    wifi_init_softap();

    // Этап 4: Captive DNS — все DNS-запросы отвечают softAP IP
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            dns_captive_start(ip_info.ip.addr);
            ESP_LOGI(TAG, "Captive DNS started, responding with " IPSTR, IP2STR(&ip_info.ip));
        }

        // Этап 5: mDNS — доступ по http://<hostname>.local
        ret = mdns_raw_init(ap_netif, g_cfg.hostname);
        if (ret == ESP_OK) {
            ret = mdns_raw_add_service(ap_netif, "_http", "tcp", 80,
                                       "Wireless Audio 2.1 Master");
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "mDNS: http://%s.local", g_cfg.hostname);
            }
        } else {
            ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "DNS/mDNS: could not get AP netif handle");
    }

    // Инициализация ESP-NOW транспорта
    ret = g_espnow.init(false); // без шифрования пока
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "ESP-NOW transport initialized");
    }

    // Инициализация I2S вывода
    I2sOutputPins i2sPins = {4, 5, 6}; // BCK=4, WS=5, DATA=6
    ret = g_i2sOut.init(i2sPins, kSampleRate, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(ret));
    } else {
        g_i2sOn = true;
        ESP_LOGI(TAG, "I2S output initialized");
    }

    // Инициализация UDP аудио приёма
    ret = g_udpAudio.init(kUdpAudioPort, g_cfg.udpAuthKey[0] ? g_cfg.udpAuthKey : nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UDP audio init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "UDP audio listener on port %u", (unsigned int)kUdpAudioPort);
    }

    // Инициализация Web сервера
    g_webServer.setCallbacks(&g_webCallbacks);
    ret = g_webServer.init(80);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Web server init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Web server started on port 80");
    }

    ESP_LOGI(TAG, "setup complete, entering main loop");

    // Инициализация watchdog (30 секунд, без паники при срабатывании)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = false,
    };
    esp_task_wdt_init(&twdt_config);
    // Подписать текущую задачу (app_main) на watchdog
    esp_task_wdt_add(NULL);

    // Основной цикл
    uint32_t loopCounter = 0;
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1) {
        loopCounter++;

        // Приём UDP аудио (неблокирующий, таймаут 10 мс)
        g_udpAudio.receive(10, &g_udpHandler);

        // Каждые 5 секунд логируем статус
        if (loopCounter % 100 == 0) {
            ESP_LOGI(TAG, "loop: heap=%" PRIu32 " bytes, counter=%" PRIu32,
                     esp_get_free_heap_size(), loopCounter);
        }

        // Сброс watchdog
        esp_task_wdt_reset();

        // Цикл примерно 10 мс (100 Гц)
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10));
    }
}
