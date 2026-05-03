#pragma once

#include "sdkconfig.h"
#include "soc/gpio_num.h"
#include "pn532_cxx/span.hpp"
#include "pn532_cxx/transport.hpp"
#include <atomic>
#include <cstddef>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

namespace pn532 {

class SpiTransport : public Transport {
public:
  SpiTransport(gpio_num_t irq = (gpio_num_t)CONFIG_PN532_IRQ,
               gpio_num_t miso = (gpio_num_t)CONFIG_PN532_MISO,
               gpio_num_t mosi = (gpio_num_t)CONFIG_PN532_MOSI,
               gpio_num_t sclk = (gpio_num_t)CONFIG_PN532_SCK,
               gpio_num_t cs = (gpio_num_t)CONFIG_PN532_SS);
  ~SpiTransport() override;

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
  static constexpr size_t DMA_BUFFER_SIZE = 265;

  spi_device_handle_t _spi = nullptr;
  gpio_num_t _ss;
  gpio_num_t _irq = GPIO_NUM_NC;

  uint8_t *_dma_buffer = nullptr;

  static void IRAM_ATTR irq_isr_handler(void *arg);

  std::atomic<bool> _pending{false};
  std::atomic<bool> _ready{false};

  bool isRdy();
};

} // namespace pn532
