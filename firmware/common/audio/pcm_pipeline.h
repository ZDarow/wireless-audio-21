// pcm_pipeline.h — DSP-конвейер мастера: volume → tone → limiter → splitter.
// Header-only. Принимает стерео int16, выдаёт:
//   - leftSat / rightSat (HPF-каналы для сателлитов)
//   - sub (LPF-моно для сабвуфера)
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <math.h>

#include "volume_control.h"
#include "crossover.h"

namespace audio21 {

// Простой темброблок: два биквада (low-shelf на басах, high-shelf на ВЧ).
// Реализует shelf-фильтры по AudioEQ Cookbook.
class ToneControl {
public:
    void configure(float bassDb, float trebleDb, float sampleRate) {
        m_bassDb = bassDb;
        m_trebleDb = trebleDb;
        m_sampleRate = sampleRate;
        computeShelf(m_bass, 250.0f, bassDb, sampleRate);    // низы ~250 Гц
        computeShelf(m_high, 4000.0f, trebleDb, sampleRate); // верхи ~4 кГц
        reset();
    }

    void reset() { m_bass.reset(); m_high.reset(); }

    float process(float x) {
        return m_high.process(m_bass.process(x));
    }

private:
    static void computeShelf(Biquad& b, float freq, float db, float sr) {
        float A = powf(10.0f, db / 40.0f);
        float w0 = 2.0f * PI * freq / sr;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / 0.7071f - 1.0f) + 2.0f);
        // low-shelf
        float b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtf(A) * alpha);
        float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
        float b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtf(A) * alpha);
        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtf(A) * alpha;
        float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
        float a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtf(A) * alpha;
        b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0;
        b.a1 = a1 / a0; b.a2 = a2 / a0;
    }

    Biquad m_bass, m_high;
    float m_bassDb = 0.0f, m_trebleDb = 0.0f, m_sampleRate = 44100.0f;
};

// Ограничитель пиков (soft limiter) — защита от клиппинга.
class PeakLimiter {
public:
    void setThreshold(float thresh) { m_thresh = thresh; } // 0..1
    float process(float x) {
        float ax = fabsf(x);
        if (ax <= m_thresh) return x;
        // мягкое ограничение выше порога
        float over = ax - m_thresh;
        float sign = (x < 0.0f) ? -1.0f : 1.0f;
        float y = m_thresh + over * (1.0f / (1.0f + over));
        return sign * y;
    }

private:
    float m_thresh = 0.98f;
};

// Полный конвейер мастера.
struct PipelineOutput {
    float left = 0.0f;   // HPF L → сателлит левый
    float right = 0.0f;  // HPF R → сателлит правый
    float sub = 0.0f;    // LPF моно → сабвуфер
};

class PcmPipeline {
public:
    void configure(float sampleRate) {
        m_sampleRate = sampleRate;
        m_tone.configure(0.0f, 0.0f, sampleRate);
        // Linkwitz-Riley 4-го порядка (24 дБ/октаву) — ровный суммарный
        // отклик на частоте раздела (спецификация §6.10).
        m_lpf.configure(CrossoverKind::LowPass, CrossoverType::LinkwitzRiley, 4,
                        m_crossoverHz, sampleRate);
        m_hpf.configure(CrossoverKind::HighPass, CrossoverType::LinkwitzRiley, 4,
                        m_crossoverHz, sampleRate);
        m_volume.resetToSilence();
        m_volLeft.resetToSilence();
        m_volRight.resetToSilence();
        m_volSub.resetToSilence();
    }

    void setCrossoverHz(int hz) {
        m_crossoverHz = hz;
        m_lpf.configure(CrossoverKind::LowPass, CrossoverType::LinkwitzRiley, 4,
                        m_crossoverHz, m_sampleRate);
        m_hpf.configure(CrossoverKind::HighPass, CrossoverType::LinkwitzRiley, 4,
                        m_crossoverHz, m_sampleRate);
    }

    void setVolume(int v) { m_volume.setVolume(v); }
    void setMute(bool m) { m_volume.setMute(m); }
    void setTone(float bassDb, float trebleDb) { m_tone.configure(bassDb, trebleDb, m_sampleRate); }

    // Покомпонентные громкости каналов (C2.2, ТЗ §7.5): left/right/sub — 0..100,
    // применяются ПОСЛЕ кроссовера (каждый канал независимо от общего master).
    void setChannelVolumes(int left, int right, int sub) {
        m_volLeft.setVolume(left);
        m_volRight.setVolume(right);
        m_volSub.setVolume(sub);
    }

    // Обработка стереопары int16 → три выхода (float -1..1).
    PipelineOutput process(int16_t inL, int16_t inR) {
        float l = inL / 32768.0f;
        float r = inR / 32768.0f;

        // tone
        l = m_tone.process(l);
        r = m_tone.process(r);

        // volume + mute (с плавным фейдом)
        l = m_volume.process(l);
        r = m_volume.process(r);

        // limiter (per-channel) — ПОСЛЕ volume: финальная защита от клиппинга
        // на выходе (B15), т.к. volume может усилить сигнал до 1.0.
        l = m_limiter.process(l);
        r = m_limiter.process(r);

        PipelineOutput out;
        out.left = m_hpf.process(l);
        out.right = m_hpf.process(r);
        out.sub = m_lpf.process((l + r) * 0.5f); // моно-микс для сабвуфера
        // Канальные громкости (C2.2) — после кроссовера, с плавным фейдом.
        out.left = m_volLeft.process(out.left);
        out.right = m_volRight.process(out.right);
        out.sub = m_volSub.process(out.sub);
        return out;
    }

    void reset() {
        m_tone.reset();
        m_lpf.reset();
        m_hpf.reset();
        m_volume.resetToSilence();
        m_volLeft.resetToSilence();
        m_volRight.resetToSilence();
        m_volSub.resetToSilence();
    }

private:
    float m_sampleRate = 44100.0f;
    int m_crossoverHz = 90;
    ToneControl m_tone;
    PeakLimiter m_limiter;
    VolumeControl m_volume;
    VolumeControl m_volLeft;   // C2.2: канальные громкости
    VolumeControl m_volRight;
    VolumeControl m_volSub;
    Crossover m_lpf;
    Crossover m_hpf;
};

} // namespace audio21