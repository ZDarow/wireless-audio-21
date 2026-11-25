idf_component_register(SRCS "main.cpp"
                     INCLUDE_DIRS "."
                     REQUIRES i2s_output dsp_pipeline esp_wifi esp_event esp_netif nvs_flash
                              espnow_transport udp_audio web_server nvs_config dns_captive ota_update mdns_raw app_update)
