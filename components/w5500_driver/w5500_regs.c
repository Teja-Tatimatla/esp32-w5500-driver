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
w5500_reg_read16(uint16_t addr, uint8_t block_select, uint16_t* value) {
  uint8_t data[2] = {0};

  if(value == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = w5500_spi_read(addr, block_select, data, sizeof(data));
  if(err != ESP_OK) {
    return err;
  }

  *value = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
  return ESP_OK;
}

esp_err_t
w5500_reg_write8(uint16_t addr, uint8_t block_select, uint8_t value) {
  return w5500_spi_write(addr, block_select, &value, 1);
}

esp_err_t
w5500_reg_write16(uint16_t addr, uint8_t block_select, uint16_t value) {
  uint8_t data[2] = {
    (uint8_t)(value >> 8),
    (uint8_t)(value & 0xFF)
  };

  return w5500_spi_write(addr, block_select, data, sizeof(data));
}

esp_err_t
w5500_read_rx_buffer(uint8_t socket_num, uint16_t offset, uint8_t* buffer, size_t len) {
  return w5500_spi_read(offset, W5500_BSB_SOCK_RX(socket_num), buffer, len);
}

esp_err_t
w5500_write_tx_buffer(uint8_t socket_num, uint16_t offset, const uint8_t* data, size_t len) {
  if(data == NULL && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }

  return w5500_spi_write(offset, W5500_BSB_SOCK_TX(socket_num), data, len);
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

esp_err_t
w5500_read_sn_sr(uint8_t socket_num, uint8_t* value) {
  return w5500_reg_read8(W5500_SREG_SN_SR, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_write_sn_mr(uint8_t socket_num, uint8_t value) {
  return w5500_reg_write8(W5500_SREG_SN_MR, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_write_sn_cr(uint8_t socket_num, uint8_t value) {
  return w5500_reg_write8(W5500_SREG_SN_CR, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_wait_for_sn_cr_clear(uint8_t socket_num, TickType_t timeout_ticks) {
  TickType_t start = xTaskGetTickCount();

  while(1) {
    uint8_t cmd = 0;
    esp_err_t err = w5500_reg_read8(W5500_SREG_SN_CR, W5500_BSB_SOCK_REG(socket_num), &cmd);
    if(err != ESP_OK) {
      return err;
    }

    if(cmd == 0) {
      return ESP_OK;
    }

    if((xTaskGetTickCount() - start) >= timeout_ticks) {
      return ESP_ERR_TIMEOUT;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

esp_err_t
w5500_write_sn_rxbuf_size(uint8_t socket_num, uint8_t value) {
  return w5500_reg_write8(W5500_SREG_SN_RXBUF_SIZE, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_write_sn_txbuf_size(uint8_t socket_num, uint8_t value) {
  return w5500_reg_write8(W5500_SREG_SN_TXBUF_SIZE, W5500_BSB_SOCK_REG(socket_num), value);
}

// Assert stability by reading RSR multiple times
esp_err_t
w5500_read_sn_rx_rsr_stable(uint8_t socket_num, uint16_t* value) {
  if(value == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  for(size_t i = 0; i < 8; i++) {
    uint16_t v1 = 0;
    uint16_t v2 = 0;

    esp_err_t err = w5500_reg_read16(W5500_SREG_SN_RX_RSR, W5500_BSB_SOCK_REG(socket_num), &v1);
    if(err != ESP_OK) {
      return err;
    }

    err = w5500_reg_read16(W5500_SREG_SN_RX_RSR, W5500_BSB_SOCK_REG(socket_num), &v2);
    if(err != ESP_OK) {
      return err;
    }

    if(v1 == v2) {
      *value = v1;
      return ESP_OK;
    }
  }

  return ESP_ERR_TIMEOUT;
}

esp_err_t
w5500_read_sn_tx_fsr_stable(uint8_t socket_num, uint16_t* value) {
  if(value == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  for(size_t i = 0; i < 8; i++) {
    uint16_t v1 = 0;
    uint16_t v2 = 0;

    esp_err_t err = w5500_reg_read16(W5500_SREG_SN_TX_FSR, W5500_BSB_SOCK_REG(socket_num), &v1);
    if(err != ESP_OK) {
      return err;
    }

    err = w5500_reg_read16(W5500_SREG_SN_TX_FSR, W5500_BSB_SOCK_REG(socket_num), &v2);
    if(err != ESP_OK) {
      return err;
    }

    if(v1 == v2) {
      *value = v1;
      return ESP_OK;
    }
  }

  return ESP_ERR_TIMEOUT;
}

esp_err_t
w5500_read_sn_rx_rd(uint8_t socket_num, uint16_t* value) {
  return w5500_reg_read16(W5500_SREG_SN_RX_RD, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_read_sn_tx_wr(uint8_t socket_num, uint16_t* value) {
  return w5500_reg_read16(W5500_SREG_SN_TX_WR, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_write_sn_rx_rd(uint8_t socket_num, uint16_t value) {
  return w5500_reg_write16(W5500_SREG_SN_RX_RD, W5500_BSB_SOCK_REG(socket_num), value);
}

esp_err_t
w5500_write_sn_tx_wr(uint8_t socket_num, uint16_t value) {
  return w5500_reg_write16(W5500_SREG_SN_TX_WR, W5500_BSB_SOCK_REG(socket_num), value);
}
