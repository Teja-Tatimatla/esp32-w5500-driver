#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"

#include "w5500_driver_priv.h"

static const char *TAG = "w5500_driver";

static esp_err_t w5500_socket0_update_tx_completion(void);
static void w5500_driver_notify_interrupt_task_if_pending(void);

static void IRAM_ATTR
w5500_gpio_isr_handler(void* args) {
  (void)args;

  BaseType_t yield_required = pdFALSE;

  if (w5500_global_context.interrupt_task_handle != NULL) {
    vTaskNotifyGiveFromISR(w5500_global_context.interrupt_task_handle, &yield_required);
  }

  if (yield_required == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

esp_err_t
w5500_driver_init(const w5500_driver_config_t *w5500_driver_config) {
  if(w5500_driver_config == NULL) {
    ESP_LOGE(TAG, "%s", "W5500 driver config is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  /* RESET pin */
  if (w5500_driver_config->pin_reset >= 0) {
    gpio_config_t io_config = {
      .pin_bit_mask = (1ULL << w5500_driver_config->pin_reset),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err =  gpio_config(&io_config);
    if(err != ESP_OK) {
      ESP_LOGE(TAG, "%s", "gpio congif for reset pin failed");
      return err;
    }

    gpio_set_level((gpio_num_t)w5500_driver_config->pin_reset, 1);
  }

  if (w5500_driver_config->pin_int >= 0) {
    gpio_config_t io_config = {
      .pin_bit_mask = (1ULL << w5500_driver_config->pin_int),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err =  gpio_config(&io_config);
    if(err != ESP_OK) {
      ESP_LOGE(TAG, "%s", "gpio congif for interrupt pin failed");
      return err;
    }

    err = gpio_intr_disable((gpio_num_t)w5500_driver_config->pin_int);
    if(err != ESP_OK) {
      ESP_LOGE(TAG, "gpio interrupt disable failed");
      return err;
    }
  }

  esp_err_t err = w5500_spi_init(w5500_driver_config);
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "%s", "SPI initialization for W5500 failed");
    return err;
  }

  ESP_LOGI(TAG, "driver init complete");
  return ESP_OK;
}

esp_err_t
w5500_driver_deinit(void) {
  if(!w5500_global_context.initialized) {
    return ESP_OK;
  }

  if(w5500_global_context.cfg.pin_int >= 0 && w5500_global_context.gpio_handler_installed) {
    gpio_isr_handler_remove(w5500_global_context.cfg.pin_int);
    w5500_global_context.gpio_handler_installed = false;
  }

  w5500_global_context.interrupt_task_handle = NULL;

  return w5500_spi_deinit();
}

esp_err_t
w5500_driver_hard_reset(void) {
  if(!w5500_global_context.initialized) {
    ESP_LOGE(TAG, "%s", "W5500 driver not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if(w5500_global_context.cfg.pin_reset < 0) {
    ESP_LOGE(TAG, "%s", "Rest pin not configured");
    return ESP_ERR_INVALID_STATE;
  }

  gpio_set_level((gpio_num_t)w5500_global_context.cfg.pin_reset, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level((gpio_num_t)w5500_global_context.cfg.pin_reset, 1);
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG, "hardware reset complete");
  return ESP_OK;
}

esp_err_t
w5500_driver_soft_reset(void) {
  esp_err_t err = w5500_soft_reset_lowlevel();
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "software reset complete");
  }
  return err;
}

esp_err_t
w5500_driver_get_version(uint8_t *version) {
  if(version == NULL) {
    ESP_LOGE(TAG, "%s", "Invalid ptr to write version");
    return ESP_ERR_INVALID_ARG;
  }
  return w5500_read_versionr(version);
}

esp_err_t
w5500_driver_get_phycfgr(uint8_t *phycfgr) {
  if(phycfgr == NULL) {
    ESP_LOGE(TAG, "%s", "Invalid prt to write phycfgr");
    return ESP_ERR_INVALID_ARG;
  }
  return w5500_read_phycfgr(phycfgr);
}

esp_err_t
w5500_driver_register_interrupt_task(TaskHandle_t task_handle) {
  if(!w5500_global_context.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if(w5500_global_context.cfg.pin_int < 0) {
    return ESP_ERR_INVALID_STATE;
  }

  if(task_handle == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if(!w5500_global_context.gpio_isr_service_installed) {
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if(err == ESP_ERR_INVALID_STATE) {
      err = ESP_OK;
    }

    if(err != ESP_OK) {
      ESP_LOGE(TAG, "%s", "Failed to install isr service");
      return err;
    }
    w5500_global_context.gpio_isr_service_installed = true;
  }

  if(w5500_global_context.gpio_handler_installed) {
    ESP_ERROR_CHECK(gpio_isr_handler_remove((gpio_num_t)w5500_global_context.cfg.pin_int));
    w5500_global_context.gpio_handler_installed = false;
  }

  esp_err_t err = gpio_set_intr_type((gpio_num_t)w5500_global_context.cfg.pin_int, GPIO_INTR_NEGEDGE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set interrupt type");
    return err;
  }

  w5500_global_context.interrupt_task_handle = task_handle;

  err = gpio_isr_handler_add((gpio_num_t)w5500_global_context.cfg.pin_int, w5500_gpio_isr_handler, NULL);
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "%s", "Failed to add gpio isr handler");
    return err;
  }
  w5500_global_context.gpio_handler_installed = true;

  err = gpio_intr_enable((gpio_num_t)w5500_global_context.cfg.pin_int);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable GPIO interrupt");
    gpio_isr_handler_remove((gpio_num_t)w5500_global_context.cfg.pin_int);
    w5500_global_context.gpio_handler_installed = false;
    w5500_global_context.interrupt_task_handle = NULL;
    return err;
  }

  w5500_driver_notify_interrupt_task_if_pending();

  return ESP_OK;
}

esp_err_t
w5500_driver_unregister_interrupt_task(void) {
  if(!w5500_global_context.initialized || w5500_global_context.cfg.pin_int < 0) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = gpio_intr_disable((gpio_num_t)w5500_global_context.cfg.pin_int);
  if (err != ESP_OK) {
    return err;
  }

  if(w5500_global_context.gpio_handler_installed) {
    ESP_ERROR_CHECK(gpio_isr_handler_remove((gpio_num_t)w5500_global_context.cfg.pin_int));
    w5500_global_context.gpio_handler_installed = false;
  }

  w5500_global_context.interrupt_task_handle = NULL;
  return ESP_OK;
}


esp_err_t
w5500_driver_disable_interrupt_gpio(void) {
  if(!w5500_global_context.initialized || w5500_global_context.cfg.pin_int < 0) {
    return ESP_ERR_INVALID_STATE;
  }

  return gpio_intr_disable((gpio_num_t)w5500_global_context.cfg.pin_int);
}

int
w5500_driver_get_interrupt_level(void) {
  if(!w5500_global_context.initialized || w5500_global_context.cfg.pin_int < 0) {
    return -1;
  }

  return gpio_get_level((gpio_num_t)w5500_global_context.cfg.pin_int);
}


esp_err_t
w5500_interrupts_enable_socket0(void) {
  esp_err_t err;

  err = w5500_write_imr(0x00);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_clear_ir(0xF0);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_clear_sn_ir(0, 0x1F);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_write_sn_imr(0, W5500_SN_IR_RECV);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_reg_write16(W5500_REG_INTLEVEL, W5500_BSB_COMMON, 0xFFFF);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_write_simr(W5500_SIR_S0);
  if (err != ESP_OK) {
    return err;
  }

  w5500_driver_notify_interrupt_task_if_pending();

  return ESP_OK;
}


static esp_err_t
w5500_handle_common_ir(uint8_t ir) {
  if(ir == 0) {
    return ESP_OK;
  }

  return w5500_clear_ir(ir & 0xF0);
}

static esp_err_t
w5500_handle_socket0_ir(uint8_t sn_ir) {
  if (sn_ir == 0) {
    return ESP_OK;
  }

  uint8_t clear_mask = 0;

  if (sn_ir & W5500_SN_IR_RECV) {
    sn_ir &= (uint8_t)~W5500_SN_IR_RECV;
  }

  if (sn_ir & (W5500_SN_IR_SEND_OK | W5500_SN_IR_TIMEOUT)) {
    esp_err_t err = w5500_socket0_update_tx_completion();
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
      return err;
    }

    sn_ir &= (uint8_t)~(W5500_SN_IR_SEND_OK | W5500_SN_IR_TIMEOUT);
  }

  if (sn_ir & W5500_SN_IR_DISCON) {
    ESP_LOGI(TAG, "S0_IR: DISCON");
    clear_mask |= W5500_SN_IR_DISCON;
  }

  if (sn_ir & W5500_SN_IR_CON) {
    ESP_LOGI(TAG, "S0_IR: CON");
    clear_mask |= W5500_SN_IR_CON;
  }

  if (clear_mask != 0) {
    return w5500_clear_sn_ir(0, clear_mask);
  }

  return ESP_OK;
}

esp_err_t
w5500_driver_service_interrupts(void) {
  while(1) {
    int int_level = w5500_driver_get_interrupt_level();
    uint8_t ir = 0;
    uint8_t sir = 0;

    esp_err_t err = w5500_read_ir(&ir);
    if(err != ESP_OK) {
      return err;
    }

    err = w5500_read_sir(&sir);
    if(err != ESP_OK) {
      return err;
    }

    if(ir != 0) {
      err = w5500_handle_common_ir(ir);
      if(err != ESP_OK) {
        return err;
      }
    }

    if(sir & W5500_SIR_S0) {
      uint8_t s0_ir = 0;

      err = w5500_read_sn_ir(0, &s0_ir);
      if(err != ESP_OK) {
        return err;
      }

      if (s0_ir == 0) {
        return ESP_OK;
      }

      err = w5500_handle_socket0_ir(s0_ir);
      if(err != ESP_OK) {
        return err;
      }

      if (s0_ir & W5500_SN_IR_RECV) {
        return ESP_OK;
      }

      continue;
    }

    // INTn is no longer asserted
    if(int_level == 1 && ir == 0 && sir == 0) {
      return ESP_OK;
    }

    // In case there is noise and state is invalid
    if(ir == 0 && sir == 0) {
      return ESP_OK;
    }
  }
}

esp_err_t
w5500_socket0_send_raw_frame(const uint8_t* frame, size_t len) {
  uint8_t sr = 0;
  uint16_t tx_fsr = 0;
  uint16_t tx_wr = 0;
  esp_err_t err;

  if (frame == NULL || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  err = w5500_socket0_update_tx_completion();
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
    return err;
  }

  err = w5500_read_sn_sr(0, &sr);
  if (err != ESP_OK) {
    return err;
  }

  if (sr != W5500_SOCK_MACRAW) {
    ESP_LOGE(TAG, "Socket0 not in MACRAW mode (Sn_SR=0x%02X)", sr);
    return ESP_ERR_INVALID_STATE;
  }

  err = w5500_read_sn_tx_fsr_stable(0, &tx_fsr);
  if (err != ESP_OK) {
    return err;
  }

  if (tx_fsr < len) {
    return ESP_ERR_NO_MEM;
  }

  err = w5500_read_sn_tx_wr(0, &tx_wr);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_write_tx_buffer(0, tx_wr, frame, len);
  if (err != ESP_OK) {
    return err;
  }

  tx_wr = (uint16_t)(tx_wr + len);

  err = w5500_write_sn_tx_wr(0, tx_wr);
  if (err != ESP_OK) {
    return err;
  }

  /*
   * Mark TX as in-flight before SEND.
   * SEND_OK may happen quickly after the command is accepted.
   */
  w5500_global_context.socket0_tx_in_flight = true;

  err = w5500_write_sn_cr(0, W5500_SN_CR_SEND);
  if (err != ESP_OK) {
    w5500_global_context.socket0_tx_in_flight = false;
    return err;
  }

  err = w5500_wait_for_sn_cr_clear(0, pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    w5500_global_context.socket0_tx_in_flight = false;
    return err;
  }

  return ESP_OK;
}

esp_err_t
w5500_socket0_open_macraw(void) {
  esp_err_t err;
  uint8_t sr = 0;

  err = w5500_write_sn_cr(0, W5500_SN_CR_CLOSE);
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_wait_for_sn_cr_clear(0, pdMS_TO_TICKS(100));
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_clear_sn_ir(0, 0x1F);
  if (err != ESP_OK) {
    return err;
  }

  for (uint8_t sock = 0; sock < 8; sock++) {
    uint8_t size_kb = (sock == 0) ? 16 : 0;

    err = w5500_write_sn_rxbuf_size(sock, size_kb);
    if (err != ESP_OK) {
      return err;
    }

    err = w5500_write_sn_txbuf_size(sock, size_kb);
    if (err != ESP_OK) {
      return err;
    }
  }

  err = w5500_write_sn_mr(0, W5500_SN_MR_MACRAW | W5500_SN_MR_MFEN | W5500_SN_MR_MMB);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_write_sn_cr(0, W5500_SN_CR_OPEN);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_wait_for_sn_cr_clear(0, pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_read_sn_sr(0, &sr);
  if (err != ESP_OK) {
    return err;
  }

  if (sr != W5500_SOCK_MACRAW) {
    ESP_LOGE(TAG, "Socket0 failed to enter MACRAW, Sn_SR=0x%02X", sr);
    return ESP_ERR_INVALID_RESPONSE;
  }

  w5500_global_context.socket0_tx_in_flight = false;

  ESP_LOGI(TAG, "Socket opened in MACRAW mode");
  return ESP_OK;
}

static esp_err_t
w5500_socket0_update_tx_completion(void) {
  uint8_t sn_ir = 0;

  esp_err_t err = w5500_read_sn_ir(0, &sn_ir);
  if (err != ESP_OK) {
    return err;
  }

  if (sn_ir & W5500_SN_IR_RECV) {
    w5500_driver_notify_interrupt_task_if_pending();
  }

  if (sn_ir & W5500_SN_IR_SEND_OK) {
    err = w5500_clear_sn_ir(0, W5500_SN_IR_SEND_OK);
    if (err != ESP_OK) {
      return err;
    }

    w5500_global_context.socket0_tx_in_flight = false;
  }

  if (sn_ir & W5500_SN_IR_TIMEOUT) {
    err = w5500_clear_sn_ir(0, W5500_SN_IR_TIMEOUT);
    if (err != ESP_OK) {
      return err;
    }

    w5500_global_context.socket0_tx_in_flight = false;
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}

static void
w5500_driver_notify_interrupt_task_if_pending(void) {
  if (!w5500_global_context.initialized) {
    return;
  }

  if (w5500_global_context.cfg.pin_int < 0) {
    return;
  }

  if (w5500_global_context.interrupt_task_handle == NULL) {
    return;
  }

  if (gpio_get_level((gpio_num_t)w5500_global_context.cfg.pin_int) == 0) {
    xTaskNotifyGive(w5500_global_context.interrupt_task_handle);
  }
}
