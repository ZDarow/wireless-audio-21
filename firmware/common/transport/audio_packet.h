// audio_packet.h — формат пакета беспроводной передачи (спецификация §6.8).
// Header-only.
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace audio21 {

// Формат пакета (little-endian):
//   Offset  Size  Field
//   0       2     magic
//   2       1     protocol_version
//   3       1     flags
//   4       4     timestamp_ms
//   8       4     packet_id
//   12      1     channel
//   13      1     sample_format
//   14      2     payload_length
//   16      N     PCM payload
struct AudioPacketHeader {
    uint16_t magic;          // 0x2151 ("21")
    uint8_t protocolVersion; // 1
    uint8_t flags;           // 0x01 = EOF, 0x02 = keyframe,
                             // 0x04 = discovery request, 0x08 = discovery response
    uint32_t timestampMs;    // метка времени источника
    uint32_t packetId;       // монотонный счётчик пакетов
    uint8_t channel;         // 0x01 = left, 0x02 = right
    uint8_t sampleFormat;    // 0x00 = int16, 0x01 = float32
    uint16_t payloadLength;  // байт полезной нагрузки
} __attribute__((packed));

static_assert(sizeof(AudioPacketHeader) == 16, "AudioPacketHeader must be 16 bytes");

// Предельные размеры
constexpr uint16_t kPacketMagic = 0x2151;
constexpr uint8_t kProtocolVersion = 1;

// Флаги
constexpr uint8_t kFlagEof = 0x01;
constexpr uint8_t kFlagKeyframe = 0x02;
constexpr uint8_t kFlagDiscoveryRequest = 0x04;  // мастер → сателлиты (broadcast)
constexpr uint8_t kFlagDiscoveryResponse = 0x08; // сателлит → мастер (unicast)

// Каналы
constexpr uint8_t kChannelLeft = 0x01;
constexpr uint8_t kChannelRight = 0x02;

// Форматы семплов
constexpr uint8_t kSampleFormatInt16 = 0x00;
constexpr uint8_t kSampleFormatFloat32 = 0x01;

// Максимальный полезный размер пакета.
// ESP-NOW: до 250 байт всего, значит payload ≤ 234 байт.
constexpr size_t kMaxPacketPayload = 234;
constexpr size_t kMaxPacketSize = sizeof(AudioPacketHeader) + kMaxPacketPayload;

// Максимальное число семплов int16 в одном пакете
constexpr size_t kMaxInt16Samples = kMaxPacketPayload / 2;

// Heartbeat (связь без аудио-пакетов): сателлит шлёт discovery-response
// каждые kHeartbeatIntervalMs, мастер считает его online в течение
// kSatelliteTimeoutMs после последнего heartbeat. Общие для мастера и
// сателлитов (интервал < таймаут).
constexpr uint32_t kHeartbeatIntervalMs = 2000;
constexpr uint32_t kSatelliteTimeoutMs = 6000;

// Сборка пакета.
inline size_t buildPacket(uint8_t* dst, size_t dstSize,
                          uint8_t channel, uint8_t sampleFormat,
                          const void* payload, uint16_t payloadLength,
                          uint32_t timestampMs, uint32_t packetId,
                          uint8_t flags = 0) {
    if (dstSize < sizeof(AudioPacketHeader) + payloadLength) return 0;
    if (payloadLength > kMaxPacketPayload) return 0;

    AudioPacketHeader h;
    h.magic = kPacketMagic;
    h.protocolVersion = kProtocolVersion;
    h.flags = flags;
    h.timestampMs = timestampMs;
    h.packetId = packetId;
    h.channel = channel;
    h.sampleFormat = sampleFormat;
    h.payloadLength = payloadLength;

    memcpy(dst, &h, sizeof(AudioPacketHeader));
    if (payload && payloadLength) memcpy(dst + sizeof(AudioPacketHeader), payload, payloadLength);
    return sizeof(AudioPacketHeader) + payloadLength;
}

// Валидация и разбор пакета. Возвращает true при корректном пакете.
inline bool parsePacket(const uint8_t* data, size_t size, AudioPacketHeader& hdr,
                        const uint8_t*& payload, size_t& payloadSize) {
    if (size < sizeof(AudioPacketHeader)) return false;
    memcpy(&hdr, data, sizeof(AudioPacketHeader));
    if (hdr.magic != kPacketMagic) return false;
    if (hdr.protocolVersion != kProtocolVersion) return false;
    if (sizeof(AudioPacketHeader) + hdr.payloadLength > size) return false;

    payload = data + sizeof(AudioPacketHeader);
    payloadSize = hdr.payloadLength;
    return true;
}

} // namespace audio21