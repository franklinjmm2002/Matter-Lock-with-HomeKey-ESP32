// UNDER CONSTRUCTION
// NOT READY FOR USE

#include "pn532_hal/i2c.hpp"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "PN532::I2C";

namespace pn532 {

I2cTransport::I2cTransport(i2c_port_t port, gpio_num_t sda, gpio_num_t scl)
    : _port(port), _address(0x24)
{
  i2c_config_t conf = {.mode = I2C_MODE_MASTER,
                       .sda_io_num = sda,
                       .scl_io_num = scl,
                       .sda_pullup_en = GPIO_PULLUP_ENABLE,
                       .scl_pullup_en = GPIO_PULLUP_ENABLE,
                       .master =
                           {
                               .clk_speed = 100000,
                           },
                       .clk_flags = 0};

  esp_err_t ret = i2c_param_config(_port, &conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure I2C");
  }

  ret = i2c_driver_install(_port, conf.mode, 0, 0, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install I2C driver");
  }
}

I2cTransport::~I2cTransport() { i2c_driver_delete(_port); }

void I2cTransport::swReset() {
}

void I2cTransport::abort() {
}

Transaction I2cTransport::begin() {
  // I2C transactions are atomic per operation
  return Transaction(*this, true);
}

Status I2cTransport::writeChunk(span<const uint8_t> data) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (_address << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write(cmd, const_cast<uint8_t *>(data.data()), data.size(), true);
  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C Write failed");
    return TRANSPORT_ERROR;
  }

  ESP_LOG_BUFFER_HEX_LEVEL(TAG, data.data(), data.size(), ESP_LOG_VERBOSE);
  return SUCCESS;
}

bool I2cTransport::waitReady(uint32_t timeout_ms) {
  uint32_t start = xTaskGetTickCount();
  uint8_t status = 0;

  while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (_address << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &status, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK && (status & 0x01)) {
      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

Status I2cTransport::prepareRead() {
  // I2C doesn't need special preparation - reads are atomic
  return SUCCESS;
}

Status I2cTransport::readChunk(span<uint8_t> buffer) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (_address << 1) | I2C_MASTER_READ, true);

  uint8_t status;
  i2c_master_read_byte(cmd, &status, I2C_MASTER_ACK);

  if (buffer.size() > 0) {
    if (buffer.size() > 1) {
      i2c_master_read(cmd, buffer.data(), buffer.size() - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, buffer.data() + buffer.size() - 1,
                         I2C_MASTER_LAST_NACK);
  }

  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C Read failed");
    return TRANSPORT_ERROR;
  }

  ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer.data(), buffer.size(), ESP_LOG_VERBOSE);
  return SUCCESS;
}

void I2cTransport::endTransaction() {
  // No cleanup needed for I2C - each operation is atomic
}

} // namespace pn532
