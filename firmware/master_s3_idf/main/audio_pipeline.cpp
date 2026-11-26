#include "audio_pipeline.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"
#include "web_server.h"

static const char* TAG = "audio_pipeline";
static constexpr uint32_t kSampleRate = 48000;

int getStatusCallback(char* buf, size_t bufSize)
{
    int len = snprintf(buf, bufSize,
        "{\"sampleRate\":%" PRIu32 ",\"volume\":%d,\"crossoverHz\":%d,\"mute\":%s,"
        "\"i2sOn\":%s,\"jitterAvailable\":%" PRIu32 ",\"espNowSatellites\":2,"
        "\"satLeft\":\"%s\",\"satRight\":\"%s\",\"heartbeatsRx\":%" PRIu32 ","
        "\"hostname\":\"%s\",\"wifiApSsid\":\"%s\",\"wifiMode\":\"%s\","
        "\"ipAddress\":\"%s\"}",
        kSampleRate, g_cfg.masterVolume, g_cfg.crossoverHz, g_cfg.mute ? "true" : "false",
        g_i2sOn ? "true" : "false", g_jitter ? g_jitter->available() : 0,
        g_leftOnline ? "online" : "offline",
        g_rightOnline ? "online" : "offline",
        g_heartbeatsRx.load(),
        g_cfg.hostname, g_cfg.wifiApSsid,
        g_wifi_inited ? (g_staIpAddr[0] ? "sta" : "ap") : "ap",
        g_staIpAddr);
    return len;
}

esp_err_t setVolumeCallback(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    g_cfg.masterVolume = volume;
    g_volumePercent = volume;
    g_pipeline.setVolume(volume);
    // Сохраняем в NVS
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "volume set to %d%% (saved to NVS)", volume);
    return ESP_OK;
}

int getVolumeCallback(void)
{
    return g_cfg.masterVolume;
}

esp_err_t setMuteCallback(bool mute)
{
    g_cfg.mute = mute;
    g_pipeline.setMute(mute);
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "mute set to %s (saved to NVS)", mute ? "on" : "off");
    return ESP_OK;
}

bool getMuteCallback(void)
{
    return g_cfg.mute;
}

esp_err_t setToneCallback(float bass, float treble)
{
    g_pipeline.setTone(bass, treble);
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "tone set: bass=%.1f dB, treble=%.1f dB", bass, treble);
    return ESP_OK;
}

void getToneCallback(float* bass, float* treble)
{
    // PcmPipeline не хранит bass/treble отдельно — возвращаем 0
    if (bass) *bass = 0.0f;
    if (treble) *treble = 0.0f;
}

esp_err_t setChannelVolumesCallback(int left, int right, int sub)
{
    if (left < 0) left = 0;
    if (left > 100) left = 100;
    if (right < 0) right = 0;
    if (right > 100) right = 100;
    if (sub < 0) sub = 0;
    if (sub > 100) sub = 100;
    g_cfg.leftVolume = left;
    g_cfg.rightVolume = right;
    g_cfg.subVolume = sub;
    g_pipeline.setChannelVolumes(left, right, sub);
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "channel volumes set: L=%d R=%d Sub=%d", left, right, sub);
    return ESP_OK;
}

void getChannelVolumesCallback(int* left, int* right, int* sub)
{
    if (left) *left = g_cfg.leftVolume;
    if (right) *right = g_cfg.rightVolume;
    if (sub) *sub = g_cfg.subVolume;
}

esp_err_t wifi_reconfigure(void);

esp_err_t setDelayCallback(const char* channel, int delayMs)
{
    if (delayMs < 0) delayMs = 0;
    if (delayMs > 200) delayMs = 200;
    if (strcmp(channel, "left") == 0) {
        g_cfg.delayLeftMs = delayMs;
        g_delayLeft->setDelayMs((uint32_t)delayMs);
    } else if (strcmp(channel, "right") == 0) {
        g_cfg.delayRightMs = delayMs;
        g_delayRight->setDelayMs((uint32_t)delayMs);
    } else if (strcmp(channel, "sub") == 0) {
        g_cfg.delaySubMs = delayMs;
        g_delaySub->setDelayMs((uint32_t)delayMs);
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "delay %s set to %d ms (saved to NVS)", channel, delayMs);
    return ESP_OK;
}

int getDelayCallback(const char* channel)
{
    if (strcmp(channel, "left") == 0) return g_cfg.delayLeftMs;
    if (strcmp(channel, "right") == 0) return g_cfg.delayRightMs;
    if (strcmp(channel, "sub") == 0) return g_cfg.delaySubMs;
    return 0;
}

esp_err_t setCrossoverHzCallback(int hz)
{
    if (hz < 40) hz = 40;
    if (hz > 500) hz = 500;
    g_cfg.crossoverHz = hz;
    g_crossoverHz = hz;
    g_pipeline.setCrossoverHz(hz);
    // Сохраняем в NVS
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "crossover set to %d Hz (saved to NVS)", hz);
    return ESP_OK;
}

int getCrossoverHzCallback(void)
{
    return g_crossoverHz;
}


