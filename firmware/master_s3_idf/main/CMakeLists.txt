idf_component_register(SRCS "main.cpp"
                     INCLUDE_DIRS "."
                     REQUIRES dsp_pipeline esp_wifi esp_event esp_netif nvs_flash)
