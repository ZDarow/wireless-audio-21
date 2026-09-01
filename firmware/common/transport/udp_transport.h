// udp_transport.h — UDP транспорт для аудио (мастер TX / сателлит RX).
// Header-only. Работает поверх Arduino WiFiUDP.
//
// Discovery (спецификация §5.6): мастер шлёт broadcast-запрос
// (kFlagDiscoveryRequest), сателлиты отвечают unicast-ответом
// (kFlagDiscoveryResponse) со своим каналом; мастер запоминает их IP
// и далее шлёт аудио unicast-ом (fallback — broadcast, пока IP не известен).
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>

#include "audio_packet.h"
#include "node_config.h"
#include "broadcast_ip.h"
#include "transport.h"

namespace audio21 {

// Callback при получении UDP-пакета (сателлит).
using UdpRxCallback = void (*)(const uint8_t* data, size_t size, IPAddress from);

class UdpTransport : public ITransport {
public:
    static constexpr uint16_t kDefaultPort = 4210;
    static constexpr uint8_t kAddrLen = 4;

    bool begin() override {
        m_leftIp = IPAddress(0, 0, 0, 0);
        m_rightIp = IPAddress(0, 0, 0, 0);
        return m_udp.begin(kDefaultPort);
    }

    bool begin(uint16_t localPort) {
        m_leftIp = IPAddress(0, 0, 0, 0);
        m_rightIp = IPAddress(0, 0, 0, 0);
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

    // Отправка широковещательно (для discovery). Broadcast вычисляется по
    // маске подсети (корректно для любой маски, не только /24).
    bool broadcast(uint16_t port, const uint8_t* data, size_t size) {
        IPAddress lip = WiFi.localIP();
        IPAddress lmask = WiFi.subnetMask();
        uint8_t ip[4] = {lip[0], lip[1], lip[2], lip[3]};
        uint8_t mask[4] = {lmask[0], lmask[1], lmask[2], lmask[3]};
        uint8_t bc[4];
        computeBroadcastAddress(ip, mask, bc);
        IPAddress bcIp(bc[0], bc[1], bc[2], bc[3]);
        return sendTo(bcIp, port, data, size);
    }

    // Отправка мастером аудио: unicast на запомненный IP сателлита канала,
    // либо broadcast, пока IP не известен (discovery не завершён).
    bool sendToChannel(uint8_t channel, uint16_t port, const uint8_t* data, size_t size) {
        IPAddress ip = (channel == kChannelLeft) ? m_leftIp : m_rightIp;
        if (ip != IPAddress(0, 0, 0, 0)) return sendTo(ip, port, data, size);
        return broadcast(port, data, size);
    }

    // Мастер: broadcast discovery-запрос (сателлиты отвечают unicast-ом).
    bool sendDiscoveryRequest(uint16_t port = kDefaultPort) {
        uint8_t buf[kMaxPacketSize];
        size_t n = buildPacket(buf, sizeof(buf), 0x00, kSampleFormatInt16,
                               nullptr, 0, 0, 0, kFlagDiscoveryRequest);
        return broadcast(port, buf, n);
    }

    // Сателлит: unicast discovery-ответ со своим каналом.
    bool sendDiscoveryResponse(IPAddress to, uint8_t channel) {
        uint8_t buf[kMaxPacketSize];
        size_t n = buildPacket(buf, sizeof(buf), channel, kSampleFormatInt16,
                               nullptr, 0, 0, 0, kFlagDiscoveryResponse);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&to);
        return sendTo(to, kDefaultPort, buf, n);
    }

    // Обработка discovery-пакета. Возвращает true, если пакет был discovery
    // (аудио обрабатывать не нужно).
    //   - мастер: запоминает IP сателлита из response;
    //   - сателлит: отвечает на request (m_myChannel должен быть задан).
    bool handleDiscovery(const uint8_t* data, size_t size, IPAddress from) {
        AudioPacketHeader hdr;
        const uint8_t* payload;
        size_t payloadSize;
        if (!parsePacket(data, size, hdr, payload, payloadSize)) return false;

        if (hdr.flags & kFlagDiscoveryRequest) {
            if (m_myChannel != 0) sendDiscoveryResponse(from, m_myChannel);
            return true;
        }
        if (hdr.flags & kFlagDiscoveryResponse) {
            m_lastDiscChannel = hdr.channel;
            if (hdr.channel == kChannelLeft) m_leftIp = from;
            else if (hdr.channel == kChannelRight) m_rightIp = from;
            return true;
        }
        return false;
    }

    // Канал последнего discovery-ответа (0, если ответа не было).
    uint8_t lastDiscoveryChannel() const { return m_lastDiscChannel; }

    // Сателлит: задать свой канал для ответа на discovery-запросы.
    void setMyChannel(uint8_t channel) { m_myChannel = channel; }

    // Мастер: известен ли IP сателлита канала.
    bool hasSatellite(uint8_t channel) const {
        IPAddress ip = (channel == kChannelLeft) ? m_leftIp : m_rightIp;
        return ip != IPAddress(0, 0, 0, 0);
    }

    IPAddress lastFrom() const { return m_lastFrom; }

    void stop() { m_udp.stop(); }

    // ITransport implementation
    void end() override { m_udp.stop(); }

    void setRxCallback(RxCallback cb) override { m_itRx = cb; }
    void setSentCallback(SentCallback /*cb*/) override {}

    bool sendTo(const void* addr, size_t addrLen, const uint8_t* data, size_t len) override {
        if (addrLen != kAddrLen || !addr) return false;
        IPAddress ip(static_cast<const uint8_t*>(addr)[0],
                     static_cast<const uint8_t*>(addr)[1],
                     static_cast<const uint8_t*>(addr)[2],
                     static_cast<const uint8_t*>(addr)[3]);
        return sendTo(ip, kDefaultPort, data, len);
    }

    bool broadcast(const uint8_t* data, size_t len) override {
        return broadcast(kDefaultPort, data, len);
    }

    bool sendToChannel(uint8_t channel, const uint8_t* data, size_t len) override {
        return sendToChannel(channel, kDefaultPort, data, len);
    }

    void update() override {
        uint8_t buf[kMaxPacketSize];
        size_t n = receive(buf, sizeof(buf));
        if (n == 0) return;
        IPAddress from = m_lastFrom;
        if (m_itRx) m_itRx(buf, n, &from, sizeof(from));
    }

private:
    WiFiUDP m_udp;
    IPAddress m_lastFrom;
    IPAddress m_leftIp;
    IPAddress m_rightIp;
    uint8_t m_myChannel = 0;
    uint8_t m_lastDiscChannel = 0;
    RxCallback m_itRx = nullptr;
};

} // namespace audio21