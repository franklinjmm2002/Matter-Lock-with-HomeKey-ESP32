/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "homekey_manager.h"

#include <esp_log.h>

#include "../lock/door_lock_manager.h"
#include "web_config.h"
#include <lib/support/CodeUtils.h>
#include <platform/PlatformManager.h>

#include <esp_timer.h>
#include <esp_matter.h>
#include <app-common/zap-generated/attributes/Accessors.h>

using namespace chip;
using namespace chip::app::Clusters;

namespace {
constexpr const char *TAG = "homekey_manager";
HomeKeyManager gHomeKeyManager;

esp_timer_handle_t s_auto_relock_timer = nullptr;

void auto_relock_timer_cb(void* arg) {
    ESP_LOGI(TAG, "Auto-relock timer expired");
    DoorLock::OperationErrorEnum err;
    HomeKeyMgr().LockFromSource(HomeKeyManager::AccessSource::kHomeKey, err);
}

} // namespace

HomeKeyManager & HomeKeyMgr()
{
    return gHomeKeyManager;
}

CHIP_ERROR HomeKeyManager::Init(EndpointId endpointId, BoltLockManager * lockManager)
{
    VerifyOrReturnError(lockManager != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    mEndpointId = endpointId;
    mLockManager = lockManager;
    ESP_LOGI(TAG, "HomeKey manager initialized for endpoint %d", endpointId);
    return CHIP_NO_ERROR;
}

void HomeKeyManager::SetExpressModeEnabled(bool enabled)
{
    mExpressModeEnabled = enabled;
    ESP_LOGI(TAG, "Express Mode %s", enabled ? "enabled" : "disabled");
}

bool HomeKeyManager::IsExpressModeEnabled() const
{
    return mExpressModeEnabled;
}

void HomeKeyManager::SetVerifier(HomeKeyVerifier verifier)
{
    mVerifier = verifier;
}

bool HomeKeyManager::LockFromMatter(const Optional<ByteSpan> & pin, DoorLock::OperationErrorEnum & err)
{
    VerifyOrReturnValue(mLockManager != nullptr, false);
    bool success = mLockManager->Lock(mEndpointId, pin, err);
    if (success) {
        trigger_lock_action(false);
    }
    return success;
}

bool HomeKeyManager::UnlockFromMatter(const Optional<ByteSpan> & pin, DoorLock::OperationErrorEnum & err)
{
    VerifyOrReturnValue(mLockManager != nullptr, false);
    bool success = mLockManager->Unlock(mEndpointId, pin, err);
    if (success) {
        trigger_lock_action(true);
    }
    return success;
}

bool HomeKeyManager::UnlockAuthenticatedHomeKey(const ByteSpan & issuer_id, const ByteSpan & endpoint_id,
                                                DoorLock::OperationErrorEnum & err)
{
    if (!mExpressModeEnabled) {
        ESP_LOGW(TAG, "Authenticated Home Key tap received while Express Mode is disabled");
    }

    ESP_LOGI(TAG, "Authenticated Home Key issuer_len=%u endpoint_len=%u", static_cast<unsigned>(issuer_id.size()),
             static_cast<unsigned>(endpoint_id.size()));
    return SetLockStateFromSource(DoorLock::DlLockState::kUnlocked, AccessSource::kHomeKey, err);
}

bool HomeKeyManager::LockFromSource(AccessSource source, DoorLock::OperationErrorEnum & err)
{
    return SetLockStateFromSource(DoorLock::DlLockState::kLocked, source, err);
}

bool HomeKeyManager::UnlockWithHomeKey(const ByteSpan & credential, DoorLock::OperationErrorEnum & err)
{
    if (!mExpressModeEnabled) {
        ESP_LOGW(TAG, "Home Key presented while Express Mode is disabled");
    }

    if (mVerifier == nullptr) {
        ESP_LOGW(TAG, "Home Key verifier is not registered. Integrate Apple-approved NFC/SE verification here.");
        err = DoorLock::OperationErrorEnum::kInvalidCredential;
        return false;
    }

    if (!mVerifier(mEndpointId, credential)) {
        ESP_LOGW(TAG, "Home Key credential rejected");
        err = DoorLock::OperationErrorEnum::kInvalidCredential;
        return false;
    }

    return SetLockStateFromSource(DoorLock::DlLockState::kUnlocked, AccessSource::kHomeKey, err);
}

bool HomeKeyManager::SetLockStateFromSource(DoorLock::DlLockState lockState, AccessSource source,
                                            DoorLock::OperationErrorEnum & err)
{
    VerifyOrReturnValue(mLockManager != nullptr, false);

    ESP_LOGI(TAG, "Setting lock state to %s from source %u", mLockManager->lockStateToString(lockState),
             static_cast<unsigned>(source));
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    DoorLockServer::Instance().SetLockState(mEndpointId, lockState);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    err = DoorLock::OperationErrorEnum::kUnspecified;

    if (lockState == DoorLock::DlLockState::kUnlocked) {
        trigger_lock_action(true);
        
        uint32_t auto_relock_time = 0;
        if (DoorLock::Attributes::AutoRelockTime::Get(mEndpointId, &auto_relock_time) == chip::Protocols::InteractionModel::Status::Success && auto_relock_time > 0) {
            if (s_auto_relock_timer == nullptr) {
                esp_timer_create_args_t timer_args = {};
                timer_args.callback = auto_relock_timer_cb;
                timer_args.name = "auto_relock";
                esp_timer_create(&timer_args, &s_auto_relock_timer);
            }
            esp_timer_stop(s_auto_relock_timer);
            esp_timer_start_once(s_auto_relock_timer, auto_relock_time * 1000000ULL);
            ESP_LOGI(TAG, "Auto-relock timer started for %lu seconds", auto_relock_time);
        }
    } else if (lockState == DoorLock::DlLockState::kLocked) {
        trigger_lock_action(false);
        if (s_auto_relock_timer != nullptr) {
            esp_timer_stop(s_auto_relock_timer);
            ESP_LOGI(TAG, "Auto-relock timer stopped");
        }
    }

    return true;
}
