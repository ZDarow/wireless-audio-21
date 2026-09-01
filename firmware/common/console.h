// console.h — общий парсер serial-консоли для master/satellite (T17).
//
// Устраняет дублирование handleConsoleCommand из трёх main.cpp.
// Базовый класс читает Serial и предоставляет общие команды (save/reboot);
// роль-специфичные команды реализуются в handleCommand() наследника.
#pragma once

#include <Arduino.h>
#include "config/storage.h"

namespace audio21 {

class Console {
public:
    virtual ~Console() = default;

    void update() {
        if (Serial.available()) {
            String line = Serial.readStringUntil('\n');
            handleLine(line);
        }
    }

protected:
    void handleLine(const String& line) {
        String cmd = line;
        cmd.trim();
        if (cmd.length() == 0) return;

        if (cmd == "save") {
            ConfigStorage::save(g_cfg);
            Serial.println("ok");
            return;
        }
        if (cmd == "reboot") {
            Serial.println("rebooting");
            delay(200);
            ESP.restart();
            return;
        }
        if (cmd == "status") {
            cmdStatus();
            return;
        }
        if (handleCommand(cmd)) return;

        Serial.println("unknown");
    }

    virtual void cmdStatus() = 0;
    virtual bool handleCommand(const String& cmd) = 0;

    NodeConfig& g_cfg;
    explicit Console(NodeConfig& cfg) : g_cfg(cfg) {}
};

} // namespace audio21
