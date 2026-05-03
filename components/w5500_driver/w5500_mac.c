#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_com.h"
#include "esp_eth_mac.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/eth_types.h"

#include "w5500_driver.h"
#include "w5500_driver_priv.h"

static const char *TAG = "w5500_mac";

static void w5500_mac_rx_task(void *arg);
static esp_err_t w5500_mac_start_rx_task(w5500_eth_mac_t* ext_mac);
static esp_err_t w5500_mac_receive_one_frame(w5500_eth_mac_t* ext_mac, uint8_t* buf, uint32_t* length);

static esp_err_t w5500_mac_drain_rx(w5500_eth_mac_t* ext_mac);
static esp_err_t w5500_mac_clear_recv_event_if_set(void);

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

esp_eth_mac_t*
w5500_eth_mac_new(const eth_mac_config_t* config) {
  if(config == NULL) {
    return NULL;
  }

  w5500_eth_mac_t* ext_mac = calloc(1, sizeof(w5500_eth_mac_t));
  if(ext_mac == NULL) {
    return NULL;
  }

  ext_mac->config = *config;

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

static esp_err_t
w5500_mac_drain_rx(w5500_eth_mac_t* ext_mac) {
  if (ext_mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  while (ext_mac->started) {
    uint16_t rx_rsr = 0;
    esp_err_t err = w5500_read_sn_rx_rsr_stable(0, &rx_rsr);
    if (err != ESP_OK) {
      return err;
    }

    if (rx_rsr == 0) {
      return ESP_OK;
    }

    uint32_t length = W5500_MAC_RX_BUF_SIZE;
    uint8_t* buffer = malloc(length);
    if (buffer == NULL) {
      return ESP_ERR_NO_MEM;
    }

    err = w5500_mac_receive_one_frame(ext_mac, buffer, &length);

    if (err == ESP_OK && length > 0) {
      if (ext_mac->mediator != NULL && ext_mac->mediator->stack_input != NULL) {
        esp_err_t input_err = ext_mac->mediator->stack_input(
            ext_mac->mediator,
            buffer,
            length
        );

        if (input_err != ESP_OK) {
          free(buffer);
        }
      } else {
        free(buffer);
      }

      continue;
    }

    free(buffer);
    return err;
  }

  return ESP_OK;
}

static esp_err_t
w5500_mac_clear_recv_event_if_set(void) {
  uint8_t s0_ir = 0;

  esp_err_t err = w5500_read_sn_ir(0, &s0_ir);
  if (err != ESP_OK) {
    return err;
  }

  if (s0_ir & W5500_SN_IR_RECV) {
    return w5500_clear_sn_ir(0, W5500_SN_IR_RECV);
  }

  return ESP_OK;
}

static esp_err_t
w5500_mac_start_rx_task(w5500_eth_mac_t* ext_mac) {
  if (ext_mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (ext_mac->mediator == NULL) {
    ESP_LOGE(TAG, "mediator is not set");
    return ESP_ERR_INVALID_STATE;
  }

  if (ext_mac->rx_task_handle != NULL) {
    ext_mac->started = true;
    return ESP_OK;
  }

  ext_mac->started = true;

  BaseType_t ok = xTaskCreate(w5500_mac_rx_task, "w5500_mac_rx", ext_mac->config.rx_task_stack_size, ext_mac,
                              ext_mac->config.rx_task_prio, &ext_mac->rx_task_handle);

  if (ok != pdPASS) {
    ext_mac->started = false;
    ext_mac->rx_task_handle = NULL;
    ESP_LOGE(TAG, "RX task create failed");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

// wiring
static esp_err_t
w5500_mac_set_mediator(esp_eth_mac_t* mac, esp_eth_mediator_t* eth) {
  if (mac == NULL || eth == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  ext_mac->mediator = eth;

  return ESP_OK;
}

static esp_err_t
w5500_mac_init(esp_eth_mac_t* mac) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);

  if (ext_mac->mediator && ext_mac->mediator->on_state_changed) {
    ext_mac->mediator->on_state_changed(ext_mac->mediator, ETH_STATE_LLINIT, NULL);
  }

  esp_err_t err = w5500_write_shar(ext_mac->mac_addr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "write SHAR failed");
    return err;
  }

  return w5500_mac_start_rx_task(ext_mac);
}

static esp_err_t
w5500_mac_start(esp_eth_mac_t* mac) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);

  return w5500_mac_start_rx_task(ext_mac);
}

static esp_err_t
w5500_mac_stop(esp_eth_mac_t* mac) {
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

static esp_err_t
w5500_mac_deinit(esp_eth_mac_t* mac) {
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
static esp_err_t
w5500_mac_transmit(esp_eth_mac_t* mac, uint8_t* buf, uint32_t length) {
  if (mac == NULL || buf == NULL || length == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  return w5500_socket0_send_raw_frame(buf, length);
}

static esp_err_t
w5500_mac_transmit_ctrl_vargs(esp_eth_mac_t* mac, void* ctrl, uint32_t argc, va_list args) {
  (void)mac;
  (void)ctrl;
  (void)argc;
  (void)args;
  return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t
w5500_mac_receive(esp_eth_mac_t* mac, uint8_t* buf, uint32_t* length) {
  if (mac == NULL) {
      return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  return w5500_mac_receive_one_frame(ext_mac, buf, length);
}

// PHY hooks
static esp_err_t
w5500_mac_read_phy_reg(esp_eth_mac_t* mac, uint32_t phy_addr, uint32_t phy_reg, uint32_t* reg_value) {
  (void)mac;
  (void)phy_addr;
  (void)phy_reg;
  (void)reg_value;
  return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t
w5500_mac_write_phy_reg(esp_eth_mac_t* mac, uint32_t phy_addr, uint32_t phy_reg, uint32_t reg_value) {
  (void)mac;
  (void)phy_addr;
  (void)phy_reg;
  (void)reg_value;
  return ESP_ERR_NOT_SUPPORTED;
}

// address/filer config
static esp_err_t
w5500_mac_set_addr(esp_eth_mac_t* mac, uint8_t* addr) {
  if (mac == NULL || addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);
  memcpy(ext_mac->mac_addr, addr, 6);

  return w5500_write_shar(ext_mac->mac_addr);
}

static esp_err_t
w5500_mac_get_addr(esp_eth_mac_t* mac, uint8_t* addr) {
  if (mac == NULL || addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_mac_t* ext_mac = w5500_mac_from_parent(mac);

  esp_err_t err = w5500_read_shar(addr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read SHAR failed");
    return err;
  }

  memcpy(ext_mac->mac_addr, addr, 6);

  return ESP_OK;
}

static esp_err_t
w5500_mac_add_mac_filter(esp_eth_mac_t* mac, uint8_t* addr) {
  if (mac == NULL || addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  /*
   * Keeping this minimal
   * accept the request so esp_netif/lwIP can proceed.
   * We are not programming any real hardware multicast/unicast filter here yet.
   */
  return ESP_OK;
}

static esp_err_t
w5500_mac_rm_mac_filter(esp_eth_mac_t* mac, uint8_t* addr) {
  if (mac == NULL || addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  /*
   * Keeping this minimal
   * acknowledge the request and move on
   */
  return ESP_OK;
}

// link/speed/duplex state
static esp_err_t
w5500_mac_set_speed(esp_eth_mac_t* mac, eth_speed_t speed) {
  (void)speed;
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

static esp_err_t
w5500_mac_set_duplex(esp_eth_mac_t* mac, eth_duplex_t duplex) {
  (void)duplex;
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

static esp_err_t
w5500_mac_set_link(esp_eth_mac_t* mac, eth_link_t link) {
  (void)link;
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

// Optional hooks
static esp_err_t
w5500_mac_set_promiscuous(esp_eth_mac_t* mac, bool enable) {
  (void)mac;
  (void)enable;
  return ESP_OK;
}

static esp_err_t
w5500_mac_set_all_multicast(esp_eth_mac_t* mac, bool enable) {
  (void)mac;
  (void)enable;
  return ESP_OK;
}

static esp_err_t
w5500_mac_enable_flow_ctrl(esp_eth_mac_t* mac, bool enable) {
  (void)mac;
  (void)enable;
  return ESP_OK;
}

static esp_err_t
w5500_mac_set_peer_pause_ability(esp_eth_mac_t* mac, uint32_t ability) {
  (void)mac;
  (void)ability;
  return ESP_OK;
}

static esp_err_t
w5500_mac_custom_ioctl(esp_eth_mac_t* mac, int cmd, void* data) {
  (void)mac;
  (void)cmd;
  (void)data;
  return ESP_ERR_NOT_SUPPORTED;
}

// Destructor
static esp_err_t
w5500_mac_del(esp_eth_mac_t* mac) {
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
  TaskHandle_t curr = xTaskGetCurrentTaskHandle();

  if (w5500_driver_register_interrupt_task(curr) != ESP_OK) {
    ESP_LOGE(TAG, "failed to register interrupt task");
    ext_mac->rx_task_handle = NULL;
    ext_mac->started = false;
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

    while (ext_mac->started) {
      esp_err_t err = w5500_driver_service_interrupts();
      if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        break;
      }

      err = w5500_mac_clear_recv_event_if_set();
      if (err != ESP_OK) {
        break;
      }

      err = w5500_mac_drain_rx(ext_mac);
      if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        break;
      }

      if (w5500_driver_get_interrupt_level() != 0) {
        break;
      }
    }
  }

  w5500_driver_unregister_interrupt_task();
  ext_mac->rx_task_handle = NULL;
  vTaskDelete(NULL);
}


// Helper receive exactly one frame
static esp_err_t
w5500_mac_receive_one_frame(w5500_eth_mac_t* ext_mac, uint8_t* buf, uint32_t* length) {
  uint8_t sr = 0;
  uint16_t remain_bytes = 0;
  uint16_t rx_rd = 0;
  uint8_t header[2] = {0};

  if (ext_mac == NULL || buf == NULL || length == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = w5500_read_sn_sr(0, &sr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read Sn_SR failed");
    return err;
  }

  if (sr != W5500_SOCK_MACRAW) {
    return ESP_ERR_INVALID_STATE;
  }

  err = w5500_read_sn_rx_rsr_stable(0, &remain_bytes);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read Sn_RX_RSR failed");
    return err;
  }

  if (remain_bytes == 0) {
    *length = 0;
    return ESP_ERR_NOT_FOUND;
  }

  if (remain_bytes < sizeof(header)) {
    *length = 0;
    return ESP_ERR_INVALID_RESPONSE;
  }

  err = w5500_read_sn_rx_rd(0, &rx_rd);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read Sn_RX_RD failed");
    return err;
  }

  err = w5500_read_rx_buffer(0, rx_rd, header, sizeof(header));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read MACRAW header failed");
    return err;
  }

  /*
   * For W5500 MACRAW receive, read the 2-byte length header first.
   * The length value is the Ethernet frame length that follows the header.
   * W5500 MACRAW RX layout: [2-byte PACKET-INFO length][Ethernet frame bytes]
   */
  uint16_t record_len = ((uint16_t)header[0] << 8) | header[1];

  if (record_len < sizeof(header)) {
    ESP_LOGE(TAG, "invalid MACRAW record length: %u", record_len);
    *length = 0;
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (record_len > remain_bytes) {
    ESP_LOGE(TAG, "incomplete MACRAW record: record_len=%u remain_bytes=%u rx_rd=0x%04X", record_len, remain_bytes, rx_rd);
    *length = 0;
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint16_t frame_len = (uint16_t)(record_len - sizeof(header));

  if (frame_len < 60 || frame_len > W5500_MAC_RX_BUF_SIZE) {
    ESP_LOGE(TAG,"invalid Ethernet frame length: %u record_len=%u remain_bytes=%u rx_rd=0x%04X", frame_len, record_len, remain_bytes, rx_rd);
    *length = 0;
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (frame_len > *length) {
    *length = frame_len;
    return ESP_ERR_INVALID_SIZE;
  }

  err = w5500_read_rx_buffer(0, (uint16_t)(rx_rd + sizeof(header)), buf, frame_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read RX payload failed");
    *length = 0;
    return err;
  }

  rx_rd = (uint16_t)(rx_rd + record_len);

  err = w5500_write_sn_rx_rd(0, rx_rd);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "write Sn_RX_RD failed");
    *length = 0;
    return err;
  }

  err = w5500_write_sn_cr(0, W5500_SN_CR_RECV);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "write RECV failed");
    *length = 0;
    return err;
  }

  err = w5500_wait_for_sn_cr_clear(0, pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "wait RECV failed");
    *length = 0;
    return err;
  }

  *length = frame_len;
  return ESP_OK;
}
