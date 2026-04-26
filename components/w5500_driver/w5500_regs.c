#include "esp_err.h"
#include "w5500_driver_priv.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>


esp_err_t
w5500_reg_read8(uint16_t addr, uint8_t block_select, uint8_t *value) {
  return w5500_spi_read(addr, block_select, value, 1);
}

esp_err_t
w5500_reg_write8(uint16_t addr, uint8_t block_select, uint8_t value) {
  return w5500_spi_write(addr, block_select, &value, 1);
}

esp_err_t
w5500_read_versionr(uint8_t *version) {
  return w5500_reg_read8(W5500_REG_VERSIONR, W5500_BSB_COMMON, version);
}

esp_err_t
w5500_read_phycfgr(uint8_t *phycfgr) {
  return w5500_reg_read8(W5500_REG_PHYCFGR, W5500_BSB_COMMON, phycfgr);
}

esp_err_t
w5500_soft_reset_lowlevel(void) {
  esp_err_t err = w5500_reg_write8(W5500_REG_MR, W5500_BSB_COMMON, W5500_MR_RST);
  if (err != ESP_OK) {
    return err;
  }

  /* The reset bit auto-clears; poll until it clears. */
  for (int i = 0; i < 100; i++) {
    uint8_t mr = 0;
    err = w5500_reg_read8(W5500_REG_MR, W5500_BSB_COMMON, &mr);
    if (err != ESP_OK) {
      return err;
    }

    if (mr & W5500_MR_RST) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    return ESP_OK;
  }

  return ESP_ERR_TIMEOUT;
}

esp_err_t
w5500_read_ir(uint8_t* value) {
  return w5500_reg_read8(W5500_REG_IR, W5500_BSB_COMMON, value);
}

esp_err_t
w5500_clear_ir(uint8_t value) {
  return w5500_reg_write8(W5500_REG_IR, W5500_BSB_COMMON, value);
}

esp_err_t
w5500_read_imr(uint8_t* value) {
  return w5500_reg_read8(W5500_REG_IMR, W5500_BSB_COMMON, value);
}

esp_err_t
w5500_write_imr(uint8_t value) {
  return w5500_reg_write8(W5500_REG_IMR, W5500_BSB_COMMON, value);
}

esp_err_t
w5500_read_sir(uint8_t* value) {
  return w5500_reg_read8(W5500_REG_SIR, W5500_BSB_COMMON, value);
}

esp_err_t
w5500_read_simr(uint8_t* value) {
  return w5500_reg_read8(W5500_REG_SIMR, W5500_BSB_COMMON, value);
}

esp_err_t
w5500_write_simr(uint8_t value) {
  return w5500_reg_write8(W5500_REG_SIMR, W5500_BSB_COMMON, value);
}

esp_err_t
w5500_read_sn_ir(uint8_t socket_num, uint8_t* value) {
  return w5500_reg_read8(W5500_SREG_SN_IR, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_clear_sn_ir(uint8_t socket_num, uint8_t value) {
  return w5500_reg_write8(W5500_SREG_SN_IR, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_read_sn_imr(uint8_t socket_num, uint8_t* value) {
  return w5500_reg_read8(W5500_SREG_SN_IMR, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_write_sn_imr(uint8_t socket_num, uint8_t value) {
  return w5500_reg_write8(W5500_SREG_SN_IMR, W5500_BSB_SOCK_REG(socket_num), value);
}
