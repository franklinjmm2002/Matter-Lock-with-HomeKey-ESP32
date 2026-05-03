#pragma once

// UNDER CONSTRUCTION
// NOT READY FOR USE

#include "pn532_cxx/span.hpp"
#include "pn532_cxx/transport.hpp"
#include <driver/gpio.h>
#include <driver/i2c.h>

namespace pn532 {

class I2cTransport : public Transport {
public:
  I2cTransport(i2c_port_t port, gpio_num_t sda, gpio_num_t scl);
  ~I2cTransport() override;

  void swReset() override;
  void abort() override;
  Transaction begin() override;

protected:
  Status writeChunk(span<const uint8_t> data) override;
  bool waitReady(uint32_t timeout_ms) override;
  Status prepareRead() override;
  Status readChunk(span<uint8_t> buffer) override;
  void endTransaction() override;

private:
  i2c_port_t _port;
  uint8_t _address;
};

} // namespace pn532
