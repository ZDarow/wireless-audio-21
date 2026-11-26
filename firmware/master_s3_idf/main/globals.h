#pragma once
#include <stdint.h>
#include <memory>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "nvs_config.h"
#include "pcm_pipeline.h"
#include "web_server.h"
#include "espnow_transport.h"
#include "udp_audio.h"
#include "delay_line.h"
#include "jitter_buffer.h"

using namespace audio21;

// Глобальное состояние (единый источник истины, R2)
extern NodeConfig g_cfg;
extern PcmPipeline g_pipeline;
extern std::unique_ptr<NvsConfig> g_nvsConfig;
extern std::unique_ptr<WebServer> g_webServer;
extern std::unique_ptr<EspNowTransport> g_espnow;
extern std::unique_ptr<UdpAudio> g_udpAudio;
extern std::unique_ptr<DelayLine> g_delayLeft;
extern std::unique_ptr<DelayLine> g_delayRight;
extern std::unique_ptr<DelayLine> g_delaySub;
extern std::unique_ptr<JitterBuffer> g_jitter;

extern bool g_wifi_inited;
extern char g_staIpAddr[16];
extern bool g_i2sOn;
extern int g_crossoverHz;
extern bool g_leftOnline;
extern bool g_rightOnline;
extern uint32_t g_lastLeftSeenMs;
extern uint32_t g_lastRightSeenMs;
extern std::atomic<uint32_t> g_heartbeatsRx;
extern std::atomic<uint32_t> g_heartbeatsTx;

extern bool g_nvsDirty;
extern bool g_nvsCritical;
extern uint32_t g_nvsLastChangeMs;

extern int g_volumePercent;
extern int g_crossoverHz;
extern uint32_t g_txPacketId;
extern uint32_t g_txCount;
extern uint32_t g_txPackets;

extern std::atomic<bool> g_sta_connecting;
extern uint32_t g_sta_connect_start_ms;

extern char g_adminUsername[32];
extern char g_adminPasswordHash[65];
extern char g_adminSalt[33];
extern bool g_adminSetupDone;

// Консоль
