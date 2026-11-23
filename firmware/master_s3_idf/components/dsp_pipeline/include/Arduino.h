// Arduino.h — шим для ESP-IDF (без Arduino-фреймворка)
// Предоставляет только минимальные типы, используемые DSP модулями.
// Все DSP модули header-only и не используют Arduino-функции (delay, Serial и т.д.).

#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

// Минимальная замена delay() для совместимости (не используется в DSP модулях)
inline void delay(unsigned long ms) {
    // В ESP-IDF используется vTaskDelay() вместо delay()
}

// Константа PI (используется в DSP модулях)
#ifndef PI
#define PI 3.14159265358979323846
#endif

// Типы Arduino, используемые в коде
using byte = uint8_t;
using word = uint16_t;
using dword = uint32_t;
