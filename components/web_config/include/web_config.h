#pragma once

#include <stdint.h>
#include <esp_err.h>

struct WebConfig {
    uint8_t pn532_ss = 4;
    uint8_t pn532_sck = 1;
    uint8_t pn532_miso = 2;
    uint8_t pn532_mosi = 3;
    uint8_t boot_btn = 9;
    uint8_t relay_pin = 8;
    uint8_t relay_active_level = 1;
    uint8_t led_pin = 5;
    char web_auth_pwd[32] = "admin123";
    char homekit_code[12] = "46637726";

    esp_err_t load();
    esp_err_t save();
};

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_config_server_start(WebConfig* config);
void trigger_lock_action(bool unlock);

#ifdef __cplusplus
}
#endif
