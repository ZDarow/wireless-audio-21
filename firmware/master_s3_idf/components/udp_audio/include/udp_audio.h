// udp_audio.h — приём PCM аудио по UDP от смартфона (мастер-узел).
// Пакет: magic 0xA210/0xA211 + заголовок + payload PCM int16 стерео.
// Аутентификация HMAC-SHA256 (V4 из аудита репозитория).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <lwip/sockets.h>
#include "esp_err.h"

namespace audio21 {

// Магические числа (совместимо с Arduino-версией)
static constexpr uint16_t kMagicUnauth = 0xA210; // без аутентификации (если ключ не настроен)
static constexpr uint16_t kMagicAuth   = 0xA211; // с HMAC-SHA256 аутентификацией

// Заголовок UDP аудио пакета (смартфон → мастер)
#pragma pack(push, 1)
struct UdpAudioHeader {
    uint16_t magic;              // kMagicUnauth или kMagicAuth
    uint16_t sequence;           // порядковый номер пакета
    uint32_t timestampSamples;   // метка времени в семплах (для clock recovery)
    uint8_t  hmac[32];           // HMAC-SHA256 (только для kMagicAuth)
    // payload следует сразу после заголовка (для kMagicUnauth hmac отсутствует)
};
#pragma pack(pop)

static_assert(sizeof(UdpAudioHeader) == 40, "UDP audio header must be 40 bytes (with HMAC)");

// Коллбек приёма PCM данных
class UdpAudioRxCallback {
public:
    virtual ~UdpAudioRxCallback() = default;
    // data — стерео int16 PCM (L, R, L, R, ...), nSamples — количество семплов на канал
    virtual void onPcm(const int16_t* data, size_t nSamplesPerChannel) = 0;
};

class UdpAudio {
public:
    UdpAudio() = default;
    ~UdpAudio();

    // Инициализация UDP сокета на указанном порту.
    // authKey — ключ для HMAC-SHA256 (если NULL/пустая — принимаются только kMagicUnauth).
    esp_err_t init(uint16_t port, const char* authKey = nullptr);

    // Деинициализация
    esp_err_t deinit();

    // Блокирующий приём одного пакета (таймаут timeoutMs).
    // Вызывает callback с данными если пакет валиден.
    esp_err_t receive(uint32_t timeoutMs, UdpAudioRxCallback* cb);

    // Последняя ошибка аутентификации (для статистики)
    uint32_t authFailCount() const { return m_authFailCount; }
    uint32_t authOkCount() const { return m_authOkCount; }
    uint32_t packetsRxCount() const { return m_packetsRxCount; }

private:
    int m_sock = -1;
    uint16_t m_port = 0;
    bool m_authEnabled = false;
    char m_authKey[64] = {0};
    uint8_t m_rxBuffer[1500];
    uint32_t m_authFailCount = 0;
    uint32_t m_authOkCount = 0;
    uint32_t m_packetsRxCount = 0;

    // Проверка HMAC-SHA256 пакета
    bool verifyHmac(const uint8_t* data, size_t len, const uint8_t* hmac);
};

} // namespace audio21
