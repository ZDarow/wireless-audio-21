// arduino_stub.h — минимальная заглушка Arduino для host-компиляции чистых
// DSP-модулей (crossover, volume, pipeline, delay, jitter). Не для прошивки.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Arduino.h определяет PI через M_PI; гарантируем наличие.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef uint8_t byte;
typedef bool boolean;

// Переменная времени для тестов timing.h (millis() читает её).
inline unsigned long g_millis = 0;
inline unsigned long millis() { return g_millis; }

// Минимальная заглушка Serial для компиляции logger.h.
struct SerialStub {
    void print(const char*) {}
    void print(char) {}
    void print(int) {}
    void print(unsigned int) {}
    void print(long) {}
    void print(unsigned long) {}
    void print(float) {}
    void print(double) {}
    void println() {}
    void println(const char*) {}
    void println(int) {}
    void println(unsigned long) {}
};
inline SerialStub Serial;