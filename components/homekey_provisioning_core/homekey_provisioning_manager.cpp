/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "homekey_provisioning_manager.h"

#include <cstdint>
#include <vector>

#include <esp_log.h>

#include <HK_HomeKit.h>

#include "homekey_reader_data_manager.h"

namespace {
constexpr const char * TAG = "hk_provisioning";
constexpr const char * kHomeKitSetupCode = "46637726";

HomeKeyProvisioningManager g_provisioning_manager;

constexpr uint8_t kReaderOperationTag               = 0x01;
constexpr uint8_t kReaderOperationRead              = 0x01;
constexpr uint8_t kReaderOperationWrite             = 0x02;
constexpr uint8_t kReaderReaderKeyRequestTag        = 0x06;
constexpr uint8_t kReaderDeviceCredentialRequestTag = 0x04;
constexpr uint8_t kReaderReqReaderPrivateKeyTag     = 0x02;
constexpr uint8_t kReaderReqIdentifierTag           = 0x03;
constexpr uint8_t kDeviceReqKeyTypeTag              = 0x01;
constexpr uint8_t kDeviceReqPublicKeyTag            = 0x02;
constexpr uint8_t kDeviceReqIssuerKeyIdentifierTag  = 0x03;

void append_tlv(std::vector<uint8_t> & buffer, uint8_t tag, const std::vector<uint8_t> & value)
{
    if (value.size() > 255) {
        return;
    }
    buffer.push_back(tag);
    buffer.push_back(static_cast<uint8_t>(value.size()));
    buffer.insert(buffer.end(), value.begin(), value.end());
}

void append_tlv_byte(std::vector<uint8_t> & buffer, uint8_t tag, uint8_t value)
{
    buffer.push_back(tag);
    buffer.push_back(1);
    buffer.push_back(value);
}
} // namespace

HomeKeyProvisioningManager & HomeKeyProvisioningMgr()
{
    return g_provisioning_manager;
}

bool HomeKeyProvisioningManager::Begin()
{
    if (m_started) {
        return true;
    }

    if (!HomeKeyReaderDataMgr().Begin()) {
        ESP_LOGE(TAG, "Failed to initialize reader data manager for Home Key provisioning");
        return false;
    }

    m_started = true;
    ESP_LOGI(TAG, "Home Key provisioning core is available; transport remains external");
    return true;
}

bool HomeKeyProvisioningManager::ProcessRequest(const std::vector<uint8_t> & request, std::vector<uint8_t> & response)
{
    response.clear();
    if (!Begin()) {
        return false;
    }

    readerData_t reader_data = HomeKeyReaderDataMgr().GetReaderDataCopy();
    std::vector<uint8_t> mutable_request = request;
    auto save_cb = [](const readerData_t & updated_data) {
        HomeKeyReaderDataMgr().UpdateReaderData(updated_data);
    };
    auto remove_key_cb = []() {
        HomeKeyReaderDataMgr().EraseReaderKey();
    };

    HK_HomeKit homekit(reader_data, save_cb, remove_key_cb, mutable_request);
    response = homekit.processResult();
    return true;
}

bool HomeKeyProvisioningManager::BuildReaderKeyReadRequest(std::vector<uint8_t> & request) const
{
    request.clear();
    append_tlv_byte(request, kReaderOperationTag, kReaderOperationRead);
    request.push_back(kReaderReaderKeyRequestTag);
    request.push_back(0);
    return true;
}

bool HomeKeyProvisioningManager::BuildReaderKeyWriteRequest(const std::vector<uint8_t> & reader_private_key,
                                                            const std::vector<uint8_t> & reader_identifier,
                                                            std::vector<uint8_t> & request) const
{
    if (reader_private_key.empty() || reader_private_key.size() > 255 || reader_identifier.empty() ||
        reader_identifier.size() > 255) {
        return false;
    }

    std::vector<uint8_t> reader_key_request;
    append_tlv(reader_key_request, kReaderReqReaderPrivateKeyTag, reader_private_key);
    append_tlv(reader_key_request, kReaderReqIdentifierTag, reader_identifier);

    request.clear();
    append_tlv_byte(request, kReaderOperationTag, kReaderOperationWrite);
    append_tlv(request, kReaderReaderKeyRequestTag, reader_key_request);
    return true;
}

bool HomeKeyProvisioningManager::BuildDeviceCredentialWriteRequest(const std::vector<uint8_t> & issuer_id,
                                                                   const std::vector<uint8_t> & endpoint_public_key,
                                                                   uint8_t key_type, std::vector<uint8_t> & request) const
{
    if (issuer_id.empty() || issuer_id.size() > 255 || endpoint_public_key.empty()) {
        return false;
    }

    std::vector<uint8_t> endpoint_key = endpoint_public_key;
    if (endpoint_key.size() == 65 && endpoint_key.front() == 0x04) {
        endpoint_key.erase(endpoint_key.begin());
    }
    if (endpoint_key.size() != 64 || endpoint_key.size() > 255) {
        return false;
    }

    std::vector<uint8_t> device_request;
    append_tlv_byte(device_request, kDeviceReqKeyTypeTag, key_type);
    append_tlv(device_request, kDeviceReqPublicKeyTag, endpoint_key);
    append_tlv(device_request, kDeviceReqIssuerKeyIdentifierTag, issuer_id);

    request.clear();
    append_tlv_byte(request, kReaderOperationTag, kReaderOperationWrite);
    append_tlv(request, kReaderDeviceCredentialRequestTag, device_request);
    return true;
}

const char * HomeKeyProvisioningManager::GetSetupCode() const
{
    return kHomeKitSetupCode;
}
