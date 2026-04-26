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

#define W5500_REG_IR          0x0015
#define W5500_REG_IMR         0x0016
#define W5500_REG_SIR         0x0017
#define W5500_REG_SIMR        0x0018

#define W5500_SREG_SN_IR      0x0002
#define W5500_SREG_SN_IMR     0x002C

#define W5500_IR_CONFLICT     (1u << 7)
#define W5500_IR_UNREACH      (1u << 6)
#define W5500_IR_PPPOE        (1u << 5)
#define W5500_IR_MP           (1u << 4)  // Wake on LAN (Magic Packet)

#define W5500_SIR_S0          (1u << 0)

#define W5500_SN_IR_CON       (1u << 0)
#define W5500_SN_IR_DISCON    (1u << 1)
#define W5500_SN_IR_RECV      (1u << 2)
#define W5500_SN_IR_TIMEOUT   (1u << 3)
#define W5500_SN_IR_SEND_OK   (1u << 4)

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

esp_err_t w5500_read_ir(uint8_t* value);
esp_err_t w5500_clear_ir(uint8_t bits);
esp_err_t w5500_read_imr(uint8_t* value);
esp_err_t w5500_write_imr(uint8_t value);

esp_err_t w5500_read_sir(uint8_t* value);
esp_err_t w5500_read_simr(uint8_t* value);
esp_err_t w5500_write_simr(uint8_t value);

esp_err_t w5500_read_sn_ir(uint8_t socket_num, uint8_t* value);
esp_err_t w5500_clear_sn_ir(uint8_t socket_num, uint8_t bits);

esp_err_t w5500_read_sn_imr(uint8_t socket_num, uint8_t* value);
esp_err_t w5500_write_sn_imr(uint8_t socket_num, uint8_t value);

#endif
