// storage.h — хранение NodeConfig в NVS (Preferences).
// Header-only.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "node_config.h"

namespace audio21 {

// Сохраняет конфигурацию узла в NVS (namespace "audio21").
// Структура сериализуется побайтово + crc-подобная проверка через
// значение-счётчик версий: при несовпадении версии возвращаются дефолты.
class ConfigStorage {
public:
    static constexpr const char* kNamespace = "audio21";
    static constexpr const char* kKey = "config";
    static constexpr const char* kKeyVersion = "version";
    static constexpr uint16_t kVersion = 1;

    // Сохранить конфиг. Возвращает true при успехе.
    static bool save(const NodeConfig& cfg) {
        Preferences prefs;
        if (!prefs.begin(kNamespace, false)) return false;
        bool ok = prefs.putBytes(kKey, &cfg, sizeof(NodeConfig));
        prefs.putUShort(kKeyVersion, kVersion);
        prefs.end();
        return ok;
    }

    // Загрузить конфиг. При отсутствии/несовпадении версии заполняет дефолтами
    // и возвращает false (вызывающий может при желании сохранить дефолты).
    // Открываем в RW-режиме: на свежем чипе namespace создаётся без
    // ошибочного лога NVS "nvs_open failed: NOT_FOUND".
    static bool load(NodeConfig& out) {
        Preferences prefs;
        if (!prefs.begin(kNamespace, false)) {
            out = defaultConfig();
            return false;
        }
        uint16_t ver = prefs.getUShort(kKeyVersion, 0);
        size_t sz = prefs.isKey(kKey) ? prefs.getBytesLength(kKey) : 0;
        if (ver != kVersion || sz != sizeof(NodeConfig)) {
            prefs.end();
            out = defaultConfig();
            return false;
        }
        prefs.getBytes(kKey, &out, sizeof(NodeConfig));
        prefs.end();
        out.clamp();
        return true;
    }

    // Очистить сохранённый конфиг (вернуть к дефолтам).
    static void erase() {
        Preferences prefs;
        if (prefs.begin(kNamespace, false)) {
            prefs.clear();
            prefs.end();
        }
    }
};

} // namespace audio21