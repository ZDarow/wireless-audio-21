// i2s_output.cpp — реализация I2S драйвера.
#include "i2s_output.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

namespace audio21 {

static const char *TAG = "i2s_out";

I2sOutput::~I2sOutput()
{
    deinit();
}

esp_err_t I2sOutput::init(const I2sOutputPins& pins, uint32_t sampleRate, bool mono)
{
    if (m_initialized) {
        ESP_LOGW(TAG, "I2S already initialized");
        return ESP_OK;
    }

    m_sampleRate = sampleRate;

    // Конфигурация канала I2S
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, nullptr, &m_txHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Конфигурация стандартного I2S
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            mono ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT,
            mono ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // PCM5102A не нуждается в MCLK
            .bclk = static_cast<gpio_num_t>(pins.bck),
            .ws   = static_cast<gpio_num_t>(pins.ws),
            .dout = static_cast<gpio_num_t>(pins.data),
            .din  = I2S_GPIO_UNUSED,
        },
    };

    ret = i2s_channel_init_std_mode(m_txHandle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(m_txHandle);
        m_txHandle = nullptr;
        return ret;
    }

    ret = i2s_channel_enable(m_txHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(m_txHandle);
        m_txHandle = nullptr;
        return ret;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "I2S output initialized: %" PRIu32 " Hz, %s, pins BCK=%d WS=%d DATA=%d",
             (uint32_t)sampleRate, mono ? "mono" : "stereo", pins.bck, pins.ws, pins.data);
    return ESP_OK;
}

esp_err_t I2sOutput::deinit()
{
    if (!m_initialized || !m_txHandle) {
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_disable(m_txHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_disable failed: %s", esp_err_to_name(ret));
    }

    ret = i2s_del_channel(m_txHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_del_channel failed: %s", esp_err_to_name(ret));
    }

    m_txHandle = nullptr;
    m_initialized = false;
    ESP_LOGI(TAG, "I2S output deinitialized");
    return ESP_OK;
}

esp_err_t I2sOutput::write(const int16_t* data, size_t samples, uint32_t timeoutMs)
{
    if (!m_initialized || !m_txHandle || !data) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytesWritten = 0;
    esp_err_t ret = i2s_channel_write(m_txHandle, data, samples * sizeof(int16_t),
                                      &bytesWritten, pdMS_TO_TICKS(timeoutMs));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_write failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (bytesWritten != samples * sizeof(int16_t)) {
        ESP_LOGW(TAG, "partial write: %zu of %zu bytes", bytesWritten, samples * sizeof(int16_t));
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

} // namespace audio21
