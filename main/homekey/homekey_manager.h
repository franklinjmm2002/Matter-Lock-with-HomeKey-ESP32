/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdint.h>

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <esp_err.h>
#include <lib/core/CHIPError.h>
#include <lib/core/Optional.h>
#include <lib/support/Span.h>

class BoltLockManager;

class HomeKeyManager {
public:
    enum class AccessSource : uint8_t {
        kMatter = 0,
        kHomeKey,
        kManual,
    };

    using HomeKeyVerifier = bool (*)(chip::EndpointId endpointId, const chip::ByteSpan & credential);

    CHIP_ERROR Init(chip::EndpointId endpointId, BoltLockManager * lockManager);
    void SetExpressModeEnabled(bool enabled);
    bool IsExpressModeEnabled() const;
    void SetVerifier(HomeKeyVerifier verifier);

    bool LockFromMatter(const chip::Optional<chip::ByteSpan> & pin, chip::app::Clusters::DoorLock::OperationErrorEnum & err);
    bool UnlockFromMatter(const chip::Optional<chip::ByteSpan> & pin, chip::app::Clusters::DoorLock::OperationErrorEnum & err);
    bool UnlockAuthenticatedHomeKey(const chip::ByteSpan & issuer_id, const chip::ByteSpan & endpoint_id,
                                    chip::app::Clusters::DoorLock::OperationErrorEnum & err);

    bool LockFromSource(AccessSource source, chip::app::Clusters::DoorLock::OperationErrorEnum & err);
    bool UnlockWithHomeKey(const chip::ByteSpan & credential, chip::app::Clusters::DoorLock::OperationErrorEnum & err);

private:
    bool SetLockStateFromSource(chip::app::Clusters::DoorLock::DlLockState lockState, AccessSource source,
                                chip::app::Clusters::DoorLock::OperationErrorEnum & err);

    chip::EndpointId mEndpointId = 0;
    BoltLockManager *mLockManager = nullptr;
    HomeKeyVerifier mVerifier = nullptr;
    bool mExpressModeEnabled = true;
};

HomeKeyManager & HomeKeyMgr();
