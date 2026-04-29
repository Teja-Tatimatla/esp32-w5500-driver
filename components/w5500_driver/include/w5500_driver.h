#ifndef W5500_DRIVER_H
#define W5500_DRIVER_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"

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

esp_err_t w5500_driver_register_interrupt_task(TaskHandle_t task_handle);
esp_err_t w5500_driver_unregister_interrupt_task(void);
esp_err_t w5500_driver_disable_interrupt_gpio(void);
int w5500_driver_get_interrupt_level(void);

esp_err_t w5500_interrupts_enable_socket0(void);

esp_eth_mac_t* w5500_eth_mac_new(const eth_mac_config_t* config);
esp_eth_phy_t* w5500_eth_phy_new(const eth_phy_config_t* config);
#endif
