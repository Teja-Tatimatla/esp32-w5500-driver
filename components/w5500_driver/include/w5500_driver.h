#ifndef W5500_DRIVER_H
#define W5500_DRIVER_H

#include <stdint.h>
#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
  spi_host_device_t spi_host;
  int pin_mosi;
  int pin_miso;
  int pin_sclk;
  int pin_cs;
  int pin_int;
  int pin_reset;
  int spi_clock_hz;
} w5500_driver_config_t;

esp_err_t w5500_driver_init(const w5500_driver_config_t* config);
esp_err_t w5500_driver_deinit(void);
esp_err_t w5500_driver_hard_reset(void);
esp_err_t w5500_driver_soft_reset(void);
esp_err_t w5500_driver_get_version(uint8_t* version);
esp_err_t w5500_driver_get_phycfgr(uint8_t* phycfgr);

#endif
