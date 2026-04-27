#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_com.h"
#include "esp_eth_mac.h"
#include "esp_log.h"

#include "freertos/idf_additions.h"
#include "hal/eth_types.h"
#include "w5500_driver.h"
#include "w5500_driver_priv.h"

static const char *TAG = "w5500_mac";

// Ethernet frame is 1518 bytes
#define W5500_MAC_RX_BUF_SIZE 1525

typedef struct {
  esp_eth_mac_t parent;
  esp_eth_mediator_t* mediator;
  eth_mac_config_t config;

  TaskHandle_t rx_task_handle;
  bool started;

  eth_link_t link;
  eth_speed_t speed;
  eth_duplex_t duplex;

  uint8_t mac_addr[6];
} w5500_eth_mac_t;

static void w5500_mac_rx_task(void *arg);
static esp_err_t w5500_mac_receive_one_frame(w5500_eth_mac_t* ext_mac, uint8_t* buf, uint32_t* length);

/* ESP-IDF MAC callbacks
 * reference:
 * https://github.com/espressif/esp-idf/blob/master/components/esp_eth/include/esp_eth_mac.h
 * https://github.com/espressif/esp-idf/blob/master/components/esp_eth/src/mac/esp_eth_mac_esp.c
 */
static esp_err_t w5500_mac_set_mediator(esp_eth_mac_t* mac, esp_eth_mediator_t* eth);
static esp_err_t w5500_mac_init(esp_eth_mac_t* mac);
static esp_err_t w5500_mac_start(esp_eth_mac_t* mac);
static esp_err_t w5500_mac_stop(esp_eth_mac_t* mac);
static esp_err_t w5500_mac_deinit(esp_eth_mac_t* mac);

static esp_err_t w5500_mac_transmit(esp_eth_mac_t* mac, uint8_t* buf, uint32_t length);
static esp_err_t w5500_mac_transmit_ctrl_vargs(esp_eth_mac_t* mac, void* ctrl, uint32_t argc, va_list args);
static esp_err_t w5500_mac_receive(esp_eth_mac_t* mac, uint8_t* buf, uint32_t* length);

static esp_err_t w5500_mac_read_phy_reg(esp_eth_mac_t* mac, uint32_t phy_addr, uint32_t phy_reg, uint32_t* reg_value);
static esp_err_t w5500_mac_write_phy_reg(esp_eth_mac_t* mac, uint32_t phy_addr, uint32_t phy_reg, uint32_t reg_value);

static esp_err_t w5500_mac_set_addr(esp_eth_mac_t* mac, uint8_t* addr);
static esp_err_t w5500_mac_get_addr(esp_eth_mac_t* mac, uint8_t* addr);
static esp_err_t w5500_mac_add_mac_filter(esp_eth_mac_t* mac, uint8_t* addr);
static esp_err_t w5500_mac_rm_mac_filter(esp_eth_mac_t* mac, uint8_t* addr);

static esp_err_t w5500_mac_set_speed(esp_eth_mac_t* mac, eth_speed_t speed);
static esp_err_t w5500_mac_set_duplex(esp_eth_mac_t* mac, eth_duplex_t duplex);
static esp_err_t w5500_mac_set_link(esp_eth_mac_t* mac, eth_link_t link);

static esp_err_t w5500_mac_set_promiscuous(esp_eth_mac_t* mac, bool enable);
static esp_err_t w5500_mac_set_all_multicast(esp_eth_mac_t* mac, bool enable);
static esp_err_t w5500_mac_enable_flow_ctrl(esp_eth_mac_t* mac, bool enable);
static esp_err_t w5500_mac_set_peer_pause_ability(esp_eth_mac_t* mac, uint32_t ability);
static esp_err_t w5500_mac_custom_ioctl(esp_eth_mac_t* mac, int cmd, void* data);

static esp_err_t w5500_mac_del(esp_eth_mac_t* mac);

static inline
w5500_eth_mac_t* w5500_mac_from_parent(esp_eth_mac_t* mac) {
  return (w5500_eth_mac_t *)mac;
}

esp_eth_mac_t* w5500_eth_mac_new(const eth_mac_config_t* config) {
  if(config == NULL) {
    return NULL;
  }

  w5500_eth_mac_t* ext_mac = calloc(1, sizeof(w5500_eth_mac_t));
  if(ext_mac == NULL) {
    return NULL;
  }

  ext_mac->config = *config;
  ext_mac->link = ETH_LINK_DOWN;
  ext_mac->speed = ETH_SPEED_10M;
  ext_mac->duplex = ETH_DUPLEX_HALF;

  // Temperory locally administered address
  ext_mac->mac_addr[0] = 0x02;
  ext_mac->mac_addr[1] = 0x00;
  ext_mac->mac_addr[2] = 0x00;
  ext_mac->mac_addr[3] = 0x00;
  ext_mac->mac_addr[4] = 0x00;
  ext_mac->mac_addr[5] = 0x01;

  ext_mac->parent.set_mediator = w5500_mac_set_mediator;
  ext_mac->parent.init = w5500_mac_init;
  ext_mac->parent.start = w5500_mac_start;
  ext_mac->parent.stop = w5500_mac_stop;
  ext_mac->parent.deinit = w5500_mac_deinit;

  ext_mac->parent.transmit = w5500_mac_transmit;
  ext_mac->parent.transmit_ctrl_vargs = w5500_mac_transmit_ctrl_vargs;
  ext_mac->parent.receive = w5500_mac_receive;

  ext_mac->parent.read_phy_reg = w5500_mac_read_phy_reg;
  ext_mac->parent.write_phy_reg = w5500_mac_write_phy_reg;

  ext_mac->parent.set_addr = w5500_mac_set_addr;
  ext_mac->parent.get_addr = w5500_mac_get_addr;
  ext_mac->parent.add_mac_filter = w5500_mac_add_mac_filter;
  ext_mac->parent.rm_mac_filter = w5500_mac_rm_mac_filter;

  ext_mac->parent.set_speed = w5500_mac_set_speed;
  ext_mac->parent.set_duplex = w5500_mac_set_duplex;
  ext_mac->parent.set_link = w5500_mac_set_link;

  ext_mac->parent.set_promiscuous = w5500_mac_set_promiscuous;
  ext_mac->parent.set_all_multicast = w5500_mac_set_all_multicast;
  ext_mac->parent.enable_flow_ctrl = w5500_mac_enable_flow_ctrl;
  ext_mac->parent.set_peer_pause_ability = w5500_mac_set_peer_pause_ability;
  ext_mac->parent.custom_ioctl = w5500_mac_custom_ioctl;

  ext_mac->parent.del = w5500_mac_del;

  return &ext_mac->parent;
}

// wiring
static esp_err_t w5500_mac_set_mediator(esp_eth_mac_t* mac, esp_eth_mediator_t* eth) {
  if (mac == NULL || eth == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  ext_mac->mediator = eth;

  return ESP_OK;
}

static esp_err_t w5500_mac_init(esp_eth_mac_t* mac) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);

  if (ext_mac->mediator && ext_mac->mediator->on_state_changed) {
    ext_mac->mediator->on_state_changed(ext_mac->mediator, ETH_STATE_LLINIT, NULL);
  }

  return ESP_OK;
}

static esp_err_t w5500_mac_start(esp_eth_mac_t* mac) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);

  if (ext_mac->started) {
    return ESP_OK;
  }

  if(ext_mac->mediator == NULL) {
    ESP_LOGE(TAG, "mediator is not set");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = w5500_socket0_open_macraw();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed to open socket0 MACRAW (0x%x)", err);
    return err;
  }

  err = w5500_interrupts_enable_socket0();
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "failed to enable socket0 interrupts");
    return err;
  }

  ext_mac->started = true;

  BaseType_t ok = xTaskCreate(w5500_mac_rx_task, "w5500_mac_rx", ext_mac->config.rx_task_stack_size, ext_mac,
                              ext_mac->config.rx_task_prio, &ext_mac->rx_task_handle);

  if (ok != pdPASS) {
    ext_mac->started = false;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

static esp_err_t w5500_mac_stop(esp_eth_mac_t* mac) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);

  if (!ext_mac->started) {
    return ESP_OK;
  }

  ext_mac->started = false;
  w5500_driver_disable_interrupt_gpio();

  if (ext_mac->rx_task_handle) {
    xTaskNotifyGive(ext_mac->rx_task_handle);
  }

  return ESP_OK;
}

static esp_err_t w5500_mac_deinit(esp_eth_mac_t* mac) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);

  if (ext_mac->mediator && ext_mac->mediator->on_state_changed) {
    ext_mac->mediator->on_state_changed(ext_mac->mediator, ETH_STATE_DEINIT, NULL);
  }

  return ESP_OK;
}

// TX/Rx functions
static esp_err_t w5500_mac_transmit(esp_eth_mac_t* mac, uint8_t* buf, uint32_t length) {
  if (mac == NULL || buf == NULL || length == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  return w5500_socket0_send_raw_frame(buf, length);
}

static esp_err_t w5500_mac_transmit_ctrl_vargs(esp_eth_mac_t* mac, void* ctrl, uint32_t argc, va_list args) {
  (void)mac;
  (void)ctrl;
  (void)argc;
  (void)args;
  return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t w5500_mac_receive(esp_eth_mac_t* mac, uint8_t* buf, uint32_t* length) {
  if (mac == NULL) {
      return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  return w5500_mac_receive_one_frame(ext_mac, buf, length);
}

// PHY hooks
static esp_err_t w5500_mac_read_phy_reg(esp_eth_mac_t* mac, uint32_t phy_addr, uint32_t phy_reg, uint32_t* reg_value) {
  (void)mac;
  (void)phy_addr;
  (void)phy_reg;
  (void)reg_value;
  return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t w5500_mac_write_phy_reg(esp_eth_mac_t* mac, uint32_t phy_addr, uint32_t phy_reg, uint32_t reg_value) {
  (void)mac;
  (void)phy_addr;
  (void)phy_reg;
  (void)reg_value;
  return ESP_ERR_NOT_SUPPORTED;
}

// address/filer config
static esp_err_t w5500_mac_set_addr(esp_eth_mac_t* mac, uint8_t* addr) {
  if (mac == NULL || addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  memcpy(ext_mac->mac_addr, addr, 6);
  return ESP_OK;
}

static esp_err_t w5500_mac_get_addr(esp_eth_mac_t* mac, uint8_t* addr) {
  if (mac == NULL || addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  memcpy(addr, ext_mac->mac_addr, 6);
  return ESP_OK;
}

static esp_err_t w5500_mac_add_mac_filter(esp_eth_mac_t* mac, uint8_t* addr) {
  (void)mac;
  (void)addr;
  return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t w5500_mac_rm_mac_filter(esp_eth_mac_t* mac, uint8_t* addr) {
  (void)mac;
  (void)addr;
  return ESP_ERR_NOT_SUPPORTED;
}

// link/speed/duplex state
static esp_err_t w5500_mac_set_speed(esp_eth_mac_t* mac, eth_speed_t speed) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  ext_mac->speed = speed;
  return ESP_OK;
}

static esp_err_t w5500_mac_set_duplex(esp_eth_mac_t* mac, eth_duplex_t duplex) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  ext_mac->duplex = duplex;
  return ESP_OK;
}

static esp_err_t w5500_mac_set_link(esp_eth_mac_t* mac, eth_link_t link) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  ext_mac->link = link;
  return ESP_OK;
}

// Optional hooks
static esp_err_t w5500_mac_set_promiscuous(esp_eth_mac_t* mac, bool enable) {
  (void)mac;
  (void)enable;
  return ESP_OK;
}

static esp_err_t w5500_mac_set_all_multicast(esp_eth_mac_t* mac, bool enable) {
  (void)mac;
  (void)enable;
  return ESP_OK;
}

static esp_err_t w5500_mac_enable_flow_ctrl(esp_eth_mac_t* mac, bool enable) {
  (void)mac;
  (void)enable;
  return ESP_OK;
}

static esp_err_t w5500_mac_set_peer_pause_ability(esp_eth_mac_t* mac, uint32_t ability) {
  (void)mac;
  (void)ability;
  return ESP_OK;
}

static esp_err_t w5500_mac_custom_ioctl(esp_eth_mac_t* mac, int cmd, void* data) {
  (void)mac;
  (void)cmd;
  (void)data;
  return ESP_ERR_NOT_SUPPORTED;
}

// Destructor
static esp_err_t w5500_mac_del(esp_eth_mac_t* mac) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);

  if (ext_mac->started) {
    w5500_mac_stop(mac);
  }

  free(ext_mac);
  return ESP_OK;
}

// RX Task
static void
w5500_mac_rx_task(void* args) {
  w5500_eth_mac_t* ext_mac = (w5500_eth_mac_t *)args;

  if (w5500_driver_register_interrupt_task(xTaskGetCurrentTaskHandle()) != ESP_OK) {
    ESP_LOGE(TAG, "failed to register interrupt task");
    ext_mac->rx_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  while (ext_mac->started) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    /*
     * We nee this because, shutdown logic from stop() may have woken this task
     * not a real interrupt
     */
    if (!ext_mac->started) {
      break;
    }

    esp_err_t err = w5500_driver_service_interrupts();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "interrupt service failed: %s", esp_err_to_name(err));
      continue;
    }

    // Drain all RX frames
    while (ext_mac->started) {
      uint32_t length = W5500_MAC_RX_BUF_SIZE;
      uint8_t* buffer = malloc(length);
      if (buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate RX buffer");
        break;
      }

      err = w5500_mac_receive_one_frame(ext_mac, buffer, &length);
      if (err == ESP_OK) {
        // hand ethernet frame to esp32's network stack
        if (ext_mac->mediator && ext_mac->mediator->stack_input) {
          err = ext_mac->mediator->stack_input(ext_mac->mediator, buffer, length);
          if (err != ESP_OK) {
            ESP_LOGE(TAG, "stack_input failed: %s", esp_err_to_name(err));
            free(buffer);
          }
        } else {
          free(buffer);
        }
        continue;
      }

      free(buffer);

      if (err == ESP_ERR_NOT_FOUND) {
        break; /* no more RX data */
      }

      if (err == ESP_ERR_INVALID_SIZE) {
        ESP_LOGW(TAG, "RX frame larger than temporary buffer");
        break;
      }

      ESP_LOGE(TAG, "receive failed: %s", esp_err_to_name(err));
      break;
    }
  }

  w5500_driver_unregister_interrupt_task();
  ext_mac->rx_task_handle = NULL;
  vTaskDelete(NULL);
}

// Helper receive exactly one frame
static esp_err_t w5500_mac_receive_one_frame(w5500_eth_mac_t* ext_mac, uint8_t* buf, uint32_t* length) {
  uint8_t sr = 0;
  uint16_t rx_size = 0;
  uint16_t rx_rd = 0;

  if (ext_mac == NULL || buf == NULL || length == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = w5500_read_sn_sr(0, &sr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read Sn_SR failed (0x%x)", err);
    return err;
  }

  if (sr != W5500_SOCK_MACRAW) {
    return ESP_ERR_INVALID_STATE;
  }

  err = w5500_read_sn_rx_rsr_stable(0, &rx_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read Sn_RX_RSR failed (0x%x)", err);
    return err;
  }


  if (rx_size == 0) {
    return ESP_ERR_NOT_FOUND;
  }

  if (rx_size > *length) {
    *length = rx_size;
    return ESP_ERR_INVALID_SIZE;
  }

  err = w5500_read_sn_rx_rd(0, &rx_rd);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read Sn_RX_RD failed (0x%x)", err);
    return err;
  }

  err = w5500_read_rx_buffer(0, rx_rd, buf, rx_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read RX buffer failed (0x%x)", err);
    return err;
  }

  rx_rd = (uint16_t)(rx_rd + rx_size);

  err = w5500_write_sn_rx_rd(0, rx_rd);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "write Sn_RX_RD failed (0x%x)", err);
    return err;
  }

  err = w5500_write_sn_cr(0, W5500_SN_CR_RECV);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "write RECV command failed (0x%x)", err);
    return err;
  }

  err = w5500_wait_for_sn_cr_clear(0, pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "wait RECV clear failed (0x%x)", err);
    return err;
  }

  *length = rx_size;
  return ESP_OK;
}
