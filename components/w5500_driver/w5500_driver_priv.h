#ifndef W5500_DRIVER_PRIV_H
#define W5500_DRIVER_PRIV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "w5500_driver.h"

#define W5500_SPI_RWB_READ    0x00
#define W5500_SPI_RWB_WRITE   0x04
#define W5500_SPI_OM_VDM      0x00
#define W5500_BSB_COMMON      0x00
#define W5500_BSB_SOCK_REG(n)   (uint8_t)(0x01 + ((n) * 4))
#define W5500_BSB_SOCK_TX(n)    (uint8_t)(0x02 + ((n) * 4))
#define W5500_BSB_SOCK_RX(n)    (uint8_t)(0x03 + ((n) * 4))
#define W5500_REG_MR         0x0000
#define W5500_REG_PHYCFGR    0x002E
#define W5500_REG_VERSIONR   0x0039
#define W5500_MR_RST         0x80

typedef struct {
  w5500_driver_config_t cfg;
  spi_device_handle_t spi_handle;
  bool initialized;
  bool gpio_isr_service_installed;
  bool gpio_handler_installed;
  TaskHandle_t interrupt_task_handle;
  volatile uint32_t interrupt_isr_count;
} w5500_context_t;

extern w5500_context_t w5500_global_context;

/* SPI layer */
esp_err_t w5500_spi_init(const w5500_driver_config_t* config);
esp_err_t w5500_spi_deinit(void);
esp_err_t w5500_spi_read(uint16_t addr, uint8_t block_select, uint8_t* read_bytes, size_t len);
esp_err_t w5500_spi_write(uint16_t addr, uint8_t block_select, const uint8_t* data, size_t len);

/* Register helpers */
esp_err_t w5500_reg_read8(uint16_t addr, uint8_t block_select, uint8_t *value);
esp_err_t w5500_reg_write8(uint16_t addr, uint8_t block_select, uint8_t value);

esp_err_t w5500_read_versionr(uint8_t* version);
esp_err_t w5500_read_phycfgr(uint8_t* phycfgr);
esp_err_t w5500_soft_reset_lowlevel(void);

#endif
