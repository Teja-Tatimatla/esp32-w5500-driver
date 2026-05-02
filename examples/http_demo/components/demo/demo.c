#include "esp_log.h"

#include "demo.h"
#include "demo_priv.h"

static const char* TAG = "demo";

typedef struct {
  bool initialized;
  bool available;
  esp_netif_t* eth_netif;
} demo_context_t;

static demo_context_t demo_context = {0};

esp_err_t
demo_init(void) {
  if (demo_context.initialized) {
    return ESP_OK;
  }

  esp_err_t err = demo_data_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "http demo data init failed: %s", esp_err_to_name(err));
    return err;
  }

  demo_context.initialized = true;
  demo_context.available = false;
  demo_context.eth_netif = NULL;

  ESP_LOGI(TAG, "http demo initialized");
  return ESP_OK;
}

esp_err_t
demo_on_network_ready(esp_netif_t* eth_netif) {
  if (!demo_context.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (eth_netif == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = demo_data_set_network_ready(eth_netif);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "http demo data update failed: %s", esp_err_to_name(err));
    return err;
  }

  demo_context.eth_netif = eth_netif;
  demo_context.available = true;

  err = demo_http_server_start();
  if (err != ESP_OK) {
    demo_context.available = false;
    demo_context.eth_netif = NULL;
    demo_data_set_network_down();
    ESP_LOGE(TAG, "http demo unavailable - HTTP server start failed");
    return err;
  }

  ESP_LOGI(TAG, "http demo ready");
  return ESP_OK;
}

esp_err_t
demo_on_network_down(void) {
  if (!demo_context.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  demo_context.available = false;
  demo_context.eth_netif = NULL;

  esp_err_t err = demo_http_server_stop();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "http demo server stop failed: %s", esp_err_to_name(err));
    return err;
  }

  err = demo_data_set_network_down();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "http demo data update failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "http demo unavailable - Ethernet interface is down");
  return ESP_OK;
}

bool
demo_is_available(void) {
  return demo_context.initialized && demo_context.available;
}
