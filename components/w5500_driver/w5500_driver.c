#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "soc/gpio_num.h"
#include "w5500_driver_priv.h"
#include "esp_check.h"
#include "esp_log.h"


static const char *TAG = "w5500_driver";

// static esp_err_t w5500_socket0_drain_rx(void);

static void IRAM_ATTR
w5500_gpio_isr_handler(void* args) {
  (void)args;

  BaseType_t yield_required = pdFALSE;
  w5500_global_context.interrupt_isr_count++;

  if(w5500_global_context.interrupt_task_handle != NULL) {
    vTaskNotifyGiveFromISR(w5500_global_context.interrupt_task_handle, &yield_required);
  }

  if(yield_required == pdTRUE) {
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
  w5500_global_context.interrupt_isr_count = 0;

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

  return ESP_OK;
}

esp_err_t
w5500_driver_unregister_interrupt_task(void) {
  if(!w5500_global_context.initialized || w5500_global_context.cfg.pin_int < 0) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_ERROR_CHECK(gpio_intr_disable((gpio_num_t)w5500_global_context.cfg.pin_int));

  if(w5500_global_context.gpio_handler_installed) {
    ESP_ERROR_CHECK(gpio_isr_handler_remove((gpio_num_t)w5500_global_context.cfg.pin_int));
    w5500_global_context.gpio_handler_installed = false;
  }

  w5500_global_context.interrupt_task_handle = NULL;
  return ESP_OK;
}

esp_err_t
w5500_driver_enable_interrupt_gpio(void) {
  if(!w5500_global_context.initialized || w5500_global_context.cfg.pin_int < 0) {
    return ESP_ERR_INVALID_STATE;
  }

  return gpio_intr_enable((gpio_num_t)w5500_global_context.cfg.pin_int);
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

uint32_t
w5500_driver_get_interrupt_count(void) {
  return w5500_global_context.interrupt_isr_count;
}

esp_err_t
w5500_interrupts_enable_socket0(void) {
  esp_err_t err;

  err = w5500_write_imr(0x00);
  if (err != ESP_OK) {
    return err;
  }

  err = w5500_clear_ir(0xF0);
  if(err != ESP_OK){
    return err;
  }

  err = w5500_clear_sn_ir(0, 0x1F);
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_write_sn_imr(0, W5500_SN_IR_SEND_OK | W5500_SN_IR_TIMEOUT | W5500_SN_IR_RECV);
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_write_simr(W5500_SIR_S0);
  if (err != ESP_OK) {
    return err;
  }

  return ESP_OK;
}


static esp_err_t
w5500_handle_common_ir(uint8_t ir) {
  if(ir == 0) {
    return ESP_OK;
  }

  ESP_LOGW(TAG, "Common IR = 0x%02X", ir);
  return w5500_clear_ir(ir & 0xF0);
}

static esp_err_t
w5500_handle_socket0_ir(uint8_t sn_ir) {
  if(sn_ir == 0) {
    return ESP_OK;
  }

  if(sn_ir & W5500_SN_IR_RECV) {
    ESP_LOGI(TAG, "S0_IR: RECV");

    /*
    esp_err_t err = w5500_socket0_drain_rx();
    if(err != ESP_OK) {
      return err;
    }
    */
  }

  if(sn_ir & W5500_SN_IR_SEND_OK) {
    if(w5500_global_context.socket0_tx_in_flight) {
      w5500_global_context.socket0_tx_in_flight = false;
      w5500_global_context.socket0_tx_complete_count++;
      ESP_LOGI(TAG, "S0_IR: SEND_OK (len=%zu, total=%" PRIu32 ")", (size_t) w5500_global_context.socket0_last_tx_len, w5500_global_context.socket0_tx_complete_count);
      w5500_global_context.socket0_last_tx_len = 0;
    } else {
      ESP_LOGW(TAG, "S0_IR: SEND_OK with no TX in flight");
    }
  }

  if(sn_ir & W5500_SN_IR_DISCON) {
    ESP_LOGI(TAG, "S0_IR: DISCON");
  }

  if(sn_ir & W5500_SN_IR_CON) {
    ESP_LOGI(TAG, "S0_IR: CON");
  }

  if (sn_ir & W5500_SN_IR_TIMEOUT) {
    w5500_global_context.socket0_tx_timeout_count++;
    ESP_LOGW(TAG, "S0_IR: TIMEOUT (total=%" PRIu32 ")",w5500_global_context.socket0_tx_timeout_count);

    w5500_global_context.socket0_tx_in_flight = false;
    w5500_global_context.socket0_last_tx_len = 0;
  }

  return w5500_clear_sn_ir(0, sn_ir & 0x1F);
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

      err = w5500_handle_socket0_ir(s0_ir);
      if(err != ESP_OK) {
        return err;
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

/*
static esp_err_t
w5500_socket0_drain_rx(void) {
  uint8_t sr = 0;
  esp_err_t err = w5500_read_sn_sr(0, &sr);
  if(err != ESP_OK) {
    return err;
  }

  if(sr != W5500_SOCK_MACRAW) {
    ESP_LOGE(TAG, "Socket0 not in MACRAW mode (Sn_SR=0x%02X)", sr);
    return ESP_ERR_INVALID_STATE;
  }

  while(1) {
    uint16_t rx_size = 0;
    uint16_t rx_rd = 0;

    err = w5500_read_sn_rx_rsr_stable(0, &rx_size);
    if(err != ESP_OK) {
      return err;
    }

    if(rx_size == 0) {
      return ESP_OK;
    }

    if(rx_size > sizeof(w5500_global_context.socket0_rx_scratch)) {
      ESP_LOGE(TAG, "RX drain too large: %zu bytes", (size_t)rx_size);
      return ESP_ERR_INVALID_SIZE;
    }

    err = w5500_read_sn_rx_rd(0, &rx_rd);
    if(err != ESP_OK) {
      return err;
    }

    err = w5500_read_rx_buffer(0, rx_rd, w5500_global_context.socket0_rx_scratch, rx_size);
    if(err != ESP_OK) {
      return err;
    }

    rx_rd = (uint16_t)(rx_rd + rx_size);

    err = w5500_write_sn_rx_rd(0, rx_rd);
    if(err != ESP_OK) {
      return err;
    }

    err = w5500_write_sn_cr(0, W5500_SN_CR_RECV);
    if(err != ESP_OK) {
      return err;
    }

    err = w5500_wait_for_sn_cr_clear(0, pdMS_TO_TICKS(100));
    if(err != ESP_OK) {
      return err;
    }

    ESP_LOGI(TAG, "Drained %zu RX bytes from socket0", (size_t)rx_size);
    size_t preview = rx_size < 64 ? rx_size : 64;
    ESP_LOG_BUFFER_HEXDUMP(TAG, w5500_global_context.socket0_rx_scratch, preview, ESP_LOG_INFO);
  }
}
*/

esp_err_t
w5500_socket0_send_raw_frame(const uint8_t* frame, size_t len) {
  uint8_t sr = 0;
  uint16_t tx_fsr = 0;
  uint16_t tx_wr = 0;
  esp_err_t err;

  if(frame == NULL || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  err = w5500_read_sn_sr(0, &sr);
  if(err != ESP_OK) {
    return err;
  }

  if(sr != W5500_SOCK_MACRAW) {
    ESP_LOGE(TAG, "Socket0 not in MACRAW mode (Sn_SR=0x%02X)", sr);
    return ESP_ERR_INVALID_STATE;
  }

  if(w5500_global_context.socket0_tx_in_flight) {
    ESP_LOGW(TAG, "Socket0 TX already in flight");
    return ESP_ERR_INVALID_STATE;
  }

  err = w5500_read_sn_tx_fsr_stable(0, &tx_fsr);
  if(err != ESP_OK) {
    return err;
  }

  if(tx_fsr < len) {
    ESP_LOGW(TAG, "Not enough TX space: need=%zu free%zu", (size_t)len, (size_t)tx_fsr);
    return ESP_ERR_NO_MEM;
  }

  err = w5500_read_sn_tx_wr(0, &tx_wr);
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_write_tx_buffer(0, tx_wr, frame, len);
  if(err != ESP_OK) {
    return err;
  }

  tx_wr = (uint16_t)(tx_wr + len);

  err = w5500_write_sn_tx_wr(0, tx_wr);
  if(err != ESP_OK) {
    return err;
  }

  w5500_global_context.socket0_tx_in_flight = true;
  w5500_global_context.socket0_last_tx_len = (uint16_t)len;

  err = w5500_write_sn_cr(0, W5500_SN_CR_SEND);
  if(err != ESP_OK) {
    w5500_global_context.socket0_tx_in_flight = false;
    w5500_global_context.socket0_last_tx_len = 0;
    return err;
  }

  err = w5500_wait_for_sn_cr_clear(0, pdMS_TO_TICKS(100));
  if(err != ESP_OK) {
    w5500_global_context.socket0_tx_in_flight = false;
    w5500_global_context.socket0_last_tx_len = 0;
    return err;
  }

  ESP_LOGI(TAG, "Queued socket0 TX of %zu bytes", (size_t)len);
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
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_write_sn_rxbuf_size(0, 2);
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_write_sn_txbuf_size(0, 2);
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_write_sn_mr(0, W5500_SN_MR_MACRAW | W5500_SN_MR_MFEN);
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_write_sn_cr(0, W5500_SN_CR_OPEN);
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_wait_for_sn_cr_clear(0, pdMS_TO_TICKS(100));
  if(err != ESP_OK) {
    return err;
  }

  err = w5500_read_sn_sr(0, &sr);
  if(err != ESP_OK) {
    return err;
  }

  if(sr != W5500_SOCK_MACRAW) {
    ESP_LOGE(TAG, "Socket0 failed to enter MACRAW, Sn_SR=0x%02X", sr);
    return ESP_ERR_INVALID_RESPONSE;
  }

  w5500_global_context.socket0_tx_in_flight = false;
  w5500_global_context.socket0_last_tx_len = 0;
  w5500_global_context.socket0_tx_complete_count = 0;
  w5500_global_context.socket0_tx_timeout_count = 0;

  ESP_LOGI(TAG, "Socket opened in MACRAW mode");
  return ESP_OK;
}
