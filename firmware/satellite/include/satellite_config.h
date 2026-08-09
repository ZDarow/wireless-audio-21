// satellite_config.h — специфичная конфигурация сателлита.
#pragma once

#include "node_config.h"

namespace audio21 {

// GPIO для I2S сателлита.
struct SatellitePins {
    uint8_t bck;
    uint8_t ws;
    uint8_t dataOut;
};

inline SatellitePins satellitePins(const NodeConfig& cfg) {
    return SatellitePins{cfg.i2sBck, cfg.i2sWs, cfg.i2sDataOut};
}

} // namespace audio21