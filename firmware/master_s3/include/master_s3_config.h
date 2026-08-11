// master_s3_config.h — специфичная конфигурация мастер-узла ESP32-S3.
#pragma once

#include "node_config.h"

namespace audio21 {

// Канал Wi-Fi/ESP-NOW из конфига (config.env → AUDIO_ESPNOW_CHANNEL).
// На этом же канале должны работать сателлиты. В APSTA канал AP следует за
// STA-каналом домашней сети, поэтому в config.env задаётся канал роутера.
constexpr uint8_t kDefaultWifiChannel = AUDIO_ESPNOW_CHANNEL;

// Пин-дефолты I2S мастера ESP32-S3 (переопределяются через config.env).
// Текущие значения взяты из конфигурации узла (node_config.h).
inline uint8_t masterI2SDataOut(const NodeConfig& cfg) { return cfg.i2sDataOut; }

} // namespace audio21
