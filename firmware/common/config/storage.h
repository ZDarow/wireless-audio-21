// storage.h — хранение NodeConfig в NVS (Preferences).
// Header-only.
//
// Ручная проверка round-trip (только на железе, NVS недоступен на хосте):
//   1. Прошить мастер/сателлит, в serial-консоли: `volume 80`, `crossover 100`,
//      `delay left 15`, затем `save`.
//   2. Перезагрузить (`reboot` или питание).
//   3. `status` — значения должны сохраниться (volume=80, crossover=100,
//      delay_left=15). При несовпадении версии/размера возвращаются дефолты.
//   4. `save` при пустой NVS (первый старт) — дефолты записываются без ошибок.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "node_config.h"

namespace audio21 {

// Зеркало v1-структуры NodeConfig (без добавленных в v2 полей). Используется
// только для миграции сохранённого конфига: первый блок полей (общие, PCM,
// DSP, сеть) идентичен v2, сателлиты/GPIO — в конце. Порядок и типы должны
// повторять v1 1:1, чтобы sizeof и layout совпали с сохранённым blob'ом.
struct NodeConfigV1 {
    NodeRole role = NodeRole::Master;
    SatelliteSide side = SatelliteSide::Left;
    AudioSource source = AudioSource::A2DP;
    TransportMode transport = TransportMode::EspNow;
    WifiMode wifiMode = WifiMode::ApDirect;
    uint32_t sampleRate = 48000;
    uint8_t bitsPerSample = 16;
    uint8_t channels = 2;
    int masterVolume = 50;
    bool mute = false;
    int crossoverHz = 90;
    int delayLeftMs = 0;
    int delayRightMs = 0;
    int delaySubMs = 0;
    char wifiSsid[33] = "";
    char wifiPassword[65] = "";
    char wifiApSsid[33] = "";
    char wifiApPassword[65] = "";
    char hostname[33] = "";
    uint16_t udpAudioPort = 5004;
    MacAddr leftSatMac = MacAddr{};
    MacAddr rightSatMac = MacAddr{};
    uint8_t i2sBck = 0;
    uint8_t i2sWs = 0;
    uint8_t i2sDataOut = 0;
};

// Зеркало v2-структуры NodeConfig: от v1 отличается добавленными блоками
// netCheck/NTP/static-IP/security. В v3 удалены write-only поля
// staticIpEnabled/staticIp/netmask/gateway/dns и wifiAutoReconnect — их
// значение не используется (статические адреса живут в WifiProfile), поэтому
// при миграции они просто отбрасываются.
struct NodeConfigV2 {
    NodeRole role = NodeRole::Master;
    SatelliteSide side = SatelliteSide::Left;
    AudioSource source = AudioSource::A2DP;
    TransportMode transport = TransportMode::EspNow;
    WifiMode wifiMode = WifiMode::ApDirect;
    uint32_t sampleRate = 48000;
    uint8_t bitsPerSample = 16;
    uint8_t channels = 2;
    int masterVolume = 50;
    bool mute = false;
    int crossoverHz = 90;
    int delayLeftMs = 0;
    int delayRightMs = 0;
    int delaySubMs = 0;
    char wifiSsid[33] = "";
    char wifiPassword[65] = "";
    char wifiApSsid[33] = "";
    char wifiApPassword[65] = "";
    char hostname[33] = "";
    uint16_t udpAudioPort = 5004;
    bool netCheckEnabled = true;
    uint16_t netCheckIntervalSec = 30;
    uint16_t netCheckTimeoutMs = 5000;
    char netCheckUrl[96] = "";
    bool ntpEnabled = true;
    char ntpServer[64] = "";
    char timezone[24] = "";
    bool staticIpEnabled = false;
    char staticIp[16] = "";
    char staticNetmask[16] = "";
    char staticGateway[16] = "";
    char staticDns[16] = "";
    char adminPasswordHash[65] = "";
    bool authEnabled = false;
    bool wifiAutoReconnect = true;
    MacAddr leftSatMac = MacAddr{};
    MacAddr rightSatMac = MacAddr{};
    uint8_t i2sBck = 0;
    uint8_t i2sWs = 0;
    uint8_t i2sDataOut = 0;
};

// Сохраняет конфигурацию узла в NVS (namespace "audio21").
// Структура сериализуется побайтово + crc-подобная проверка через
// значение-счётчик версий: при несовпадении версии возвращаются дефолты.
class ConfigStorage {
public:
    static constexpr const char* kNamespace = "audio21";
    static constexpr const char* kKey = "config";
    static constexpr const char* kKeyVersion = "version";
    static constexpr uint16_t kVersion = 3;
    static constexpr uint16_t kVersionV1 = 1;
    static constexpr uint16_t kVersionV2 = 2;

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

        if (ver == kVersion && sz == sizeof(NodeConfig)) {
            prefs.getBytes(kKey, &out, sizeof(NodeConfig));
            prefs.end();
            out.clamp();
            return true;
        }

        // Миграция v1 → v2: сохраняем старый blob, перекладываем поля в новую
        // структуру, записываем обратно как v2 (чтобы повторный загрузчик не
        // мигрировал дважды). Поля, добавленные в v2, получают дефолты.
        if (ver == kVersionV1 && sz == sizeof(NodeConfigV1)) {
            NodeConfigV1 old{};
            prefs.getBytes(kKey, &old, sizeof(NodeConfigV1));
            out = defaultConfig();
            out.role = old.role;
            out.side = old.side;
            out.source = old.source;
            out.transport = old.transport;
            out.wifiMode = old.wifiMode;
            out.sampleRate = old.sampleRate;
            out.bitsPerSample = old.bitsPerSample;
            out.channels = old.channels;
            out.masterVolume = old.masterVolume;
            out.mute = old.mute;
            out.crossoverHz = old.crossoverHz;
            out.delayLeftMs = old.delayLeftMs;
            out.delayRightMs = old.delayRightMs;
            out.delaySubMs = old.delaySubMs;
            strlcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
            strlcpy(out.wifiPassword, old.wifiPassword, sizeof(out.wifiPassword));
            strlcpy(out.wifiApSsid, old.wifiApSsid, sizeof(out.wifiApSsid));
            strlcpy(out.wifiApPassword, old.wifiApPassword, sizeof(out.wifiApPassword));
            strlcpy(out.hostname, old.hostname, sizeof(out.hostname));
            out.udpAudioPort = old.udpAudioPort;
            out.leftSatMac = old.leftSatMac;
            out.rightSatMac = old.rightSatMac;
            out.i2sBck = old.i2sBck;
            out.i2sWs = old.i2sWs;
            out.i2sDataOut = old.i2sDataOut;
            prefs.putBytes(kKey, &out, sizeof(NodeConfig));
            prefs.putUShort(kKeyVersion, kVersion);
            prefs.end();
            out.clamp();
            return true;
        }

        // Миграция v2 → v3: из v2 удалены write-only поля static-IP блока и
        // wifiAutoReconnect (значения живут в WifiProfile). Остальное переносится
        // как есть, включая adminPasswordHash/authEnabled (настройки безопасности).
        if (ver == kVersionV2 && sz == sizeof(NodeConfigV2)) {
            NodeConfigV2 old{};
            prefs.getBytes(kKey, &old, sizeof(NodeConfigV2));
            out = defaultConfig();
            out.role = old.role;
            out.side = old.side;
            out.source = old.source;
            out.transport = old.transport;
            out.wifiMode = old.wifiMode;
            out.sampleRate = old.sampleRate;
            out.bitsPerSample = old.bitsPerSample;
            out.channels = old.channels;
            out.masterVolume = old.masterVolume;
            out.mute = old.mute;
            out.crossoverHz = old.crossoverHz;
            out.delayLeftMs = old.delayLeftMs;
            out.delayRightMs = old.delayRightMs;
            out.delaySubMs = old.delaySubMs;
            strlcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
            strlcpy(out.wifiPassword, old.wifiPassword, sizeof(out.wifiPassword));
            strlcpy(out.wifiApSsid, old.wifiApSsid, sizeof(out.wifiApSsid));
            strlcpy(out.wifiApPassword, old.wifiApPassword, sizeof(out.wifiApPassword));
            strlcpy(out.hostname, old.hostname, sizeof(out.hostname));
            out.udpAudioPort = old.udpAudioPort;
            out.netCheckEnabled = old.netCheckEnabled;
            out.netCheckIntervalSec = old.netCheckIntervalSec;
            out.netCheckTimeoutMs = old.netCheckTimeoutMs;
            strlcpy(out.netCheckUrl, old.netCheckUrl, sizeof(out.netCheckUrl));
            out.ntpEnabled = old.ntpEnabled;
            strlcpy(out.ntpServer, old.ntpServer, sizeof(out.ntpServer));
            strlcpy(out.timezone, old.timezone, sizeof(out.timezone));
            strlcpy(out.adminPasswordHash, old.adminPasswordHash, sizeof(out.adminPasswordHash));
            out.authEnabled = old.authEnabled;
            out.leftSatMac = old.leftSatMac;
            out.rightSatMac = old.rightSatMac;
            out.i2sBck = old.i2sBck;
            out.i2sWs = old.i2sWs;
            out.i2sDataOut = old.i2sDataOut;
            prefs.putBytes(kKey, &out, sizeof(NodeConfig));
            prefs.putUShort(kKeyVersion, kVersion);
            prefs.end();
            out.clamp();
            return true;
        }

        prefs.end();
        out = defaultConfig();
        return false;
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