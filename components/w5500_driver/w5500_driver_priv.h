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
#define W5500_SREG_SN_MR          0x0000
#define W5500_SREG_SN_CR          0x0001 // command register
#define W5500_SREG_SN_SR          0x0003 // status register
#define W5500_SREG_SN_RXBUF_SIZE  0x001E
#define W5500_SREG_SN_TXBUF_SIZE  0x001F
#define W5500_SREG_SN_RX_RSR      0x0026 // received size register
#define W5500_SREG_SN_RX_RD       0x0028 // RX read pointer
#define W5500_SREG_SN_TX_FSR      0x0020 // TX free size register
#define W5500_SREG_SN_TX_WR       0x0024 // tx write pointer

#define W5500_SN_CR_OPEN          0x01 // command register open value
#define W5500_SN_CR_CLOSE         0x10 // command register close value
#define W5500_SN_CR_RECV          0x40 // command register receive value
#define W5500_SN_CR_SEND          0x20 // command register send value

#define W5500_IR_CONFLICT     (1u << 7)
#define W5500_IR_UNREACH      (1u << 6)
#define W5500_IR_PPPOE        (1u << 5)
#define W5500_IR_MP           (1u << 4)  // wake on LAN (Magic Packet)

#define W5500_SIR_S0          (1u << 0)
#define W5500_SOCK_MACRAW         0x42 // status SN_SR value for macraw

#define W5500_SN_IR_CON       (1u << 0)
#define W5500_SN_IR_DISCON    (1u << 1)
#define W5500_SN_IR_RECV      (1u << 2)
#define W5500_SN_IR_TIMEOUT   (1u << 3)
#define W5500_SN_IR_SEND_OK   (1u << 4)
#define W5500_SN_MR_MFEN          0x80
/*
 * mode register MAC filter enable
 * When enabled, W5500 only puts packets that are meant for it
 * When disabled, W5500 operates in promiscuous mode. It puts all the
 * packets in the local network on the RX buffer.
 */
#define W5500_SN_MR_MACRAW        0x04 // Value for setting MACRAW in SN_MR

#define W5500_SOCKET0_RX_SCRATCH_SIZE 2048

typedef struct {
  w5500_driver_config_t cfg;
  spi_device_handle_t spi_handle;
  bool initialized;
  bool gpio_isr_service_installed;
  bool gpio_handler_installed;
  TaskHandle_t interrupt_task_handle;
  volatile uint32_t interrupt_isr_count;
  uint8_t socket0_rx_scratch[W5500_SOCKET0_RX_SCRATCH_SIZE];
  bool socket0_tx_in_flight;
  uint16_t socket0_last_tx_len;
  uint32_t socket0_tx_complete_count;
  uint32_t socket0_tx_timeout_count;
} w5500_context_t;

extern w5500_context_t w5500_global_context;

/* SPI layer */
esp_err_t w5500_spi_init(const w5500_driver_config_t* config);
esp_err_t w5500_spi_deinit(void);
esp_err_t w5500_spi_read(uint16_t addr, uint8_t block_select, uint8_t* read_bytes, size_t len);
esp_err_t w5500_spi_write(uint16_t addr, uint8_t block_select, const uint8_t* data, size_t len);

// ISR
esp_err_t w5500_driver_service_interrupts(void);

// MACRAW
esp_err_t w5500_socket0_open_macraw(void);
esp_err_t w5500_socket0_send_raw_frame(const uint8_t* frame, size_t len);
esp_eth_mac_t* w5500_eth_mac_new(const eth_mac_config_t* config);

/* Register helpers */
esp_err_t w5500_reg_read8(uint16_t addr, uint8_t block_select, uint8_t *value);
esp_err_t w5500_reg_read16(uint16_t addr, uint8_t block_select, uint16_t* value);
esp_err_t w5500_reg_write8(uint16_t addr, uint8_t block_select, uint8_t value);
esp_err_t w5500_reg_write16(uint16_t addr, uint8_t block_select, uint16_t value);

esp_err_t w5500_read_rx_buffer(uint8_t socket_num, uint16_t offset, uint8_t* buffer, size_t len);
esp_err_t w5500_write_tx_buffer(uint8_t socket_num, uint16_t offset, const uint8_t* data, size_t len);

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

esp_err_t w5500_read_sn_sr(uint8_t socket_num, uint8_t* value);
esp_err_t w5500_write_sn_mr(uint8_t socket_num, uint8_t value);
esp_err_t w5500_write_sn_cr(uint8_t socket_num, uint8_t value);
esp_err_t w5500_wait_for_sn_cr_clear(uint8_t socket_num, TickType_t timeout_ticks);
esp_err_t w5500_write_sn_rxbuf_size(uint8_t socket_num, uint8_t value);
esp_err_t w5500_write_sn_txbuf_size(uint8_t socket_num, uint8_t value);
esp_err_t w5500_read_sn_rx_rsr_stable(uint8_t socket_num, uint16_t* value);
esp_err_t w5500_read_sn_tx_fsr_stable(uint8_t socket_num, uint16_t* value);
esp_err_t w5500_read_sn_rx_rd(uint8_t socket_num, uint16_t* value);
esp_err_t w5500_read_sn_tx_wr(uint8_t socket_num, uint16_t* value);
esp_err_t w5500_write_sn_rx_rd(uint8_t socket_num, uint16_t value);
esp_err_t w5500_write_sn_tx_wr(uint8_t socket_num, uint16_t value);

#endif
