// udp_audio_packet.h — формат UDP-пакета смартфон → мастер (спецификация §9.1).
// Header-only, без зависимостей от Arduino — тестируется на хосте.
#pragma once

#include <stdint.h>
#include <string.h>

#include "auth.h"

namespace audio21 {

// Формат пакета (little-endian):
//
// Неаутентифицированный (магия 0xA210):
//   Offset  Size  Field
//   0       2     magic
//   2       1     protocol_version
//   3       1     flags
//   4       4     sequence
//   8       4     timestamp_samples
//   12      2     sample_rate
//   14      1     channels
//   15      1     bits_per_sample
//   16      2     payload_length
//   18      N     PCM payload
//
// Аутентифицированный (магия 0xA211, C5.7 / REPO_AUDIT V4):
//   Offset  Size  Field
//   0       2     magic = 0xA211
//   2       1     protocol_version
//   3       1     flags
//   4       4     sequence
//   8       4     timestamp_samples
//   12      2     sample_rate
//   14      1     channels
//   15      1     bits_per_sample
//   16      2     payload_length
//   18      N     PCM payload
//   18+N    8     HMAC-SHA256(key, header || payload) — первые 8 байт дайджеста
//
// HMAC вычисляется над заголовком + payload (без самого HMAC).
// Мастер принимает 0xA210 (обратная совместимость) и 0xA211 (с проверкой HMAC).
// Пакеты 0xA211 с неверным HMAC отбрасываются молча.

struct UdpAudioHeader {
    uint16_t magic;            // 0xA210 (неаут.) или 0xA211 (аут.)
    uint8_t  protocolVersion;  // 1
    uint8_t  flags;            // 0x01 = end of stream, 0x02 = keyframe
    uint32_t sequence;         // монотонный счётчик пакетов
    uint32_t timestampSamples; // счётчик сэмплов источника (для синхронизации)
    uint16_t sampleRate;       // 48000
    uint8_t  channels;         // 2
    uint8_t  bitsPerSample;    // 16
    uint16_t payloadLength;    // байт PCM
} __attribute__((packed));

static_assert(sizeof(UdpAudioHeader) == 18, "UdpAudioHeader must be 18 bytes");

// Предельные размеры (§9.2: MTU-safe, payload < 1200 байт).
constexpr uint16_t kUdpMagic = 0xA210;          // неаутентифицированный пакет
constexpr uint16_t kUdpMagicAuthenticated = 0xA211; // аутентифицированный пакет
constexpr uint8_t  kUdpProtocolVersion = 1;
constexpr size_t   kUdpMaxPayload = 1200;
constexpr size_t   kUdpAuthHmacSize = 8;        // усечённый HMAC (8 байт)
constexpr size_t   kUdpPacketSize = sizeof(UdpAudioHeader) + kUdpMaxPayload;

// Флаги (§9.1).
constexpr uint8_t kUdpFlagEndOfStream = 0x01;
constexpr uint8_t kUdpFlagKeyframe    = 0x02;

// Результат валидации аутентификации.
enum class UdpAuthResult : uint8_t {
    Unauthenticated, // магия 0xA210 — принят без проверки (обратная совместимость)
    Authenticated,   // магия 0xA211, HMAC верный
    Invalid,         // магия 0xA211, HMAC неверный — отбросить
};

// Вспомогательная сборка с заданной магией (внутренняя).
inline size_t buildUdpPacketMagic(uint8_t* dst, size_t dstSize,
                                  uint16_t magic,
                                  const uint8_t* key, size_t keyLen,
                                  uint32_t sequence, uint32_t timestampSamples,
                                  uint16_t sampleRate, uint8_t channels,
                                  uint8_t bitsPerSample,
                                  const void* payload, uint16_t payloadLength,
                                  uint8_t flags = 0) {
    if (payloadLength > kUdpMaxPayload) return 0;
    size_t hmacSize = (magic == kUdpMagicAuthenticated) ? kUdpAuthHmacSize : 0;
    if (dstSize < sizeof(UdpAudioHeader) + payloadLength + hmacSize) return 0;

    UdpAudioHeader h;
    h.magic = magic;
    h.protocolVersion = kUdpProtocolVersion;
    h.flags = flags;
    h.sequence = sequence;
    h.timestampSamples = timestampSamples;
    h.sampleRate = sampleRate;
    h.channels = channels;
    h.bitsPerSample = bitsPerSample;
    h.payloadLength = payloadLength;

    memcpy(dst, &h, sizeof(UdpAudioHeader));
    if (payload && payloadLength) memcpy(dst + sizeof(UdpAudioHeader), payload, payloadLength);

    if (hmacSize > 0 && key) {
        size_t msgLen = sizeof(UdpAudioHeader) + payloadLength;
        uint8_t hmac[32];
        hmacSha256(key, keyLen, dst, msgLen, hmac);
        memcpy(dst + sizeof(UdpAudioHeader) + payloadLength, hmac, hmacSize);
    }

    return sizeof(UdpAudioHeader) + payloadLength + hmacSize;
}

// Сборка неаутентифицированного пакета. Возвращает размер (заголовок + payload) или 0 при ошибке.
inline size_t buildUdpPacket(uint8_t* dst, size_t dstSize,
                             uint32_t sequence, uint32_t timestampSamples,
                             uint16_t sampleRate, uint8_t channels,
                             uint8_t bitsPerSample,
                             const void* payload, uint16_t payloadLength,
                             uint8_t flags = 0) {
    return buildUdpPacketMagic(dst, dstSize, kUdpMagic, nullptr, 0,
                               sequence, timestampSamples, sampleRate, channels,
                               bitsPerSample, payload, payloadLength, flags);
}

// Сборка аутентифицированного пакета с HMAC. Возвращает размер (заголовок + payload + HMAC) или 0 при ошибке.
// key — предварительно согласованный ключ (AUDIO_UDP_AUTH_KEY из config.env).
inline size_t buildUdpPacketAuthenticated(uint8_t* dst, size_t dstSize,
                                          const uint8_t* key, size_t keyLen,
                                          uint32_t sequence, uint32_t timestampSamples,
                                          uint16_t sampleRate, uint8_t channels,
                                          uint8_t bitsPerSample,
                                          const void* payload, uint16_t payloadLength,
                                          uint8_t flags = 0) {
    return buildUdpPacketMagic(dst, dstSize, kUdpMagicAuthenticated, key, keyLen,
                               sequence, timestampSamples, sampleRate, channels,
                               bitsPerSample, payload, payloadLength, flags);
}

// Валидация и разбор пакета. Возвращает результат аутентификации.
// payload указывает на начало PCM данных внутри data; payloadSize — их длина.
// Для аутентифицированных пакетов (0xA211) HMAC проверяется; при несовпадении
// возвращается Invalid и пакет должен быть отброшен.
inline UdpAuthResult parseUdpPacket(const uint8_t* data, size_t size,
                                    const uint8_t* key, size_t keyLen,
                                    UdpAudioHeader& hdr,
                                    const uint8_t*& payload, size_t& payloadSize) {
    if (size < sizeof(UdpAudioHeader)) return UdpAuthResult::Invalid;
    memcpy(&hdr, data, sizeof(UdpAudioHeader));

    if (hdr.magic != kUdpMagic && hdr.magic != kUdpMagicAuthenticated) return UdpAuthResult::Invalid;
    if (hdr.protocolVersion != kUdpProtocolVersion) return UdpAuthResult::Invalid;
    if (hdr.payloadLength > kUdpMaxPayload) return UdpAuthResult::Invalid;

    size_t expectedSize = sizeof(UdpAudioHeader) + hdr.payloadLength;
    if (hdr.magic == kUdpMagicAuthenticated) expectedSize += kUdpAuthHmacSize;
    if (size < expectedSize) return UdpAuthResult::Invalid;

    // Аудио-поток: только 16-битный PCM.
    if (hdr.bitsPerSample != 16) return UdpAuthResult::Invalid;

    payload = data + sizeof(UdpAudioHeader);
    payloadSize = hdr.payloadLength;

    if (hdr.magic == kUdpMagicAuthenticated) {
        // Проверка HMAC.
        if (!key || keyLen == 0) return UdpAuthResult::Invalid;
        size_t msgLen = sizeof(UdpAudioHeader) + hdr.payloadLength;
        uint8_t hmac[32];
        hmacSha256(key, keyLen, data, msgLen, hmac);
        const uint8_t* receivedHmac = data + expectedSize - kUdpAuthHmacSize;
        for (size_t i = 0; i < kUdpAuthHmacSize; i++) {
            if (hmac[i] != receivedHmac[i]) return UdpAuthResult::Invalid;
        }
        return UdpAuthResult::Authenticated;
    }

    return UdpAuthResult::Unauthenticated;
}

// Устаревший интерфейс без ключа (для обратной совместимости с тестами).
// Принимает только 0xA210; 0xA211 без ключа считается невалидным.
inline bool parseUdpPacket(const uint8_t* data, size_t size, UdpAudioHeader& hdr,
                           const uint8_t*& payload, size_t& payloadSize) {
    return parseUdpPacket(data, size, nullptr, 0, hdr, payload, payloadSize)
           == UdpAuthResult::Unauthenticated;
}

} // namespace audio21
