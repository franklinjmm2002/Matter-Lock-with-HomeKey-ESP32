#pragma once

#include "transaction.hpp"
#include <cstdint>
#include "pn532_cxx/span.hpp"

namespace pn532 {

/**
 * @brief Abstract base class for PN532 communication protocols
 */
class Transport {
public:
  virtual ~Transport() = default;

  /**
   * @brief Software reset the PN532
   */
  virtual void swReset() = 0;

  /**
   * @brief Abort current operation
   */
  virtual void abort() = 0;

  /**
   * @brief Begin a transaction.
   *
   * Starts the transport-level transaction (e.g., asserts CS for SPI).
   * The returned Transaction guard manages the full lifecycle.
   *
   * @return Transaction guard (check with operator bool())
   */
  virtual Transaction begin() = 0;

protected:
  friend class Transaction;

  /**
   * @brief Write data during an active transaction.
   * @param data Data to send
   */
  virtual Status writeChunk(span<const uint8_t> data) = 0;

  /**
   * @brief Wait for data ready within transaction.
   * @param timeout_ms Maximum time to wait
   * @return true if ready, false on timeout
   */
  virtual bool waitReady(uint32_t timeout_ms) = 0;

  /**
   * @brief Prepare for reading (e.g., send DATA_READ command for SPI).
   */
  virtual Status prepareRead() = 0;

  /**
   * @brief Read a chunk of data during an active transaction.
   * @param buffer Span to fill with data
   */
  virtual Status readChunk(span<uint8_t> buffer) = 0;

  /**
   * @brief End the current transaction.
   *
   * Called automatically by Transaction destructor.
   */
  virtual void endTransaction() = 0;
};

} // namespace pn532
