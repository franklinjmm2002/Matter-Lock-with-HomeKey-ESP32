#include "web_config.h"
#include <esp_http_server.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gpio.h"
#include "mbedtls/base64.h"
#include "esp_ota_ops.h"
#include <mdns.h>

static const char* TAG = "WebConfig";
static WebConfig* g_config = nullptr;

esp_err_t WebConfig::load() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("hw_config", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_u8(handle, "pn532_ss", &pn532_ss);
        nvs_get_u8(handle, "pn532_sck", &pn532_sck);
        nvs_get_u8(handle, "pn532_miso", &pn532_miso);
        nvs_get_u8(handle, "pn532_mosi", &pn532_mosi);
        nvs_get_u8(handle, "boot_btn", &boot_btn);
        nvs_get_u8(handle, "relay_pin", &relay_pin);
        nvs_get_u8(handle, "relay_lvl", &relay_active_level);
        nvs_get_u8(handle, "led_pin", &led_pin);
        size_t len = sizeof(web_auth_pwd);
        nvs_get_str(handle, "web_pwd", web_auth_pwd, &len);
        len = sizeof(homekit_code);
        nvs_get_str(handle, "hk_code", homekit_code, &len);
        nvs_close(handle);
        ESP_LOGI(TAG, "Hardware config loaded from NVS");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Hardware config not found in NVS, using defaults");
    return ESP_FAIL;
}

esp_err_t WebConfig::save() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("hw_config", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u8(handle, "pn532_ss", pn532_ss);
        nvs_set_u8(handle, "pn532_sck", pn532_sck);
        nvs_set_u8(handle, "pn532_miso", pn532_miso);
        nvs_set_u8(handle, "pn532_mosi", pn532_mosi);
        nvs_set_u8(handle, "boot_btn", boot_btn);
        nvs_set_u8(handle, "relay_pin", relay_pin);
        nvs_set_u8(handle, "relay_lvl", relay_active_level);
        nvs_set_u8(handle, "led_pin", led_pin);
        nvs_set_str(handle, "web_pwd", web_auth_pwd);
        nvs_set_str(handle, "hk_code", homekit_code);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Hardware config saved to NVS");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to open NVS for saving hardware config");
    return err;
}

static bool is_authenticated(httpd_req_t *req) {
    char auth_hdr[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr)) == ESP_OK) {
        if (strncmp(auth_hdr, "Basic ", 6) == 0) {
            char user_pass[128];
            size_t olen;
            mbedtls_base64_decode((unsigned char*)user_pass, sizeof(user_pass)-1, &olen, (const unsigned char*)(auth_hdr + 6), strlen(auth_hdr + 6));
            user_pass[olen] = '\0';
            
            char expected[128];
            snprintf(expected, sizeof(expected), "admin:%s", g_config->web_auth_pwd);
            if (strcmp(user_pass, expected) == 0) {
                return true;
            }
        }
    }
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Web Config\"");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    return false;
}

static const char* index_html = R"html(
<!DOCTYPE html>
<html>
<head>
    <title>Lock Hardware Config</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; padding: 20px; background-color: #f5f5f7; color: #1d1d1f; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
        h2 { text-align: center; font-weight: 600; margin-bottom: 20px; }
        .row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }
        label { font-weight: 500; font-size: 14px; }
        input { padding: 8px 12px; width: 100px; border: 1px solid #d2d2d7; border-radius: 6px; font-size: 14px; text-align: right; }
        button { width: 100%%; padding: 12px; background: #007aff; color: white; border: none; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; margin-top: 15px; transition: background 0.2s; }
        button:hover { background: #006ce6; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Hardware Config</h2>
        <form method="POST" action="/save">
            <div class="row"><label>PN532 SS:</label><input type="number" name="pn532_ss" value="%d" required></div>
            <div class="row"><label>PN532 SCK:</label><input type="number" name="pn532_sck" value="%d" required></div>
            <div class="row"><label>PN532 MISO:</label><input type="number" name="pn532_miso" value="%d" required></div>
            <div class="row"><label>PN532 MOSI:</label><input type="number" name="pn532_mosi" value="%d" required></div>
            <div class="row"><label>BOOT / Trigger:</label><input type="number" name="boot_btn" value="%d" required></div>
            <div class="row"><label>Relay Pin:</label><input type="number" name="relay_pin" value="%d" required></div>
            <div class="row"><label>Relay Level (1=High,0=Low):</label><input type="number" name="relay_lvl" min="0" max="1" value="%d" required></div>
            <div class="row"><label>LED Pin:</label><input type="number" name="led_pin" value="%d" required></div>
            <div class="row"><label>Web Password:</label><input type="text" name="web_pwd" value="%s" required></div>
            <div class="row"><label>HomeKit Setup Code:</label><input type="text" name="hk_code" value="%s" required></div>
            <button type="submit">Save & Reboot</button>
        </form>
        <hr style="margin: 20px 0; border: none; border-top: 1px solid #d2d2d7;">
        <form method="POST" action="/reboot_ota1"><button type="submit" style="background:#ff3b30;">Reboot to HomeKit Provisioner</button></form>
        <form method="POST" action="/reboot_ota0"><button type="submit" style="background:#34c759; margin-top:10px;">Reboot to Matter Runtime</button></form>
    </div>
</body>
</html>
)html";

static esp_err_t index_get_handler(httpd_req_t *req) {
    if (!g_config) return ESP_FAIL;
    if (!is_authenticated(req)) return ESP_OK;

    char* buffer = (char*)malloc(4096);
    if (!buffer) return ESP_FAIL;
    snprintf(buffer, 4096, index_html, 
             g_config->pn532_ss, g_config->pn532_sck, 
             g_config->pn532_miso, g_config->pn532_mosi, 
             g_config->boot_btn, g_config->relay_pin, g_config->relay_active_level, g_config->led_pin,
             g_config->web_auth_pwd, g_config->homekit_code);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buffer, strlen(buffer));
    free(buffer);
    return ESP_OK;
}

static int get_post_val(const char* body, const char* key, int default_val) {
    const char* p = strstr(body, key);
    if (p) {
        p += strlen(key);
        if (*p == '=') {
            return atoi(p + 1);
        }
    }
    return default_val;
}

static void get_post_str(const char* body, const char* key, char* out_val, size_t max_len) {
    const char* p = strstr(body, key);
    if (p) {
        p += strlen(key);
        if (*p == '=') {
            p++;
            size_t i = 0;
            while (*p && *p != '&' && i < max_len - 1) {
                out_val[i++] = *p++;
            }
            out_val[i] = '\0';
        }
    }
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) return ESP_OK;

    char buf[1024];
    int ret, remaining = req->content_len;
    if (remaining >= (int)sizeof(buf)) return ESP_FAIL;

    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    if (g_config) {
        g_config->pn532_ss = get_post_val(buf, "pn532_ss", g_config->pn532_ss);
        g_config->pn532_sck = get_post_val(buf, "pn532_sck", g_config->pn532_sck);
        g_config->pn532_miso = get_post_val(buf, "pn532_miso", g_config->pn532_miso);
        g_config->pn532_mosi = get_post_val(buf, "pn532_mosi", g_config->pn532_mosi);
        g_config->boot_btn = get_post_val(buf, "boot_btn", g_config->boot_btn);
        g_config->relay_pin = get_post_val(buf, "relay_pin", g_config->relay_pin);
        g_config->relay_active_level = get_post_val(buf, "relay_lvl", g_config->relay_active_level);
        g_config->led_pin = get_post_val(buf, "led_pin", g_config->led_pin);
        get_post_str(buf, "web_pwd", g_config->web_auth_pwd, sizeof(g_config->web_auth_pwd));
        get_post_str(buf, "hk_code", g_config->homekit_code, sizeof(g_config->homekit_code));
        g_config->save();
    }

    const char* resp_html = "<html><body style='font-family: sans-serif; text-align: center; margin-top: 50px;'><h2>Saved successfully!</h2><p>Rebooting device...</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp_html, strlen(resp_html));
    
    esp_timer_handle_t timer;
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = [](void* arg) { esp_restart(); };
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_once(timer, 1500000); // 1.5 seconds

    return ESP_OK;
}

static esp_err_t reboot_ota1_post_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) return ESP_OK;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (it) {
        esp_ota_set_boot_partition(esp_partition_get(it));
        esp_partition_iterator_release(it);
    }
    const char* resp = "<html><body style='font-family: sans-serif; text-align: center; margin-top: 50px;'><h2>Rebooting to HomeKit Provisioner...</h2></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, strlen(resp));
    esp_timer_handle_t timer; esp_timer_create_args_t timer_args = {}; timer_args.callback = [](void* arg) { esp_restart(); };
    esp_timer_create(&timer_args, &timer); esp_timer_start_once(timer, 1500000);
    return ESP_OK;
}

static esp_err_t reboot_ota0_post_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) return ESP_OK;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (it) {
        esp_ota_set_boot_partition(esp_partition_get(it));
        esp_partition_iterator_release(it);
    }
    const char* resp = "<html><body style='font-family: sans-serif; text-align: center; margin-top: 50px;'><h2>Rebooting to Matter Runtime...</h2></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, strlen(resp));
    esp_timer_handle_t timer; esp_timer_create_args_t timer_args = {}; timer_args.callback = [](void* arg) { esp_restart(); };
    esp_timer_create(&timer_args, &timer); esp_timer_start_once(timer, 1500000);
    return ESP_OK;
}

extern "C" esp_err_t web_config_server_start(WebConfig* config) {
    g_config = config;
    httpd_handle_t server = NULL;
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = 8080;
    server_config.stack_size = 8192;

    esp_err_t ret = httpd_start(&server, &server_config);
    if (ret == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t save_uri = {
            .uri       = "/save",
            .method    = HTTP_POST,
            .handler   = save_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &save_uri);

        httpd_uri_t reboot1_uri = { .uri = "/reboot_ota1", .method = HTTP_POST, .handler = reboot_ota1_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &reboot1_uri);

        httpd_uri_t reboot0_uri = { .uri = "/reboot_ota0", .method = HTTP_POST, .handler = reboot_ota0_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &reboot0_uri);

        ESP_LOGI(TAG, "Web Config Server started on port 8080");

        // Initialize mDNS for Web Config
        esp_err_t err = mdns_init();
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            mdns_hostname_set("matter-lock");
            mdns_instance_name_set("Matter Lock Web Config");
            mdns_service_add("Web Config", "_http", "_tcp", 8080, NULL, 0);
            ESP_LOGI(TAG, "mDNS initialized, access via http://matter-lock.local:8080");
        } else {
            ESP_LOGW(TAG, "mDNS failed to initialize: %d", err);
        }
    } else {
        ESP_LOGE(TAG, "Failed to start Web Config Server");
    }
    return ret;
}

extern "C" void trigger_lock_action(bool unlock) {
    if (!g_config) return;
    gpio_set_direction((gpio_num_t)g_config->relay_pin, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)g_config->led_pin, GPIO_MODE_OUTPUT);
    
    uint32_t active = g_config->relay_active_level ? 1 : 0;
    uint32_t inactive = g_config->relay_active_level ? 0 : 1;

    if (unlock) {
        gpio_set_level((gpio_num_t)g_config->relay_pin, active);
        gpio_set_level((gpio_num_t)g_config->led_pin, 1);
    } else {
        gpio_set_level((gpio_num_t)g_config->relay_pin, inactive);
        gpio_set_level((gpio_num_t)g_config->led_pin, 0);
    }
}
