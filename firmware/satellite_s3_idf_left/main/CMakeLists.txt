idf_component_register(SRCS "main.cpp"
                     INCLUDE_DIRS "."
                     REQUIRES i2s_output dsp_pipeline espnow_transport nvs_config
                              esp_wifi esp_event esp_netif nvs_flash driver log)
