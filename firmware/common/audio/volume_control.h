// volume_control.h — управление громкостью и мягкий фейд (анти-клик).
// Header-only, работает с int16_t PCM.
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace audio21 {

// Громкость в линейной шкале с плавным фейдом.
// - volume: 0..100 (0 = тишина, 100 = 0 дБ).
// - fadeStep: приращение коэффициента на каждый вызов process (анти-клик).
class VolumeControl {
public:
    explicit VolumeControl(float fadeStep = 0.02f) : m_fadeStep(fadeStep) {}

    void setVolume(int volume) {
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
        m_target = volumeToLinear(volume);
    }

    int volume() const { return linearToVolume(m_current); }

    void setMute(bool mute) { m_muted = mute; }
    bool isMuted() const { return m_muted; }

    void setFadeStep(float step) { m_fadeStep = step; }

    // Обработка одного семпла (моно). Возвращает семпл с применённой громкостью.
    int16_t process(int16_t sample) {
        float g = fade();
        float out = static_cast<float>(sample) * g;
        // Ограничение по int16
        if (out > 32767.0f) out = 32767.0f;
        if (out < -32768.0f) out = -32768.0f;
        return static_cast<int16_t>(out);
    }

    // Обработка одного семпла в float (-1..1). Возвращает float с громкостью.
    // Используется DSP-конвейером, где сигнал уже в float.
    float process(float sample) {
        float g = fade();
        float out = sample * g;
        // Ограничение по диапазону float -1..1
        if (out > 1.0f) out = 1.0f;
        if (out < -1.0f) out = -1.0f;
        return out;
    }

    // Обработка стереопары (L, R).
    void process(int16_t& l, int16_t& r) {
        l = process(l);
        r = process(r);
    }

    // Быстрый сброс к 0 (например, при старте — плавный fade-in из тишины).
    void resetToSilence() { m_current = 0.0f; }

private:
    // Плавный фейд к целевому значению — убирает щелчки при скачках громкости.
    // Возвращает текущий коэффициент громкости (0..1).
    float fade() {
        if (m_current < m_target) {
            m_current += m_fadeStep;
            if (m_current > m_target) m_current = m_target;
        } else if (m_current > m_target) {
            m_current -= m_fadeStep;
            if (m_current < m_target) m_current = m_target;
        }
        return m_muted ? 0.0f : m_current;
    }
    // Линейная шкала: 0..100 → 0.0..1.0. Кривая близка к воспринимаемой громкости.
    static float volumeToLinear(int v) {
        if (v <= 0) return 0.0f;
        if (v >= 100) return 1.0f;
        // sqrt-кривая: больше разрешения на малых уровнях.
        return sqrtf(static_cast<float>(v) / 100.0f);
    }
    static int linearToVolume(float g) {
        if (g <= 0.0f) return 0;
        if (g >= 1.0f) return 100;
        return static_cast<int>(g * g * 100.0f + 0.5f);
    }

    float m_current = 0.0f;  // текущий коэффициент (плавно дрейфует)
    float m_target = 0.0f;   // целевой коэффициент
    float m_fadeStep;
    bool m_muted = false;
};

} // namespace audio21