#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "w5500_driver.h"

static const char *TAG = "main";

void
app_main(void) {
  esp_err_t err;

  w5500_driver_config_t w5500_driver_config = {
    .spi_host = SPI3_HOST,
    .pin_mosi = 23,
    .pin_miso = 19,
    .pin_sclk = 18,
    .pin_cs = 21,
    .pin_int = 4,
    .pin_reset = 22,
    .spi_clock_hz = 8 * 1000 * 1000,
  };

  ESP_LOGI(TAG, "Starting W5500 phase-1 bring-up");

  err = w5500_driver_init(&w5500_driver_config);
  ESP_ERROR_CHECK(err);

  err = w5500_driver_hard_reset();
  ESP_ERROR_CHECK(err);

  uint8_t version = 0;
  err = w5500_driver_get_version(&version);
  ESP_ERROR_CHECK(err);
  ESP_LOGI(TAG, "VERSIONR = 0x%02X", version);

  err = w5500_driver_soft_reset();
  ESP_ERROR_CHECK(err);

  version = 0;
  err = w5500_driver_get_version(&version);
  ESP_ERROR_CHECK(err);
  ESP_LOGI(TAG, "VERSIONR after soft reset = 0x%02X", version);

  uint8_t phycfgr = 0;
  err = w5500_driver_get_phycfgr(&phycfgr);
  ESP_ERROR_CHECK(err);
  ESP_LOGI(TAG, "PHYCFGR = 0x%02X", phycfgr);

  if (version != 0x04) {
    ESP_LOGE(TAG, "Unexpected W5500 version: 0x%02X (expected 0x04)", version);
  } else {
    ESP_LOGI(TAG, "W5500 SPI/register path looks healthy");
  }

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
