// broadcast_ip.h — чистая логика вычисления broadcast-адреса IPv4.
// Header-only, без зависимостей от Arduino — тестируется на хосте.
//
// Broadcast = (ip & mask) | ~mask. Корректен для любой маски подсети
// (не только /24, как наивное ~ip + bc[3]=255).
#pragma once

#include <stdint.h>

namespace audio21 {

// Вычислить broadcast-адрес по IP и маске подсети (оба — 4 байта, сетевой
// порядок). Результат — в out[4].
inline void computeBroadcastAddress(const uint8_t ip[4], const uint8_t mask[4],
                                    uint8_t out[4]) {
    for (int i = 0; i < 4; i++) {
        out[i] = static_cast<uint8_t>((ip[i] & mask[i]) | (~mask[i] & 0xFF));
    }
}

} // namespace audio21