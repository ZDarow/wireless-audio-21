// espnow.h — ESP-NOW транспорт (TX и RX в одном модуле).
// Header-only. Использует ESP-NOW API ESP-IDF (доступен в Arduino core).
#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>

#include "audio_packet.h"
#include "node_config.h"

namespace audio21 {

// Callback при получении пакета от мастера (используется сателлитами).
using EspNowRxCallback = void (*)(const uint8_t* data, size_t size, const MacAddr& from);

// Callback при подтверждении отправки (необязательный).
using EspNowSentCallback = void (*)(bool success);

class EspNowTransport {
public:
    // Инициализация в режиме станции (Wi-Fi должен быть уже инициализирован).
    bool begin() {
        if (esp_now_init() != ESP_OK) return false;

        esp_now_register_recv_cb([](const uint8_t* mac, const uint8_t* data, int len) {
            if (g_rxCallback) {
                MacAddr from;
                memcpy(from.bytes, mac, 6);
                g_rxCallback(data, static_cast<size_t>(len), from);
            }
        });

        esp_now_register_send_cb([](const uint8_t*, esp_now_send_status_t status) {
            if (g_sentCallback) g_sentCallback(status == ESP_NOW_SEND_SUCCESS);
        });

        return true;
    }

    // Добавить пира по MAC (сателлит или мастер).
    bool addPeer(const MacAddr& mac) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac.bytes, 6);
        peer.channel = 0;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        return esp_now_add_peer(&peer) == ESP_OK;
    }

    // Удалить пира.
    void removePeer(const MacAddr& mac) {
        esp_now_del_peer(mac.bytes);
    }

    // Отправить пакет конкретному пиру. Возвращает ESP_OK/ошибку.
    esp_err_t sendTo(const MacAddr& mac, const uint8_t* data, size_t size) {
        if (size > 250) return ESP_ERR_INVALID_SIZE;
        return esp_now_send(mac.bytes, data, size);
    }

    // Широковещательная отправка (мастер → все сателлиты).
    esp_err_t broadcast(const uint8_t* data, size_t size) {
        if (size > 250) return ESP_ERR_INVALID_SIZE;
        static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        return esp_now_send(bc, data, size);
    }

    void setRxCallback(EspNowRxCallback cb) { g_rxCallback = cb; }
    void setSentCallback(EspNowSentCallback cb) { g_sentCallback = cb; }

    void end() {
        g_rxCallback = nullptr;
        g_sentCallback = nullptr;
        esp_now_deinit();
    }

private:
    static EspNowRxCallback g_rxCallback;
    static EspNowSentCallback g_sentCallback;
};

inline EspNowRxCallback EspNowTransport::g_rxCallback = nullptr;
inline EspNowSentCallback EspNowTransport::g_sentCallback = nullptr;

} // namespace audio21