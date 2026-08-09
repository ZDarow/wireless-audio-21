// jitter_buffer.h — буфер для компенсации джиттера пакетной передачи.
// Header-only. Принимает пачки семплов (пакеты) и выдаёт ровный поток.
// Header-only, без зависимостей от железа — тестируется на хосте.
#pragma once

#include <stdint.h>
#include <string.h>

namespace audio21 {

// Простой FIFO-буфер с адаптивным целевым уровнем наполнения.
// Уровень в сэмплах: задержка джиттер-буфера = (targetLevel / sampleRate) мс.
// Если буфер пуст — выдаём тишину (0), если переполнен — отбрасываем старьё.
class JitterBuffer {
public:
    // capacity — ёмкость в сэмплах (моно-канал одного сателлита).
    explicit JitterBuffer(uint32_t capacity)
        : m_capacity(capacity) {
        m_buffer = new int16_t[m_capacity];
        clear();
    }

    ~JitterBuffer() { delete[] m_buffer; }

    // Целевой уровень наполнения в сэмплах (обычно 10–30 мс звука).
    void setTargetLevel(uint32_t samples) {
        if (samples > m_capacity) samples = m_capacity;
        m_targetLevel = samples;
    }

    // Уровень в миллисекундах при известной частоте.
    void setTargetMs(uint32_t ms, uint32_t sampleRate) {
        setTargetLevel((ms * sampleRate) / 1000);
    }

    // Записать пачку семплов (из одного пакета).
    void push(const int16_t* data, size_t n) {
        for (size_t i = 0; i < n; i++) {
            if (m_count == m_capacity) {
                // Переполнение: сдвигаем окно чтения — выкидываем самый старый
                // семпл и добавляем новый (overwrite).
                m_readIdx++;
                if (m_readIdx >= m_capacity) m_readIdx = 0;
                m_count--;
            }
            m_buffer[m_writeIdx] = data[i];
            m_writeIdx++;
            if (m_writeIdx >= m_capacity) m_writeIdx = 0;
            m_count++;
        }
    }

    // Прочитать один семпл. Возвращает true, если семпл реальный,
    // false — если буфер пуст (output не изменён).
    bool pop(int16_t& out) {
        if (m_count == 0) return false;
        out = m_buffer[m_readIdx];
        m_readIdx++;
        if (m_readIdx >= m_capacity) m_readIdx = 0;
        m_count--;
        return true;
    }

    // Число семплов в буфере.
    uint32_t available() const { return m_count; }

    // Число семплов до целевого уровня (сколько ещё нужно накопить).
    int32_t deficit() const { return static_cast<int32_t>(m_targetLevel) - static_cast<int32_t>(m_count); }

    // Полный или нет (по целевому уровню).
    bool ready() const { return m_count >= m_targetLevel; }

    void clear() {
        memset(m_buffer, 0, m_capacity * sizeof(int16_t));
        m_readIdx = 0;
        m_writeIdx = 0;
        m_count = 0;
    }

private:
    int16_t* m_buffer;
    uint32_t m_capacity;
    uint32_t m_targetLevel = 0;
    uint32_t m_readIdx = 0;
    uint32_t m_writeIdx = 0;
    uint32_t m_count = 0;
};

} // namespace audio21
