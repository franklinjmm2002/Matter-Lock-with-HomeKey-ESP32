idf_component_register(SRCS "web_config.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server nvs_flash esp_timer driver mbedtls app_update)
