/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <Arduino.h>
#include <HomeSpan.h>
#include <sdkconfig.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cinttypes>
#include <string>
#include <vector>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <button_gpio.h>
#include <iot_button.h>
#include <mbedtls/sha256.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "bsp/esp-bsp.h"
#include "homekey_boot_mode_manager.h"
#include "homekey_factory_data.h"
#include "homekey_provisioning_manager.h"
#include "homekey_reader_data_manager.h"
#include "web_config.h"

CUSTOM_SERV(NFCAccess, 00000266-0000-1000-8000-0026BB765291);
CUSTOM_SERV(LockManagement, 44);
CUSTOM_CHAR(HomeKeyConfigurationState, 00000263-0000-1000-8000-0026BB765291, PR + EV, UINT16, 0, 0, 1, true);
CUSTOM_CHAR_TLV8(HomeKeyAccessControlPoint, 00000264-0000-1000-8000-0026BB765291, PR + PW + WR);
CUSTOM_CHAR_TLV8(HomeKeySupportedConfiguration, 00000265-0000-1000-8000-0026BB765291, PR);
CUSTOM_CHAR_TLV8(HomeKeyHardwareFinish, 0000026C-0000-1000-8000-0026BB765291, PR);
CUSTOM_CHAR_TLV8(LockControlPoint, 19, PW);

namespace {
constexpr const char * TAG = "hk_provisioner";
constexpr uint16_t kReturnLongPressMs = 3000;
constexpr unsigned long kAutoReturnDelayMs = 10000;
constexpr const char * kProvisionerName = "Matter Lock Home Key";
constexpr const char * kProvisionerModel = "Matter Lock Provisioner";
constexpr const char * kProvisionerSerial = "MLHK-PROVISIONER";
constexpr const char * kProvisionerQrId = "HKEY";
constexpr const char * kProvisionerApSsid = "MatterLock-HomeKey";

constexpr uint8_t kSupportedIssuerSlotsTag = 0x01;
constexpr uint8_t kSupportedInactiveCredentialsTag = 0x02;
constexpr uint8_t kSupportedIssuerSlots = 0x10;
constexpr uint8_t kSupportedInactiveCredentials = 0x10;

constexpr uint8_t kHardwareFinishTag = 0x01;
constexpr std::array<uint8_t, 4> kHardwareFinishTan = { 0xCE, 0xD5, 0xDA, 0x00 };

button_handle_t s_button_handle = nullptr;
WebConfig g_web_config;
bool s_started_with_reader_data = false;
bool s_started_with_complete_homekey_data = false;
bool s_provisioner_ready = false;
unsigned long s_first_provisioned_at_ms = 0;
String s_serial_command;

void return_to_main_runtime(const char * reason)
{
    ESP_LOGI(TAG, "Returning to Matter runtime: %s", reason);

    esp_err_t err = HomeKeyBootModeMgr().SetRequestedMode(HomeKeyBootMode::kReturnToMain);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark return-to-main boot mode: %s", esp_err_to_name(err));
        return;
    }

    err = HomeKeyBootModeMgr().SwitchToMainRuntime();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch back to main runtime slot: %s", esp_err_to_name(err));
        return;
    }

    esp_restart();
}

std::string bytes_to_hex(const std::vector<uint8_t> & bytes)
{
    static constexpr char kHex[] = "0123456789ABCDEF";

    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (uint8_t byte : bytes) {
        hex.push_back(kHex[(byte >> 4) & 0x0F]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    return hex;
}

bool parse_hex(const std::string & input, std::vector<uint8_t> & out)
{
    std::string hex = input;
    hex.erase(std::remove_if(hex.begin(), hex.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), hex.end());
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) {
        hex.erase(0, 2);
    }

    if (hex.empty() || (hex.size() % 2) != 0) {
        return false;
    }

    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const auto hi = static_cast<unsigned char>(hex[i]);
        const auto lo = static_cast<unsigned char>(hex[i + 1]);
        if (!std::isxdigit(hi) || !std::isxdigit(lo)) {
            return false;
        }
        out.push_back(static_cast<uint8_t>(strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
    }

    return true;
}

std::vector<uint8_t> key_identifier(const std::vector<uint8_t> & key, size_t output_len)
{
    static constexpr char prefix[] = "key-identifier";
    std::vector<uint8_t> material(prefix, prefix + strlen(prefix));
    material.insert(material.end(), key.begin(), key.end());

    uint8_t hash[32] = {};
    mbedtls_sha256(material.data(), material.size(), hash, 0);
    return std::vector<uint8_t>(hash, hash + output_len);
}

std::vector<uint8_t> tlv_to_bytes(TLV8 & tlv)
{
    std::vector<uint8_t> bytes(tlv.pack_size());
    if (!bytes.empty()) {
        tlv.pack(bytes.data());
    }
    return bytes;
}

TLV8 bytes_to_tlv(const std::vector<uint8_t> & bytes)
{
    TLV8 tlv;
    if (!bytes.empty()) {
        tlv.unpack(const_cast<uint8_t *>(bytes.data()), bytes.size());
    }
    return tlv;
}

void log_reader_state(const char * prefix)
{
    const readerData_t reader_data = HomeKeyReaderDataMgr().GetReaderDataCopy();
    size_t endpoint_count = 0;
    for (const hkIssuer_t & issuer : reader_data.issuers) {
        endpoint_count += issuer.endpoints.size();
    }
    ESP_LOGI(TAG,
             "%s provisioned=%s gid=%zu reader_id=%zu reader_sk=%zu reader_pk=%zu issuers=%zu endpoint_sets=%zu",
             prefix, HomeKeyReaderDataMgr().IsProvisioned() ? "yes" : "no", reader_data.reader_gid.size(),
             reader_data.reader_id.size(), reader_data.reader_sk.size(), reader_data.reader_pk.size(),
             reader_data.issuers.size(), endpoint_count);
}

bool has_complete_homekey_configuration(const readerData_t & reader_data)
{
    if (reader_data.reader_gid.size() != 8 || reader_data.reader_id.empty() || reader_data.reader_sk.empty() ||
        reader_data.reader_pk.empty()) {
        return false;
    }

    for (const hkIssuer_t & issuer : reader_data.issuers) {
        if (!issuer.endpoints.empty()) {
            return true;
        }
    }

    return false;
}

bool has_complete_homekey_configuration()
{
    return has_complete_homekey_configuration(HomeKeyReaderDataMgr().GetReaderDataCopy());
}

void log_tlv_details(const char * prefix, TLV8 & tlv)
{
    const std::vector<uint8_t> packed = tlv_to_bytes(tlv);
    ESP_LOGI(TAG, "%s (%zu bytes): %s", prefix, packed.size(), bytes_to_hex(packed).c_str());
    Serial.printf("%s decoded TLV follows:\n", prefix);
    tlv.printAll();
}

void print_serial_help()
{
    Serial.println("Provisioner commands:");
    Serial.println("  help");
    Serial.println("  status");
    Serial.println("  dump");
    Serial.println("  add-issuer <32-byte-ed25519-pubkey-hex>");
}

bool seed_factory_issuer_if_needed()
{
    return HomeKeySeedIssuerFromFactoryDataOrConfig(CONFIG_HOMEKEY_FACTORY_ISSUER_PUBKEY_HEX);
}

void print_serial_status()
{
    const readerData_t reader_data = HomeKeyReaderDataMgr().GetReaderDataCopy();
    size_t endpoint_count = 0;
    for (const hkIssuer_t & issuer : reader_data.issuers) {
        endpoint_count += issuer.endpoints.size();
    }

    Serial.printf("provisioned=%s\n", HomeKeyReaderDataMgr().IsProvisioned() ? "yes" : "no");
    Serial.printf("reader_gid=%s\n", bytes_to_hex(reader_data.reader_gid).c_str());
    Serial.printf("reader_id=%s\n", bytes_to_hex(reader_data.reader_id).c_str());
    Serial.printf("reader_pk=%s\n", bytes_to_hex(reader_data.reader_pk).c_str());
    Serial.printf("issuers=%u\n", static_cast<unsigned>(reader_data.issuers.size()));
    Serial.printf("endpoint_sets=%u\n", static_cast<unsigned>(endpoint_count));
}

void print_serial_dump()
{
    const readerData_t reader_data = HomeKeyReaderDataMgr().GetReaderDataCopy();
    print_serial_status();
    for (size_t i = 0; i < reader_data.issuers.size(); ++i) {
        const auto & issuer = reader_data.issuers[i];
        Serial.printf("issuer[%u].id=%s\n", static_cast<unsigned>(i), bytes_to_hex(issuer.issuer_id).c_str());
        Serial.printf("issuer[%u].pk=%s\n", static_cast<unsigned>(i), bytes_to_hex(issuer.issuer_pk).c_str());
        Serial.printf("issuer[%u].endpoints=%u\n", static_cast<unsigned>(i),
                      static_cast<unsigned>(issuer.endpoints.size()));
    }
}

void handle_serial_command(const std::string & raw_command)
{
    std::string command = raw_command;
    command.erase(command.begin(),
                  std::find_if(command.begin(), command.end(), [](unsigned char ch) { return std::isspace(ch) == 0; }));
    command.erase(std::find_if(command.rbegin(), command.rend(), [](unsigned char ch) { return std::isspace(ch) == 0; }).base(),
                  command.end());

    if (command.empty()) {
        return;
    }

    if (command == "help") {
        print_serial_help();
        return;
    }

    if (command == "status") {
        print_serial_status();
        return;
    }

    if (command == "dump") {
        print_serial_dump();
        return;
    }

    static constexpr const char * kAddIssuerPrefix = "add-issuer ";
    if (command.rfind(kAddIssuerPrefix, 0) == 0) {
        std::vector<uint8_t> issuer_pk;
        if (!parse_hex(command.substr(strlen(kAddIssuerPrefix)), issuer_pk) || issuer_pk.size() != 32) {
            Serial.println("issuer public key must be 32 bytes of hex");
            return;
        }

        const std::vector<uint8_t> issuer_id = key_identifier(issuer_pk, 8);
        if (!HomeKeyReaderDataMgr().AddIssuerIfNotExists(issuer_id, issuer_pk.data())) {
            Serial.println("issuer already exists or could not be added");
            return;
        }

        Serial.println("issuer added");
        Serial.printf("issuer_id=%s\n", bytes_to_hex(issuer_id).c_str());
        log_reader_state("Reader state after add-issuer");
        return;
    }

    Serial.printf("unknown command: %s\n", command.c_str());
    print_serial_help();
}

void poll_serial_commands()
{
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            const std::string command = std::string(s_serial_command.c_str());
            s_serial_command = "";
            handle_serial_command(command);
            continue;
        }
        s_serial_command += ch;
    }
}

uint16_t current_configuration_state()
{
    return HomeKeyReaderDataMgr().IsProvisioned() ? 1 : 0;
}

TLV8 build_supported_configuration_tlv()
{
    TLV8 tlv;
    // These capacity values are inferred from observed Home Key accessories.
    tlv.add(kSupportedIssuerSlotsTag, static_cast<uint64_t>(kSupportedIssuerSlots));
    tlv.add(kSupportedInactiveCredentialsTag, static_cast<uint64_t>(kSupportedInactiveCredentials));
    return tlv;
}

TLV8 build_hardware_finish_tlv()
{
    TLV8 tlv;
    tlv.add(kHardwareFinishTag, kHardwareFinishTan.size(), kHardwareFinishTan.data());
    return tlv;
}

bool init_nvs()
{
    const esp_partition_t * nvs_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (nvs_partition == nullptr) {
        nvs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    }

    if (nvs_partition != nullptr) {
        ESP_LOGI(TAG, "Using NVS partition '%s' @ 0x%08" PRIx32 " size 0x%" PRIx32, nvs_partition->label,
                 nvs_partition->address, nvs_partition->size);
    } else {
        ESP_LOGE(TAG, "No NVS partition visible via esp_partition_find_first()");
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NOT_FOUND && nvs_partition != nullptr) {
        ESP_LOGW(TAG, "Default NVS lookup failed, retrying via partition pointer");
        err = nvs_flash_init_partition_ptr(nvs_partition);
    }
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = (nvs_partition != nullptr) ? nvs_flash_erase_partition_ptr(nvs_partition) : nvs_flash_erase();
        if (err == ESP_OK && nvs_partition != nullptr) {
            err = nvs_flash_init_partition_ptr(nvs_partition);
        } else if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool clear_homespan_pairing_state()
{
    nvs_handle hap_nvs = 0;
    esp_err_t err = nvs_open("HAP", NVS_READWRITE, &hap_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HomeSpan HAP namespace: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_all(hap_nvs);
    if (err == ESP_OK) {
        err = nvs_commit(hap_nvs);
    }
    nvs_close(hap_nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear HomeSpan HAP namespace: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGW(TAG, "Cleared stale HomeSpan pairing state from HAP namespace");
    return true;
}

bool try_seed_wifi_credentials_from_station_config()
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_netif_init failed while probing Wi-Fi credentials: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_event_loop_create_default failed while probing Wi-Fi credentials: %s", esp_err_to_name(err));
        return false;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_init failed while probing Wi-Fi credentials: %s", esp_err_to_name(err));
        return false;
    }

    wifi_config_t sta_cfg = {};
    err = esp_wifi_get_config(WIFI_IF_STA, &sta_cfg);
    const bool has_credentials = (err == ESP_OK) && (sta_cfg.sta.ssid[0] != '\0');
    if (has_credentials) {
        homeSpan.setWifiCredentials(reinterpret_cast<const char *>(sta_cfg.sta.ssid),
                                    reinterpret_cast<const char *>(sta_cfg.sta.password));
        ESP_LOGI(TAG, "Provisioner reusing stored Wi-Fi SSID: %s", reinterpret_cast<const char *>(sta_cfg.sta.ssid));
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "No reusable Wi-Fi credentials found in station config: %s", esp_err_to_name(err));
    }

    const esp_err_t deinit_err = esp_wifi_deinit();
    if (deinit_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_deinit failed after probing Wi-Fi credentials: %s", esp_err_to_name(deinit_err));
    }

    return has_credentials;
}

void pair_callback(bool is_paired)
{
    ESP_LOGI(TAG, "HomeKit controller list changed. is_paired=%d", is_paired);
    if (!is_paired) {
        // Do not wipe reader data here, as the user might want to keep the factory issuer
        return;
    }
    bool dataChanged = false;
    for (auto it = homeSpan.controllerListBegin(); it != homeSpan.controllerListEnd(); ++it) {
        std::vector<uint8_t> ltpk(it->getLTPK(), it->getLTPK() + 32);
        std::vector<uint8_t> issuerId = key_identifier(ltpk, 8);
        if (HomeKeyReaderDataMgr().AddIssuerIfNotExists(issuerId, ltpk.data())) {
            dataChanged = true;
            ESP_LOGI(TAG, "Added HomeKit Controller as HomeKey Issuer: %s", bytes_to_hex(issuerId).c_str());
        }
    }
    if (dataChanged) {
        log_reader_state("Reader state after sync controllers");
    }
}

void return_to_main_runtime_cb(void * button_handle, void * user_data)
{
    return_to_main_runtime("BOOT button long-press");
}

bool init_return_button()
{
    const button_config_t button_config = {};
    const button_gpio_config_t gpio_config = {
        .gpio_num = static_cast<int32_t>(g_web_config.boot_btn),
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };

    const esp_err_t create_err = iot_button_new_gpio_device(&button_config, &gpio_config, &s_button_handle);
    if (create_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BOOT button: %s", esp_err_to_name(create_err));
        return false;
    }

    button_event_args_t long_press_args = {};
    long_press_args.long_press.press_time = kReturnLongPressMs;
    const esp_err_t reg_err = iot_button_register_cb(s_button_handle, BUTTON_LONG_PRESS_START, &long_press_args,
                                                     return_to_main_runtime_cb, nullptr);
    if (reg_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register return button callback: %s", esp_err_to_name(reg_err));
        return false;
    }

    return true;
}

struct ProvisioningLockMechanism : Service::LockMechanism {
    Characteristic::LockCurrentState current_state{ 1 };
    Characteristic::LockTargetState target_state{ 1 };

    ProvisioningLockMechanism() : Service::LockMechanism()
    {
        new Characteristic::ConfiguredName("Digital Door Lock");
    }

    boolean update() override
    {
        if (target_state.updated()) {
            const auto new_state = target_state.getNewVal();
            ESP_LOGI(TAG, "Provisioner lock target changed to %d", new_state);
            current_state.setVal(new_state, true);
        }
        return true;
    }
};

struct ProvisioningLockManagementService : Service::LockManagement {
    ProvisioningLockManagementService() : Service::LockManagement()
    {
        new Characteristic::LockControlPoint();
        new Characteristic::Version();
    }
};

struct HomeKeyNfcAccessService : Service::NFCAccess {
    Characteristic::HomeKeyConfigurationState configuration_state{ current_configuration_state() };
    Characteristic::HomeKeyAccessControlPoint control_point{ NULL_TLV };
    Characteristic::HomeKeySupportedConfiguration supported_configuration{ build_supported_configuration_tlv() };

    HomeKeyNfcAccessService() : Service::NFCAccess()
    {
    }

    boolean update() override
    {
        if (!control_point.updated()) {
            return true;
        }

        TLV8 request_tlv;
        control_point.getNewTLV(request_tlv);
        std::vector<uint8_t> request = tlv_to_bytes(request_tlv);
        log_reader_state("Reader state before TLV");
        log_tlv_details("Received Home Key provisioning TLV", request_tlv);

        std::vector<uint8_t> response;
        if (!HomeKeyProvisioningMgr().ProcessRequest(request, response)) {
            ESP_LOGE(TAG, "Home Key provisioning core failed to process TLV");
            log_reader_state("Reader state after failed TLV");
            return false;
        }

        TLV8 response_tlv = bytes_to_tlv(response);
        control_point.setTLV(response_tlv);
        configuration_state.setVal(current_configuration_state(), false);
        supported_configuration.setTLV(build_supported_configuration_tlv(), false);

        log_tlv_details("Responding with Home Key TLV", response_tlv);
        log_reader_state("Reader state after TLV");
        return true;
    }
};

void monitor_provisioning_completion()
{
    if (s_started_with_complete_homekey_data) {
        return;
    }

    if (!has_complete_homekey_configuration()) {
        s_first_provisioned_at_ms = 0;
        return;
    }

    if (s_first_provisioned_at_ms == 0) {
        s_first_provisioned_at_ms = millis();
        ESP_LOGI(TAG, "Home Key provisioning is now complete; will return to Matter runtime in %lu ms",
                 kAutoReturnDelayMs);
        return;
    }

    if (millis() - s_first_provisioned_at_ms >= kAutoReturnDelayMs) {
        return_to_main_runtime("Home Key provisioning completed");
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(1000);

    if (!init_nvs()) {
        return;
    }

    g_web_config.load();
    if (!HomeKeyBootModeMgr().Begin()) {
        ESP_LOGE(TAG, "Failed to initialize Home Key boot-mode manager");
        return;
    }

    if (!HomeKeyProvisioningMgr().Begin()) {
        ESP_LOGE(TAG, "Failed to initialize Home Key provisioning core");
        return;
    }

    if (!seed_factory_issuer_if_needed()) {
        ESP_LOGE(TAG, "Failed to seed factory issuer state");
        return;
    }

    s_started_with_reader_data = HomeKeyReaderDataMgr().IsProvisioned();
    s_started_with_complete_homekey_data = has_complete_homekey_configuration();

    if (!s_started_with_reader_data) {
        clear_homespan_pairing_state();
    }

    if (!init_return_button()) {
        ESP_LOGW(TAG, "Return-to-main button flow is unavailable");
    }

    homeSpan.setLogLevel(2);
    homeSpan.enableAutoStartAP();
    homeSpan.setApSSID(kProvisionerApSsid);
    homeSpan.setPairingCode(g_web_config.homekit_code);
    homeSpan.setQRID(kProvisionerQrId);
    try_seed_wifi_credentials_from_station_config();

    homeSpan.setPairCallback(pair_callback);
    homeSpan.begin(Category::Locks, kProvisionerName);
    web_config_server_start(&g_web_config);

    SPAN_ACCESSORY(kProvisionerName);
    new Characteristic::Manufacturer("Adwait Kale");
    new Characteristic::Model(kProvisionerModel);
    new Characteristic::SerialNumber(kProvisionerSerial);
    new Characteristic::FirmwareRevision("1.0");
    new Characteristic::HardwareRevision("1");
    new Characteristic::HomeKeyHardwareFinish(build_hardware_finish_tlv());
    new Service::HAPProtocolInformation();
    new Characteristic::Version();
    new ProvisioningLockManagementService();

    (new ProvisioningLockMechanism())->setPrimary();
    new HomeKeyNfcAccessService();

    ESP_LOGI(TAG, "Temporary Home Key provisioner started");
    ESP_LOGI(TAG, "Stored reader data provisioned at boot: %s", s_started_with_reader_data ? "yes" : "no");
    ESP_LOGI(TAG, "Home Key setup code: %s", HomeKeyProvisioningMgr().GetSetupCode());
    ESP_LOGI(TAG, "Fallback HomeSpan AP SSID: %s", kProvisionerApSsid);
    ESP_LOGI(TAG, "Hold BOOT for %u ms to reboot back into the Matter runtime", kReturnLongPressMs);
    print_serial_help();
    log_reader_state("Reader state at provisioner startup");
    s_provisioner_ready = true;
}

void loop()
{
    if (!s_provisioner_ready) {
        delay(100);
        return;
    }

    homeSpan.poll();
    poll_serial_commands();
    monitor_provisioning_completion();
}
