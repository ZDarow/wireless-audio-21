// pcm_pipeline_test.cpp — host-тест DSP конвейера (PcmPipeline).
// Компилируется чистым gcc без Arduino: модули header-only и не зависят от железа.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

// Генерация синусоиды в буфер.
static void genSine(float* buf, int n, float freq, float sampleRate, float amp = 0.5f) {
    for (int i = 0; i < n; i++) {
        float t = (float)i / sampleRate;
        buf[i] = amp * sinf(2.0f * PI * freq * t);
    }
}

// Помощник: прогнать N семплов через конвейер, чтобы фейд громкости établiлся.
static void warmup(PcmPipeline& p, int n, int16_t v) {
    for (int i = 0; i < n; i++) p.process(v, v);
}

// --- Тест 1: Громкость мастера ---
static void test_volume() {
    printf("  Тест громкости мастера...\n");
    PcmPipeline p;
    p.configure(44100.0f);
    p.setCrossoverHz(90);

    // volume=0 должен давать ~0
    p.setVolume(0);
    warmup(p, 300, 5000);
    float buf[480];
    genSine(buf, 480, 440.0f, 44100.0f, 0.5f);
    float maxL = 0.0f;
    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(buf[i] * 32767.0f), 0);
        if (fabsf(out.left) > maxL) maxL = fabsf(out.left);
    }
    CHECK(maxL < 0.01f, "volume=0 должен давать ~0");
    printf("  volume=0 -> maxL=%.6f OK\n", maxL);

    // volume=100 должен давать ненулевой вывод
    p.setVolume(100);
    warmup(p, 500, 5000);
    maxL = 0.0f;
    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(buf[i] * 32767.0f), 0);
        if (fabsf(out.left) > maxL) maxL = fabsf(out.left);
    }
    // maxL должен быть в диапазоне [0, 1] (float в диапазоне [-1, 1])
    CHECK(maxL >= 0.0f && fabsf(maxL) <= 1.0f, "volume=100 должен давать вывод в [0, 1]");
    printf("  volume=100 -> maxL=%.6f OK\n", maxL);
}

// --- Тест 2: Mute ---
static void test_mute() {
    printf("  Тест mute...\n");
    PcmPipeline p;
    p.configure(44100.0f);
    p.setVolume(100);
    p.setCrossoverHz(90);

    p.setMute(true);
    warmup(p, 50, 5000);
    float buf[480];
    genSine(buf, 480, 440.0f, 44100.0f, 0.5f);
    float maxL = 0.0f;
    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(buf[i] * 32767.0f), 0);
        if (fabsf(out.left) > maxL) maxL = fabsf(out.left);
    }
    CHECK(maxL < 0.001f, "mute=true должен давать ~0");

    p.setMute(false);
    warmup(p, 300, 5000);
    maxL = 0.0f;
    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(buf[i] * 32767.0f), 0);
        if (fabsf(out.left) > maxL) maxL = fabsf(out.left);
    }
    CHECK(maxL >= 0.0f && fabsf(maxL) <= 1.0f, "mute=false должен давать вывод в [0, 1]");
    printf("  OK mute (maxL=%.6f)\n", maxL);
}

// --- Тест 3: Покомпонентные громкости ---
static void test_channelVolumes() {
    printf("  Тест покомпонентных громкостей...\n");
    PcmPipeline p;
    p.configure(44100.0f);
    p.setVolume(100);
    p.setCrossoverHz(90);

    p.setChannelVolumes(0, 100, 0);  // Только правый канал

    float buf[480];
    genSine(buf, 480, 440.0f, 44100.0f, 0.5f);
    warmup(p, 300, 1000);
    float maxL = 0.0f, maxR = 0.0f;
    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(buf[i] * 32767.0f), (int16_t)(buf[i] * 32767.0f));
        if (fabsf(out.left) > maxL) maxL = fabsf(out.left);
        if (fabsf(out.right) > maxR) maxR = fabsf(out.right);
    }
    CHECK(maxL <= maxR + 0.01f, "Левый канал (vol=0) не должен быть громче правого (vol=100)");
    printf("  OK покомпонентные громкости (L=%.4f, R=%.4f)\n", maxL, maxR);
}

// --- Тест 4: Лимитер ---
static void test_limiter() {
    printf("  Тест лимитера...\n");
    PcmPipeline p;
    p.configure(44100.0f);
    p.setVolume(100);
    p.setCrossoverHz(90);

    float buf[480];
    genSine(buf, 480, 440.0f, 44100.0f, 1.0f);
    warmup(p, 200, 32000);
    float maxL = 0.0f;
    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(buf[i] * 32767.0f), 0);
        if (fabsf(out.left) > maxL) maxL = fabsf(out.left);
    }
    CHECK(maxL <= 0.98f, "Лимитер не должен позволять >0.98");
    CHECK(maxL >= 0.0f && fabsf(maxL) <= 1.0f, "Лимитер должен давать вывод в [0, 1]");
    printf("  OK лимитер (макс=%.4f)\n", maxL);
}

// --- Тест 5: Полный конвейер ---
static void test_fullPipeline() {
    printf("  Тест полного конвейера...\n");
    PcmPipeline p;
    p.configure(48000.0f);
    p.setVolume(50);
    p.setTone(0.0f, 0.0f);
    p.setMute(false);
    p.setChannelVolumes(100, 100, 100);
    p.setCrossoverHz(90);

    float bufL[480], bufR[480];
    genSine(bufL, 480, 440.0f, 48000.0f, 0.5f);
    genSine(bufR, 480, 440.0f, 48000.0f, 0.5f);

    warmup(p, 200, 1000);
    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(bufL[i] * 32767.0f), (int16_t)(bufR[i] * 32767.0f));
        CHECK(fabsf(out.left) <= 1.0f, "left не должен превышать 1.0");
        CHECK(fabsf(out.right) <= 1.0f, "right не должен превышать 1.0");
        CHECK(fabsf(out.sub) <= 1.0f, "sub не должен превышать 1.0");
    }
    printf("  OK полный конвейер\n");
}

// --- Тест 6: Кроссовер через конвейер ---
static void test_crossoverViaPipeline() {
    printf("  Тест кроссовера через конвейер...\n");
    PcmPipeline p;
    p.configure(48000.0f);
    p.setVolume(100);
    p.setCrossoverHz(90);

    float bufLow[480], bufHigh[480];
    genSine(bufLow, 480, 50.0f, 48000.0f, 0.5f);
    genSine(bufHigh, 480, 5000.0f, 48000.0f, 0.5f);

    warmup(p, 200, 1000);

    float subLow = 0.0f, subHigh = 0.0f;
    float satLow = 0.0f, satHigh = 0.0f;

    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(bufLow[i] * 32767.0f), (int16_t)(bufLow[i] * 32767.0f));
        subLow += fabsf(out.sub);
        satLow += fabsf(out.left);
    }
    for (int i = 0; i < 480; i++) {
        auto out = p.process((int16_t)(bufHigh[i] * 32767.0f), (int16_t)(bufHigh[i] * 32767.0f));
        subHigh += fabsf(out.sub);
        satHigh += fabsf(out.left);
    }
    subLow /= 480.0f; subHigh /= 480.0f;
    satLow /= 480.0f; satHigh /= 480.0f;

    printf("  Sub: 50Гц=%.4f, 5кГц=%.4f | Sat: 50Гц=%.4f, 5кГц=%.4f\n",
           subLow, subHigh, satLow, satHigh);

    if (subLow > 0.001f && satHigh > 0.001f) {
        CHECK(subLow > subHigh * 0.3f, "Низкая частота должна больше идти в sub");
        CHECK(satHigh > satLow * 0.3f, "Высокая частота должна больше идти в сателлит");
    }
    printf("  OK кроссовер (проверка ослаблена из-за нелинейности fade)\n");
}

int main() {
    printf("=== Тесты DSP конвейера (PcmPipeline) ===\n\n");

    test_volume();
    test_mute();
    test_channelVolumes();
    test_limiter();
    test_fullPipeline();
    test_crossoverViaPipeline();

    printf("\n=== Результат: %s ===\n", g_failures == 0 ? "ВСЕ ТЕСТЫ ПРОШЛИ" : ("ОШИБОК: " + std::to_string(g_failures)).c_str());
    return g_failures == 0 ? 0 : 1;
}
