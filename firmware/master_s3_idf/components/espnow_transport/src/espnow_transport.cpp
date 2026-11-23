// espnow_transport.cpp — реализация ESP-NOW транспорта.
#include "espnow_transport.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <string.h>

namespace audio21 {

static const char *TAG = "espnow";

// Указатель на экземпляр для статического коллбека приёма
static EspNowTransport* g_espnow_instance = nullptr;

EspNowTransport::~EspNowTransport()
{
    deinit();
}

esp_err_t EspNowTransport::init(bool encrypt)
{
    if (m_initialized) {
        return ESP_OK;
    }
    g_espnow_instance = this;


    // Wi-Fi должен быть в режиме AP или AP_STA для ESP-NOW
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA && mode != WIFI_MODE_STA) {
        ESP_LOGE(TAG, "Wi-Fi mode %d not suitable for ESP-NOW, need AP/STA/APSTA", mode);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_now_register_send_cb(onDataSent);
    // В ESP-IDF v5.3 сигнатура recv_cb: void(*)(const esp_now_recv_info*, const uint8_t*, int)
    esp_now_register_recv_cb([](const esp_now_recv_info* info, const uint8_t* data, int len) {
        EspNowTransport::handleRecv(info->src_addr, data, len);
        ESP_LOGD(TAG, "ESP-NOW recv from %02x:%02x:%02x:%02x:%02x:%02x len=%d",
                 info->src_addr[0], info->src_addr[1], info->src_addr[2],
                 info->src_addr[3], info->src_addr[4], info->src_addr[5], len);
    });

    m_encrypted = encrypt;
    m_initialized = true;

    ESP_LOGI(TAG, "ESP-NOW initialized (encrypt=%d)", encrypt ? 1 : 0);
    return ESP_OK;
}

esp_err_t EspNowTransport::deinit()
{
    if (!m_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = esp_now_deinit();
    m_initialized = false;
    m_rxCb = nullptr;
    g_espnow_instance = nullptr;


    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_deinit failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "ESP-NOW deinitialized");
    }
    return ret;
}

esp_err_t EspNowTransport::sendUnicast(const uint8_t* dstMac, const uint8_t* data, size_t len)
{
    if (!m_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > kEspNowMaxPayload) {
        ESP_LOGE(TAG, "payload too large: %zu > %d", len, kEspNowMaxPayload);
        return ESP_ERR_INVALID_SIZE;
    }

    // Добавляем peer если ещё не добавлен
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, dstMac, 6);
    peerInfo.channel = 6; // тот же канал, что и AP
    peerInfo.encrypt = m_encrypted;
    peerInfo.ifidx = WIFI_IF_AP;

    esp_err_t ret = esp_now_add_peer(&peerInfo);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_send(dstMac, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t EspNowTransport::sendBroadcast(const uint8_t* data, size_t len)
{
    if (!m_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > kEspNowMaxPayload) {
        ESP_LOGE(TAG, "payload too large: %zu > %d", len, kEspNowMaxPayload);
        return ESP_ERR_INVALID_SIZE;
    }

    // Broadcast MAC: FF:FF:FF:FF:FF:FF
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    esp_err_t ret = esp_now_send(broadcastMac, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_send (broadcast) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

void EspNowTransport::setRxCallback(EspNowRxCallback* cb)
{
    m_rxCb = cb;
}

void EspNowTransport::handleRecv(const uint8_t* srcMac, const uint8_t* data, size_t len)
{
    if (g_espnow_instance && g_espnow_instance->m_rxCb) {
        g_espnow_instance->m_rxCb->onPacket(srcMac, data, len);
    }
}

void EspNowTransport::updateSatellite(const uint8_t* srcMac, int8_t rssi)
{
    for (size_t i = 0; i < 2; i++) {
        if (memcmp(m_satellites[i].mac, srcMac, 6) == 0) {
            m_satellites[i].online = true;
            m_satellites[i].rssi = rssi;
            ESP_LOGI(TAG, "satellite %zu online (RSSI=%d)", i, rssi);
            return;
        }
    }
    // Новый сателлит — добавляем в первый свободный слот
    for (size_t i = 0; i < 2; i++) {
        if (!m_satellites[i].online) {
            memcpy(m_satellites[i].mac, srcMac, 6);
            m_satellites[i].online = true;
            m_satellites[i].rssi = rssi;
            ESP_LOGI(TAG, "new satellite %zu: %02x:%02x:%02x:%02x:%02x:%02x (RSSI=%d)", i,
                     srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5], rssi);
            return;
        }
    }
    ESP_LOGW(TAG, "satellite slots full, ignoring %02x:%02x:%02x:%02x:%02x:%02x",
             srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
}

void EspNowTransport::onDataSent(const uint8_t* mac, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "ESP-NOW send OK to %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "ESP-NOW send FAILED to %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

} // namespace audio21
