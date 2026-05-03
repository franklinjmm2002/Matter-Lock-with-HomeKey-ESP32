idf_component_register(SRC_DIRS "." "./lock" "./homekey"
                       PRIV_INCLUDE_DIRS
                         "." "./lock" "./homekey" "${ESP_MATTER_PATH}/examples/common/utils"
                       PRIV_REQUIRES
                         esp_matter esp_matter_console chip DigitalDoorKey pn532_hal nvs_flash fmt
                         homekey_provisioning_core app_update esp_partition espressif__button web_config)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
