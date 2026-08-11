// delay_line.h — линия задержки для int16_t PCM (выравнивание каналов).
// Header-only. Задержка задаётся в миллисекундах и пересчитывается в сэмплы.
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

namespace audio21 {

class DelayLine {
public:
    // capacityMs — максимальная задержка в мс (буфер выделяется под неё).
    // sampleRate — частота дискретизации.
    // externalBuffer — опциональный внешний буфер (например, из PSRAM через
    // ps_malloc): если задан, DelayLine НЕ владеет им и не освобождает его;
    // иначе буфер выделяется внутри (new) и освобождается в деструкторе.
    DelayLine(uint32_t capacityMs, uint32_t sampleRate, int16_t* externalBuffer = nullptr)
        : m_capacityMs(capacityMs), m_sampleRate(sampleRate) {
        m_capacitySamples = (capacityMs * sampleRate) / 1000;
        if (m_capacitySamples < 1) m_capacitySamples = 1;
        if (externalBuffer) {
            m_buffer = externalBuffer;
            m_ownsBuffer = false;
        } else {
            m_buffer = new int16_t[m_capacitySamples];
            m_ownsBuffer = true;
        }
        clear();
    }

    ~DelayLine() { if (m_ownsBuffer) delete[] m_buffer; }

    // Задать задержку в мс (0..capacityMs). Пересчитывается в сэмплы.
    void setDelayMs(uint32_t ms) {
        if (ms > m_capacityMs) ms = m_capacityMs;
        m_delaySamples = (ms * m_sampleRate) / 1000;
        if (m_delaySamples > m_capacitySamples) m_delaySamples = m_capacitySamples;
    }

    void setDelaySamples(uint32_t samples) {
        if (samples > m_capacitySamples) samples = m_capacitySamples;
        m_delaySamples = samples;
    }

    uint32_t delayMs() const { return (m_delaySamples * 1000) / m_sampleRate; }
    uint32_t delaySamples() const { return m_delaySamples; }

    // Пропустить один семпл через задержку.
    int16_t process(int16_t sample) {
        // Кольцевой буфер: читаем из ячейки, отстоящей на m_delaySamples
        // от точки записи, затем пишем новый семпл.
        size_t readIdx = (m_writeIdx + m_capacitySamples - m_delaySamples) % m_capacitySamples;
        int16_t out = m_buffer[readIdx];
        m_buffer[m_writeIdx] = sample;
        m_writeIdx++;
        if (m_writeIdx >= m_capacitySamples) m_writeIdx = 0;
        return out;
    }

    void clear() {
        memset(m_buffer, 0, m_capacitySamples * sizeof(int16_t));
        m_writeIdx = 0;
    }

private:
    int16_t* m_buffer;
    bool m_ownsBuffer = true;
    uint32_t m_capacitySamples;
    uint32_t m_capacityMs;
    uint32_t m_sampleRate;
    uint32_t m_delaySamples = 0;
    uint32_t m_writeIdx = 0;
};

} // namespace audio21