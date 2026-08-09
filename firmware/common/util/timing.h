// timing.h — утилиты времени: millis-разность, тайм-ауты, дедлайны.
// Header-only.
#pragma once

#include <Arduino.h>

namespace audio21 {

// Безопасная разность millis() с учётом переполнения (uint32).
inline uint32_t elapsedSince(uint32_t start) {
    return static_cast<uint32_t>(millis() - start);
}

// Истёк ли дедлайн (учитывает переполнение millis).
inline bool deadlineExpired(uint32_t deadline, uint32_t now = millis()) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

// Простейший период-генератор: каждый вызов проверяет, прошло ли period_ms.
class Period {
public:
    explicit Period(uint32_t periodMs) : m_period(periodMs) {}

    void setPeriod(uint32_t ms) { m_period = ms; }
    uint32_t period() const { return m_period; }

    // true, когда период истёк; при срабатывании перезапускает отсчёт.
    bool tick() {
        uint32_t now = millis();
        if (!elapsedSince(m_last) >= m_period) return false;
        m_last = now;
        return true;
    }

    void reset() { m_last = millis(); }

private:
    uint32_t m_period;
    uint32_t m_last = 0;
};

// Фильтр экспоненциального сглаживания для телеметрии (ppm, fill и т.п.).
class ExponentialSmoother {
public:
    ExponentialSmoother(float alpha = 0.1f) : m_alpha(alpha) {}
    void setAlpha(float a) { m_alpha = a; }
    float update(float sample) {
        if (!m_initialized) {
            m_value = sample;
            m_initialized = true;
        } else {
            m_value = m_alpha * sample + (1.0f - m_alpha) * m_value;
        }
        return m_value;
    }
    float value() const { return m_value; }
    void reset() { m_initialized = false; m_value = 0.0f; }

private:
    float m_alpha;
    float m_value = 0.0f;
    bool m_initialized = false;
};

} // namespace audio21