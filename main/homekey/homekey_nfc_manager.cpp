/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "homekey_nfc_manager.h"

#include <chrono>
#include <cstring>
#include <new>

#include <esp_log.h>
#include <soc/gpio_num.h>

#include "homekey_manager.h"

using chip::ByteSpan;
using chip::app::Clusters::DoorLock::OperationErrorEnum;

namespace {
constexpr const char *TAG = "homekey_nfc";
}

HomeKeyNfcManager::HomeKeyNfcManager(HomeKeyReaderDataManager & reader_data_manager, uint8_t ss, uint8_t sck, uint8_t miso, uint8_t mosi)
    : m_reader_data_manager(reader_data_manager), m_ss(ss), m_sck(sck), m_miso(miso), m_mosi(mosi)
{
}

bool HomeKeyNfcManager::Begin()
{
    if (m_started) {
        return true;
    }

    if (!m_reader_data_manager.Begin()) {
        ESP_LOGW(TAG, "Reader data manager did not initialize cleanly");
    }

    m_transport = new (std::nothrow) pn532::SpiTransport(GPIO_NUM_NC, static_cast<gpio_num_t>(m_miso),
                                                         static_cast<gpio_num_t>(m_mosi),
                                                         static_cast<gpio_num_t>(m_sck),
                                                         static_cast<gpio_num_t>(m_ss));
    if (m_transport == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate PN532 SPI transport");
        return false;
    }

    m_frontend = new (std::nothrow) pn532::Frontend(*m_transport);
    if (m_frontend == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate PN532 frontend");
        return false;
    }

    if (xTaskCreate(&HomeKeyNfcManager::PollingTaskEntry, "homekey_nfc_poll", 8192, this, 2, &m_polling_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Home Key NFC polling task");
        return false;
    }

    m_started = true;
    return true;
}

void HomeKeyNfcManager::PollingTaskEntry(void * arg)
{
    static_cast<HomeKeyNfcManager *>(arg)->PollingTask();
}

bool HomeKeyNfcManager::InitializeReader()
{
    m_frontend->begin();
    const uint32_t version = m_frontend->GetFirmwareVersion();
    if (version == 0) {
        ESP_LOGE(TAG, "Failed to read PN532 firmware version");
        return false;
    }

    ESP_LOGI(TAG, "PN532 firmware %u.%u detected", static_cast<unsigned>((version >> 24) & 0xFF),
             static_cast<unsigned>((version >> 16) & 0xFF));
    m_frontend->RFConfiguration(0x01, {0x03});
    m_frontend->setPassiveActivationRetries(0);
    m_frontend->RFConfiguration(0x02, {0x00, 0x0B, 0x10});
    m_frontend->RFConfiguration(0x04, {0xFF});
    UpdateEcpData();
    return true;
}

void HomeKeyNfcManager::UpdateEcpData()
{
    const readerData_t reader_data = m_reader_data_manager.GetReaderDataCopy();
    if (reader_data.reader_gid.size() == 8) {
        memcpy(m_ecp_data.data() + 8, reader_data.reader_gid.data(), 8);
        Crc16A(m_ecp_data.data(), 16, m_ecp_data.data() + 16);
    }
}

void HomeKeyNfcManager::PollingTask()
{
    while (true) {
        if (!InitializeReader()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        while (true) {
            if (m_frontend->WriteRegister({0x63, 0x3D, 0x00}) != pn532::SUCCESS) {
                ESP_LOGW(TAG, "PN532 became unresponsive, restarting NFC initialization");
                break;
            }

            UpdateEcpData();
            std::vector<uint8_t> ecp_response;
            m_frontend->InCommunicateThru(std::vector<uint8_t>(m_ecp_data.begin(), m_ecp_data.end()), ecp_response, 50);

            std::vector<uint8_t> uid;
            std::array<uint8_t, 2> sens_res = {};
            uint8_t sel_res = 0;
            if (m_frontend->InListPassiveTarget(PN532_MIFARE_ISO14443A, uid, sens_res, sel_res, 500) == pn532::SUCCESS) {
                HandleTagPresence(uid, sens_res, sel_res);
                while (m_frontend->InListPassiveTarget(0x00, uid, sens_res, sel_res) == pn532::SUCCESS) {
                    m_frontend->InRelease(1);
                    vTaskDelay(pdMS_TO_TICKS(60));
                }
                m_frontend->setPassiveActivationRetries(0);
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void HomeKeyNfcManager::HandleTagPresence(const std::vector<uint8_t> & uid, const std::array<uint8_t, 2> & atqa, uint8_t sak)
{
    (void) atqa;
    (void) sak;

    const auto start_time = std::chrono::high_resolution_clock::now();
    static constexpr uint8_t kSelectAppletCmd[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x08, 0x58, 0x01,
                                                   0x01, 0x00};
    std::vector<uint8_t> response;
    const pn532::Status status =
        m_frontend->InDataExchange(std::vector<uint8_t>(kSelectAppletCmd, kSelectAppletCmd + sizeof(kSelectAppletCmd)), response);

    if (status == pn532::SUCCESS && response.size() >= 2 && response[response.size() - 2] == 0x90 &&
        response[response.size() - 1] == 0x00) {
        HandleHomeKeyAuth();
    } else {
        ESP_LOGI(TAG, "Non-Home Key NFC tag detected, uid_len=%u", static_cast<unsigned>(uid.size()));
    }

    m_frontend->InRelease(1);
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
    ESP_LOGI(TAG, "NFC tag processing completed in %lld ms", duration);
}

void HomeKeyNfcManager::HandleHomeKeyAuth()
{
    if (!m_reader_data_manager.IsProvisioned()) {
        ESP_LOGW(TAG, "Home Key reader data is not provisioned yet");
        return;
    }

    readerData_t reader_data = m_reader_data_manager.GetReaderDataCopy();
    std::function<bool(std::vector<uint8_t> &, std::vector<uint8_t> &, bool)> nfc_fn =
        [this](std::vector<uint8_t> & send, std::vector<uint8_t> & recv, bool) {
            if (send.size() > 255) {
                return false;
            }
            const pn532::Status ok = m_frontend->InDataExchange(send, recv);
            if (recv.size() >= 2) {
                recv.erase(recv.begin(), recv.begin() + 2);
            }
            return ok == pn532::SUCCESS;
        };
    std::function<void(const readerData_t &)> save_fn = [this](const readerData_t & updated_data) {
        m_reader_data_manager.UpdateReaderData(updated_data);
        UpdateEcpData();
    };

    DDKAuthenticationContext auth_ctx(kHomeKey, nfc_fn, reader_data, save_fn);
    const auto auth_result = auth_ctx.authenticate(kFlowFAST);
    if (std::get<2>(auth_result) == kFlowFailed) {
        ESP_LOGW(TAG, "Home Key authentication failed");
        return;
    }

    OperationErrorEnum err = OperationErrorEnum::kUnspecified;
    const auto & issuer_id = std::get<0>(auth_result);
    const auto & endpoint_id = std::get<1>(auth_result);
    if (!HomeKeyMgr().UnlockAuthenticatedHomeKey(ByteSpan(issuer_id.data(), issuer_id.size()),
                                                 ByteSpan(endpoint_id.data(), endpoint_id.size()), err)) {
        ESP_LOGW(TAG, "Matter lock did not accept the authenticated Home Key tap, err=%u",
                 static_cast<unsigned>(err));
        return;
    }

    ESP_LOGI(TAG, "Home Key authentication succeeded");
}

void HomeKeyNfcManager::Crc16A(unsigned char * data, unsigned int size, unsigned char * result)
{
    unsigned short crc = 0x6363;
    for (unsigned int i = 0; i < size; ++i) {
        unsigned char byte = data[i];
        byte = (byte ^ (crc & 0x00FF));
        byte = ((byte ^ (byte << 4)) & 0xFF);
        crc = ((crc >> 8) ^ (byte << 8) ^ (byte << 3) ^ (byte >> 4)) & 0xFFFF;
    }
    result[0] = static_cast<unsigned char>(crc & 0xFF);
    result[1] = static_cast<unsigned char>((crc >> 8) & 0xFF);
}
