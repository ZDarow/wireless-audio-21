// encoder.h — роторный энкодер KY-040 с кнопкой.
// Header-only. Опрос в loop(), события через колбэки.
#pragma once

#include <Arduino.h>

namespace audio21 {

class RotaryEncoder {
public:
    // pinA, pinB — каналы энкодера; pinBtn — кнопка (или -1, если нет).
    RotaryEncoder(uint8_t pinA, uint8_t pinB, int8_t pinBtn = -1)
        : m_pinA(pinA), m_pinB(pinB), m_pinBtn(pinBtn) {}

    void begin() {
        pinMode(m_pinA, INPUT_PULLUP);
        pinMode(m_pinB, INPUT_PULLUP);
        if (m_pinBtn >= 0) pinMode(m_pinBtn, INPUT_PULLUP);
        m_lastA = digitalRead(m_pinA);
        m_lastBtn = (m_pinBtn >= 0) ? digitalRead(m_pinBtn) : HIGH;
    }

    // Вызывать в loop(). Возвращает накопленное число шагов с прошлого вызова
    // (положительное = по часовой, отрицательное = против).
    int readSteps() {
        int steps = 0;
        int a = digitalRead(m_pinA);
        int b = digitalRead(m_pinB);

        // Простой детектор вращения по смене состояния канала A.
        if (a != m_lastA) {
            // Если B отличается от A при переходе — вращение по часовой.
            steps = (a != b) ? 1 : -1;
            m_lastA = a;
        }
        return steps;
    }

    // Кнопка: true при нажатии (переход из HIGH в LOW).
    bool buttonPressed() {
        if (m_pinBtn < 0) return false;
        int btn = digitalRead(m_pinBtn);
        bool pressed = (btn == LOW && m_lastBtn == HIGH);
        m_lastBtn = btn;
        return pressed;
    }

private:
    uint8_t m_pinA, m_pinB;
    int8_t m_pinBtn;
    int m_lastA = 0, m_lastB = 0;
    int m_lastBtn = HIGH;
};

} // namespace audio21