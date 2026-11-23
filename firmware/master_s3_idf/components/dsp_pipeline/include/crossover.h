// crossover.h — цифровой кроссовер: LPF (сабвуфер) и HPF (сателлиты).
// Header-only. Реализует биквады (Direct Form I) для Butterworth и
// Linkwitz-Riley фильтров 2-го и 4-го порядка.
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <math.h>

namespace audio21 {

enum class CrossoverType : uint8_t { Butterworth = 0, LinkwitzRiley = 1 };
enum class CrossoverKind : uint8_t { LowPass = 0, HighPass = 1 };

// Один биквад (Direct Form I), коэффициенты в формате Audio EQ Cookbook.
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f; // state

    void reset() { z1 = z2 = 0.0f; }

    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Кроссовер: цепочка из 1..2 каскадных биквадов.
// order: 2 (1 биквад) или 4 (2 каскада).
class Crossover {
public:
    static constexpr int kMaxStages = 2;

    // Настроить фильтр.
    // kind: LowPass / HighPass
    // type: Butterworth / LinkwitzRiley
    // order: 2 или 4
    // freqHz: частота раздела
    // sampleRate: частота дискретизации
    void configure(CrossoverKind kind, CrossoverType type, int order,
                   float freqHz, float sampleRate) {
        m_kind = kind;
        m_type = type;
        m_order = (order == 4) ? 4 : 2;
        m_stageCount = m_order / 2; // 2→1, 4→2
        m_freqHz = freqHz;
        m_sampleRate = sampleRate;

        if (m_type == CrossoverType::LinkwitzRiley) {
            // LR = два одинаковых Butterworth, каскадированные.
            // LR2 = BW1^2 (Q=0.5), LR4 = BW2^2.
            int bwOrder = (m_order == 4) ? 2 : 1;
            float q = (bwOrder == 2) ? 0.7071f : 0.5f; // BW2 Q, BW1 ~ Q0.5
            for (int i = 0; i < m_stageCount; i++) {
                computeBiquad(m_stages[i], kind, freqHz, sampleRate, q);
            }
        } else {
            // Butterworth: для 2-го порядка Q=0.7071; для 4-го — два каскада
            // с разными Q (0.5412 и 1.3066).
            if (m_order == 4) {
                const float q4[2] = {0.5412f, 1.3066f};
                for (int i = 0; i < 2; i++) {
                    computeBiquad(m_stages[i], kind, freqHz, sampleRate, q4[i]);
                }
            } else {
                computeBiquad(m_stages[0], kind, freqHz, sampleRate, 0.7071f);
            }
        }
        reset();
    }

    void reset() {
        for (int i = 0; i < kMaxStages; i++) m_stages[i].reset();
    }

    float process(float x) {
        float y = x;
        for (int i = 0; i < m_stageCount; i++) {
            y = m_stages[i].process(y);
        }
        return y;
    }

    CrossoverKind kind() const { return m_kind; }
    CrossoverType type() const { return m_type; }
    int order() const { return m_order; }
    float freqHz() const { return m_freqHz; }

private:
    // Расчёт коэффициентов биквада по AudioEQ Cookbook.
    static void computeBiquad(Biquad& b, CrossoverKind kind, float freqHz,
                              float sampleRate, float q) {
        float w0 = 2.0f * PI * freqHz / sampleRate;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float alpha = sinw0 / (2.0f * q);

        if (kind == CrossoverKind::LowPass) {
            b.b0 = (1.0f - cosw0) / 2.0f;
            b.b1 = 1.0f - cosw0;
            b.b2 = (1.0f - cosw0) / 2.0f;
        } else { // HighPass
            b.b0 = (1.0f + cosw0) / 2.0f;
            b.b1 = -(1.0f + cosw0);
            b.b2 = (1.0f + cosw0) / 2.0f;
        }
        float a0 = 1.0f + alpha;
        b.a1 = -2.0f * cosw0 / a0;
        b.a2 = (1.0f - alpha) / a0;
        b.b0 /= a0;
        b.b1 /= a0;
        b.b2 /= a0;
    }

    Biquad m_stages[kMaxStages];
    CrossoverKind m_kind = CrossoverKind::LowPass;
    CrossoverType m_type = CrossoverType::Butterworth;
    int m_order = 2;
    int m_stageCount = 1;
    float m_freqHz = 90.0f;
    float m_sampleRate = 44100.0f;
};

} // namespace audio21