/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <array>
#include <vector>

#include "DDKAuthContext.h"
#include "DDKReaderData.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pn532_cxx/pn532.hpp"
#include "pn532_hal/spi.hpp"

#include "homekey_reader_data_manager.h"

class HomeKeyNfcManager {
public:
    explicit HomeKeyNfcManager(HomeKeyReaderDataManager & reader_data_manager, uint8_t ss, uint8_t sck, uint8_t miso, uint8_t mosi);
    bool Begin();

private:
    static void PollingTaskEntry(void * arg);
    void PollingTask();
    bool InitializeReader();
    void UpdateEcpData();
    void HandleTagPresence(const std::vector<uint8_t> & uid, const std::array<uint8_t, 2> & atqa, uint8_t sak);
    void HandleHomeKeyAuth();
    static void Crc16A(unsigned char * data, unsigned int size, unsigned char * result);

    HomeKeyReaderDataManager & m_reader_data_manager;
    std::array<uint8_t, 18> m_ecp_data = {0x6A, 0x02, 0xCB, 0x02, 0x06, 0x02, 0x11, 0x00};
    pn532::SpiTransport * m_transport = nullptr;
    pn532::Frontend * m_frontend = nullptr;
    TaskHandle_t m_polling_task = nullptr;
    bool m_started = false;

    uint8_t m_ss;
    uint8_t m_sck;
    uint8_t m_miso;
    uint8_t m_mosi;
};
