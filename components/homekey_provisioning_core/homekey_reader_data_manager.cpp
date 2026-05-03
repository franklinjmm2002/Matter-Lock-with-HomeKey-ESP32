/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "homekey_reader_data_manager.h"

#include <algorithm>

#include <esp_log.h>
#include <nvs_flash.h>

namespace {
constexpr const char * TAG = "homekey_reader_data";
HomeKeyReaderDataManager g_homekey_reader_data_manager;
}

HomeKeyReaderDataManager & HomeKeyReaderDataMgr()
{
    return g_homekey_reader_data_manager;
}

HomeKeyReaderDataManager::HomeKeyReaderDataManager() = default;

HomeKeyReaderDataManager::~HomeKeyReaderDataManager()
{
    if (m_initialized) {
        nvs_close(m_nvs_handle);
    }
}

bool HomeKeyReaderDataManager::Begin()
{
    if (m_initialized) {
        return true;
    }

    const esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &m_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    m_initialized = true;
    return Load();
}

readerData_t HomeKeyReaderDataManager::GetReaderDataCopy() const
{
    std::lock_guard<std::mutex> lock(m_reader_data_mutex);
    return m_reader_data;
}

const readerData_t * HomeKeyReaderDataManager::UpdateReaderData(const readerData_t & new_data)
{
    {
        std::lock_guard<std::mutex> lock(m_reader_data_mutex);
        m_reader_data = new_data;
    }
    return SaveData();
}

const readerData_t * HomeKeyReaderDataManager::SaveData()
{
    if (!m_initialized) {
        return nullptr;
    }

    const readerData_t snapshot = GetReaderDataCopy();
    std::vector<uint8_t> buffer;
    SerializeReaderData(buffer, snapshot);

    esp_err_t err = nvs_set_blob(m_nvs_handle, kKey, buffer.data(), buffer.size());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store reader data: %s", esp_err_to_name(err));
        return nullptr;
    }

    err = nvs_commit(m_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit reader data: %s", esp_err_to_name(err));
        return nullptr;
    }

    return &m_reader_data;
}

bool HomeKeyReaderDataManager::EraseReaderKey()
{
    {
        std::lock_guard<std::mutex> lock(m_reader_data_mutex);
        m_reader_data.reader_gid.clear();
        m_reader_data.reader_id.clear();
        m_reader_data.reader_pk.clear();
        m_reader_data.reader_pk_x.clear();
        m_reader_data.reader_sk.clear();
    }
    return SaveData() != nullptr;
}

bool HomeKeyReaderDataManager::DeleteAllReaderData()
{
    {
        std::lock_guard<std::mutex> lock(m_reader_data_mutex);
        m_reader_data = {};
    }

    if (!m_initialized) {
        return false;
    }

    esp_err_t err = nvs_erase_key(m_nvs_handle, kKey);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase reader data: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_commit(m_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit reader data erase: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool HomeKeyReaderDataManager::AddIssuerIfNotExists(const std::vector<uint8_t> & issuer_id, const uint8_t * public_key)
{
    {
        std::lock_guard<std::mutex> lock(m_reader_data_mutex);
        const auto it = std::find_if(m_reader_data.issuers.begin(), m_reader_data.issuers.end(),
                                     [&](const hkIssuer_t & issuer) { return issuer.issuer_id == issuer_id; });
        if (it != m_reader_data.issuers.end()) {
            return false;
        }

        hkIssuer_t issuer;
        issuer.issuer_id = issuer_id;
        issuer.issuer_pk.assign(public_key, public_key + 32);
        m_reader_data.issuers.push_back(std::move(issuer));
    }

    return SaveData() != nullptr;
}

bool HomeKeyReaderDataManager::IsProvisioned() const
{
    std::lock_guard<std::mutex> lock(m_reader_data_mutex);
    return m_reader_data.reader_gid.size() == 8 && !m_reader_data.reader_id.empty() && !m_reader_data.reader_sk.empty() &&
           !m_reader_data.reader_pk.empty();
}

bool HomeKeyReaderDataManager::Load()
{
    if (!m_initialized) {
        return false;
    }

    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(m_nvs_handle, kKey, nullptr, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        std::lock_guard<std::mutex> lock(m_reader_data_mutex);
        m_reader_data = {};
        return true;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get reader data size: %s", esp_err_to_name(err));
        return false;
    }

    std::vector<uint8_t> buffer(required_size);
    err = nvs_get_blob(m_nvs_handle, kKey, buffer.data(), &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read reader data: %s", esp_err_to_name(err));
        return false;
    }

    readerData_t loaded_data;
    if (!DeserializeReaderData(buffer, loaded_data)) {
        ESP_LOGW(TAG, "Stored reader data is invalid, resetting it");
        std::lock_guard<std::mutex> lock(m_reader_data_mutex);
        m_reader_data = {};
        return true;
    }

    std::lock_guard<std::mutex> lock(m_reader_data_mutex);
    m_reader_data = std::move(loaded_data);
    return true;
}

void HomeKeyReaderDataManager::AppendU32(std::vector<uint8_t> & buffer, uint32_t value)
{
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

bool HomeKeyReaderDataManager::ReadU32(const std::vector<uint8_t> & buffer, size_t & offset, uint32_t & value)
{
    if (offset + 4 > buffer.size()) {
        return false;
    }
    value = static_cast<uint32_t>(buffer[offset]) | (static_cast<uint32_t>(buffer[offset + 1]) << 8) |
            (static_cast<uint32_t>(buffer[offset + 2]) << 16) | (static_cast<uint32_t>(buffer[offset + 3]) << 24);
    offset += 4;
    return true;
}

void HomeKeyReaderDataManager::AppendBytes(std::vector<uint8_t> & buffer, const std::vector<uint8_t> & data)
{
    AppendU32(buffer, static_cast<uint32_t>(data.size()));
    buffer.insert(buffer.end(), data.begin(), data.end());
}

bool HomeKeyReaderDataManager::ReadBytes(const std::vector<uint8_t> & buffer, size_t & offset, std::vector<uint8_t> & data)
{
    uint32_t size = 0;
    if (!ReadU32(buffer, offset, size) || offset + size > buffer.size()) {
        return false;
    }
    data.assign(buffer.begin() + static_cast<long>(offset), buffer.begin() + static_cast<long>(offset + size));
    offset += size;
    return true;
}

void HomeKeyReaderDataManager::SerializeEndpoint(std::vector<uint8_t> & buffer, const hkEndpoint_t & endpoint)
{
    AppendBytes(buffer, endpoint.endpoint_id);
    AppendU32(buffer, endpoint.last_used_at);
    buffer.push_back(endpoint.counter);
    buffer.push_back(endpoint.key_type);
    AppendBytes(buffer, endpoint.endpoint_pk);
    AppendBytes(buffer, endpoint.endpoint_pk_x);
    AppendBytes(buffer, endpoint.endpoint_prst_k);
}

bool HomeKeyReaderDataManager::DeserializeEndpoint(const std::vector<uint8_t> & buffer, size_t & offset,
                                                   hkEndpoint_t & endpoint)
{
    if (!ReadBytes(buffer, offset, endpoint.endpoint_id)) {
        return false;
    }
    if (!ReadU32(buffer, offset, endpoint.last_used_at) || offset + 2 > buffer.size()) {
        return false;
    }
    endpoint.counter = buffer[offset++];
    endpoint.key_type = buffer[offset++];
    return ReadBytes(buffer, offset, endpoint.endpoint_pk) && ReadBytes(buffer, offset, endpoint.endpoint_pk_x) &&
           ReadBytes(buffer, offset, endpoint.endpoint_prst_k);
}

void HomeKeyReaderDataManager::SerializeIssuer(std::vector<uint8_t> & buffer, const hkIssuer_t & issuer)
{
    AppendBytes(buffer, issuer.issuer_id);
    AppendBytes(buffer, issuer.issuer_pk);
    AppendBytes(buffer, issuer.issuer_pk_x);
    AppendU32(buffer, static_cast<uint32_t>(issuer.endpoints.size()));
    for (const auto & endpoint : issuer.endpoints) {
        SerializeEndpoint(buffer, endpoint);
    }
}

bool HomeKeyReaderDataManager::DeserializeIssuer(const std::vector<uint8_t> & buffer, size_t & offset, hkIssuer_t & issuer)
{
    if (!ReadBytes(buffer, offset, issuer.issuer_id) || !ReadBytes(buffer, offset, issuer.issuer_pk) ||
        !ReadBytes(buffer, offset, issuer.issuer_pk_x)) {
        return false;
    }

    uint32_t endpoint_count = 0;
    if (!ReadU32(buffer, offset, endpoint_count)) {
        return false;
    }
    issuer.endpoints.clear();
    issuer.endpoints.reserve(endpoint_count);
    for (uint32_t i = 0; i < endpoint_count; ++i) {
        hkEndpoint_t endpoint;
        if (!DeserializeEndpoint(buffer, offset, endpoint)) {
            return false;
        }
        issuer.endpoints.push_back(std::move(endpoint));
    }
    return true;
}

void HomeKeyReaderDataManager::SerializeReaderData(std::vector<uint8_t> & buffer, const readerData_t & reader_data)
{
    AppendU32(buffer, kSerializationVersion);
    AppendBytes(buffer, reader_data.reader_sk);
    AppendBytes(buffer, reader_data.reader_pk);
    AppendBytes(buffer, reader_data.reader_pk_x);
    AppendBytes(buffer, reader_data.reader_gid);
    AppendBytes(buffer, reader_data.reader_id);
    AppendU32(buffer, static_cast<uint32_t>(reader_data.issuers.size()));
    for (const auto & issuer : reader_data.issuers) {
        SerializeIssuer(buffer, issuer);
    }
}

bool HomeKeyReaderDataManager::DeserializeReaderData(const std::vector<uint8_t> & buffer, readerData_t & reader_data)
{
    size_t offset = 0;
    uint32_t version = 0;
    if (!ReadU32(buffer, offset, version) || version != kSerializationVersion) {
        return false;
    }
    if (!ReadBytes(buffer, offset, reader_data.reader_sk) || !ReadBytes(buffer, offset, reader_data.reader_pk) ||
        !ReadBytes(buffer, offset, reader_data.reader_pk_x) || !ReadBytes(buffer, offset, reader_data.reader_gid) ||
        !ReadBytes(buffer, offset, reader_data.reader_id)) {
        return false;
    }

    uint32_t issuer_count = 0;
    if (!ReadU32(buffer, offset, issuer_count)) {
        return false;
    }
    reader_data.issuers.clear();
    reader_data.issuers.reserve(issuer_count);
    for (uint32_t i = 0; i < issuer_count; ++i) {
        hkIssuer_t issuer;
        if (!DeserializeIssuer(buffer, offset, issuer)) {
            return false;
        }
        reader_data.issuers.push_back(std::move(issuer));
    }
    return offset == buffer.size();
}
