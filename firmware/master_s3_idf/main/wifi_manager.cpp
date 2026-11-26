#include "wifi_manager.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "espnow_transport.h"
#include "web_server.h"
#include "dns_captive.h"
#include "mdns_raw.h"

static const char* TAG = "wifi_manager";

static void enable_napt(void);
static void log_default_password_warning(void);

int wifiScanCallback(char* buf, size_t bufSize)
{
    // Сканирование Wi-Fi — блокирующее, возвращает JSON со списком сетей.
    // Вызывается из контекста HTTP обработчика, поэтому используем короткое сканирование.
    uint16_t ap_count = 0;
    esp_err_t ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK || ap_count == 0) {
        int len = snprintf(buf, bufSize, "{\"networks\":[]}");
        return len;
    }
    wifi_ap_record_t ap_list[8];
    uint16_t count = ap_count > 8 ? 8 : ap_count;
    ret = esp_wifi_scan_get_ap_records(&count, ap_list);
    if (ret != ESP_OK) {
        int len = snprintf(buf, bufSize, "{\"networks\":[]}");
        return len;
    }
    int len = snprintf(buf, bufSize, "{\"networks\":[");
    for (uint16_t i = 0; i < count; i++) {
        const char *auth_str;
        switch (ap_list[i].authmode) {
            case WIFI_AUTH_OPEN:          auth_str = "OPEN"; break;
            case WIFI_AUTH_WEP:           auth_str = "WEP"; break;
            case WIFI_AUTH_WPA_PSK:       auth_str = "WPA-PSK"; break;
            case WIFI_AUTH_WPA2_PSK:      auth_str = "WPA2-PSK"; break;
            case WIFI_AUTH_WPA_WPA2_PSK:  auth_str = "WPA/WPA2-PSK"; break;
            case WIFI_AUTH_WPA3_PSK:      auth_str = "WPA3-PSK"; break;
            case WIFI_AUTH_WPA2_WPA3_PSK: auth_str = "WPA2/WPA3-PSK"; break;
            default:                      auth_str = "UNKNOWN"; break;
        }
        int item_len = snprintf(buf + len, bufSize - len,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%d,\"security\":\"%s\"}",
            i > 0 ? "," : "",
            ap_list[i].ssid, ap_list[i].rssi, ap_list[i].primary, auth_str);
        len += item_len;
        if (len >= (int)bufSize) break;
    }
    snprintf(buf + len, bufSize - len, "]}");
    return strlen(buf);
}
esp_err_t wifiSetModeCallback(const char* mode)
{
    if (strcmp(mode, "ap") == 0) {
        g_cfg.wifiMode = WifiMode::ApDirect;
    } else if (strcmp(mode, "sta") == 0) {
        g_cfg.wifiMode = WifiMode::Sta;
    } else if (strcmp(mode, "apsta") == 0) {
        g_cfg.wifiMode = WifiMode::ApSta;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "wifi mode set to %s (saved to NVS, reboot to apply)", mode);
    // Переконфигурируем Wi-Fi без перезагрузки
    return wifi_reconfigure();
}
esp_err_t wifiSetApPasswordCallback(const char* password)
{
    if (!password || strlen(password) < 8 || strlen(password) >= sizeof(g_cfg.wifiApPassword)) {
        return ESP_ERR_INVALID_ARG;
    }
    strcpy(g_cfg.wifiApPassword, password);
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    g_nvsCritical = true;
    ESP_LOGI(TAG, "AP password changed (saved to NVS, reboot to apply)");
    return ESP_OK;
}
esp_err_t setHostnameCallback(const char* hostname)
{
    if (!hostname || strlen(hostname) == 0 || strlen(hostname) >= sizeof(g_cfg.hostname)) {
        return ESP_ERR_INVALID_ARG;
    }
    strcpy(g_cfg.hostname, hostname);
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "hostname set to %s (saved to NVS)", hostname);
    return ESP_OK;
}
int getWifiConfigCallback(char* buf, size_t bufSize)
{
    const char* modeStr = "ap";
    if (g_cfg.wifiMode == WifiMode::Sta) modeStr = "sta";
    else if (g_cfg.wifiMode == WifiMode::ApSta) modeStr = "apsta";
    int len = snprintf(buf, bufSize,
        "{\"mode\":\"%s\",\"apSsid\":\"%s\",\"apPassword\":\"%s\","
        "\"staSsid\":\"%s\",\"staPassword\":\"%s\","
        "\"staDhcp\":\"%s\",\"staIp\":\"%s\",\"staGateway\":\"%s\","
        "\"staNetmask\":\"%s\",\"staDns\":\"%s\"}",
        modeStr, g_cfg.wifiApSsid, g_cfg.wifiApPassword,
        g_cfg.wifiSsid, g_cfg.wifiPassword,
        g_cfg.wifiStaDhcp ? "true" : "false",
        g_cfg.wifiStaIp, g_cfg.wifiStaGateway,
        g_cfg.wifiStaNetmask, g_cfg.wifiStaDns);
    return len;
}
esp_err_t setWifiConfigCallback(const char* mode,
                                       const char* apSsid,
                                       const char* apPassword,
                                       const char* staSsid,
                                       const char* staPassword,
                                       const char* staDhcp,
                                       const char* staIp,
                                       const char* staGateway,
                                       const char* staNetmask,
                                       const char* staDns)
{
    ESP_LOGW(TAG, "Wi-Fi config requested: mode=%s", mode ? mode : "ap");

    if (apSsid) {
        strncpy(g_cfg.wifiApSsid, apSsid, sizeof(g_cfg.wifiApSsid) - 1);
        g_cfg.wifiApSsid[sizeof(g_cfg.wifiApSsid) - 1] = '\0';
    }
    if (apPassword) {
        strncpy(g_cfg.wifiApPassword, apPassword, sizeof(g_cfg.wifiApPassword) - 1);
        g_cfg.wifiApPassword[sizeof(g_cfg.wifiApPassword) - 1] = '\0';
    }
    if (staSsid) {
        strncpy(g_cfg.wifiSsid, staSsid, sizeof(g_cfg.wifiSsid) - 1);
        g_cfg.wifiSsid[sizeof(g_cfg.wifiSsid) - 1] = '\0';
    }
    if (staPassword) {
        strncpy(g_cfg.wifiPassword, staPassword, sizeof(g_cfg.wifiPassword) - 1);
        g_cfg.wifiPassword[sizeof(g_cfg.wifiPassword) - 1] = '\0';
    }
    if (staDhcp) {
        g_cfg.wifiStaDhcp = (strcmp(staDhcp, "true") == 0);
    }
    if (staIp) {
        strncpy(g_cfg.wifiStaIp, staIp, sizeof(g_cfg.wifiStaIp) - 1);
        g_cfg.wifiStaIp[sizeof(g_cfg.wifiStaIp) - 1] = '\0';
    }
    if (staGateway) {
        strncpy(g_cfg.wifiStaGateway, staGateway, sizeof(g_cfg.wifiStaGateway) - 1);
        g_cfg.wifiStaGateway[sizeof(g_cfg.wifiStaGateway) - 1] = '\0';
    }
    if (staNetmask) {
        strncpy(g_cfg.wifiStaNetmask, staNetmask, sizeof(g_cfg.wifiStaNetmask) - 1);
        g_cfg.wifiStaNetmask[sizeof(g_cfg.wifiStaNetmask) - 1] = '\0';
    }
    if (staDns) {
        strncpy(g_cfg.wifiStaDns, staDns, sizeof(g_cfg.wifiStaDns) - 1);
        g_cfg.wifiStaDns[sizeof(g_cfg.wifiStaDns) - 1] = '\0';
    }

    // Сохраняем в NVS (с дебаунсом — реальная запись в основном цикле)
    g_nvsDirty = true; g_nvsLastChangeMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    g_nvsCritical = true; // Wi-Fi конфиг — критическое изменение

    // Применяем без перезагрузки
    esp_err_t ret = wifi_reconfigure();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi config saved and applied: apSsid=%s, staDhcp=%d, staIp=%s",
             g_cfg.wifiApSsid, g_cfg.wifiStaDhcp, g_cfg.wifiStaIp);
    return ESP_OK;
}
esp_err_t wifi_init(void)
{
    if (!g_wifi_inited) {
        esp_err_t netif_ret = esp_netif_init();
        if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(netif_ret));
            return netif_ret;
        }
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        g_wifi_inited = true;
    }

    // Обработчики событий Wi-Fi (общие для всех режимов).
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, [](void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI("wifi_event", "Wi-Fi STA started");
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t* conn = (wifi_event_sta_connected_t*)event_data;
            ESP_LOGI("wifi_event", "Wi-Fi connected to SSID=%s, channel=%d",
                     conn->ssid, conn->channel);
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* disc = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW("wifi_event", "Wi-Fi disconnected from SSID=%s, reason=%d, reconnecting...",
                     disc->ssid, disc->reason);
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI("wifi_event", "Wi-Fi AP started");
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t* conn = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI("wifi_event", "Station %02X:%02X:%02X:%02X:%02X:%02X joined AP",
                     conn->mac[0], conn->mac[1], conn->mac[2],
                     conn->mac[3], conn->mac[4], conn->mac[5]);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t* conn = (wifi_event_ap_stadisconnected_t*)event_data;
            ESP_LOGI("wifi_event", "Station %02X:%02X:%02X:%02X:%02X:%02X left AP",
                     conn->mac[0], conn->mac[1], conn->mac[2],
                     conn->mac[3], conn->mac[4], conn->mac[5]);
        }
    }, nullptr));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, [](void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
        ip_event_got_ip_t* ip_event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI("wifi_event", "Got IPv4 address: " IPSTR, IP2STR(&ip_event->ip_info.ip));
        snprintf(g_staIpAddr, sizeof(g_staIpAddr), IPSTR, IP2STR(&ip_event->ip_info.ip));
        g_sta_connecting = false;
    }, nullptr));

    // Настраиваем режим в зависимости от конфига.
    wifi_mode_t mode;
    switch (g_cfg.wifiMode) {
        case WifiMode::ApDirect:
            mode = WIFI_MODE_AP;
            break;
        case WifiMode::Sta:
            mode = WIFI_MODE_STA;
            break;
        case WifiMode::ApSta:
        default:
            mode = WIFI_MODE_APSTA;
            break;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    // --- AP настройка (для ApDirect и ApSta) ---
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        esp_netif_create_default_wifi_ap();
        wifi_config_t ap_config = {};
        strcpy((char*)ap_config.ap.ssid, g_cfg.wifiApSsid);
        strcpy((char*)ap_config.ap.password, g_cfg.wifiApPassword);
        ap_config.ap.ssid_len = (uint8_t)strlen(g_cfg.wifiApSsid);
        ap_config.ap.channel = 6;
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        ap_config.ap.ssid_hidden = 0;
        ap_config.ap.max_connection = 4;
        ap_config.ap.beacon_interval = 100;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_LOGI(TAG, "Wi-Fi AP '%s' configured", g_cfg.wifiApSsid);
        log_default_password_warning();
    }

    // --- STA настройка (для Sta и ApSta) ---
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        esp_netif_create_default_wifi_sta();
        wifi_config_t sta_config = {};
        strcpy((char*)sta_config.sta.ssid, g_cfg.wifiSsid);
        strcpy((char*)sta_config.sta.password, g_cfg.wifiPassword);
        sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sta_config.sta.pmf_cfg.capable = false;
        sta_config.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_LOGI(TAG, "Wi-Fi STA connecting to SSID=%s", g_cfg.wifiSsid);

        // Статический IP для STA (если DHCP отключён)
        if (!g_cfg.wifiStaDhcp) {
            esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (sta_netif) {
                esp_netif_dhcps_stop(sta_netif);
                esp_netif_ip_info_t ip_info = {};
                ip_info.ip.addr = ipaddr_addr(g_cfg.wifiStaIp);
                ip_info.gw.addr = ipaddr_addr(g_cfg.wifiStaGateway);
                ip_info.netmask.addr = ipaddr_addr(g_cfg.wifiStaNetmask);
                esp_netif_set_ip_info(sta_netif, &ip_info);
                esp_netif_dns_info_t dns_info;
                dns_info.ip.u_addr.ip4.addr = ipaddr_addr(g_cfg.wifiStaDns);
                dns_info.ip.type = IPADDR_TYPE_V4;
                esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
                ESP_LOGI(TAG, "Static IP configured: %s/%s gw=%s dns=%s",
                         g_cfg.wifiStaIp, g_cfg.wifiStaNetmask,
                         g_cfg.wifiStaGateway, g_cfg.wifiStaDns);
            }
        }
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    // Сканируем сети (неблокирующе) — результаты доступны через /api/wifi/scan.
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_scan_config_t scan_cfg = {};
        scan_cfg.ssid = NULL;
        scan_cfg.bssid = NULL;
        scan_cfg.channel = 0;
        scan_cfg.show_hidden = true;
        esp_wifi_scan_start(&scan_cfg, /*blocking=*/false);
    }

    // Подключаемся к STA сети (если режим включает STA).
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        g_sta_connecting = true;
        g_sta_connect_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    // NAPT для APSTA репитера.
    if (mode == WIFI_MODE_APSTA) {
        enable_napt();
    }

    // Запускаем captive DNS для AP интерфейса.
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            dns_captive_start(ip_info.ip.addr);
            ESP_LOGI(TAG, "Captive DNS started, responding with " IPSTR, IP2STR(&ip_info.ip));
        }
    }

    return ESP_OK;
}
esp_err_t wifi_reconfigure(void)
{
    ESP_LOGW(TAG, "Wi-Fi reconfiguration requested");

    // Устанавливаем флаг переконфигурации — основной цикл не должен использовать сеть
    g_wifi_reconfiguring.store(true);

    // Проверяем, был ли Wi-Fi инициализирован
    if (!g_wifi_inited) {
        ESP_LOGW(TAG, "Wi-Fi not initialized, skipping reconfigure");
        g_wifi_reconfiguring.store(false);
        return ESP_OK;
    }

    // Останавливаем Wi-Fi
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Останавливаем captive DNS и mDNS перед изменением IP
    dns_captive_stop();
    mdns_raw_deinit();

    // Сбрасываем флаги подключения
    g_staIpAddr[0] = '\0';
    g_sta_connecting = false;
    g_leftOnline = false;
    g_rightOnline = false;

    // Настраиваем режим в зависимости от конфига.
    wifi_mode_t mode;
    switch (g_cfg.wifiMode) {
        case WifiMode::ApDirect:
            mode = WIFI_MODE_AP;
            break;
        case WifiMode::Sta:
            mode = WIFI_MODE_STA;
            break;
        case WifiMode::ApSta:
        default:
            mode = WIFI_MODE_APSTA;
            break;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    // --- AP настройка (для ApDirect и ApSta) ---
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        esp_netif_create_default_wifi_ap();
        wifi_config_t ap_config = {};
        strcpy((char*)ap_config.ap.ssid, g_cfg.wifiApSsid);
        strcpy((char*)ap_config.ap.password, g_cfg.wifiApPassword);
        ap_config.ap.ssid_len = (uint8_t)strlen(g_cfg.wifiApSsid);
        ap_config.ap.channel = 6;
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        ap_config.ap.ssid_hidden = 0;
        ap_config.ap.max_connection = 4;
        ap_config.ap.beacon_interval = 100;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_LOGI(TAG, "Wi-Fi AP '%s' configured", g_cfg.wifiApSsid);
        log_default_password_warning();
    }

    // --- STA настройка (для Sta и ApSta) ---
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        esp_netif_create_default_wifi_sta();
        wifi_config_t sta_config = {};
        strcpy((char*)sta_config.sta.ssid, g_cfg.wifiSsid);
        strcpy((char*)sta_config.sta.password, g_cfg.wifiPassword);
        sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sta_config.sta.pmf_cfg.capable = false;
        sta_config.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_LOGI(TAG, "Wi-Fi STA connecting to SSID=%s", g_cfg.wifiSsid);

        // Статический IP для STA (если DHCP отключён)
        if (!g_cfg.wifiStaDhcp) {
            esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (sta_netif) {
                esp_netif_dhcps_stop(sta_netif);
                esp_netif_ip_info_t ip_info = {};
                ip_info.ip.addr = ipaddr_addr(g_cfg.wifiStaIp);
                ip_info.gw.addr = ipaddr_addr(g_cfg.wifiStaGateway);
                ip_info.netmask.addr = ipaddr_addr(g_cfg.wifiStaNetmask);
                esp_netif_set_ip_info(sta_netif, &ip_info);
                esp_netif_dns_info_t dns_info;
                dns_info.ip.u_addr.ip4.addr = ipaddr_addr(g_cfg.wifiStaDns);
                dns_info.ip.type = IPADDR_TYPE_V4;
                esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
                ESP_LOGI(TAG, "Static IP configured: %s/%s gw=%s dns=%s",
                         g_cfg.wifiStaIp, g_cfg.wifiStaNetmask,
                         g_cfg.wifiStaGateway, g_cfg.wifiStaDns);
            }
        }
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    // Сканируем сети (неблокирующе) для Web UI.
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_scan_config_t scan_cfg = {};
        scan_cfg.ssid = NULL;
        scan_cfg.bssid = NULL;
        scan_cfg.channel = 0;
        scan_cfg.show_hidden = true;
        esp_wifi_scan_start(&scan_cfg, /*blocking=*/false);
    }

    // Подключаемся к STA сети (если режим включает STA).
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        g_sta_connecting = true;
        g_sta_connect_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    // NAPT для APSTA репитера.
    if (mode == WIFI_MODE_APSTA) {
        enable_napt();
    }

    // Запускаем captive DNS для AP интерфейса.
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            dns_captive_start(ip_info.ip.addr);
            ESP_LOGI(TAG, "Captive DNS restarted, responding with " IPSTR, IP2STR(&ip_info.ip));
        }
    }

    // Запускаем mDNS.
    esp_netif_t *mdns_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!mdns_netif) {
        mdns_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    }
    if (mdns_netif) {
        ret = mdns_raw_init(mdns_netif, g_cfg.hostname);
        if (ret == ESP_OK) {
            ret = mdns_raw_add_service("_http", "tcp", 80,
                                       "Wireless Audio 2.1 Master");
        }
    }

    ESP_LOGI(TAG, "Wi-Fi reconfigured: mode=%s, SSID=%s",
             g_cfg.wifiMode == WifiMode::ApDirect ? "ap" :
             g_cfg.wifiMode == WifiMode::Sta ? "sta" : "apsta",
             g_cfg.wifiApSsid);
    g_wifi_reconfiguring.store(false);
    return ESP_OK;
}


// Включить NAPT (сетевой адресный перевод) для APSTA режима репитера.
static void enable_napt(void)
{
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap != nullptr) {
        esp_err_t ret = esp_netif_napt_enable(ap);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "NAPT enabled — AP clients routed to STA uplink");
        } else {
            ESP_LOGE(TAG, "NAPT enable failed: %s (need CONFIG_LWIP_IPV4_NAPT=y)",
                     esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "NAPT: could not get AP netif handle");
    }
}

// S-1: предупреждение если используется дефолтный пароль AP.
static void log_default_password_warning(void)
{
    if (strcmp(g_cfg.wifiApPassword, "audio21master") == 0) {
        ESP_LOGW(TAG, "AP password is DEFAULT ('audio21master') — change it via Web UI / config.env!");
    } else {
        ESP_LOGI(TAG, "AP password changed from default — OK");
    }
}
