#include "wifi_manager.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "espnow_transport.h"
#include "audio_packet.h"
#include "web_server.h"

static const char* TAG = "wifi_manager";

// Forward declarations for callbacks defined in main.cpp
extern esp_err_t getStatusCallback(char* buf, size_t bufSize);
extern esp_err_t setDelayCallback(const char* channel, int delayMs);
extern esp_err_t setMuteCallback(bool mute);
extern esp_err_t setVolumeCallback(int volume);
extern esp_err_t setToneCallback(float bass, float treble);
extern esp_err_t setChannelVolumesCallback(int left, int right, int sub);
extern esp_err_t setCrossoverHzCallback(int hz);
extern esp_err_t netInternetCheckCallback(void);

// Wi-Fi init and reconfigure implementations are in main.cpp (complex, depend on
// many globals). This file provides the header declarations and will gradually
// absorb implementations as refactoring progresses.
// For now, the implementations remain in main.cpp to avoid breaking the build.

