// display.h — OLED SSD1306 (U8g2) с простым статусным экраном.
// Header-only.
#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "node_config.h"

namespace audio21 {

class Display {
public:
    // I2C OLED 0.96" SSD1306, 128x64.
    Display(uint8_t sda, uint8_t scl)
        : m_sda(sda), m_scl(scl),
          m_u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/scl, /* data=*/sda) {}

    bool begin() {
        Wire.begin(m_sda, m_scl);
        m_u8g2.begin();
        m_u8g2.setFont(u8g2_font_6x10_tf);
        m_u8g2.setContrast(128);
        clear();
        return true;
    }

    void clear() {
        m_u8g2.clearBuffer();
        m_u8g2.sendBuffer();
    }

    // Основной статусный экран: роль, источник, громкость, статус сателлитов.
    void showStatus(const char* source, int volume, bool muted,
                    bool leftOnline, bool rightOnline) {
        m_u8g2.clearBuffer();
        m_u8g2.setCursor(0, 10);
        m_u8g2.print("Audio 2.1 Master");
        m_u8g2.setCursor(0, 24);
        m_u8g2.print("Src: ");
        m_u8g2.print(source);
        m_u8g2.setCursor(0, 38);
        m_u8g2.print("Vol: ");
        m_u8g2.print(volume);
        m_u8g2.print(muted ? " [M]" : "");
        m_u8g2.setCursor(0, 52);
        m_u8g2.print("L:");
        m_u8g2.print(leftOnline ? "on " : "off");
        m_u8g2.print(" R:");
        m_u8g2.print(rightOnline ? "on" : "off");
        m_u8g2.sendBuffer();
    }

    void showText(const char* line1, const char* line2 = "", const char* line3 = "", const char* line4 = "") {
        m_u8g2.clearBuffer();
        m_u8g2.setCursor(0, 10); m_u8g2.print(line1);
        m_u8g2.setCursor(0, 24); m_u8g2.print(line2);
        m_u8g2.setCursor(0, 38); m_u8g2.print(line3);
        m_u8g2.setCursor(0, 52); m_u8g2.print(line4);
        m_u8g2.sendBuffer();
    }

private:
    uint8_t m_sda, m_scl;
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C m_u8g2;
};

} // namespace audio21