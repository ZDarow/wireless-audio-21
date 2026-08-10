// Wireless Audio 2.1 — пример конфигурации (ручной режим)
// Скопируйте в firmware/common/generated/generated_config.h
// и отредактируйте. Либо сгенерируйте автоматически:
//   python3 scripts/generate_config.py config.example.env
//   python3 scripts/generate_config.py config.env firmware/common/generated/generated_config.h
#pragma once

// --- Роль и режимы ---
#define AUDIO_NODE_ROLE_MASTER    1   // 1 = мастер, 0 = сателлит
#define AUDIO_SOURCE_MODE_A2DP    0   // 1 = A2DP, 0 = Wi-Fi (S3: только Wi-Fi)
#define AUDIO_TRANSPORT_MODE_ESPNOW 1 // 1 = ESP-NOW, 0 = UDP
#define AUDIO_WIFI_MODE_AP        1   // 1 = AP_DIRECT, 0 = STA

// --- Wi-Fi ---
#define AUDIO_WIFI_SSID      "Audio21-Master"
#define AUDIO_WIFI_PASSWORD  "audio21master"
#define AUDIO_HOSTNAME       "audio-master"
#define AUDIO_UDP_PORT       5004

// --- Сателлиты (MAC) ---
#define AUDIO_LEFT_SAT_MAC   {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01}
#define AUDIO_RIGHT_SAT_MAC  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02}

// --- Формат PCM ---
#define AUDIO_SAMPLE_RATE        48000
#define AUDIO_BITS_PER_SAMPLE    16
#define AUDIO_CHANNELS           2

// --- Кроссовер и задержки ---
#define AUDIO_CROSSOVER_HZ       90
#define AUDIO_DELAY_LEFT_MS      0
#define AUDIO_DELAY_RIGHT_MS     0
#define AUDIO_DELAY_SUB_MS       0

// --- GPIO I2S ---
#define AUDIO_I2S_BCK            26
#define AUDIO_I2S_WS             25
#define AUDIO_I2S_DATA_OUT       22

// --- OLED ---
#define AUDIO_OLED_SDA           21
#define AUDIO_OLED_SCL           22

// --- Энкодер ---
#define AUDIO_ENC_A              32
#define AUDIO_ENC_B              33
#define AUDIO_ENC_BTN            34