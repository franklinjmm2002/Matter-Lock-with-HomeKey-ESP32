#pragma once

// UNDER CONSTRUCTION
// NOT READY FOR USE

#include "pn532_cxx/span.hpp"
#include "pn532_cxx/transport.hpp"
#include <driver/gpio.h>
#include <driver/uart.h>

namespace pn532 {

class HsuTransport : public Transport {
public:
  HsuTransport(uart_port_t uart_num, gpio_num_t tx, gpio_num_t rx);
  ~HsuTransport() override;

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
  uart_port_t _uart_num;
  uint32_t _timeout_ms = 1000;
};

} // namespace pn532
