// transport_packet_test.cpp — host-тест формата аудио-пакета (audio_packet.h).
#include <stdio.h>
#include <string.h>

#include "arduino_stub.h"
#include "audio_packet.h"
#include "broadcast_ip.h"

using namespace audio21;

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_failures++; } \
} while (0)

int main() {
    printf("== transport_packet_test ==\n");

    // Размер заголовка ровно 16 байт.
    CHECK(sizeof(AudioPacketHeader) == 16, "заголовок = 16 байт");
    CHECK(kMaxPacketSize == 16 + 234, "макс. размер пакета = 250 байт (ESP-NOW)");

    // Сборка пакета с int16 payload.
    uint8_t buf[kMaxPacketSize];
    int16_t pcm[4] = {100, -200, 300, -400};
    size_t sz = buildPacket(buf, sizeof(buf), kChannelLeft, kSampleFormatInt16,
                            pcm, sizeof(pcm), 12345, 7);
    CHECK(sz == 16 + 8, "размер собранного пакета = 16 + 8 байт");

    // Разбор.
    AudioPacketHeader hdr;
    const uint8_t* payload;
    size_t payloadSize;
    CHECK(parsePacket(buf, sz, hdr, payload, payloadSize), "пакет парсится");
    CHECK(hdr.magic == kPacketMagic, "magic корректен");
    CHECK(hdr.protocolVersion == kProtocolVersion, "версия корректна");
    CHECK(hdr.channel == kChannelLeft, "канал left");
    CHECK(hdr.sampleFormat == kSampleFormatInt16, "формат int16");
    CHECK(hdr.timestampMs == 12345, "timestamp корректен");
    CHECK(hdr.packetId == 7, "packetId корректен");
    CHECK(payloadSize == 8, "размер payload = 8 байт");
    CHECK(memcmp(payload, pcm, 8) == 0, "payload совпадает");

    // Граничные случаи.
    uint8_t tiny[5];
    CHECK(buildPacket(tiny, sizeof(tiny), kChannelLeft, kSampleFormatInt16, pcm, 8, 0, 0) == 0,
          "маленький буфер → сборка не удаётся");
    CHECK(buildPacket(buf, sizeof(buf), kChannelLeft, kSampleFormatInt16, nullptr, 235, 0, 0) == 0,
          "payload > 234 → сборка не удаётся");

    // Испорченный magic → парсинг отклоняется.
    buf[0] = 0x00;
    CHECK(!parsePacket(buf, sz, hdr, payload, payloadSize), "битый magic отклоняется");

    // Слишком короткий буфер → отклоняется.
    CHECK(!parsePacket(buf, 10, hdr, payload, payloadSize), "короткий буфер отклоняется");

    // Float32 формат тоже собирается/парсится.
    float fdata[2] = {0.5f, -0.25f};
    sz = buildPacket(buf, sizeof(buf), kChannelRight, kSampleFormatFloat32,
                     fdata, sizeof(fdata), 1, 2);
    CHECK(parsePacket(buf, sz, hdr, payload, payloadSize), "float32 пакет парсится");
    CHECK(hdr.sampleFormat == kSampleFormatFloat32, "формат float32");
    CHECK(hdr.channel == kChannelRight, "канал right");
    CHECK(payloadSize == 8, "float32 payload = 8 байт");

    // Broadcast-адрес: корректный для любой маски (не только /24).
    uint8_t bc[4];
    const uint8_t ipA[4] = {192, 168, 1, 5};
    const uint8_t mask24[4] = {255, 255, 255, 0};
    computeBroadcastAddress(ipA, mask24, bc);
    CHECK(bc[0] == 192 && bc[1] == 168 && bc[2] == 1 && bc[3] == 255,
          "broadcast /24: 192.168.1.5 -> 192.168.1.255");

    const uint8_t ipB[4] = {10, 0, 0, 1};
    const uint8_t mask8[4] = {255, 0, 0, 0};
    computeBroadcastAddress(ipB, mask8, bc);
    CHECK(bc[0] == 10 && bc[1] == 255 && bc[2] == 255 && bc[3] == 255,
          "broadcast /8: 10.0.0.1 -> 10.255.255.255");

    const uint8_t ipC[4] = {192, 168, 10, 42};
    const uint8_t mask16[4] = {255, 255, 0, 0};
    computeBroadcastAddress(ipC, mask16, bc);
    CHECK(bc[0] == 192 && bc[1] == 168 && bc[2] == 255 && bc[3] == 255,
          "broadcast /16: 192.168.10.42 -> 192.168.255.255");

    // Маска /32 (один хост): broadcast = сам IP.
    const uint8_t mask32[4] = {255, 255, 255, 255};
    computeBroadcastAddress(ipA, mask32, bc);
    CHECK(bc[0] == 192 && bc[1] == 168 && bc[2] == 1 && bc[3] == 5,
          "broadcast /32 = сам IP");

    if (g_failures) {
        printf("ИТОГ: %d ПРОВАЛОВ\n", g_failures);
        return 1;
    }
    printf("ИТОГ: ВСЕ ТЕСТЫ ПРОЙДЕНЫ\n");
    return 0;
}