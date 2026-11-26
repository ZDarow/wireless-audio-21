// espnow_transport.cpp — реализация ESP-NOW транспорта.
#include "espnow_transport.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
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
    m_peerCount = 0;
    m_addedPeerCount = 0;
    memset(m_addedPeers, 0, sizeof(m_addedPeers));

    // Определяем интерфейс ESP-NOW в зависимости от режима Wi-Fi
    wifi_mode_t wifi_mode;
    esp_wifi_get_mode(&wifi_mode);
    wifi_interface_t ifidx = (wifi_mode == WIFI_MODE_STA) ? WIFI_IF_STA : WIFI_IF_AP;

    // Добавляем broadcast peer (FF:FF:FF:FF:FF:FF) для Discovery/heartbeat
    esp_now_peer_info_t bcastPeer = {};
    memset(bcastPeer.peer_addr, 0xFF, 6);
    bcastPeer.channel = m_channel;  // текущий канал Wi-Fi
    bcastPeer.encrypt = false;
    bcastPeer.ifidx = ifidx;
    esp_err_t bret = esp_now_add_peer(&bcastPeer);
    if (bret != ESP_OK && bret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGW(TAG, "broadcast peer add failed: %s", esp_err_to_name(bret));
    } else {
        // Отслеживаем broadcast peer для обновления канала
        memcpy(m_addedPeers[m_addedPeerCount], bcastPeer.peer_addr, 6);
        m_addedPeerCount++;
    }

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
    m_peerCount = 0;
    m_addedPeerCount = 0;
    memset(m_addedPeers, 0, sizeof(m_addedPeers));


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

    // Проверяем, не добавлен ли уже этот пир
    bool peerExists = false;
    for (size_t i = 0; i < m_addedPeerCount; i++) {
        if (memcmp(m_addedPeers[i], dstMac, 6) == 0) {
            peerExists = true;
            break;
        }
    }

    // Добавляем peer только если ещё не добавлен
    if (!peerExists) {
        // Проверяем лимит пиров (ESP-NOW обычно поддерживает до 20, с шифрованием до 10)
        if (m_addedPeerCount >= 10) {
            ESP_LOGE(TAG, "peer table full (%zu), cannot add new peer", m_addedPeerCount);
            return ESP_ERR_NO_MEM;
        }

        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, dstMac, 6);
        peerInfo.channel = m_channel; // тот же канал, что и Wi-Fi
        peerInfo.encrypt = m_encrypted;
        // Определяем интерфейс в зависимости от текущего режима Wi-Fi
        wifi_mode_t wmode;
        esp_wifi_get_mode(&wmode);
        peerInfo.ifidx = (wmode == WIFI_MODE_STA) ? WIFI_IF_STA : WIFI_IF_AP;

        esp_err_t ret = esp_now_add_peer(&peerInfo);
        if (ret == ESP_OK) {
            memcpy(m_addedPeers[m_addedPeerCount], dstMac, 6);
            m_addedPeerCount++;
            m_peerCount++;
        } else if (ret == ESP_ERR_ESPNOW_EXIST) {
            // Пир уже существует в таблице ESP-NOW, но не в нашем отслеживании
            memcpy(m_addedPeers[m_addedPeerCount], dstMac, 6);
            m_addedPeerCount++;
            m_peerCount++;
        } else {
            ESP_LOGE(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    esp_err_t ret = esp_now_send(dstMac, data, len);
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

esp_err_t EspNowTransport::removePeer(const uint8_t* dstMac)
{
    if (!m_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_now_del_peer(dstMac);
    if (ret == ESP_OK) {
        // Удаляем из отслеживания
        for (size_t i = 0; i < m_addedPeerCount; i++) {
            if (memcmp(m_addedPeers[i], dstMac, 6) == 0) {
                // Сдвигаем оставшиеся пиры влево
                memmove(m_addedPeers + i, m_addedPeers + i + 1,
                        (m_addedPeerCount - i - 1) * 6);
                m_addedPeerCount--;
                if (m_peerCount > 0) {
                    m_peerCount--;
                }
                break;
            }
        }
        ESP_LOGD(TAG, "Peer removed: %02x:%02x:%02x:%02x:%02x:%02x",
                 dstMac[0], dstMac[1], dstMac[2], dstMac[3], dstMac[4], dstMac[5]);
    } else if (ret == ESP_ERR_ESPNOW_NOT_FOUND) {
        // Пир не был добавлен — не ошибка, но чистим отслеживание
        for (size_t i = 0; i < m_addedPeerCount; i++) {
            if (memcmp(m_addedPeers[i], dstMac, 6) == 0) {
                memmove(m_addedPeers + i, m_addedPeers + i + 1,
                        (m_addedPeerCount - i - 1) * 6);
                m_addedPeerCount--;
                break;
            }
        }
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "esp_now_del_peer failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t EspNowTransport::setChannel(uint8_t channel)
{
    if (channel == m_channel) {
        return ESP_OK;
    }
    m_channel = channel;
    ESP_LOGI(TAG, "ESP-NOW channel changed to %u", channel);

    // Обновляем канал у всех существующих пиров (удаляем и добавляем заново)
    wifi_mode_t wmode;
    esp_wifi_get_mode(&wmode);
    wifi_interface_t ifidx = (wmode == WIFI_MODE_STA) ? WIFI_IF_STA : WIFI_IF_AP;

    for (size_t i = 0; i < m_addedPeerCount; i++) {
        esp_now_del_peer(m_addedPeers[i]);
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, m_addedPeers[i], 6);
        peerInfo.channel = m_channel;
        peerInfo.encrypt = m_encrypted;
        peerInfo.ifidx = ifidx;
        esp_now_add_peer(&peerInfo);
    }

    return ESP_OK;
}

void EspNowTransport::handleRecv(const uint8_t* srcMac, const uint8_t* data, size_t len)
{
    if (g_espnow_instance && g_espnow_instance->m_rxCb) {
        g_espnow_instance->m_rxCb->onPacket(srcMac, data, len);
    }
}

void EspNowTransport::updateSatellite(const uint8_t* srcMac, int8_t rssi)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t nowMs = now * portTICK_PERIOD_MS;

    for (size_t i = 0; i < 2; i++) {
        if (memcmp(m_satellites[i].mac, srcMac, 6) == 0) {
            m_satellites[i].online = true;
            m_satellites[i].rssi = rssi;
            m_satellites[i].lastSeenMs = nowMs;
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
            m_satellites[i].lastSeenMs = nowMs;
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
