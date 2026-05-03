#include "pn532_cxx/pn532.hpp"
#include "fmt/ranges.h"
#include "loggable.hpp"
#include "pn532_cxx/transport.hpp"
#include <array>
#include <cstdint>
#include <cstring>

#define PN532_PREAMBLE (0x00)
#define PN532_STARTCODE1 (0x00)
#define PN532_STARTCODE2 (0xFF)
#define PN532_POSTAMBLE (0x00)

#define PN532_HOSTTOPN532 (0xD4)
#define PN532_PN532TOHOST (0xD5)

#define PN532_COMMAND_GETFIRMWAREVERSION (0x02)
#define PN532_COMMAND_SAMCONFIGURATION (0x14)
#define PN532_COMMAND_INLISTPASSIVETARGET (0x4A)
#define PN532_COMMAND_RFCONFIGURATION (0x32)

#define PN532_COMMAND_INRELEASE (0x52)
#define PN532_COMMAND_INSELECT (0x54)

#define PN532_COMMAND_GETGENERALSTATUS (0x04)

#define PN532_COMMAND_POWERDOWN (0x16)

#define PN532_COMMAND_INDATAEXCHANGE (0x40)
#define PN532_COMMAND_INCOMMUNICATETHRU (0x42)

#define PN532_COMMAND_READREGISTER (0x06)
#define PN532_COMMAND_WRITEREGISTER (0x08)

namespace pn532 {

const char *Frontend::TAG = "PN532";

Frontend::Frontend(Transport &protocol)
    : Loggable("PN532::Frontend"), _protocol(protocol) {
  _packetBuffer.resize(64);
}

Status Frontend::begin() {
  _protocol.swReset();
  return SAMConfig();
}

uint32_t Frontend::GetFirmwareVersion() {
  uint8_t command[] = {PN532_COMMAND_GETFIRMWAREVERSION};

  std::vector<uint8_t> response;
  Status status = transceive(command, response);

  if (status != Status::SUCCESS) {
    return 0;
  }

  if (response.size() < 5) {
    return 0;
  }

  uint32_t version = response[2];
  version <<= 8;
  version |= response[3];
  version <<= 16;
  version |= response[4];
  return version;
}

Status Frontend::SAMConfig() {
  uint8_t command[] = {
      PN532_COMMAND_SAMCONFIGURATION,
      0x01, // Normal mode
      0x14, // Timeout 50ms * 20 = 1 second
      0x01  // Use IRQ pin
  };

  return transceive(command, _packetBuffer);
}

Status Frontend::RFConfiguration(uint8_t cfgItem,
                                 std::vector<uint8_t> confData) {
  std::vector<uint8_t> command = {PN532_COMMAND_RFCONFIGURATION, cfgItem};
  command.insert(command.end(), confData.begin(), confData.end());

  return transceive(command, _packetBuffer);
}

Status Frontend::setPassiveActivationRetries(uint8_t maxRetries) {
  std::vector<uint8_t> confData = {0xFF, // MxRtyATR (default = 0xFF)
                                   0x01, // MxRtyPSL (default = 0x01)
                                   maxRetries};

  return RFConfiguration(0x05, confData);
}

Status Frontend::InRelease(uint8_t target) {
  std::vector<uint8_t> command = {PN532_COMMAND_INRELEASE, target};
  return transceive(command, _packetBuffer);
}

Status Frontend::InSelect(uint8_t target) {
  std::vector<uint8_t> command = {PN532_COMMAND_INSELECT, target};
  Status status = transceive(command, _packetBuffer);
  if (status != Status::SUCCESS) {
    return status;
  }
  if (_packetBuffer.size() > 1 && _packetBuffer[1] != 0x00) {
    return Status::INVALID_TARGET;
  }
  return status;
}

Status Frontend::GetGeneralStatus(std::vector<uint8_t> &response) {
  std::vector<uint8_t> command = {PN532_COMMAND_GETGENERALSTATUS};
  return transceive(command, response);
}

Status Frontend::PowerDown(uint8_t wakeupEnable, uint8_t genIrqEnable) {
  std::vector<uint8_t> command = {PN532_COMMAND_POWERDOWN, wakeupEnable,
                                  genIrqEnable};
  return transceive(command, _packetBuffer);
}

Status Frontend::InListPassiveTarget(uint8_t cardbaudrate,
                                     std::vector<uint8_t> &uid,
                                     std::array<uint8_t, 2> &sens_res,
                                     uint8_t &sel_res, uint16_t timeout) {
  uint8_t command[] = {PN532_COMMAND_INLISTPASSIVETARGET, 1, cardbaudrate};

  std::vector<uint8_t> response;
  Status status = transceive(command, response, timeout);
  if (status != Status::SUCCESS) {
    return status;
  }

  if (response.size() < 2 || response[1] == 0) {
    return Status::NO_TAGS_FOUND;
  }

  if (response.size() < 8) {
    return Status::INVALID_FRAME;
  }

  uint8_t uid_len = response[6];
  if (response.size() < 7 + uid_len) {
    return Status::INVALID_FRAME;
  }

  std::copy(response.begin() + 7, response.begin() + 7 + uid_len,
            std::back_inserter(uid));

  sens_res[0] = response[3];
  sens_res[1] = response[4];
  sel_res = response[5];

  return status;
}

Status Frontend::InCommunicateThru(std::vector<uint8_t> send,
                                   std::vector<uint8_t> &response,
                                   uint16_t timeout) {
  std::vector<uint8_t> command = {PN532_COMMAND_INCOMMUNICATETHRU};
  command.insert(command.end(), send.begin(), send.end());

  return transceive(command, response, timeout);
}

Status Frontend::InDataExchange(std::vector<uint8_t> send,
                                std::vector<uint8_t> &response,
                                uint16_t timeout) {
  std::vector<uint8_t> command = {PN532_COMMAND_INDATAEXCHANGE, 0x01};
  command.insert(command.end(), send.begin(), send.end());

  Status status = transceive(command, response, timeout);

  LOG(loggable::LogLevel::Debug, "Response Status: {}",
       fmt::underlying(status));

  if (!response.empty() && response[0] != 0x41) {
    return Status::INVALID_FRAME;
  }
  return status;
}

Status Frontend::ReadRegister(std::vector<uint8_t> reg,
                              std::vector<uint8_t> &response) {
  std::vector<uint8_t> command = {PN532_COMMAND_READREGISTER};
  command.insert(command.end(), reg.begin(), reg.end());

  return transceive(command, response);
}

Status Frontend::WriteRegister(std::vector<uint8_t> data) {
  std::vector<uint8_t> command = {PN532_COMMAND_WRITEREGISTER};
  command.insert(command.end(), data.begin(), data.end());

  return transceive(command, _packetBuffer);
}

Status Frontend::transceive(span<const uint8_t> cmd,
                             std::vector<uint8_t> &response,
                             uint32_t timeout_ms) {
  std::vector<uint8_t> packet;
  packet.push_back(PN532_PREAMBLE);
  packet.push_back(PN532_STARTCODE1);
  packet.push_back(PN532_STARTCODE2);

  uint8_t cmd_len = cmd.size() + 1;
  packet.push_back(cmd_len);
  packet.push_back(~cmd_len + 1);

  packet.push_back(PN532_HOSTTOPN532);
  uint8_t sum = PN532_HOSTTOPN532;

  for (uint8_t b : cmd) {
    packet.push_back(b);
    sum += b;
  }

  packet.push_back(~sum + 1);
  packet.push_back(PN532_POSTAMBLE);

  auto txn = _protocol.begin();
  if (!txn) {
    return Status::TRANSPORT_ERROR;
  }

  Status st = txn.write(packet);
  if (st != Status::SUCCESS) {
    return st;
  }

  st = txn.waitForAck();
  if (st != Status::SUCCESS) {
    return st;
  }

  st = txn.waitForResponse(timeout_ms);
  if (st != Status::SUCCESS) {
    return st;
  }

  return parseResponse(txn, response);
}

Status Frontend::parseResponse(Transaction &txn, std::vector<uint8_t> &buffer) {
  // PN532 Frame formats:
  // Standard: [00] [00] [FF] [LEN] [LCS] [TFI] [DATA...] [DCS] [00]
  // Extended: [00] [00] [FF] [FF]  [FF]  [LENM] [LENL] [LCS] [TFI] [DATA...]
  // [DCS] [00]

  std::array<uint8_t, 3> preamble;
  bool found_start = false;
  constexpr size_t MAX_SCAN = 10;

  for (size_t scan = 0; scan < MAX_SCAN; ++scan) {
    Status st = txn.read(preamble);
    if (st != Status::SUCCESS) {
      return st;
    }

    if (preamble[0] == 0x00 && preamble[1] == 0x00 && preamble[2] == 0xFF) {
      found_start = true;
      break;
    }

    preamble[0] = preamble[1];
    preamble[1] = preamble[2];
    std::array<uint8_t, 1> next;
    st = txn.read(next);
    if (st != Status::SUCCESS) {
      return st;
    }
    preamble[2] = next[0];

    if (preamble[0] == 0x00 && preamble[1] == 0x00 && preamble[2] == 0xFF) {
      found_start = true;
      break;
    }
  }

  if (!found_start) {
    LOG(loggable::LogLevel::Error, "Preamble missing");
    return Status::INVALID_FRAME;
  }

  std::array<uint8_t, 2> len_lcs;
  Status st = txn.read(len_lcs);
  if (st != Status::SUCCESS) {
    return st;
  }

  uint16_t len = 0;

  if (len_lcs[0] == 0xFF && len_lcs[1] == 0xFF) {
    // Extended frame: read LENM, LENL, LCS
    std::array<uint8_t, 3> ext_len;
    st = txn.read(ext_len);
    if (st != Status::SUCCESS) {
      return st;
    }

    uint8_t len_m = ext_len[0];
    uint8_t len_l = ext_len[1];
    uint8_t lcs = ext_len[2];

    if (static_cast<uint8_t>(len_m + len_l + lcs) != 0x00) {
      LOG(loggable::LogLevel::Error, "Extended Length Checksum Error");
      return Status::CHECKSUM_ERROR;
    }

    len = (len_m << 8) | len_l;
    LOG(loggable::LogLevel::Debug, "Extended frame (LEN={})", len);
  } else {
    uint8_t len_std = len_lcs[0];
    uint8_t lcs = len_lcs[1];

    if (static_cast<uint8_t>(len_std + lcs) != 0x00) {
      LOG(loggable::LogLevel::Error, "Length Checksum Error");
      return Status::CHECKSUM_ERROR;
    }
    LOG(loggable::LogLevel::Verbose, "Standard frame (LEN={})", len_std);

    len = len_std;
  }

  if (len == 0) {
    LOG(loggable::LogLevel::Error, "Zero length frame");
    return Status::INVALID_FRAME;
  }

  std::vector<uint8_t> frame_data(len);
  st = txn.read(frame_data);
  if (st != Status::SUCCESS) {
    LOG(loggable::LogLevel::Error, "Failed to read frame data of size {}",
         len);
    return st;
  }

  uint8_t tfi = frame_data[0];
  std::array<uint8_t, 1> dcs;
  st = txn.read(dcs);
  if (st != Status::SUCCESS) {
    LOG(loggable::LogLevel::Error, "Failed to read DCS");
    return st;
  }

  if (len == 1 && frame_data[0] == 0x7F && dcs[0] == 0x81) {
    return Status::ERROR_FRAME;
  }

  if (tfi != PN532_PN532TOHOST) {
    LOG(loggable::LogLevel::Error, "Invalid TFI: {:02x}", tfi);
    return Status::INVALID_FRAME;
  }

  uint8_t sum = 0;
  for (size_t i = 0; i < len; ++i) {
    sum += frame_data[i];
  }
  if (static_cast<uint8_t>(sum + dcs[0]) != 0) {
    LOG(loggable::LogLevel::Error, "Data Checksum Error");
    LOG(loggable::LogLevel::Debug, "Packet Data: {:02X}",
         fmt::join(frame_data, ""));
    return Status::CHECKSUM_ERROR;
  }

  std::array<uint8_t, 1> postamble;
  st = txn.read(postamble);
  if (st != Status::SUCCESS) {
    LOG(loggable::LogLevel::Warning, "Failed to read postamble");
  } else if (postamble[0] != 0x00) {
    LOG(loggable::LogLevel::Warning, "Postamble invalid");
  }

  size_t data_len = len - 1;
  buffer.assign(frame_data.begin() + 1, frame_data.begin() + 1 + data_len);

  return Status::SUCCESS;
}

} // namespace pn532
