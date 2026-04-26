#include <stdint.h>
#include "freertos/FreeRTOS.h"
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

  /* INT pin: not used yet, but configure as input now */
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
    return ESP_ERR_INVALID_STATE;
  }

  return gpio_get_level((gpio_num_t)w5500_global_context.cfg.pin_int);
}

uint32_t
w5500_driver_get_interrupt_count(void) {
  return w5500_global_context.interrupt_isr_count;
}
