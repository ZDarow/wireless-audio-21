// jitter_buffer.h — буфер для компенсации джиттера пакетной передачи.
// Header-only. Принимает пачки семплов (пакеты) и выдаёт ровный поток.
// Header-only, без зависимостей от железа — тестируется на хосте.
#pragma once

#include <stdint.h>
#include <string.h>
#include <new>
#include <stdexcept>

namespace audio21 {

// Простой FIFO-буфер с адаптивным целевым уровнем наполнения.
// Уровень в сэмплах: задержка джиттер-буфера = (targetLevel / sampleRate) мс.
// Если буфер пуст — выдаём тишину (0), если переполнен — отбрасываем старьё.
class JitterBuffer {
public:
    // Функция выделения памяти под буфер (по умолчанию new int16_t[]).
    // На ESP32-S3 можно передать аллокатор PSRAM, чтобы не расходовать
    // внутреннюю DRAM (см. psramAlloc в main.cpp мастера).
    using AllocFn = int16_t* (*)(size_t samples);

    // capacity — ёмкость в сэмплах (моно-канал одного сателлита).
    // alloc — необязательный аллокатор (например, ps_malloc из PSRAM на
    // ESP32-S3); если передан nullptr, используется new int16_t[].
    explicit JitterBuffer(uint32_t capacity, AllocFn alloc = nullptr)
        : m_buffer(nullptr), m_capacity(capacity), m_useDelete(false),
          m_targetLevel(capacity / 2), m_readIdx(0), m_writeIdx(0), m_count(0),
          m_overflowCount(0), m_overflowFlag(false) {
        if (alloc) {
            m_buffer = alloc(m_capacity);
            if (!m_buffer) {
                m_useDelete = false;
                m_buffer = nullptr;
            } else {
                m_useDelete = false; // память из внешнего пула — не delete[]
            }
        } else {
            m_buffer = new int16_t[m_capacity];
            m_useDelete = true;
        }
        // clear() вызовется только если буфер выделен успешно
        if (m_buffer) {
            clear();
        }
        // Дефолт целевого уровня — 50% ёмкости (B13): ready() осмыслен сразу
        // после создания без явной конфигурации. Иначе m_targetLevel=0 →
        // ready() всегда true → щелчки/прерывания на старте потока.
    }

    ~JitterBuffer() { if (m_useDelete && m_buffer) delete[] m_buffer; }

    bool allocated() const { return m_buffer != nullptr; }

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
                m_overflowCount++; // P17: считаем переполнения
                m_overflowFlag = true;
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

    // P17: количество переполнений буфера с момента создания/сброса.
    uint32_t overflowCount() const { return m_overflowCount; }

    // P17: был ли хотя бы один переполнение с момента последнего сброса флага.
    bool overflowOccurred() {
        bool occurred = m_overflowFlag;
        m_overflowFlag = false;
        return occurred;
    }

    void clear() {
        memset(m_buffer, 0, m_capacity * sizeof(int16_t));
        m_readIdx = 0;
        m_writeIdx = 0;
        m_count = 0;
    }

private:
    int16_t* m_buffer;
    uint32_t m_capacity;
    bool m_useDelete = true;  // буфер выделен new[] — освобождать delete[]
    uint32_t m_targetLevel = 0;
    uint32_t m_readIdx = 0;
    uint32_t m_writeIdx = 0;
    uint32_t m_count = 0;
    uint32_t m_overflowCount = 0; // P17: счётчик переполнений
    bool m_overflowFlag = false;  // P17: флаг переполнения (сбрасывается при чтении)
};

} // namespace audio21
