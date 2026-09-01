// transport.h — абстрактный интерфейс транспорта (T15).
//
// Позволяет использовать EspNowTransport и UdpTransport через единый
// указатель, убирая if/else по TransportMode из main.cpp.
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace audio21 {

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool begin() = 0;
    virtual void end() = 0;

    using RxCallback = void (*)(const uint8_t* data, size_t size, const void* addr, size_t addrLen);
    virtual void setRxCallback(RxCallback cb) = 0;

    using SentCallback = void (*)(const void* addr, bool success);
    virtual void setSentCallback(SentCallback cb) = 0;

    // Отправка на конкретный пир. addr — MacAddr (6) или IPAddress (4).
    virtual bool sendTo(const void* addr, size_t addrLen, const uint8_t* data, size_t len) = 0;

    // Широковещательная отправка.
    virtual bool broadcast(const uint8_t* data, size_t len) = 0;

    // Отправка на логический канал (left/right). Для ESP-NOW resolves MAC
    // по каналу; для UDP — по сохранённому IP.
    virtual bool sendToChannel(uint8_t channel, const uint8_t* data, size_t len) = 0;

    // Добавить пир (для ESP-NOW). Для UDP — no-op.
    virtual void addPeer(const void* addr, size_t addrLen) = 0;

    // Опрос входящих пакетов (для UDP). Для ESP-NOW — no-op.
    virtual void update() = 0;
};

} // namespace audio21
