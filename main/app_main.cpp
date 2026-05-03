/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sdkconfig.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <inttypes.h>
#include <nvs_flash.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <app_priv.h>
#include <iot_button.h>
#include "lock/door_lock_manager.h"
#include "homekey/homekey_manager.h"
#include "homekey/homekey_nfc_manager.h"
#include "homekey_boot_mode_manager.h"
#include "homekey_factory_data.h"
#include "homekey_provisioning_manager.h"
#include "homekey_reader_data_manager.h"
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include "web_config.h"

static const char *TAG = "app_main";
uint16_t door_lock_endpoint_id = 0;

namespace {
WebConfig g_web_config;
HomeKeyNfcManager* s_homekey_nfc_manager = nullptr;
constexpr uint16_t kProvisioningLongPressMs = 5000;
gpio_num_t kBootButtonGpio = GPIO_NUM_9;
constexpr TickType_t kBootButtonPollPeriod = pdMS_TO_TICKS(50);
constexpr uint32_t kBootButtonDebounceMs = 100;
bool s_provisioner_reboot_requested = false;

void seed_factory_issuer_if_needed()
{
    if (!HomeKeySeedIssuerFromFactoryDataOrConfig(CONFIG_HOMEKEY_FACTORY_ISSUER_PUBKEY_HEX)) {
        ESP_LOGW(TAG, "Home Key factory issuer seeding did not complete");
    }
}

void request_provisioner_reboot(const char * source, uint32_t pressed_ms)
{
    if (s_provisioner_reboot_requested) {
        ESP_LOGW(TAG, "Provisioner reboot already requested; ignoring duplicate trigger from %s", source);
        return;
    }

    s_provisioner_reboot_requested = true;
    ESP_LOGI(TAG, "%s long press detected (%" PRIu32 " ms); scheduling Home Key provisioner", source, pressed_ms);

    if (HomeKeyBootModeMgr().IsRunningFromPartition(ESP_PARTITION_SUBTYPE_APP_OTA_1)) {
        ESP_LOGW(TAG, "Already running from the provisioner slot; ignoring request");
        s_provisioner_reboot_requested = false;
        return;
    }

    esp_err_t err = HomeKeyBootModeMgr().SetRequestedMode(HomeKeyBootMode::kProvisioner);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist provisioning boot mode: %s", esp_err_to_name(err));
        s_provisioner_reboot_requested = false;
        return;
    }

    err = HomeKeyBootModeMgr().SwitchToProvisioner();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch to provisioner slot: %s", esp_err_to_name(err));
        s_provisioner_reboot_requested = false;
        return;
    }

    ESP_LOGI(TAG, "Rebooting into temporary Home Key provisioner");
    esp_restart();
}

void homekey_provisioning_button_cb(void * button_handle, void * user_data)
{
    button_handle_t button = static_cast<button_handle_t>(button_handle);
    const uint32_t pressed_ms = iot_button_get_pressed_time(button);
    request_provisioner_reboot("BSP BOOT button", pressed_ms);
}

void handle_boot_mode_on_startup()
{
    if (!HomeKeyBootModeMgr().Begin()) {
        ESP_LOGW(TAG, "Home Key boot-mode manager is unavailable");
        return;
    }

    const HomeKeyBootMode mode = HomeKeyBootModeMgr().GetRequestedMode();
    if (mode == HomeKeyBootMode::kReturnToMain) {
        ESP_LOGI(TAG, "Provisioner return marker detected; clearing it for normal runtime");
        const esp_err_t err = HomeKeyBootModeMgr().ClearRequestedMode();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear return marker: %s", esp_err_to_name(err));
        }
    }
}

void register_homekey_boot_button(app_driver_handle_t button_handle)
{
    ESP_LOGI(TAG, "Registering Home Key provisioner trigger on BSP BOOT button");
    if (button_handle == nullptr) {
        ESP_LOGW(TAG, "BOOT button handle is unavailable");
        return;
    }

    button_event_args_t long_press_args = {};
    long_press_args.long_press.press_time = kProvisioningLongPressMs;
    const esp_err_t err = iot_button_register_cb(static_cast<button_handle_t>(button_handle), BUTTON_LONG_PRESS_START,
                                                 &long_press_args, homekey_provisioning_button_cb, nullptr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register Home Key provisioner button callback: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Hold BOOT for %u ms to enter temporary Home Key provisioning mode", kProvisioningLongPressMs);
}

void homekey_boot_button_poll_task(void * arg)
{
    bool last_pressed = false;
    TickType_t pressed_since_ticks = 0;
    bool debounce_satisfied = false;

    for (;;) {
        const bool pressed = gpio_get_level(kBootButtonGpio) == 0;
        const TickType_t now_ticks = xTaskGetTickCount();

        if (pressed && !last_pressed) {
            pressed_since_ticks = now_ticks;
            debounce_satisfied = false;
        } else if (!pressed) {
            pressed_since_ticks = 0;
            debounce_satisfied = false;
        } else if (pressed_since_ticks != 0) {
            const uint32_t held_ms = pdTICKS_TO_MS(now_ticks - pressed_since_ticks);
            if (!debounce_satisfied && held_ms >= kBootButtonDebounceMs) {
                debounce_satisfied = true;
            }
            if (debounce_satisfied && held_ms >= kProvisioningLongPressMs) {
                request_provisioner_reboot("GPIO0 fallback", held_ms);
                pressed_since_ticks = 0;
                debounce_satisfied = false;
            }
        }

        last_pressed = pressed;
        vTaskDelay(kBootButtonPollPeriod);
    }
}

void register_homekey_boot_button_fallback()
{
    ESP_LOGI(TAG, "Registering GPIO9 fallback trigger for Home Key provisioner");

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << kBootButtonGpio;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    const esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure GPIO9 fallback trigger: %s", esp_err_to_name(err));
        return;
    }

    BaseType_t task_ok =
        xTaskCreate(homekey_boot_button_poll_task, "hk_boot_btn", 4096, nullptr, 5, nullptr);
    if (task_ok != pdPASS) {
        ESP_LOGW(TAG, "Failed to start GPIO0 fallback trigger task");
        return;
    }

    ESP_LOGI(TAG, "GPIO9 fallback trigger armed; hold BOOT for %u ms to enter temporary Home Key provisioning mode",
             kProvisioningLongPressMs);
}
} // namespace

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using namespace chip;

constexpr auto k_timeout_seconds = 300;

static void log_pairing_codes()
{
    ESP_LOGI(TAG, "Matter onboarding payloads:");
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
    ESP_LOGI(TAG, "HomeKit setup code: %s", g_web_config.homekit_code);
}

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed successfully");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager  &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
            if (!commissionMgr.IsCommissioningWindowOpen()) {
                /* After removing last fabric, this example does not remove the Wi-Fi credentials
                 * and still has IP connectivity so, only advertising on DNS-SD.
                 */
                CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                                            chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        /* Driver update */
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    return err;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();
    handle_boot_mode_on_startup();

#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif
    };
    err = esp_pm_configure(&pm_config);
#endif

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    app_driver_handle_t button_handle = app_driver_button_init();
    register_homekey_boot_button(button_handle);
    register_homekey_boot_button_fallback();

    // node handle can be used to add/modify other endpoints.
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    door_lock::config_t door_lock_config;
    cluster::door_lock::feature::credential_over_the_air_access::config_t cota_config;
    cluster::door_lock::feature::pin_credential::config_t pin_credential_config;
    cluster::door_lock::feature::user::config_t user_config;
    // endpoint handles can be used to add/modify clusters.
    endpoint_t *endpoint = door_lock::create(node, &door_lock_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create door lock endpoint"));
    cluster_t *door_lock_cluster = cluster::get(endpoint, DoorLock::Id);
    cluster::door_lock::feature::credential_over_the_air_access::add(door_lock_cluster, &cota_config);
    cluster::door_lock::feature::pin_credential::add(door_lock_cluster, &pin_credential_config);
    cluster::door_lock::feature::user::add(door_lock_cluster, &user_config);
    cluster::door_lock::attribute::create_auto_relock_time(door_lock_cluster, 5);

    door_lock_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Door lock created with endpoint_id %d", door_lock_endpoint_id);
    CHIP_ERROR homekey_err = HomeKeyMgr().Init(door_lock_endpoint_id, &BoltLockMgr());
    ABORT_APP_ON_FAILURE(homekey_err == CHIP_NO_ERROR,
                         ESP_LOGE(TAG, "Failed to initialize Home Key manager: %" CHIP_ERROR_FORMAT,
                                  homekey_err.Format()));
    HomeKeyMgr().SetExpressModeEnabled(true);
    g_web_config.load();
    kBootButtonGpio = static_cast<gpio_num_t>(g_web_config.boot_btn);
    s_homekey_nfc_manager = new HomeKeyNfcManager(HomeKeyReaderDataMgr(), g_web_config.pn532_ss, g_web_config.pn532_sck, g_web_config.pn532_miso, g_web_config.pn532_mosi);
    if (!s_homekey_nfc_manager->Begin()) {
        ESP_LOGW(TAG, "Home Key NFC path did not start");
    }
    if (!HomeKeyProvisioningMgr().Begin()) {
        ESP_LOGW(TAG, "Experimental HomeKit provisioning path did not start");
    } else {
        seed_factory_issuer_if_needed();
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
    web_config_server_start(&g_web_config);
    log_pairing_codes();

    /* do nothing now */
    door_lock_init();

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    homekey_console_register_commands();
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif
}
