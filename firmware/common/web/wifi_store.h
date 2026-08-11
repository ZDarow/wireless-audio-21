// wifi_store.h — сохранённые Wi-Fi профили (ТЗ_Веб §6.5).
// Header-only. Хранение в NVS через Preferences (namespace "wifiprofs").
// Минимум 5 профилей (ТЗ §6.5): SSID, пароль, скрытая сеть, IP-режим,
// статический IP/маска/шлюз/DNS, приоритет, авто-переподключение.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace audio21 {

struct WifiProfile {
    char ssid[33] = "";
    char password[65] = "";
    bool hidden = false;
    bool staticIp = false;      // false = DHCP
    char ip[16] = "";
    char netmask[16] = "";
    char gateway[16] = "";
    char dns[16] = "";
    bool autoReconnect = true;
    uint8_t priority = 0;       // 0 = низший
};

class WifiStore {
public:
    static constexpr int kMaxProfiles = 5;
    static constexpr const char* kNamespace = "wifiprofs";

    static bool saveProfile(const WifiProfile& p) {
        // Обновить существующий профиль по SSID или добавить новый (до 5).
        Preferences prefs;
        if (!prefs.begin(kNamespace, false)) return false;
        int idx = findIndex(prefs, p.ssid);
        if (idx < 0) {
            if (countProfiles(prefs) >= kMaxProfiles) {
                // Вытеснить профиль с наименьшим приоритетом.
                idx = lowestPriorityIndex(prefs);
            } else {
                idx = countProfiles(prefs);
            }
        }
        bool ok = put(prefs, idx, p);
        prefs.end();
        return ok;
    }

    static bool loadProfile(const char* ssid, WifiProfile& out) {
        Preferences prefs;
        if (!prefs.begin(kNamespace, true)) return false;
        int idx = findIndex(prefs, ssid);
        bool ok = idx >= 0 && get(prefs, idx, out);
        prefs.end();
        return ok;
    }

    static bool forget(const char* ssid) {
        Preferences prefs;
        if (!prefs.begin(kNamespace, false)) return false;
        int idx = findIndex(prefs, ssid);
        if (idx < 0) { prefs.end(); return false; }
        WifiProfile empty;
        put(prefs, idx, empty);
        prefs.end();
        return true;
    }

    static int loadAll(WifiProfile out[kMaxProfiles]) {
        Preferences prefs;
        if (!prefs.begin(kNamespace, true)) return 0;
        int n = countProfiles(prefs);
        if (n > kMaxProfiles) n = kMaxProfiles;
        for (int i = 0; i < n; i++) get(prefs, i, out[i]);
        prefs.end();
        return n;
    }

    static int count() {
        Preferences prefs;
        if (!prefs.begin(kNamespace, true)) return 0;
        int n = countProfiles(prefs);
        prefs.end();
        return n;
    }

    static void clearAll() {
        Preferences prefs;
        if (prefs.begin(kNamespace, false)) {
            prefs.clear();
            prefs.end();
        }
    }

private:
    static constexpr const char* keyPrefix = "p";

    static int countProfiles(Preferences& prefs) {
        int n = 0;
        for (int i = 0; i < kMaxProfiles; i++) {
            char key[8];
            snprintf(key, sizeof(key), "%s%d", keyPrefix, i);
            if (prefs.isKey(key)) n++;
        }
        return n;
    }

    static int findIndex(Preferences& prefs, const char* ssid) {
        for (int i = 0; i < kMaxProfiles; i++) {
            char key[8];
            snprintf(key, sizeof(key), "%s%d", keyPrefix, i);
            if (prefs.isKey(key)) {
                WifiProfile p;
                if (get(prefs, i, p) && strcmp(p.ssid, ssid) == 0) return i;
            }
        }
        return -1;
    }

    static int lowestPriorityIndex(Preferences& prefs) {
        int idx = 0;
        uint8_t lowest = 255;
        for (int i = 0; i < kMaxProfiles; i++) {
            char key[8];
            snprintf(key, sizeof(key), "%s%d", keyPrefix, i);
            if (prefs.isKey(key)) {
                WifiProfile p;
                if (get(prefs, i, p) && p.priority < lowest) {
                    lowest = p.priority;
                    idx = i;
                }
            }
        }
        return idx;
    }

    static bool get(Preferences& prefs, int idx, WifiProfile& out) {
        char key[8];
        snprintf(key, sizeof(key), "%s%d", keyPrefix, idx);
        size_t sz = prefs.getBytesLength(key);
        if (sz != sizeof(WifiProfile)) return false;
        prefs.getBytes(key, &out, sizeof(WifiProfile));
        return true;
    }

    static bool put(Preferences& prefs, int idx, const WifiProfile& p) {
        char key[8];
        snprintf(key, sizeof(key), "%s%d", keyPrefix, idx);
        return prefs.putBytes(key, &p, sizeof(WifiProfile)) > 0;
    }
};

} // namespace audio21
