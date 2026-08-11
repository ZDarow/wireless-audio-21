// udp_audio_receiver_test.cpp — host-тест приёмника UDP-аудио (udp_audio_receiver.h).
// Логика потери пакетов §9.3: разрыв sequence → concealment, потеря > 200 мс →
// ramp to mute, отсутствие потока > 3 с → standby.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "arduino_stub.h"
#include "udp_audio_receiver.h"

using namespace audio21;

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_failures++; } \
} while (0)

// Конфигурация по умолчанию: 48 кГц, пакет = 5 мс (960 байт стерео 16 бит).
static UdpAudioReceiver makeRx() {
    UdpAudioReceiver rx;
    rx.configure(48000, 5.0f);
    return rx;
}

int main() {
    printf("== udp_audio_receiver_test ==\n");

    int16_t pcm[8] = {100, -200, 300, -400, 500, -600, 700, -800};

    // --- Первый пакет и непрерывная последовательность ---
    {
        UdpAudioReceiver rx = makeRx();
        CHECK(rx.state() == StreamState::Standby, "до первого пакета — standby");
        CHECK(rx.packetsRx() == 0 && rx.packetsLost() == 0, "счётчики изначально нулевые");

        CHECK(rx.feed(0, 0, pcm, 8, 0) == StreamState::Active, "первый пакет → active");
        CHECK(rx.feed(1, 480, pcm, 8, 5) == StreamState::Active, "seq=1 → active");
        CHECK(rx.feed(2, 960, pcm, 8, 10) == StreamState::Active, "seq=2 → active");
        CHECK(rx.packetsRx() == 3, "принято 3 пакета");
        CHECK(rx.packetsLost() == 0, "потерь нет");
        CHECK(rx.concealGain() == 1.0f, "на активном потоке gain = 1.0");
    }

    // --- Разрыв sequence: 0,1,3 → conceal, потеря учтена ---
    {
        UdpAudioReceiver rx = makeRx();
        rx.feed(0, 0, pcm, 8, 0);
        rx.feed(1, 480, pcm, 8, 5);
        CHECK(rx.feed(3, 1440, pcm, 8, 10) == StreamState::Conceal,
              "пропуск seq (0,1,3) → conceal");
        CHECK(rx.packetsLost() == 1, "потерян 1 пакет");
        CHECK(rx.concealGain() == 1.0f, "малая потеря (<50 мс) → gain 1.0");
    }

    // --- Потеря 50..200 мс — concealment с затуханием ---
    {
        UdpAudioReceiver rx = makeRx();
        rx.feed(0, 0, pcm, 8, 0);
        // Пропуск 20 пакетов = 100 мс.
        CHECK(rx.feed(21, 0, pcm, 8, 5) == StreamState::Conceal,
              "потеря 100 мс → conceal");
        CHECK(rx.packetsLost() == 20, "потеряно 20 пакетов");
        // Эскалация по времени: 150 мс от последнего валидного пакета.
        CHECK(rx.tick(150) == StreamState::Conceal, "150 мс → conceal");
        float g = rx.concealGain();
        CHECK(fabsf(g - 0.333f) < 0.01f, "150 мс → gain ≈ 1/3");
    }

    // --- Потеря ≥200 мс — ramp to mute ---
    {
        UdpAudioReceiver rx = makeRx();
        rx.feed(0, 0, pcm, 8, 0);
        // Пропуск 42 пакетов = 210 мс.
        CHECK(rx.feed(43, 0, pcm, 8, 5) == StreamState::RampOut,
              "потеря 210 мс → ramp to mute");
        CHECK(rx.concealGain() == 0.0f, "при ramp-to-mute gain = 0");
    }

    // --- Отсутствие потока: >3 с → standby ---
    {
        UdpAudioReceiver rx = makeRx();
        rx.feed(0, 0, pcm, 8, 0);
        rx.feed(1, 480, pcm, 8, 5);
        CHECK(rx.tick(215) == StreamState::RampOut, "210 мс молчания → ramp to mute");
        CHECK(rx.tick(3205) == StreamState::Standby, "3.2 с молчания → standby");
        CHECK(rx.concealGain() == 0.0f, "в standby gain = 0");
    }

    // --- Восстановление потока после standby ---
    {
        UdpAudioReceiver rx = makeRx();
        rx.feed(0, 0, pcm, 8, 0);
        rx.tick(3205);
        CHECK(rx.state() == StreamState::Standby, "дошло до standby");
        CHECK(rx.feed(1, 480, pcm, 8, 3210) == StreamState::Active,
              "новый пакет выводит из standby");
    }

    // --- Дубликат / переупорядочивание не учитываются как потери ---
    {
        UdpAudioReceiver rx = makeRx();
        rx.feed(5, 0, pcm, 8, 0);
        StreamState st = rx.feed(5, 0, pcm, 8, 5);
        CHECK(st == StreamState::Active, "дубликат seq не меняет состояние");
        CHECK(rx.packetsLost() == 0, "дубликат не считается потерей");
        st = rx.feed(4, 0, pcm, 8, 10);
        CHECK(st == StreamState::Active, "переупорядочивание не ломает поток");
        CHECK(rx.packetsLost() == 0, "переупорядочивание не считается потерей");
    }

    // --- Обёртка sequence (2^32) обрабатывается корректно ---
    {
        UdpAudioReceiver rx = makeRx();
        CHECK(rx.feed(0xFFFFFFFE, 0, pcm, 8, 0) == StreamState::Active, "первый пакет у wrap");
        CHECK(rx.feed(0xFFFFFFFF, 0, pcm, 8, 5) == StreamState::Active, "seq непрерывен у wrap");
        CHECK(rx.feed(0, 0, pcm, 8, 10) == StreamState::Active, "wrap 2^32 непрерывен");
        CHECK(rx.packetsLost() == 0, "при wrap потерь нет");
        CHECK(rx.feed(2, 0, pcm, 8, 15) == StreamState::Conceal, "пропуск после wrap → conceal");
        CHECK(rx.packetsLost() == 1, "после wrap потеря = 1");
    }

    // --- Вызванный до конфигурации tick не приводит к зависанию ---
    {
        UdpAudioReceiver rx;
        CHECK(rx.tick(3500) == StreamState::Standby, "3.5 с без пакетов → standby");
        CHECK(rx.concealGain() == 0.0f, "без потока gain = 0");
    }

    if (g_failures) {
        printf("ИТОГ: %d ПРОВАЛОВ\n", g_failures);
        return 1;
    }
    printf("ИТОГ: ВСЕ ТЕСТЫ ПРОЙДЕНЫ\n");
    return 0;
}
