// udp.h — UDP транспорт для аудио (мастер TX / сателлит RX).
// Header-only. Работает поверх Arduino WiFiUDP.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>

#include "audio_packet.h"
#include "node_config.h"

namespace audio21 {

// Callback при получении UDP-пакета (сателлит).
using UdpRxCallback = void (*)(const uint8_t* data, size_t size, IPAddress from);

class UdpTransport {
public:
    static constexpr uint16_t kDefaultPort = 4210;

    bool begin(uint16_t localPort = kDefaultPort) {
        return m_udp.begin(localPort);
    }

    // Приём: вызывать периодически. Возвращает размер полученного пакета
    // (0 — пакета нет) и заполняет data буфером.
    size_t receive(uint8_t* data, size_t maxSize) {
        int packetSize = m_udp.parsePacket();
        if (packetSize <= 0) return 0;
        if (static_cast<size_t>(packetSize) > maxSize) packetSize = maxSize;
        size_t n = m_udp.read(data, packetSize);
        m_lastFrom = m_udp.remoteIP();
        return n;
    }

    // Отправка мастером на конкретный IP сателлита.
    bool sendTo(IPAddress ip, uint16_t port, const uint8_t* data, size_t size) {
        m_udp.beginPacket(ip, port);
        m_udp.write(data, size);
        return m_udp.endPacket() == 1;
    }

    // Отправка широковещательно (для discovery).
    bool broadcast(uint16_t port, const uint8_t* data, size_t size) {
        IPAddress bc = ~WiFi.localIP();
        bc[3] = 255;
        return sendTo(bc, port, data, size);
    }

    IPAddress lastFrom() const { return m_lastFrom; }

    void stop() { m_udp.stop(); }

private:
    WiFiUDP m_udp;
    IPAddress m_lastFrom;
};

} // namespace audio21