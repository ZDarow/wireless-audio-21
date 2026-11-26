#pragma once
#include "esp_err.h"

// Wi-Fi управление (R1: декомпозиция main.cpp)
esp_err_t wifi_init(void);
esp_err_t wifi_reconfigure(void);
int wifiScanCallback(char* buf, size_t bufSize);
esp_err_t wifiSetModeCallback(const char* mode);
esp_err_t wifiSetApPasswordCallback(const char* password);
esp_err_t setHostnameCallback(const char* hostname);
int getWifiConfigCallback(char* buf, size_t bufSize);
esp_err_t setWifiConfigCallback(const char* mode, const char* apSsid, const char* apPassword,
                               const char* staSsid, const char* staPassword,
                               bool staDhcp, const char* staIp, const char* staGw,
                               const char* staMask, const char* staDns);
