// espnow_transport.h — ESP-NOW транспорт для связи мастер ↔ сателлиты.
// Header-only. Обёртка над esp_now_* API.
//
// Пакет: 16-байт заголовок AudioPacketHeader + payload ≤ 234 байт.
// - Broadcast discovery (каждые 2 с, пока сателлиты offline)
// - Unicast аудио L/R батчи (117 семплов канала)
// - Unicast heartbeat от сателлитов (статус online)
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_err.h"

namespace audio21 {

// Магические числа пакетов (совместимо с Arduino-версией)
static constexpr uint16_t kMagicDiscovery = 0xA210; // discovery/heartbeat
static constexpr uint16_t kMagicAudioL    = 0xA211; // аудио левый канал
static constexpr uint16_t kMagicAudioR    = 0xA212; // аудио правый канал

// Заголовок пакета мастер→сателлит (16 байт)
#pragma pack(push, 1)
struct AudioPacketHeader {
    uint16_t magic;        // kMagicDiscovery / kMagicAudioL / kMagicAudioR
    uint16_t sequence;     // порядковый номер (для detection потерь)
    uint32_t timestampMs;  // метка времени источника (ms)
    int16_t  volumeL;      // громкость левого канала (0..100)
    int16_t  volumeR;      // громкость правого канала (0..100)
    int16_t  delaySubMs;   // задержка сабвуфера (ms)
    uint8_t  reserved[2];  // выравнивание до 16 байт
};
#pragma pack(pop)

static_assert(sizeof(AudioPacketHeader) == 16, "header must be 16 bytes");

// Максимальный размер payload ESP-NOW (250 - 16 заголовка = 234, но оставим запас)
static constexpr size_t kEspNowMaxPayload = 234;

// Результат инициализации
enum class EspNowResult {
    Ok,
    InitFailed,
    SendFailed,
    NotPaired,
};

// Состояние сателлита
struct SatelliteStatus {
    uint8_t mac[6];
    bool online = false;
    uint32_t lastSeenMs = 0;
    int8_t rssi = -128;
};

// Интерфейс коллбека приёма
class EspNowRxCallback {
public:
    virtual ~EspNowRxCallback() = default;
    virtual void onPacket(const uint8_t* srcMac, const uint8_t* data, size_t len) = 0;
};

class EspNowTransport {
public:
    EspNowTransport() = default;
    ~EspNowTransport();

    // Инициализация ESP-NOW (режим мастера: AP + ESP-NOW)
    // encrypt — использовать ли шифрование (PMK/LMK из конфига)
    esp_err_t init(bool encrypt = false);

    // Деинициализация
    esp_err_t deinit();

    // Отправить пакет на конкретный MAC (unicast)
    esp_err_t sendUnicast(const uint8_t* dstMac, const uint8_t* data, size_t len);

    // Отправить broadcast ( discovery / heartbeat )
    esp_err_t sendBroadcast(const uint8_t* data, size_t len);

    // Зарегистрировать коллбек приёма
    void setRxCallback(EspNowRxCallback* cb);

    // Вызвать коллбек приёма (используется из статического C-коллбэка ESP-NOW)
    static void handleRecv(const uint8_t* srcMac, const uint8_t* data, size_t len);

    // Удалить пир из таблицы ESP-NOW
    esp_err_t removePeer(const uint8_t* dstMac);

    // Установить канал ESP-NOW (синхронизировать с текущим каналом Wi-Fi STA/AP)
    esp_err_t setChannel(uint8_t channel);

    // Получить количество активных пиров
    size_t peerCount() const { return m_peerCount; }

    // Получить статус сателлита по индексу (0 = left, 1 = right)
    const SatelliteStatus& satellite(size_t idx) const { return m_satellites[idx]; }

    // Обновить статус сателлита (вызывается из коллбека приёма heartbeat)
    void updateSatellite(const uint8_t* srcMac, int8_t rssi);

private:
    EspNowRxCallback* m_rxCb = nullptr;
    SatelliteStatus m_satellites[2];
    bool m_initialized = false;
    bool m_encrypted = false;
    uint8_t m_channel = 6;
    size_t m_peerCount = 0;
    uint8_t m_addedPeers[10][6] = {};  // MAC пиров, уже добавленных через esp_now_add_peer
    size_t m_addedPeerCount = 0;

    static void onDataSent(const uint8_t* mac, esp_now_send_status_t status);
    static void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);
};

} // namespace audio21
