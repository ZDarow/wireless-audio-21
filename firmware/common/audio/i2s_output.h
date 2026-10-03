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
    // При ошибке возвращает false (вызов ESP.restart() оставлен только как
    // явный fallback через onInitFailure — решение о перезагрузке принимает
    // приложение, а не библиотека).
    using InitFailHandler = void (*)();

    bool init(const I2sOutputPins& pins, uint32_t sampleRate, bool mono,
              InitFailHandler onInitFailure = nullptr) {
        auto fail = [&](const char* msg) -> bool {
            Logger::error("audio", "%s", msg);
            if (onInitFailure) onInitFailure();
            return false;
        };
        m_mono = mono;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        if (i2s_new_channel(&chanCfg, &m_tx, nullptr) != ESP_OK) {
            return fail("I2S new_channel failed");
        }

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
                .invert_flags = {},
            },
        };
        if (i2s_channel_init_std_mode(m_tx, &stdCfg) != ESP_OK) {
            return fail("I2S init_std_mode failed");
        }
        if (i2s_channel_enable(m_tx) != ESP_OK) {
            return fail("I2S channel_enable failed");
        }
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

        if (i2s_driver_install(I2S_NUM_0, &conf, 0, nullptr) != ESP_OK) {
            return fail("I2S driver_install failed");
        }
        if (i2s_set_pin(I2S_NUM_0, &pinsCfg) != ESP_OK) {
            return fail("I2S set_pin failed");
        }
        m_port = I2S_NUM_0;
#endif
        m_initialized = true;
        return true;
    }

    // n — число моно-сэмплов (mono) или стерео-пар (stereo).
    // timeoutMs — конечный таймаут записи: если DMA-буфер забит (например,
    // остановлена тактовая), запись прерывается вместо вечной блокировки
    // loop() и watchdog-panic. Дефолт 100 мс.
    void write(const int16_t* samples, size_t n, uint32_t timeoutMs = 100) {
        if (!m_initialized) return; // guard: не писать в неинициализированный I2S
        if (m_mono) {
            // Блочное дублирование L=R с одним writeRaw на чанк: раньше
            // каждый семпл отправлялся отдельным 4-байтовым трансфером.
            while (n > 0) {
                size_t chunk = n > kMaxWrite ? kMaxWrite : n;
                int16_t frame[2 * kMaxWrite];
                for (size_t i = 0; i < chunk; i++) {
                    frame[2 * i] = samples[i];
                    frame[2 * i + 1] = samples[i];
                }
                writeRaw(frame, 2 * chunk, timeoutMs);
                samples += chunk;
                n -= chunk;
            }
        } else {
            while (n > 0) {
                size_t chunk = n > kMaxWrite ? kMaxWrite : n;
                writeRaw(samples, chunk * 2, timeoutMs);
                samples += chunk * 2;
                n -= chunk;
            }
        }
    }

    // Активность без реального потока — тишина.
    void silence(size_t nFrames, uint32_t timeoutMs = 100) {
        if (!m_initialized) return; // guard
        while (nFrames > 0) {
            size_t chunk = nFrames > kMaxWrite ? kMaxWrite : nFrames;
            int16_t zeros[kMaxWrite] = {};
            write(zeros, chunk, timeoutMs);
            nFrames -= chunk;
        }
    }

private:
    static constexpr size_t kMaxWrite = 512; // максимум моно-семплов за чанк write()

    void writeRaw(const int16_t* samples, size_t n, uint32_t timeoutMs) {
        size_t bytes = n * sizeof(int16_t);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(samples);
        // Конечный таймаут вместо portMAX_DELAY, чтобы зависший I2S
        // не останавливал loop().
        TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
        while (bytes > 0) {
            size_t written = 0;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
            if (i2s_channel_write(m_tx, p, bytes, &written, ticks) != ESP_OK) return;
#else
            if (i2s_write(m_port, p, bytes, &written, ticks) != ESP_OK) return;
#endif
            if (written == 0) return; // защита от бесконечного цикла
            p += written;
            bytes -= written;
        }
    }

    bool m_mono = true;
    bool m_initialized = false;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    i2s_chan_handle_t m_tx = nullptr;
#else
    i2s_port_t m_port = I2S_NUM_0;
#endif
};

} // namespace audio21
#endif // ESP32 && ARDUINO
