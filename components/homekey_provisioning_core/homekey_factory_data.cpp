#include "homekey_factory_data.h"

#include <sdkconfig.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>
#include <mbedtls/sha256.h>
#include <nvs.h>

#include "homekey_reader_data_manager.h"

namespace {
constexpr const char * TAG = "homekey_factory";
constexpr const char * kNamespace = "HK_FACTORY";
constexpr const char * kIssuerKey = "ISSUER_PUBKEY";
constexpr const char * kChipFactoryPartition = "nvs";
constexpr const char * kChipFactoryNamespace = "chip-factory";
constexpr const char * kChipFactoryIssuerBlobKey = "hk-issuer-pk";
constexpr const char * kChipFactoryIssuerHexKey = "hk-issuer-hex";

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

bool read_chip_factory_blob(std::vector<uint8_t> & public_key)
{
    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open_from_partition(kChipFactoryPartition, kChipFactoryNamespace, NVS_READONLY, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open chip-factory namespace: %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, kChipFactoryIssuerBlobKey, nullptr, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to query chip-factory issuer blob: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    public_key.resize(required_size);
    err = nvs_get_blob(nvs_handle, kChipFactoryIssuerBlobKey, public_key.data(), &required_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read chip-factory issuer blob: %s", esp_err_to_name(err));
        public_key.clear();
        return false;
    }

    return true;
}

bool read_chip_factory_hex(std::vector<uint8_t> & public_key)
{
    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open_from_partition(kChipFactoryPartition, kChipFactoryNamespace, NVS_READONLY, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open chip-factory namespace for hex issuer: %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, kChipFactoryIssuerHexKey, nullptr, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to query chip-factory issuer hex: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    std::string hex(required_size, '\0');
    err = nvs_get_str(nvs_handle, kChipFactoryIssuerHexKey, hex.data(), &required_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read chip-factory issuer hex: %s", esp_err_to_name(err));
        return false;
    }

    return parse_hex(hex, public_key);
}

bool read_chip_factory_issuer_public_key(std::vector<uint8_t> & public_key)
{
    public_key.clear();

    if (read_chip_factory_blob(public_key) && public_key.size() == 32) {
        ESP_LOGI(TAG, "Loaded Home Key issuer public key from chip-factory blob");
        return true;
    }

    public_key.clear();
    if (read_chip_factory_hex(public_key) && public_key.size() == 32) {
        ESP_LOGI(TAG, "Loaded Home Key issuer public key from chip-factory hex");
        return true;
    }

    public_key.clear();
    return false;
}
} // namespace

bool HomeKeyFactoryDataReadIssuerPublicKey(std::vector<uint8_t> & public_key)
{
    public_key.clear();

    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open factory issuer namespace: %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, kIssuerKey, nullptr, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to query factory issuer public key size: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    public_key.resize(required_size);
    err = nvs_get_blob(nvs_handle, kIssuerKey, public_key.data(), &required_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read factory issuer public key: %s", esp_err_to_name(err));
        public_key.clear();
        return false;
    }

    if (public_key.size() != 32) {
        ESP_LOGE(TAG, "Factory issuer public key has invalid size: %u", static_cast<unsigned>(public_key.size()));
        public_key.clear();
        return false;
    }

    return true;
}

bool HomeKeyFactoryDataWriteIssuerPublicKey(const std::vector<uint8_t> & public_key)
{
    if (public_key.size() != 32) {
        ESP_LOGE(TAG, "Factory issuer public key must be exactly 32 bytes");
        return false;
    }

    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open factory issuer namespace for write: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(nvs_handle, kIssuerKey, public_key.data(), public_key.size());
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store factory issuer public key: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool HomeKeyFactoryDataClearIssuerPublicKey()
{
    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open factory issuer namespace for erase: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_key(nvs_handle, kIssuerKey);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear factory issuer public key: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool HomeKeySeedIssuerFromFactoryDataOrConfig(const char * config_hex)
{
    const readerData_t current_data = HomeKeyReaderDataMgr().GetReaderDataCopy();
    if (!current_data.issuers.empty()) {
        return true;
    }

    std::vector<uint8_t> issuer_pk;
    if (!HomeKeyFactoryDataReadIssuerPublicKey(issuer_pk)) {
        if (read_chip_factory_issuer_public_key(issuer_pk)) {
            if (!HomeKeyFactoryDataWriteIssuerPublicKey(issuer_pk)) {
                return false;
            }
            ESP_LOGI(TAG, "Imported Home Key issuer public key from chip-factory into HK_FACTORY");
        } else {
            if (config_hex == nullptr || config_hex[0] == '\0') {
                return true;
            }

            if (!parse_hex(config_hex, issuer_pk) || issuer_pk.size() != 32) {
                ESP_LOGE(TAG, "Configured factory issuer public key is invalid; expected 32 bytes of hex");
                return false;
            }

            if (!HomeKeyFactoryDataWriteIssuerPublicKey(issuer_pk)) {
                return false;
            }
            ESP_LOGI(TAG, "Persisted factory issuer public key from build configuration");
        }
    }

    const std::vector<uint8_t> issuer_id = key_identifier(issuer_pk, 8);
    if (!HomeKeyReaderDataMgr().AddIssuerIfNotExists(issuer_id, issuer_pk.data())) {
        ESP_LOGW(TAG, "Factory issuer was not added; it may already exist");
        return true;
    }

    ESP_LOGI(TAG, "Auto-seeded factory issuer id=%s", bytes_to_hex(issuer_id).c_str());
    return true;
}
