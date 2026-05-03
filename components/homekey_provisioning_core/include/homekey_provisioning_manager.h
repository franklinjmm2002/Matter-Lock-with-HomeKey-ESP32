/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <cstdint>
#include <vector>

class HomeKeyProvisioningManager {
public:
    bool Begin();
    bool ProcessRequest(const std::vector<uint8_t> & request, std::vector<uint8_t> & response);
    bool BuildReaderKeyReadRequest(std::vector<uint8_t> & request) const;
    bool BuildReaderKeyWriteRequest(const std::vector<uint8_t> & reader_private_key,
                                    const std::vector<uint8_t> & reader_identifier, std::vector<uint8_t> & request) const;
    bool BuildDeviceCredentialWriteRequest(const std::vector<uint8_t> & issuer_id,
                                           const std::vector<uint8_t> & endpoint_public_key, uint8_t key_type,
                                           std::vector<uint8_t> & request) const;
    const char * GetSetupCode() const;

private:
    bool m_started = false;
};

HomeKeyProvisioningManager & HomeKeyProvisioningMgr();
