// drift_correction.h — C3.4: дрейф-коррекция для сателлита.
//
// Алгоритм: по меткам времени мастера (timestampMs) оцениваем смещение
// локальных часов, медианный фильтр 8 отсчётов, подстройка targetMs
// джиттер-буфера в диапазоне 15–50 мс.
//
// Header-only, без зависимостей от Arduino/ESP-IDF.

#ifndef DRIFT_CORRECTION_H
#define DRIFT_CORRECTION_H

#include <stdint.h>
#include <string.h>

namespace audio21 {

class DriftCorrector {
public:
    DriftCorrector()
        : m_idx(0), m_medianMs(0), m_lastAdjustMs(0) {
        memset(m_history, 0, sizeof(m_history));
    }

    void reset() {
        m_idx = 0;
        m_medianMs = 0;
        m_lastAdjustMedianMs = 0;
        m_lastAdjustMs = 0;
        memset(m_history, 0, sizeof(m_history));
    }

    // Обработать метку времени мастера (мс).
    // Возвращает подсказку по targetMs (15..50) или 0, если корректировка
    // не требуется.
    int32_t process(uint32_t masterTimestampMs, uint32_t nowMs) {
        int32_t offset = (int32_t)masterTimestampMs - (int32_t)nowMs;
        m_history[m_idx++] = offset;
        if (m_idx == kHistorySize) m_idx = 0;

        int32_t copy[kHistorySize];
        memcpy(copy, m_history, sizeof(copy));
        m_medianMs = medianInt32(copy, kHistorySize);

        if (abs(m_medianMs - m_lastAdjustMedianMs) > 10 && nowMs - m_lastAdjustMs > 1000) {
            m_lastAdjustMedianMs = m_medianMs;
            int target = 20 + m_medianMs / 10;
            if (target < 15) target = 15;
            if (target > 50) target = 50;
            m_lastAdjustMs = nowMs;
            return target;
        }
        return 0;
    }

    int32_t medianMs() const { return m_medianMs; }

private:
    static constexpr size_t kHistorySize = 8;
    static int32_t medianInt32(int32_t* arr, size_t n) {
        for (size_t i = 1; i < n; i++) {
            int32_t key = arr[i];
            size_t j = i;
            while (j > 0 && arr[j - 1] > key) {
                arr[j] = arr[j - 1];
                j--;
            }
            arr[j] = key;
        }
        return arr[n / 2];
    }

    int32_t m_history[kHistorySize];
    uint8_t m_idx;
    int32_t m_medianMs;
    int32_t m_lastAdjustMedianMs;
    uint32_t m_lastAdjustMs;
};

} // namespace audio21

#endif // DRIFT_CORRECTION_H
