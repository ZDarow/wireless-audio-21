#include "console_handler.h"
#include "globals.h"

// Callbacks определены в main.cpp
extern esp_err_t getStatusCallback(char* buf, size_t bufSize);
extern esp_err_t setDelayCallback(const char* channel, int delayMs);
extern esp_err_t setMuteCallback(bool mute);
extern esp_err_t setVolumeCallback(int volume);
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_log.h"
#include "cJSON.h"
#include "espnow_transport.h"

static const char* TAG = "console";

static char g_consoleLine[64];
static int g_consolePos = 0;

void consolePrint(const char* s) {
    uart_write_bytes(UART_NUM_0, s, strlen(s));
}

static void consoleProcessLine(char* line) {
    // Trim
    char* end = line + strlen(line) - 1;
    while (end >= line && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
    char* start = line;
    while (*start == ' ') start++;
    if (start[0] == '\0') return;

    // Парсим команду и аргументы
    char* cmd = start;
    char* arg1 = nullptr;
    char* arg2 = nullptr;
    char* sp = strchr(start, ' ');
    if (sp) { *sp = '\0'; arg1 = sp + 1; char* sp2 = strchr(arg1, ' '); if (sp2) { *sp2 = '\0'; arg2 = sp2 + 1; } }

    if (strcmp(cmd, "status") == 0) {
        char buf[256];
        int len = getStatusCallback(buf, sizeof(buf));
        consolePrint("status: ");
        consolePrint(buf);
        consolePrint("\n");
    } else if (strcmp(cmd, "wifi") == 0) {
        consolePrint("Wi-Fi mode: ");
        consolePrint(g_wifi_inited ? (g_staIpAddr[0] ? "sta" : "ap") : "off");
        consolePrint(", IP: ");
        consolePrint(g_staIpAddr[0] ? g_staIpAddr : "—");
        consolePrint(", AP SSID: ");
        consolePrint(g_cfg.wifiApSsid);
        consolePrint("\n");
    } else if (strcmp(cmd, "setsat") == 0) {
        if (!arg1 || !arg2) {
            consolePrint("usage: setsat <left|right|sub> <delay_ms>\n");
            return;
        }
        int ms = atoi(arg2);
        if (setDelayCallback(arg1, ms) == ESP_OK) {
            consolePrint("delay set\n");
        } else {
            consolePrint("error\n");
        }
    } else if (strcmp(cmd, "net") == 0) {
        consolePrint("IP: ");
        consolePrint(g_staIpAddr[0] ? g_staIpAddr : "—");
        consolePrint(", satellites: L=");
        consolePrint(g_leftOnline ? "online" : "offline");
        consolePrint(", R=");
        consolePrint(g_rightOnline ? "online" : "offline");
        consolePrint("\n");
    } else if (strcmp(cmd, "erase") == 0) {
        esp_err_t ret = g_nvsConfig->erase();
        consolePrint(ret == ESP_OK ? "NVS erased\n" : "erase failed\n");
    } else if (strcmp(cmd, "tone") == 0) {
        if (!arg1 || !arg2) {
            consolePrint("usage: tone <bass_db> <treble_db>\n");
            return;
        }
        float bass = atof(arg1);
        float treble = atof(arg2);
        g_pipeline.setTone(bass, treble);
        consolePrint("tone set\n");
    } else if (strcmp(cmd, "mute") == 0) {
        if (!arg1) {
            consolePrint(g_cfg.mute ? "mute: on\n" : "mute: off\n");
            return;
        }
        bool mute = atoi(arg1) != 0;
        setMuteCallback(mute);
        consolePrint(mute ? "mute: on\n" : "mute: off\n");
    } else if (strcmp(cmd, "volume") == 0) {
        if (!arg1) {
            consolePrint("volume: ");
            char buf[16];
            snprintf(buf, sizeof(buf), "%d\n", g_volumePercent);
            consolePrint(buf);
            return;
        }
        int vol = atoi(arg1);
        if (setVolumeCallback(vol) == ESP_OK) {
            char buf[32];
            snprintf(buf, sizeof(buf), "volume: %d%%\n", g_volumePercent);
            consolePrint(buf);
        } else {
            consolePrint("error\n");
        }
    } else if (strcmp(cmd, "pair") == 0) {
        uint8_t buf[16];
        AudioPacketHeader hdr = {};
        hdr.magic = kMagicDiscovery;
        hdr.sequence = 0;
        hdr.timestampMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        memcpy(buf, &hdr, sizeof(hdr));
        g_espnow->sendBroadcast(buf, sizeof(AudioPacketHeader));
        consolePrint("discovery sent\n");
    } else if (strcmp(cmd, "save") == 0) {
        g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        consolePrint("save queued (debounced)\n");
    } else if (strcmp(cmd, "reboot") == 0) {
        consolePrint("rebooting...\n");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else {
        consolePrint("unknown command\n");
    }
}

void consoleTick(void) {
    static std::atomic<bool> uart_installed{false};
    if (!uart_installed.load()) {
        uart_config_t uartCfg = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        uart_param_config(UART_NUM_0, &uartCfg);
        uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
        uart_installed.store(true);
    }

    uint8_t ch;
    int len = uart_read_bytes(UART_NUM_0, &ch, 1, 0);
    if (len <= 0) return;

    if (ch == '\n' || ch == '\r') {
        g_consoleLine[g_consolePos] = '\0';
        consoleProcessLine(g_consoleLine);
        g_consolePos = 0;
        g_consoleLine[0] = '\0';
    } else if (g_consolePos < (int)sizeof(g_consoleLine) - 1) {
        g_consoleLine[g_consolePos++] = (char)ch;
        g_consoleLine[g_consolePos] = '\0';
    }
}

void consoleInit(void) {
    // Ничего — consoleTick() сам инициализирует UART
}