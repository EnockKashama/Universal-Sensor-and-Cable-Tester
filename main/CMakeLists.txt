idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_adc esp_driver_gpio LovyanGFX esp_driver_i2c
                             esp_driver_ledc esp_wifi nvs_flash esp_event
                             esp_netif esp_http_server)
