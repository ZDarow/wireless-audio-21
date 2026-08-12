// audio_filter_test.cpp — host-тест DSP-модулей (кроссовер, громкость, pipeline).
// Компилируется чистым gcc без Arduino: модули header-only и не зависят от железа.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Заглушка Arduino.h для host-компиляции.
#include "arduino_stub.h"

#include "crossover.h"
#include "volume_control.h"
#include "pcm_pipeline.h"
#include "delay_line.h"
#include "jitter_buffer.h"

using namespace audio21;

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_failures++; } \
} while (0)

// Среднеквадратичный уровень сигнала.
static float rmsLevel(const float* data, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += data[i] * data[i];
    return sqrtf(sum / (float)n);
}

// --- Кроссовер: LPF пропускает низкие частоты, режет высокие ---
// Проверяем конфигурацию, которую использует PcmPipeline: Linkwitz-Riley 4-го
// порядка (24 дБ/октаву) на частоте раздела 90 Гц.
static void test_crossover_frequency_response() {
    const float sampleRate = 44100.0f;
    const int N = 44100; // 1 секунда
    float lpfLow = 0.0f, lpfHigh = 0.0f, hpfLow = 0.0f, hpfHigh = 0.0f;

    auto runFilter = [&](CrossoverKind kind, float freq) -> float {
        Crossover xo;
        xo.configure(kind, CrossoverType::LinkwitzRiley, 4, 90.0f, sampleRate);
        float* buf = new float[N];
        for (int i = 0; i < N; i++) {
            float t = (float)i / sampleRate;
            buf[i] = xo.process(0.8f * sinf(2.0f * PI * freq * t));
        }
        float level = rmsLevel(buf, N);
        delete[] buf;
        return level;
    };

    lpfLow = runFilter(CrossoverKind::LowPass, 30.0f);
    lpfHigh = runFilter(CrossoverKind::LowPass, 5000.0f);
    hpfLow = runFilter(CrossoverKind::HighPass, 30.0f);
    hpfHigh = runFilter(CrossoverKind::HighPass, 5000.0f);

    printf("  LPF @30Hz=%.4f, LPF @5kHz=%.4f, HPF @30Hz=%.4f, HPF @5kHz=%.4f\n",
           lpfLow, lpfHigh, hpfLow, hpfHigh);

    // Низкая частота должна проходить через LPF и резаться HPF.
    CHECK(lpfLow > 0.2f, "LPF должен пропускать низкие частоты (30 Гц)");
    CHECK(hpfLow < 0.05f, "HPF должен резать низкие частоты (30 Гц)");
    // Высокая частота должна резаться LPF и проходить HPF.
    CHECK(lpfHigh < 0.05f, "LPF должен резать высокие частоты (5 кГц)");
    CHECK(hpfHigh > 0.2f, "HPF должен пропускать высокие частоты (5 кГц)");
}

// --- Громкость: шкала 0..100, mute ---
static void test_volume() {
    VolumeControl vol(0.5f); // быстрый фейд для теста
    const int16_t kTestSample = 1000;
    vol.setVolume(100);
    int16_t a = vol.process(kTestSample);
    // При первом вызове фейд ещё идёт от 0 к 1.0, но с шагом 0.5 быстро сойдётся.
    for (int i = 0; i < 10; i++) a = vol.process(kTestSample);
    CHECK(a > 900, "volume 100 ≈ passthrough");

    vol.setVolume(0);
    for (int i = 0; i < 10; i++) a = vol.process(kTestSample);
    CHECK(a == 0, "volume 0 = тишина");

    vol.setVolume(50);
    for (int i = 0; i < 50; i++) a = vol.process(kTestSample);
    CHECK(a > 0 && a < 1000, "volume 50 = промежуточное значение");

    vol.setMute(true);
    a = vol.process(kTestSample);
    CHECK(a == 0, "mute = тишина");
}

// --- Задержка: выход = вход, отложенный на delayMs ---
static void test_delay_line() {
    const uint32_t sampleRate = 44100;
    DelayLine dl(200, sampleRate); // до 200 мс
    dl.setDelayMs(10);             // 10 мс = 441 сэмпла

    // Подаём импульс, проверяем, что он появится на выходе через 441 сэмпла.
    int delaySamples = dl.delaySamples();
    CHECK(delaySamples == 441, "10 мс при 44100 Гц = 441 сэмпл");

    int outIdx = -1;
    for (int i = 0; i < delaySamples + 20; i++) {
        int16_t in = (i == 0) ? 1000 : 0;
        int16_t out = dl.process(in);
        if (out == 1000 && outIdx < 0) outIdx = i;
    }
    CHECK(outIdx == delaySamples, "импульс задержан ровно на delaySamples");
}

// --- Jitter buffer: накопление и ровная выдача ---
static void test_jitter_buffer() {
    JitterBuffer jb(4410); // ~100 мс при 44100
    jb.setTargetLevel(441); // ~10 мс

    // Пока буфер не накопил целевой уровень — не готов.
    CHECK(!jb.ready(), "буфер не готов до накопления");

    int16_t in[441];
    for (int i = 0; i < 441; i++) in[i] = (int16_t)(i + 1);
    jb.push(in, 441);
    CHECK(jb.ready(), "буфер готов после накопления целевого уровня");

    // Выдача идемпотентна по порядку.
    int16_t out = 0;
    CHECK(jb.pop(out) && out == 1, "первый семпл = 1");
    CHECK(jb.pop(out) && out == 2, "второй семпл = 2");

    // Переполнение не ломает буфер.
    for (int k = 0; k < 3; k++) jb.push(in, 441);
    int16_t last = 0;
    while (jb.pop(out)) last = out;
    printf("  jitter last=%d\n", last);
}

// --- Jitter buffer: дефицит (pop из пустого буфера) ---
static void test_jitter_buffer_underflow() {
    JitterBuffer jb(100);
    jb.setTargetLevel(50);

    CHECK(jb.available() == 0, "пустой буфер: 0 семплов");
    CHECK(jb.deficit() == 50, "дефицит = целевой уровень");
    CHECK(!jb.ready(), "пустой буфер не готов");

    int16_t out = 12345;
    CHECK(!jb.pop(out), "pop из пустого буфера -> false");
    CHECK(out == 12345, "output не изменён при пустом буфере");
    CHECK(jb.available() == 0, "после pop из пустого — по-прежнему 0");

    // Частичное наполнение: дефицит уменьшается, но буфер ещё не готов.
    int16_t in[30];
    memset(in, 0, sizeof(in));
    jb.push(in, 30);
    CHECK(jb.available() == 30, "после push 30 семплов");
    CHECK(jb.deficit() == 20, "дефицит = 50 - 30");
    CHECK(!jb.ready(), "30 < 50 — не готов");
}

// --- Jitter buffer: переполнение (overwrite самых старых семплов) ---
static void test_jitter_buffer_overflow() {
    JitterBuffer jb(10); // маленькая ёмкость для наглядности
    jb.setTargetLevel(5);

    // Кладём 20 семплов при ёмкости 10: должны остаться последние 10.
    int16_t in[20];
    for (int i = 0; i < 20; i++) in[i] = (int16_t)(i + 1);
    jb.push(in, 20);

    CHECK(jb.available() == 10, "после переполнения остаётся ровно capacity");
    CHECK(jb.ready(), "буфер готов (10 >= 5)");

    // Первый выданный семпл — 11-й (первые 10 перезаписаны).
    int16_t out = 0;
    CHECK(jb.pop(out) && out == 11, "overwrite: старые семплы отброшены");
    CHECK(jb.pop(out) && out == 12, "второй семпл после overwrite = 12");

    // Вычитываем всё до конца: 20-й семпл — последний.
    while (jb.pop(out)) {}
    CHECK(out == 20, "последний семпл = 20");
    CHECK(jb.available() == 0, "буфер полностью вычитан");
}

// --- Jitter buffer: дефолтный целевой уровень (B13) ---
static void test_jitter_buffer_default_target() {
    JitterBuffer jb(100);
    CHECK(!jb.ready(), "без конфигурации пустой буфер не готов (дефолт target>0)");
    CHECK(jb.deficit() > 0, "дефолтный дефицит > 0");
    int16_t in[60];
    memset(in, 0, sizeof(in));
    jb.push(in, 60);
    CHECK(jb.ready(), "после наполнения > 50% буфер готов");
}

// --- Pipeline: громкость + кроссовер, выходы не NaN ---
static void test_pipeline() {
    PcmPipeline pipe;
    pipe.configure(44100);
    pipe.setVolume(80);
    pipe.setCrossoverHz(90);

    for (int i = 0; i < 1000; i++) {
        PipelineOutput out = pipe.process(1000, 1000);
        CHECK(isfinite(out.left), "left не NaN");
        CHECK(isfinite(out.right), "right не NaN");
        CHECK(isfinite(out.sub), "sub не NaN");
    }
}

int main() {
    printf("== audio_filter_test ==\n");
    test_crossover_frequency_response();
    test_volume();
    test_delay_line();
    test_jitter_buffer();
    test_jitter_buffer_underflow();
    test_jitter_buffer_overflow();
    test_jitter_buffer_default_target();
    test_pipeline();

    if (g_failures) {
        printf("ИТОГ: %d ПРОВАЛОВ\n", g_failures);
        return 1;
    }
    printf("ИТОГ: ВСЕ ТЕСТЫ ПРОЙДЕНЫ\n");
    return 0;
}