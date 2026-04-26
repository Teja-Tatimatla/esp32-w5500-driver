#include "esp_err.h"
#include "w5500_driver_priv.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "w5500_driver";

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
