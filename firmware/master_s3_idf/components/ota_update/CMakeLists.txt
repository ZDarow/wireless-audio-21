idf_component_register(SRCS "src/ota_update.cpp"
                     INCLUDE_DIRS "include"
                     REQUIRES esp_https_ota esp_http_client esp_event esp_wifi esp_netif nvs_flash log
                     PRIV_REQUIRES app_update)
