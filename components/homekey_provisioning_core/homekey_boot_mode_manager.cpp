/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "homekey_boot_mode_manager.h"

#include <inttypes.h>

#include <esp_log.h>
#include <esp_ota_ops.h>

namespace {
constexpr const char * TAG = "hk_boot_mode";
HomeKeyBootModeManager g_homekey_boot_mode_manager;
}

HomeKeyBootModeManager & HomeKeyBootModeMgr()
{
    return g_homekey_boot_mode_manager;
}

HomeKeyBootModeManager::~HomeKeyBootModeManager()
{
    if (m_initialized) {
        nvs_close(m_nvs_handle);
    }
}

bool HomeKeyBootModeManager::Begin() const
{
    if (m_initialized) {
        return true;
    }

    nvs_handle_t handle = 0;
    const esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open boot-mode namespace: %s", esp_err_to_name(err));
        return false;
    }

    m_nvs_handle = handle;
    m_initialized = true;
    return true;
}

HomeKeyBootMode HomeKeyBootModeManager::GetRequestedMode() const
{
    if (!Begin()) {
        return HomeKeyBootMode::kNormal;
    }

    uint8_t raw_mode = static_cast<uint8_t>(HomeKeyBootMode::kNormal);
    const esp_err_t err = nvs_get_u8(m_nvs_handle, kModeKey, &raw_mode);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return HomeKeyBootMode::kNormal;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read boot-mode flag: %s", esp_err_to_name(err));
        return HomeKeyBootMode::kNormal;
    }

    switch (raw_mode) {
    case static_cast<uint8_t>(HomeKeyBootMode::kProvisioner):
        return HomeKeyBootMode::kProvisioner;
    case static_cast<uint8_t>(HomeKeyBootMode::kReturnToMain):
        return HomeKeyBootMode::kReturnToMain;
    default:
        return HomeKeyBootMode::kNormal;
    }
}

esp_err_t HomeKeyBootModeManager::SetRequestedMode(HomeKeyBootMode mode)
{
    if (!Begin()) {
        return ESP_FAIL;
    }

    esp_err_t err = nvs_set_u8(m_nvs_handle, kModeKey, static_cast<uint8_t>(mode));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write boot-mode flag: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_commit(m_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit boot-mode flag: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t HomeKeyBootModeManager::ClearRequestedMode()
{
    return SetRequestedMode(HomeKeyBootMode::kNormal);
}

bool HomeKeyBootModeManager::IsRunningFromPartition(esp_partition_subtype_t subtype) const
{
    const esp_partition_t * running = esp_ota_get_running_partition();
    return running != nullptr && running->subtype == subtype;
}

const esp_partition_t * HomeKeyBootModeManager::FindPartition(esp_partition_subtype_t subtype) const
{
    const esp_partition_t * partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, nullptr);
    if (partition == nullptr) {
        ESP_LOGE(TAG, "Failed to locate app partition subtype 0x%x", subtype);
    }
    return partition;
}

esp_err_t HomeKeyBootModeManager::SwitchToPartition(esp_partition_subtype_t subtype) const
{
    const esp_partition_t * partition = FindPartition(subtype);
    if (partition == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    const esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select boot partition subtype 0x%x: %s", subtype, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Next boot partition set to subtype 0x%x at offset 0x%" PRIx32, subtype, partition->address);
    return ESP_OK;
}

esp_err_t HomeKeyBootModeManager::SwitchToProvisioner() const
{
    return SwitchToPartition(ESP_PARTITION_SUBTYPE_APP_OTA_1);
}

esp_err_t HomeKeyBootModeManager::SwitchToMainRuntime() const
{
    return SwitchToPartition(ESP_PARTITION_SUBTYPE_APP_OTA_0);
}
