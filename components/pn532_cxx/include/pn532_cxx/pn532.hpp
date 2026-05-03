#pragma once

#include "transport.hpp"
#include <cstdint>
#include "pn532_cxx/span.hpp"
#include <vector>

#include "loggable.hpp"

#define PN532_MIFARE_ISO14443A (0x00)

namespace pn532 {

class Frontend : loggable::Loggable {
public:
  explicit Frontend(Transport &protocol);

  Status begin();

  Status setPassiveActivationRetries(uint8_t maxRetries);
  Status RFConfiguration(uint8_t cfgItem, std::vector<uint8_t> confData);
  uint32_t GetFirmwareVersion();
  Status GetGeneralStatus(std::vector<uint8_t> &response);
  Status PowerDown(uint8_t wakeupEnable, uint8_t genIrqEnable);
  Status ReadRegister(std::vector<uint8_t> reg, std::vector<uint8_t> &response);
  Status WriteRegister(std::vector<uint8_t> data);
  Status SAMConfig();

  Status InRelease(uint8_t target);

  Status InSelect(uint8_t target);

  Status InListPassiveTarget(uint8_t cardbaudrate, std::vector<uint8_t> &uid,
                             std::array<uint8_t, 2> &sens_res, uint8_t &sel_res,
                             uint16_t timeout = 1000);
  Status InCommunicateThru(std::vector<uint8_t> send,
                           std::vector<uint8_t> &response,
                           uint16_t timeout = 1000);
  Status InDataExchange(std::vector<uint8_t> send,
                        std::vector<uint8_t> &response,
                        uint16_t timeout = 1000);

private:
  static const char *TAG;
  Transport &_protocol;
  std::vector<uint8_t> _packetBuffer;

  /**
   * @brief Send a command and receive response using Transaction.
   */
  Status transceive(span<const uint8_t> cmd, std::vector<uint8_t> &response,
                     uint32_t timeout_ms = 1000);

  /**
   * @brief Parse response frame from active transaction.
   */
  Status parseResponse(Transaction &txn, std::vector<uint8_t> &buffer);
};

} // namespace pn532
