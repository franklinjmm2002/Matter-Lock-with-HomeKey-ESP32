// UNDER CONSTRUCTION
// NOT READY FOR USE

#include "pn532_hal/hsu.hpp"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

static const char *TAG = "PN532::HSU";

namespace pn532 {

HsuTransport::HsuTransport(uart_port_t uart_num, gpio_num_t tx, gpio_num_t rx)
    : _uart_num(uart_num) {
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t ret = uart_param_config(_uart_num, &uart_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure UART");
  }

  ret = uart_set_pin(_uart_num, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set UART pins");
  }

  ret = uart_driver_install(_uart_num, 1024, 0, 0, nullptr, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install UART driver");
  }
}

HsuTransport::~HsuTransport() { uart_driver_delete(_uart_num); }

void HsuTransport::swReset() {
  std::vector<uint8_t> wakeup{0x55, 0x55, 0x00, 0x00, 0x00};
  uart_write_bytes(_uart_num, wakeup.data(), wakeup.size());
}

void HsuTransport::abort() { uart_flush_input(_uart_num); }

Transaction HsuTransport::begin() {
  // HSU is stream-based, no special begin needed
  return Transaction(*this, true);
}

Status HsuTransport::writeChunk(span<const uint8_t> data) {
  int written = uart_write_bytes(_uart_num, data.data(), data.size());
  if (written < 0) {
    ESP_LOGE(TAG, "UART write failed");
    return TRANSPORT_ERROR;
  }

  ESP_LOG_BUFFER_HEX_LEVEL(TAG, data.data(), data.size(), ESP_LOG_VERBOSE);
  return SUCCESS;
}

bool HsuTransport::waitReady(uint32_t timeout_ms) {
  _timeout_ms = timeout_ms;
  vTaskDelay(pdMS_TO_TICKS(5));
  return true;
}

Status HsuTransport::prepareRead() {
  return SUCCESS;
}

Status HsuTransport::readChunk(span<uint8_t> buffer) {
  int length = uart_read_bytes(_uart_num, buffer.data(), buffer.size(),
                               pdMS_TO_TICKS(_timeout_ms));
  if (length < 0) {
    ESP_LOGE(TAG, "UART Read failed");
    return TRANSPORT_ERROR;
  }
  if (static_cast<size_t>(length) < buffer.size()) {
    ESP_LOGD(TAG, "UART Read short: got %d, expected %zu", length,
             buffer.size());
    return TIMEOUT;
  }

  ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer.data(), buffer.size(), ESP_LOG_VERBOSE);
  return SUCCESS;
}

void HsuTransport::endTransaction() {
  // No cleanup needed for HSU
}

} // namespace pn532
