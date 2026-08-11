// udp_audio_packet.h — формат UDP-пакета смартфон → мастер (спецификация §9.1).
// Header-only, без зависимостей от Arduino — тестируется на хосте.
#pragma once

#include <stdint.h>
#include <string.h>

namespace audio21 {

// Формат пакета (little-endian, магия 0xA210):
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
struct UdpAudioHeader {
    uint16_t magic;            // 0xA210
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
constexpr uint16_t kUdpMagic = 0xA210;
constexpr uint8_t  kUdpProtocolVersion = 1;
constexpr size_t   kUdpMaxPayload = 1200;
constexpr size_t   kUdpPacketSize = sizeof(UdpAudioHeader) + kUdpMaxPayload;

// Флаги (§9.1).
constexpr uint8_t kUdpFlagEndOfStream = 0x01;
constexpr uint8_t kUdpFlagKeyframe    = 0x02;

// Сборка пакета. Возвращает размер (заголовок + payload) или 0 при ошибке.
inline size_t buildUdpPacket(uint8_t* dst, size_t dstSize,
                             uint32_t sequence, uint32_t timestampSamples,
                             uint16_t sampleRate, uint8_t channels,
                             uint8_t bitsPerSample,
                             const void* payload, uint16_t payloadLength,
                             uint8_t flags = 0) {
    if (payloadLength > kUdpMaxPayload) return 0;
    if (dstSize < sizeof(UdpAudioHeader) + payloadLength) return 0;

    UdpAudioHeader h;
    h.magic = kUdpMagic;
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
    return sizeof(UdpAudioHeader) + payloadLength;
}

// Валидация и разбор пакета. Возвращает true при корректном пакете.
inline bool parseUdpPacket(const uint8_t* data, size_t size, UdpAudioHeader& hdr,
                           const uint8_t*& payload, size_t& payloadSize) {
    if (size < sizeof(UdpAudioHeader)) return false;
    memcpy(&hdr, data, sizeof(UdpAudioHeader));
    if (hdr.magic != kUdpMagic) return false;
    if (hdr.protocolVersion != kUdpProtocolVersion) return false;
    if (hdr.payloadLength > kUdpMaxPayload) return false;
    if (sizeof(UdpAudioHeader) + hdr.payloadLength > size) return false;

    payload = data + sizeof(UdpAudioHeader);
    payloadSize = hdr.payloadLength;
    return true;
}

} // namespace audio21
