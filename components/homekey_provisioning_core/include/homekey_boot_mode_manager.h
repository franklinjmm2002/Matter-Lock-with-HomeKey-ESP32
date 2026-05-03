/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <cstdint>

#include <esp_err.h>
#include <esp_partition.h>
#include <nvs.h>

enum class HomeKeyBootMode : uint8_t {
    kNormal = 0,
    kProvisioner = 1,
    kReturnToMain = 2,
};

class HomeKeyBootModeManager {
public:
    ~HomeKeyBootModeManager();

    bool Begin() const;
    HomeKeyBootMode GetRequestedMode() const;
    esp_err_t SetRequestedMode(HomeKeyBootMode mode);
    esp_err_t ClearRequestedMode();

    bool IsRunningFromPartition(esp_partition_subtype_t subtype) const;
    esp_err_t SwitchToPartition(esp_partition_subtype_t subtype) const;
    esp_err_t SwitchToProvisioner() const;
    esp_err_t SwitchToMainRuntime() const;

private:
    const esp_partition_t * FindPartition(esp_partition_subtype_t subtype) const;

    mutable bool m_initialized = false;
    mutable nvs_handle_t m_nvs_handle = 0;

    static constexpr const char * kNamespace = "HK_BOOTMODE";
    static constexpr const char * kModeKey = "mode";
};

HomeKeyBootModeManager & HomeKeyBootModeMgr();
