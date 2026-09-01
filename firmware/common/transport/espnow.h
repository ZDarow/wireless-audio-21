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

// Callback при получении пакета от мастера (используется сателлитами).
using EspNowRxCallback = void (*)(const uint8_t* data, size_t size, const MacAddr& from);

// Callback при подтверждении отправки (необязательный): MAC пира + статус.
using EspNowSentCallback = void (*)(const MacAddr& from, bool success);

class EspNowTransport : public ITransport {
public:
    // Инициализация в режиме станции (Wi-Fi должен быть уже инициализирован).
    bool begin() override {
        s_instance = this;
        if (esp_now_init() != ESP_OK) return false;

        // Ключи шифрования (REPO_AUDIT V1): PMK обязателен до добавления
        // шифрованных пиров; длина строго 16 байт.
        static_assert(sizeof(AUDIO_ESPNOW_PMK) - 1 == 16, "AUDIO_ESPNOW_PMK: ровно 16 байт");
        static_assert(sizeof(AUDIO_ESPNOW_LMK) - 1 == 16, "AUDIO_ESPNOW_LMK: ровно 16 байт");
        if (esp_now_set_pmk(reinterpret_cast<const uint8_t*>(AUDIO_ESPNOW_PMK)) != ESP_OK) {
            return false;
        }

        // ESP-IDF требует зарегистрированного peer для broadcast-отправки:
        // без записи FF:FF:FF:FF:FF:FF esp_now_send() возвращает
        // ESP_ERR_ESPNOW_NOT_FOUND. Пир с channel=0 использует текущий канал.
        // Примечание: шифрование multicast/broadcast в ESP-NOW не поддерживается
        // (см. ESP-IDF Programming Guide, ESP-NOW Security), поэтому encrypt=false.
        esp_now_peer_info_t bc = {};
        memset(bc.peer_addr, 0xFF, 6);
        bc.channel = 0;
        bc.ifidx = WIFI_IF_STA;
        bc.encrypt = false;
        memcpy(bc.lmk, AUDIO_ESPNOW_LMK, 16);
        esp_now_add_peer(&bc);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
        esp_now_register_recv_cb([](const esp_now_recv_info_t* info, const uint8_t* data, int len) {
            MacAddr from;
            memcpy(from.bytes, info->src_addr, 6);
            if (g_rxCallback) g_rxCallback(data, static_cast<size_t>(len), from);
            auto* self = EspNowTransport::instance();
            if (self && self->m_itRx) self->m_itRx(data, static_cast<size_t>(len), from.bytes, 6);
        });
        esp_now_register_send_cb([](const wifi_tx_info_t* info, esp_now_send_status_t status) {
            MacAddr from;
            memcpy(from.bytes, info->des_addr, 6);
            if (g_sentCallback) g_sentCallback(from, status == ESP_NOW_SEND_SUCCESS);
            auto* self = EspNowTransport::instance();
            if (self && self->m_itSent) self->m_itSent(from.bytes, status == ESP_NOW_SEND_SUCCESS);
        });
#else
        esp_now_register_recv_cb([](const uint8_t* mac, const uint8_t* data, int len) {
            MacAddr from;
            memcpy(from.bytes, mac, 6);
            if (g_rxCallback) g_rxCallback(data, static_cast<size_t>(len), from);
            auto* self = EspNowTransport::instance();
            if (self && self->m_itRx) self->m_itRx(data, static_cast<size_t>(len), from.bytes, 6);
        });
        esp_now_register_send_cb([](const uint8_t* mac, esp_now_send_status_t status) {
            MacAddr from;
            memcpy(from.bytes, mac, 6);
            if (g_sentCallback) g_sentCallback(from, status == ESP_NOW_SEND_SUCCESS);
            auto* self = EspNowTransport::instance();
            if (self && self->m_itSent) self->m_itSent(from.bytes, status == ESP_NOW_SEND_SUCCESS);
        });
#endif

        return true;
    }

    // Добавить пира по MAC (сателлит или мастер). Шифрование включено
    // (encrypt + LMK) — ESP-NOW без шифрования был открытой уязвимостью
    // (REPO_AUDIT V1): любой ESP32 в эфире мог подписаться на аудиопоток.
    bool addPeer(const MacAddr& mac) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac.bytes, 6);
        peer.channel = 0;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = true;
        memcpy(peer.lmk, AUDIO_ESPNOW_LMK, 16);
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

    // ITransport implementation
    void end() override {
        g_rxCallback = nullptr;
        g_sentCallback = nullptr;
        esp_now_deinit();
    }

    void setRxCallback(RxCallback cb) override { m_itRx = cb; }
    void setSentCallback(SentCallback cb) override { m_itSent = cb; }

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
        if (channel == kChannelLeft && g_hasLeftMac) {
            return sendTo(g_leftMac.bytes, 6, data, len);
        }
        if (channel == kChannelRight && g_hasRightMac) {
            return sendTo(g_rightMac.bytes, 6, data, len);
        }
        return broadcast(data, len);
    }

    void update() override {}

    void setLeftSatMac(const MacAddr& mac) { g_leftMac = mac; g_hasLeftMac = true; }
    void setRightSatMac(const MacAddr& mac) { g_rightMac = mac; g_hasRightMac = true; }

    static EspNowTransport* instance() { return s_instance; }

private:
    static EspNowRxCallback g_rxCallback;
    static EspNowSentCallback g_sentCallback;
    static MacAddr g_leftMac;
    static MacAddr g_rightMac;
    static bool g_hasLeftMac;
    static bool g_hasRightMac;
    static EspNowTransport* s_instance;
    RxCallback m_itRx = nullptr;
    SentCallback m_itSent = nullptr;
};

inline EspNowRxCallback EspNowTransport::g_rxCallback = nullptr;
inline EspNowSentCallback EspNowTransport::g_sentCallback = nullptr;
inline MacAddr EspNowTransport::g_leftMac = {};
inline MacAddr EspNowTransport::g_rightMac = {};
inline bool EspNowTransport::g_hasLeftMac = false;
inline bool EspNowTransport::g_hasRightMac = false;
inline EspNowTransport* EspNowTransport::s_instance = nullptr;

} // namespace audio21