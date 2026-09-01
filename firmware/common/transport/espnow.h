// espnow.h — ESP-NOW транспорт (TX и RX в одном модуле).
// Header-only. Использует ESP-NOW API ESP-IDF (доступен в Arduino core).
#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>

#include "audio_packet.h"
#include "node_config.h"
#include "transport.h"

namespace audio21 {

// Внутренние типы колбэков (совместимость с существующим кодом).
using EspNowRxCallback = void (*)(const uint8_t* data, size_t size, const MacAddr& from);
using EspNowSentCallback = void (*)(const MacAddr& from, bool success);

class EspNowTransport : public ITransport {
public:
    // Инициализация в режиме станции (Wi-Fi должен быть уже инициализирован).
    bool begin() override {
        if (esp_now_init() != ESP_OK) return false;

        static_assert(sizeof(AUDIO_ESPNOW_PMK) - 1 == 16, "AUDIO_ESPNOW_PMK: ровно 16 байт");
        static_assert(sizeof(AUDIO_ESPNOW_LMK) - 1 == 16, "AUDIO_ESPNOW_LMK: ровно 16 байт");
        if (esp_now_set_pmk(reinterpret_cast<const uint8_t*>(AUDIO_ESPNOW_PMK)) != ESP_OK) {
            return false;
        }

        esp_now_peer_info_t bc = {};
        memset(bc.peer_addr, 0xFF, 6);
        bc.channel = 0;
        bc.ifidx = WIFI_IF_STA;
        bc.encrypt = false;
        memcpy(bc.lmk, AUDIO_ESPNOW_LMK, 16);
        esp_now_add_peer(&bc);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
        esp_now_register_recv_cb([](const esp_now_recv_info_t* info, const uint8_t* data, int len) {
            if (g_rxCallback) {
                MacAddr from;
                memcpy(from.bytes, info->src_addr, 6);
                g_rxCallback(data, static_cast<size_t>(len), from);
            }
        });
        esp_now_register_send_cb([](const wifi_tx_info_t* info, esp_now_send_status_t status) {
            if (g_sentCallback) {
                MacAddr from;
                memcpy(from.bytes, info->des_addr, 6);
                g_sentCallback(from, status == ESP_NOW_SEND_SUCCESS);
            }
        });
#else
        esp_now_register_recv_cb([](const uint8_t* mac, const uint8_t* data, int len) {
            if (g_rxCallback) {
                MacAddr from;
                memcpy(from.bytes, mac, 6);
                g_rxCallback(data, static_cast<size_t>(len), from);
            }
        });
        esp_now_register_send_cb([](const uint8_t* mac, esp_now_send_status_t status) {
            if (g_sentCallback) {
                MacAddr from;
                memcpy(from.bytes, mac, 6);
                g_sentCallback(from, status == ESP_NOW_SEND_SUCCESS);
            }
        });
#endif

        return true;
    }

    void end() override {
        g_rxCallback = nullptr;
        g_sentCallback = nullptr;
        esp_now_deinit();
    }

    void setRxCallback(RxCallback cb) override { g_rxCallback = cb; }
    void setSentCallback(SentCallback cb) override { g_sentCallback = cb; }

    bool sendTo(const void* addr, size_t addrLen, const uint8_t* data, size_t len) override {
        if (addrLen != 6 || !addr) return false;
        if (len > 250) return false;
        return esp_now_send(static_cast<const uint8_t*>(addr), data, len) == ESP_OK;
    }

    bool broadcast(const uint8_t* data, size_t len) override {
        if (len > 250) return false;
        static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        return esp_now_send(bc, data, len) == ESP_OK;
    }

    bool sendToChannel(uint8_t channel, const uint8_t* data, size_t len) override {
        if (channel == kChannelLeft && g_hasLeftSatMac) {
            return sendTo(g_leftSatMac.bytes, 6, data, len);
        }
        if (channel == kChannelRight && g_hasRightSatMac) {
            return sendTo(g_rightSatMac.bytes, 6, data, len);
        }
        return broadcast(data, len);
    }

    void addPeer(const void* addr, size_t addrLen) override {
        if (addrLen != 6 || !addr) return;
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, addr, 6);
        peer.channel = 0;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = true;
        memcpy(peer.lmk, AUDIO_ESPNOW_LMK, 16);
        esp_now_add_peer(&peer);
    }

    void update() override {}

    // Совместимость со старым API (используется в legacy master и satellite).
    bool addPeer(const MacAddr& mac) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac.bytes, 6);
        peer.channel = 0;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = true;
        memcpy(peer.lmk, AUDIO_ESPNOW_LMK, 16);
        return esp_now_add_peer(&peer) == ESP_OK;
    }

    void removePeer(const MacAddr& mac) {
        esp_now_del_peer(mac.bytes);
    }

    esp_err_t sendTo(const MacAddr& mac, const uint8_t* data, size_t size) {
        if (size > 250) return ESP_ERR_INVALID_SIZE;
        return esp_now_send(mac.bytes, data, size);
    }

    esp_err_t broadcast(const uint8_t* data, size_t size) {
        if (size > 250) return ESP_ERR_INVALID_SIZE;
        static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        return esp_now_send(bc, data, size);
    }

    void setLeftSatMac(const MacAddr& mac) { g_leftSatMac = mac; g_hasLeftSatMac = true; }
    void setRightSatMac(const MacAddr& mac) { g_rightSatMac = mac; g_hasRightSatMac = true; }

private:
    static EspNowRxCallback g_rxCallback;
    static EspNowSentCallback g_sentCallback;
    static MacAddr g_leftSatMac;
    static MacAddr g_rightSatMac;
    static bool g_hasLeftSatMac;
    static bool g_hasRightSatMac;
};

inline EspNowRxCallback EspNowTransport::g_rxCallback = nullptr;
inline EspNowSentCallback EspNowTransport::g_sentCallback = nullptr;
inline MacAddr EspNowTransport::g_leftSatMac = {};
inline MacAddr EspNowTransport::g_rightSatMac = {};
inline bool EspNowTransport::g_hasLeftSatMac = false;
inline bool EspNowTransport::g_hasRightSatMac = false;

} // namespace audio21
