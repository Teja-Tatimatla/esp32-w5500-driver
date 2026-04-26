#include <string.h>
#include <stdlib.h>

#include "driver/spi_common.h"
#include "esp_err.h"
#include "w5500_driver_priv.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "w5500_spi";

w5500_context_t w5500_global_context = {0};

static inline uint8_t w5500_build_control(uint8_t block_select, bool is_read) {
  return (uint8_t)((block_select << 3) | (is_read ? W5500_SPI_RWB_READ : W5500_SPI_RWB_WRITE) | W5500_SPI_OM_VDM);
}

esp_err_t
w5500_spi_init(const w5500_driver_config_t* w5500_driver_config) {
  if(w5500_driver_config == NULL) {
    ESP_LOGE(TAG, "%s", "Error message");
    return ESP_ERR_INVALID_ARG;
  }

  if(w5500_global_context.initialized) {
    ESP_LOGE(TAG, "%s", "W5500 SPI already initialized");
    return ESP_ERR_INVALID_STATE;
  }

  spi_bus_config_t bus_config = {
    .mosi_io_num = w5500_driver_config->pin_mosi,
    .miso_io_num = w5500_driver_config->pin_miso,
    .sclk_io_num = w5500_driver_config->pin_sclk,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 2048,
  };
  esp_err_t init_bus_err = spi_bus_initialize(w5500_driver_config->spi_host, &bus_config, SPI_DMA_CH_AUTO);
  if(init_bus_err != ESP_OK) {
    ESP_LOGE(TAG, "%s", "SPI bus initialization failed");
    return init_bus_err;
  }

  spi_device_interface_config_t devcfg = {
    .spics_io_num = w5500_driver_config->pin_cs,
    .clock_speed_hz = w5500_driver_config->spi_clock_hz,
    .mode = 0,
    .queue_size = 4,
  };
  esp_err_t add_bus_err = spi_bus_add_device(w5500_driver_config->spi_host, &devcfg, &w5500_global_context.spi_handle);
  if (add_bus_err != ESP_OK) {
    spi_bus_free(w5500_driver_config->spi_host);
    return add_bus_err;
  }

  w5500_global_context.cfg = *w5500_driver_config;
  w5500_global_context.initialized = true;

  ESP_LOGI(TAG, "SPI initialized: host=%d, clk=%d Hz, CS=%d",
            w5500_driver_config->spi_host, w5500_driver_config->spi_clock_hz, w5500_driver_config->pin_cs);

  return ESP_OK;
}

esp_err_t
w5500_spi_deinit(void) {
  if (!w5500_global_context.initialized) {
    return ESP_OK;
  }

  spi_bus_remove_device(w5500_global_context.spi_handle);
  spi_bus_free(w5500_global_context.cfg.spi_host);

  memset(&w5500_global_context, 0, sizeof(w5500_global_context));
  return ESP_OK;
}

esp_err_t
w5500_spi_read(uint16_t addr, uint8_t block_select, uint8_t* read_bytes, size_t len) {
  if(!w5500_global_context.initialized) {
    ESP_LOGE(TAG, "%s", "W5500 driver not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if(read_bytes == NULL) {
    ESP_LOGE(TAG, "%s", "Invalid read_bytes ptr");
    return ESP_ERR_INVALID_ARG;
  }

  const size_t total = 3 + len;
  uint8_t *tx = calloc(total, 1);
  uint8_t *rx = calloc(total, 1);
  if(tx == NULL || rx == NULL) {
    ESP_LOGE(TAG, "%s", "Memory allocation failed");
    free(tx);
    free(rx);
    return ESP_ERR_NO_MEM;
  }

  tx[0] = (uint8_t)(addr >> 8);
  tx[1] = (uint8_t)(addr & 0xFF);
  tx[2] = w5500_build_control(block_select, true);

  spi_transaction_t transaction = {
    .length = total * 8, // length is in bits so * 8
    .tx_buffer = tx,
    .rx_buffer = rx,
  };

  //TODO: Avoid polling transmit for large reads
  esp_err_t err = spi_device_polling_transmit(w5500_global_context.spi_handle, &transaction);
  if (err == ESP_OK) {
    memcpy(read_bytes, &rx[3], len);
  }

  free(tx);
  free(rx);
  return err;
}

esp_err_t
w5500_spi_write(uint16_t addr, uint8_t block_select, const uint8_t *data, size_t len) {
  if(!w5500_global_context.initialized) {
    ESP_LOGE(TAG, "%s", "W5500 driver not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  const size_t total = 3 + len;
  uint8_t *tx = calloc(total, 1);
  if(tx == NULL) {
    ESP_LOGE(TAG, "%s", "Memory allocation failed");
    return ESP_ERR_NO_MEM;
  }

  tx[0] = (uint8_t)(addr >> 8);
  tx[1] = (uint8_t)(addr & 0xFF);
  tx[2] = w5500_build_control(block_select, false);

  if (data && len > 0) {
    memcpy(&tx[3], data, len);
  }

  spi_transaction_t t = {
    .length = total * 8,
    .tx_buffer = tx,
    .rx_buffer = NULL,
  };

  //TODO: Avoid polling transmit for large writes
  esp_err_t err = spi_device_polling_transmit(w5500_global_context.spi_handle, &t);
  free(tx);
  return err;
}
