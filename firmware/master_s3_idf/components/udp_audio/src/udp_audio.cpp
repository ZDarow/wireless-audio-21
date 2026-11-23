// udp_audio.cpp — реализация UDP аудио приёма.
#include "udp_audio.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include <sys/time.h>

namespace audio21 {

static const char *TAG = "udp_audio";

UdpAudio::~UdpAudio()
{
    deinit();
}

esp_err_t UdpAudio::init(uint16_t port, const char* authKey)
{
    if (m_sock >= 0) {
        ESP_LOGW(TAG, "UDP audio already initialized");
        return ESP_OK;
    }

    m_port = port;
    m_authEnabled = (authKey != nullptr && authKey[0] != '\0');
    if (m_authEnabled && authKey) {
        strncpy(m_authKey, authKey, sizeof(m_authKey) - 1);
        m_authKey[sizeof(m_authKey) - 1] = '\0';
    }

    m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (m_sock < 0) {
        ESP_LOGE(TAG, "failed to create socket: errno %d", errno);
        return ESP_ERR_NO_MEM;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(m_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "failed to bind socket to port %u: errno %d", port, errno);
        close(m_sock);
        m_sock = -1;
        return ESP_ERR_INVALID_STATE;
    }

    // Установим таймаут приёма по умолчанию
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; // 100 мс
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "UDP audio listener on port %u (auth=%d)", port, m_authEnabled ? 1 : 0);
    return ESP_OK;
}

esp_err_t UdpAudio::deinit()
{
    if (m_sock >= 0) {
        close(m_sock);
        m_sock = -1;
    }
    return ESP_OK;
}

bool UdpAudio::verifyHmac(const uint8_t* data, size_t len, const uint8_t* hmac)
{
    if (!m_authEnabled || m_authKey[0] == '\0') {
        return true; // аутентификация не включена
    }

    unsigned char computed[32];
    size_t hmacLen = 0;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)m_authKey, strlen(m_authKey));
    mbedtls_md_hmac_update(&ctx, data, len);
    mbedtls_md_hmac_finish(&ctx, computed);
    mbedtls_md_free(&ctx);
    hmacLen = 32;

    // Постоянное время сравнения
    int diff = 0;
    for (size_t i = 0; i < 32 && i < hmacLen; i++) {
        diff |= computed[i] ^ hmac[i];
    }
    return diff == 0;
}

esp_err_t UdpAudio::receive(uint32_t timeoutMs, UdpAudioRxCallback* cb)
{
    if (m_sock < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    // Установим таймаут для этого вызова
    struct timeval tv = { .tv_sec = (time_t)(timeoutMs / 1000), .tv_usec = (suseconds_t)((timeoutMs % 1000) * 1000) };
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in srcAddr = {};
    socklen_t srcAddrLen = sizeof(srcAddr);

    ssize_t n = recvfrom(m_sock, m_rxBuffer, sizeof(m_rxBuffer), 0,
                         (struct sockaddr*)&srcAddr, &srcAddrLen);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return ESP_ERR_TIMEOUT; // таймаут — не ошибка
        }
        ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
        return ESP_ERR_INVALID_STATE;
    }

    m_packetsRxCount++;

    if (n < (ssize_t)sizeof(UdpAudioHeader)) {
        ESP_LOGW(TAG, "packet too small: %d bytes", n);
        return ESP_ERR_INVALID_SIZE;
    }

    UdpAudioHeader* hdr = reinterpret_cast<UdpAudioHeader*>(m_rxBuffer);
    uint8_t* payload = m_rxBuffer + sizeof(UdpAudioHeader);
    size_t payloadLen = n - sizeof(UdpAudioHeader);

    // Проверка аутентификации
    bool authOk = false;
    if (hdr->magic == kMagicAuth) {
        if (payloadLen < 32) {
            ESP_LOGW(TAG, "auth packet too small: payload %zu bytes", payloadLen);
            m_authFailCount++;
            return ESP_ERR_INVALID_SIZE;
        }
        // HMAC поле находится в заголовке по смещению 8 (32 байта)
        // HMAC считается над: magic(2) + sequence(2) + timestampSamples(4) + payload(payloadLen)
        // Данные в буфере: [magic(2)][seq(2)][ts(4)][hmac(32)][payload(N)]
        // Нужно скопировать magic+seq+ts и payload, пропуская поле hmac
        uint8_t hmacData[1208];  // 8 header bytes + max payload 1200
        size_t hmacDataLen = 0;
        memcpy(hmacData + hmacDataLen, m_rxBuffer, 8);           // magic + sequence + timestampSamples
        hmacDataLen += 8;
        memcpy(hmacData + hmacDataLen, m_rxBuffer + sizeof(UdpAudioHeader), payloadLen);  // payload
        hmacDataLen += payloadLen;
        authOk = verifyHmac(hmacData, hmacDataLen, hdr->hmac);
        if (!authOk) {
            m_authFailCount++;
            ESP_LOGW(TAG, "HMAC verification failed");
            return ESP_ERR_INVALID_STATE;
        }
        // Payload после HMAC
        payload = m_rxBuffer + sizeof(UdpAudioHeader) + 32;
        payloadLen -= 32;
        m_authOkCount++;
    } else if (hdr->magic == kMagicUnauth) {
        if (m_authEnabled) {
            // Ключ настроен — не принимаем неаутентифицированные пакеты (V4)
            m_authFailCount++;
            return ESP_ERR_INVALID_STATE;
        }
        authOk = true;
    } else {
        ESP_LOGW(TAG, "unknown magic: 0x%04x", hdr->magic);
        return ESP_ERR_INVALID_STATE;
    }

    // Payload должен быть чётным числом байт (стерео int16)
    if (payloadLen % 2 != 0) {
        ESP_LOGW(TAG, "payload odd size: %zu bytes", payloadLen);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t nSamplesPerChannel = payloadLen / 2 / 2; // /2 для int16, /2 для стерео
    if (nSamplesPerChannel == 0 || cb == nullptr) {
        return ESP_OK;
    }

    const int16_t* pcm = reinterpret_cast<const int16_t*>(payload);
    cb->onPcm(pcm, nSamplesPerChannel);

    return ESP_OK;
}

} // namespace audio21
