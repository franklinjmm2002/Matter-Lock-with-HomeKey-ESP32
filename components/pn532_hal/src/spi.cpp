#include "pn532_hal/spi.hpp"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "fmt/ranges.h"
#include "hal/gpio_types.h"
#include "soc/gpio_num.h"
#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>

static const char *TAG = "PN532::SPI";

#define PN532_SPI_CLOCK_SPEED_HZ (1000 * 1000)
#define PN532_CMD_DATA_WRITE 0x01
#define PN532_CMD_STATUS_READ 0x02
#define PN532_CMD_DATA_READ 0x03

namespace pn532 {

SpiTransport::SpiTransport(gpio_num_t irq, gpio_num_t miso, gpio_num_t mosi,
                           gpio_num_t sck, gpio_num_t ss)
    : _ss(ss), _irq(irq) {
  esp_err_t ret;

  if (!GPIO_IS_VALID_GPIO(miso) ||
      !GPIO_IS_VALID_OUTPUT_GPIO(mosi) ||
      !GPIO_IS_VALID_GPIO(sck) || !GPIO_IS_VALID_OUTPUT_GPIO(ss)) {
    ESP_LOGE(TAG, "Invalid GPIO configuration");
    return;
  }

  gpio_config_t ss_conf = {};
  ss_conf.intr_type = GPIO_INTR_DISABLE;
  ss_conf.mode = GPIO_MODE_OUTPUT;
  ss_conf.pin_bit_mask = (1ULL << ss);
  ss_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  ss_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&ss_conf);
  gpio_set_level(_ss, 1);

  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = mosi;
  buscfg.miso_io_num = miso;
  buscfg.sclk_io_num = sck;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = SPI_MAX_DMA_LEN;

  ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "SPI bus already initialized, reusing");
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
    return;
  }

  spi_device_interface_config_t devcfg = {};
  devcfg.command_bits = 0;
  devcfg.address_bits = 0;
  devcfg.dummy_bits = 0;
  devcfg.mode = 0;
  devcfg.clock_speed_hz = PN532_SPI_CLOCK_SPEED_HZ;
  devcfg.spics_io_num = -1;
  devcfg.flags = SPI_DEVICE_BIT_LSBFIRST;
  devcfg.queue_size = 1;

  ret = spi_bus_add_device(SPI2_HOST, &devcfg, &_spi);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
    return;
  }

  if (_irq != GPIO_NUM_NC) {
    gpio_config_t conf = {};
    conf.pin_bit_mask = BIT(_irq);
    conf.mode = GPIO_MODE_INPUT;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    conf.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&conf);
    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to install ISR service");
    }
    esp_err_t err = gpio_isr_handler_add(_irq, irq_isr_handler, (void *)this);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to add ISR handler");
    }
  }

  _dma_buffer =
      static_cast<uint8_t *>(spi_bus_dma_memory_alloc(SPI2_HOST, DMA_BUFFER_SIZE, MALLOC_CAP_8BIT));
  if (!_dma_buffer) {
    ESP_LOGE(TAG, "Failed to allocate DMA buffer");
  }
}

SpiTransport::~SpiTransport() {
  if (_dma_buffer) {
    heap_caps_free(_dma_buffer);
    _dma_buffer = nullptr;
  }
  if (_spi) {
    spi_bus_remove_device(_spi);
  }
  spi_bus_free(SPI2_HOST);
}

void IRAM_ATTR SpiTransport::irq_isr_handler(void *arg) {
  auto *transport = static_cast<SpiTransport *>(arg);
  if (transport->_pending.load(std::memory_order_acquire)) {
    transport->_ready.store(true, std::memory_order_release);
  }
}

void SpiTransport::swReset() { abort(); }

void SpiTransport::abort() {
  gpio_set_level(_ss, 0);
  vTaskDelay(2 / portTICK_PERIOD_MS);
  static const uint8_t ABORT[7] = {0x01, 0, 0, 0xFF, 0, 0xFF, 0};
  spi_transaction_t t_data = {};
  t_data.length = sizeof(ABORT) * 8;
  t_data.tx_buffer = ABORT;

  spi_device_transmit(_spi, &t_data);
  gpio_set_level(_ss, 1);
  _pending.store(false, std::memory_order_release);
  _ready.store(false, std::memory_order_release);
}

Transaction SpiTransport::begin() {
  _ready.store(false, std::memory_order_release);
  _pending.store(true, std::memory_order_release);

  gpio_set_level(_ss, 0);
  ets_delay_us(500);

  spi_transaction_t t_cmd = {};
  t_cmd.flags = SPI_TRANS_USE_TXDATA;
  t_cmd.length = 8;
  t_cmd.tx_data[0] = PN532_CMD_DATA_WRITE;

  esp_err_t ret = spi_device_transmit(_spi, &t_cmd);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPI begin() cmd failed");
    gpio_set_level(_ss, 1);
    _pending.store(false, std::memory_order_release);
    return Transaction(*this, false);
  }

  return Transaction(*this, true);
}

Status SpiTransport::writeChunk(span<const uint8_t> data) {
  if (!_dma_buffer || data.size() > DMA_BUFFER_SIZE) {
    ESP_LOGE(TAG, "writeChunk: DMA buffer unavailable or data too large");
    return TRANSPORT_ERROR;
  }

  std::copy(data.begin(), data.end(), _dma_buffer);

  spi_transaction_t t_data = {};
  t_data.length = data.size() * 8;
  t_data.tx_buffer = _dma_buffer;

  esp_err_t ret = spi_device_transmit(_spi, &t_data);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPI writeChunk failed");
    return TRANSPORT_ERROR;
  }

  ESP_LOGV(TAG, "writeChunk: len=%d, data=%s", data.size(), fmt::format("{:02X}", fmt::join(data, "")).c_str());
  return SUCCESS;
}

bool SpiTransport::waitReady(uint32_t timeout_ms) {
  _ready.store(false, std::memory_order_release);
  _pending.store(true, std::memory_order_release);

  uint32_t start = xTaskGetTickCount();
  while (!isRdy()) {
    if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout_ms)) {
      ESP_LOGV(TAG, "Timeout waiting for RDY");
      abort();
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

Status SpiTransport::prepareRead() {
  gpio_set_level(_ss, 0);
  ets_delay_us(100);

  spi_transaction_t t_cmd = {};
  t_cmd.flags = SPI_TRANS_USE_TXDATA;
  t_cmd.length = 8;
  t_cmd.tx_data[0] = PN532_CMD_DATA_READ;

  esp_err_t ret = spi_device_transmit(_spi, &t_cmd);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPI prepareRead cmd failed");
    gpio_set_level(_ss, 1);
    return TRANSPORT_ERROR;
  }

  return SUCCESS;
}

Status SpiTransport::readChunk(span<uint8_t> buffer) {
  if (!_dma_buffer || buffer.size() > DMA_BUFFER_SIZE) {
    ESP_LOGE(TAG, "readChunk: DMA buffer unavailable or request too large");
    return TRANSPORT_ERROR;
  }

  spi_transaction_t t_data = {};
  t_data.length = buffer.size() * 8;
  t_data.rxlength = buffer.size() * 8;
  t_data.rx_buffer = _dma_buffer;

  esp_err_t ret = spi_device_transmit(_spi, &t_data);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPI readChunk failed");
    return TRANSPORT_ERROR;
  }

  std::copy_n(_dma_buffer, buffer.size(), buffer.data());

  ESP_LOGV(TAG, "readChunk: len=%d, data=%s", buffer.size(), fmt::format("{:02X}", fmt::join(buffer, "")).c_str());
  return SUCCESS;
}

void SpiTransport::endTransaction() {
  gpio_set_level(_ss, 1);
  _pending.store(false, std::memory_order_release);
}

bool SpiTransport::isRdy() {
  if (_irq != GPIO_NUM_NC) {
    return _ready;
  }
  gpio_set_level(_ss, 0);
  ets_delay_us(100);

  spi_transaction_t t = {};

  t.flags = SPI_TRANS_USE_TXDATA;
  t.length = 8;
  t.tx_data[0] = PN532_CMD_STATUS_READ;
  spi_device_transmit(_spi, &t);

  uint8_t status_byte = 0;
  memset(&t, 0, sizeof(t));
  t.flags = SPI_TRANS_USE_RXDATA;
  t.length = 8;
  t.rxlength = 8;

  spi_device_transmit(_spi, &t);
  status_byte = t.rx_data[0];

  gpio_set_level(_ss, 1);

  return status_byte & 0x01;
}

} // namespace pn532
