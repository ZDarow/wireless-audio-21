// i2s_output.h — общий I2S-выход узлов (мастер/сателлит), ТЗ §6.3, T14.
// Header-only. Поддерживает Arduino core 2.x (legacy driver/i2s.h) и 3.x
// (driver/i2s_std.h) через guard по ESP_ARDUINO_VERSION_MAJOR.
//
// write() принимает МОНО-сэмплы и дублирует их в стерео-фреймы (L=R).
// stereo-режим (mono=false) интерпретирует вход как пары {L, R}.
#pragma once

#include <stdint.h>
#include <string.h>

#if defined(ESP32) && defined(ARDUINO)
#include <Arduino.h>

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#include "driver/i2s_std.h"
#else
#include "driver/i2s.h"
#endif

namespace audio21 {

struct I2sOutputPins {
    int bck;
    int ws;
    int data;
};

class I2sOutput {
public:
    // init: инициализация I2S в master-TX режиме. mono=true — write() принимает
    // моно-сэмплы и дублирует в стерео; mono=false — write() принимает пары {L,R}.
    bool init(const I2sOutputPins& pins, uint32_t sampleRate, bool mono) {
        m_mono = mono;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        if (i2s_new_channel(&chanCfg, &m_tx, nullptr) != ESP_OK) return false;

        i2s_std_config_t stdCfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                            I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = static_cast<gpio_num_t>(pins.bck),
                .ws = static_cast<gpio_num_t>(pins.ws),
                .dout = static_cast<gpio_num_t>(pins.data),
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {0, 0, 0},
            },
        };
        if (i2s_channel_init_std_mode(m_tx, &stdCfg) != ESP_OK) return false;
        if (i2s_channel_enable(m_tx) != ESP_OK) return false;
#else
        i2s_config_t conf = {};
        conf.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
        conf.sample_rate = sampleRate;
        conf.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        conf.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; // стерео-фрейм
        conf.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        conf.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
        conf.dma_buf_count = 8;
        conf.dma_buf_len = 256;
        conf.use_apll = false;
        conf.tx_desc_auto_clear = true;

        i2s_pin_config_t pinsCfg = {};
        pinsCfg.bck_io_num = pins.bck;
        pinsCfg.ws_io_num = pins.ws;
        pinsCfg.data_out_num = pins.data;
        pinsCfg.data_in_num = I2S_PIN_NO_CHANGE;

        if (i2s_driver_install(I2S_NUM_0, &conf, 0, nullptr) != ESP_OK) return false;
        if (i2s_set_pin(I2S_NUM_0, &pinsCfg) != ESP_OK) return false;
        m_port = I2S_NUM_0;
#endif
        return true;
    }

    // n — число моно-сэмплов (mono) или стерео-пар (stereo).
    void write(const int16_t* samples, size_t n) {
        if (m_mono) {
            int16_t frame[2];
            for (size_t i = 0; i < n; i++) {
                frame[0] = samples[i];
                frame[1] = samples[i];
                writeRaw(frame, 2);
            }
        } else {
            writeRaw(samples, n * 2);
        }
    }

    // Активность без реального потока — тишина.
    void silence(size_t nFrames) {
        int16_t zero = 0;
        for (size_t i = 0; i < nFrames; i++) write(&zero, 1);
    }

private:
    void writeRaw(const int16_t* samples, size_t n) {
        size_t bytes = n * sizeof(int16_t);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(samples);
        while (bytes > 0) {
            size_t written = 0;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
            if (i2s_channel_write(m_tx, p, bytes, &written, portMAX_DELAY) != ESP_OK) return;
#else
            if (i2s_write(m_port, p, bytes, &written, portMAX_DELAY) != ESP_OK) return;
#endif
            p += written;
            bytes -= written;
        }
    }

    bool m_mono = true;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    i2s_chan_handle_t m_tx = nullptr;
#else
    i2s_port_t m_port = I2S_NUM_0;
#endif
};

} // namespace audio21
#endif // ESP32 && ARDUINO
