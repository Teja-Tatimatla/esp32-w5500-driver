#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "demo_priv.h"

typedef struct {
  bool initialized;
  SemaphoreHandle_t lock;
  demo_data_snapshot_t snapshot;
} demo_data_context_t;

static demo_data_context_t demo_data_context = {0};

static int64_t
demo_data_now_seconds(void) {
  return esp_timer_get_time() / 1000000;
}

static void
demo_data_set_ip_string(char* dest, size_t dest_size, esp_ip4_addr_t* addr) {
  snprintf(dest, dest_size, IPSTR, IP2STR(addr));
}

esp_err_t
demo_data_init(void) {
  if (demo_data_context.initialized) {
    return ESP_OK;
  }

  demo_data_context.lock = xSemaphoreCreateMutex();
  if (demo_data_context.lock == NULL) {
    return ESP_ERR_NO_MEM;
  }

  memset(&demo_data_context.snapshot, 0, sizeof(demo_data_context.snapshot));

  demo_data_context.snapshot.ethernet_up = false;
  snprintf(demo_data_context.snapshot.ip, sizeof(demo_data_context.snapshot.ip), "0.0.0.0");
  snprintf(demo_data_context.snapshot.driver, sizeof(demo_data_context.snapshot.driver), "W5500 in MACRAW mode");
  demo_data_context.snapshot.last_update_seconds = demo_data_now_seconds();

  demo_data_context.initialized = true;
  return ESP_OK;
}

esp_err_t
demo_data_set_network_ready(esp_netif_t* eth_netif) {
  if (!demo_data_context.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (eth_netif == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_netif_ip_info_t ip_info = {0};
  esp_err_t err = esp_netif_get_ip_info(eth_netif, &ip_info);
  if (err != ESP_OK) {
    return err;
  }

  if (xSemaphoreTake(demo_data_context.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  demo_data_context.snapshot.ethernet_up = true;
  demo_data_set_ip_string(demo_data_context.snapshot.ip, sizeof(demo_data_context.snapshot.ip), &ip_info.ip);
  snprintf(demo_data_context.snapshot.driver, sizeof(demo_data_context.snapshot.driver), "W5500 in MACRAW mode");
  demo_data_context.snapshot.last_update_seconds = demo_data_now_seconds();

  xSemaphoreGive(demo_data_context.lock);
  return ESP_OK;
}

esp_err_t
demo_data_set_network_down(void) {
  if (!demo_data_context.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(demo_data_context.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  demo_data_context.snapshot.ethernet_up = false;
  snprintf(demo_data_context.snapshot.ip, sizeof(demo_data_context.snapshot.ip), "0.0.0.0");
  demo_data_context.snapshot.last_update_seconds = demo_data_now_seconds();

  xSemaphoreGive(demo_data_context.lock);
  return ESP_OK;
}

esp_err_t
demo_data_get_snapshot(demo_data_snapshot_t* snapshot) {
  if (!demo_data_context.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (snapshot == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(demo_data_context.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  *snapshot = demo_data_context.snapshot;

  xSemaphoreGive(demo_data_context.lock);
  return ESP_OK;
}

esp_err_t
demo_data_add_bytes(uint64_t bytes) {
  if (!demo_data_context.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(demo_data_context.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  demo_data_context.snapshot.bytes_transferred += bytes;
  demo_data_context.snapshot.last_update_seconds = demo_data_now_seconds();

  xSemaphoreGive(demo_data_context.lock);
  return ESP_OK;
}
