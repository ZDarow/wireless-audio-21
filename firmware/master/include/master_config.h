// master_config.h — специфичная конфигурация мастер-узла.
#pragma once

#include "node_config.h"

namespace audio21 {

// GPIO для I2S сабвуфера (мастер).
struct MasterPins {
    uint8_t bck;
    uint8_t ws;
    uint8_t dataOut;
};

// Дефолтные пины мастера (из конфигурации узла).
inline MasterPins masterPins(const NodeConfig& cfg) {
    return MasterPins{cfg.i2sBck, cfg.i2sWs, cfg.i2sDataOut};
}

} // namespace audio21