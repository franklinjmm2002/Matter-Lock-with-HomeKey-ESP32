/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <mutex>
#include <vector>

#include <nvs.h>

#include "DDKReaderData.h"

class HomeKeyReaderDataManager {
public:
    HomeKeyReaderDataManager();
    ~HomeKeyReaderDataManager();

    bool Begin();
    readerData_t GetReaderDataCopy() const;
    const readerData_t * UpdateReaderData(const readerData_t & new_data);
    const readerData_t * SaveData();
    bool EraseReaderKey();
    bool DeleteAllReaderData();
    bool AddIssuerIfNotExists(const std::vector<uint8_t> & issuer_id, const uint8_t * public_key);
    bool IsProvisioned() const;

private:
    bool Load();
    static void AppendU32(std::vector<uint8_t> & buffer, uint32_t value);
    static bool ReadU32(const std::vector<uint8_t> & buffer, size_t & offset, uint32_t & value);
    static void AppendBytes(std::vector<uint8_t> & buffer, const std::vector<uint8_t> & data);
    static bool ReadBytes(const std::vector<uint8_t> & buffer, size_t & offset, std::vector<uint8_t> & data);
    static void SerializeEndpoint(std::vector<uint8_t> & buffer, const hkEndpoint_t & endpoint);
    static bool DeserializeEndpoint(const std::vector<uint8_t> & buffer, size_t & offset, hkEndpoint_t & endpoint);
    static void SerializeIssuer(std::vector<uint8_t> & buffer, const hkIssuer_t & issuer);
    static bool DeserializeIssuer(const std::vector<uint8_t> & buffer, size_t & offset, hkIssuer_t & issuer);
    static void SerializeReaderData(std::vector<uint8_t> & buffer, const readerData_t & reader_data);
    static bool DeserializeReaderData(const std::vector<uint8_t> & buffer, readerData_t & reader_data);

    mutable std::mutex m_reader_data_mutex;
    readerData_t m_reader_data;
    nvs_handle_t m_nvs_handle = 0;
    bool m_initialized = false;

    static constexpr uint32_t kSerializationVersion = 1;
    static constexpr const char * kNamespace = "SAVED_DATA";
    static constexpr const char * kKey = "HK_READERDATA";
};

HomeKeyReaderDataManager & HomeKeyReaderDataMgr();
