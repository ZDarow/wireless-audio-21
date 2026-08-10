// master_s3_config.h — специфичная конфигурация мастер-узла ESP32-S3.
#pragma once

#include "node_config.h"

namespace audio21 {

// Фиксированный Wi-Fi канал (ТЗ §6.3: channel = 6, bandwidth = 20 MHz).
// На этом же канале должны работать сателлиты.
constexpr uint8_t kDefaultWifiChannel = 6;

// Пин-дефолты I2S мастера ESP32-S3 (переопределяются через config.env).
// Текущие значения взяты из конфигурации узла (node_config.h).
inline uint8_t masterI2SDataOut(const NodeConfig& cfg) { return cfg.i2sDataOut; }

} // namespace audio21
