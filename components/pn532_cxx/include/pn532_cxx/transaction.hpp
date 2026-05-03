#pragma once

#include <cstdint>
#include "span.hpp"

namespace pn532 {

enum Status {
  SUCCESS = 0,
  TIMEOUT = 1,
  INVALID_FRAME = 2,
  INVALID_TFI = 3,
  NO_SPACE = 4,
  ERROR_FRAME = 5,
  CHECKSUM_ERROR = 6,
  TRANSPORT_ERROR = 7,
  INVALID_TARGET = 8,
  NO_TAGS_FOUND = 9,
};

class Transport;

/**
 * @brief RAII guard for transport transactions.
 *
 * Manages the full command-response cycle:
 * 1. begin() - starts transaction (CS low for SPI)
 * 2. write() - sends command data
 * 3. waitForAck() - waits for and validates ACK frame
 * 4. waitForResponse() - waits for response data to be ready
 * 5. read() - reads response data incrementally
 * 6. ~Transaction() - ends transaction (CS high for SPI)
 */
class Transaction {
public:
  Transaction() = default;
  Transaction(Transport &transport, bool valid);
  Transaction(const Transaction &) = delete;
  Transaction &operator=(const Transaction &) = delete;
  Transaction(Transaction &&other) noexcept;
  Transaction &operator=(Transaction &&other) noexcept;
  ~Transaction();

  /**
   * @brief Write data within the transaction.
   * @param data Data to send
   * @return Status of the write operation
   */
  Status write(span<const uint8_t> data);

  /**
   * @brief Wait for and validate ACK frame.
   * @param timeout_ms Maximum time to wait for ACK
   * @return Status of the ACK read
   */
  Status waitForAck(uint32_t timeout_ms = 100);

  /**
   * @brief Wait for response data to be ready.
   * @param timeout_ms Maximum time to wait
   * @return Status (SUCCESS if ready, TIMEOUT otherwise)
   */
  Status waitForResponse(uint32_t timeout_ms = 1000);

  /**
   * @brief Read a chunk of data from the transport.
   * @param buffer Span to fill with data
   * @return Status of the read operation
   */
  Status read(span<uint8_t> buffer);

  /**
   * @brief Check if the transaction is valid.
   */
  explicit operator bool() const { return _valid; }

private:
  Transport *_transport = nullptr;
  bool _valid = false;
  bool _in_read_mode = false;
};

} // namespace pn532
