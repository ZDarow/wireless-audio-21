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

// Минимальные псевдонимы, которые могут использоваться в заголовках.
inline unsigned long millis() { return 0; }