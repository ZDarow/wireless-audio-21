// i2s_output.h — I2S драйвер для вывода PCM на PCM5102A (сабвуфер, моно).
// Использует esp_driver_i2s из ESP-IDF v5.3.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "driver/i2s_std.h"
#include "esp_err.h"

namespace audio21 {

struct I2sOutputPins {
    int bck;   // GPIO для BCK (например, 4)
    int ws;    // GPIO для WS (например, 5)
    int data;  // GPIO для DATA (например, 6)
};

class I2sOutput {
public:
    I2sOutput() = default;
    ~I2sOutput();

    // Инициализация I2S в режиме master TX, моно, 16-bit, I2S формат.
    // sampleRate — частота дискретизации (44100 или 48000).
    esp_err_t init(const I2sOutputPins& pins, uint32_t sampleRate, bool mono = true);

    // Деинициализация
    esp_err_t deinit();

    // Запись семплов в I2S DMA буфер.
    // data — массив int16_t, samples — количество семплов.
    // Возвращает ESP_OK при успехе, ESP_ERR_TIMEOUT при таймауте.
    esp_err_t write(const int16_t* data, size_t samples, uint32_t timeoutMs = 100);

    bool isInitialized() const { return m_initialized; }

private:
    i2s_chan_handle_t m_txHandle = nullptr;
    bool m_initialized = false;
    uint32_t m_sampleRate = 48000;
};

} // namespace audio21
