#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_phy.h"
#include "esp_log.h"

#include "w5500_driver.h"
#include "w5500_driver_priv.h"

static const char *TAG = "w5500_phy";

// W5500 PHYCFGR bits
#define W5500_PHYCFGR_RST         (1u << 7)
#define W5500_PHYCFGR_OPMD        (1u << 6)
#define W5500_PHYCFGR_OPMDC_MASK  (0x7u << 3)
#define W5500_PHYCFGR_DPX         (1u << 2)
#define W5500_PHYCFGR_SPD         (1u << 1)
#define W5500_PHYCFGR_LNK         (1u << 0)

typedef struct {
    esp_eth_phy_t parent;
    esp_eth_mediator_t *mediator;
    eth_phy_config_t config;

    uint32_t addr;
    bool autoneg_enabled;
    bool power_enabled;
    uint32_t pause_ability;

    eth_link_t link;
    eth_speed_t speed;
    eth_duplex_t duplex;
} w5500_eth_phy_t;

static inline
w5500_eth_phy_t* w5500_phy_from_parent(esp_eth_phy_t* phy) {
  return (w5500_eth_phy_t *)phy;
}
static esp_err_t w5500_phy_read_status(w5500_eth_phy_t* ext_phy, eth_link_t* link, eth_speed_t* speed, eth_duplex_t* duplex);


/* ESP-IDF PHY callbacks
 * reference:
 * https://github.com/espressif/esp-idf/blob/master/components/esp_eth/include/esp_eth_phy.h
 * https://github.com/espressif/esp-idf/blob/master/components/esp_eth/src/phy/esp_eth_phy_802_3.c
 */
static esp_err_t w5500_phy_set_mediator(esp_eth_phy_t* phy, esp_eth_mediator_t* eth);
static esp_err_t w5500_phy_reset_hw(esp_eth_phy_t* phy);
static esp_err_t w5500_phy_reset(esp_eth_phy_t* phy);
static esp_err_t w5500_phy_init(esp_eth_phy_t* phy);
static esp_err_t w5500_phy_deinit(esp_eth_phy_t* phy);

static esp_err_t w5500_phy_autonego_ctrl(esp_eth_phy_t* phy, eth_phy_autoneg_cmd_t cmd, bool* autnego_enable_status);

static esp_err_t w5500_phy_get_link(esp_eth_phy_t* phy);
static esp_err_t w5500_phy_set_link(esp_eth_phy_t* phy, eth_link_t link);

static esp_err_t w5500_phy_pwrctl(esp_eth_phy_t* phy, bool enable);
static esp_err_t w5500_phy_set_addr(esp_eth_phy_t* phy, uint32_t addr);
static esp_err_t w5500_phy_get_addr(esp_eth_phy_t* phy, uint32_t* addr);
static esp_err_t w5500_phy_advertise_pause_ability(esp_eth_phy_t* phy, uint32_t ability);
static esp_err_t w5500_phy_loopback(esp_eth_phy_t* phy, bool enable);

static esp_err_t w5500_phy_set_speed(esp_eth_phy_t* phy, eth_speed_t speed);
static esp_err_t w5500_phy_set_duplex(esp_eth_phy_t* phy, eth_duplex_t duplex);

static esp_err_t w5500_phy_custom_ioctl(esp_eth_phy_t* phy, int cmd, void* data);
static esp_err_t w5500_phy_del(esp_eth_phy_t* phy);

esp_eth_phy_t*
w5500_eth_phy_new(const eth_phy_config_t* config) {
  if (config == NULL) {
    return NULL;
  }

  w5500_eth_phy_t* ext_phy = calloc(1, sizeof(w5500_eth_phy_t));
  if (ext_phy == NULL) {
    return NULL;
  }

  ext_phy->config = *config;
  ext_phy->addr = (config->phy_addr < 0) ? 0 : (uint32_t)config->phy_addr;
  ext_phy->autoneg_enabled = true;
  ext_phy->power_enabled = true;
  ext_phy->pause_ability = 0;

  ext_phy->link = ETH_LINK_DOWN;
  ext_phy->speed = ETH_SPEED_10M;
  ext_phy->duplex = ETH_DUPLEX_HALF;

  ext_phy->parent.set_mediator = w5500_phy_set_mediator;
  ext_phy->parent.reset = w5500_phy_reset;
  ext_phy->parent.reset_hw = w5500_phy_reset_hw;
  ext_phy->parent.init = w5500_phy_init;
  ext_phy->parent.deinit = w5500_phy_deinit;

  ext_phy->parent.autonego_ctrl = w5500_phy_autonego_ctrl;
  ext_phy->parent.get_link = w5500_phy_get_link;
  ext_phy->parent.set_link = w5500_phy_set_link;

  ext_phy->parent.pwrctl = w5500_phy_pwrctl;
  ext_phy->parent.set_addr = w5500_phy_set_addr;
  ext_phy->parent.get_addr = w5500_phy_get_addr;
  ext_phy->parent.advertise_pause_ability = w5500_phy_advertise_pause_ability;
  ext_phy->parent.loopback = w5500_phy_loopback;

  ext_phy->parent.set_speed = w5500_phy_set_speed;
  ext_phy->parent.set_duplex = w5500_phy_set_duplex;

  ext_phy->parent.custom_ioctl = w5500_phy_custom_ioctl;
  ext_phy->parent.del = w5500_phy_del;

  return &ext_phy->parent;
}

static esp_err_t
w5500_phy_apply_mode(w5500_eth_phy_t* ext_phy) {
  if (ext_phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t opmdc = 0;

  if (!ext_phy->power_enabled) {
    opmdc = 0x06;
  } else if (ext_phy->autoneg_enabled) {
    opmdc = 0x07;
  } else {
    // Forced mode, auto-negotiation disabled
    if (ext_phy->speed == ETH_SPEED_10M && ext_phy->duplex == ETH_DUPLEX_HALF) {
      opmdc = 0x00;
    } else if (ext_phy->speed == ETH_SPEED_10M && ext_phy->duplex == ETH_DUPLEX_FULL) {
      opmdc = 0x01;
    } else if (ext_phy->speed == ETH_SPEED_100M && ext_phy->duplex == ETH_DUPLEX_HALF) {
      opmdc = 0x02;
    } else {
      // 100 Full
      opmdc = 0x03;
    }
  }

  // Only bits 7:3 are writable
  const uint8_t base_cfg = (uint8_t)(W5500_PHYCFGR_OPMD | (opmdc << 3));

  esp_err_t err = w5500_write_phycfgr((uint8_t)(base_cfg | W5500_PHYCFGR_RST));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "write PHYCFGR prepare failed");
    return err;
  }


  err = w5500_write_phycfgr(base_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "assert PHY reset failed");
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(1));

  err = w5500_write_phycfgr((uint8_t)(base_cfg | W5500_PHYCFGR_RST));
  if (err != ESP_OK) {
      ESP_LOGE(TAG, "release PHY reset failed");
      return err;
  }
  vTaskDelay(pdMS_TO_TICKS(1));

  return ESP_OK;
}

// wiring
static esp_err_t
w5500_phy_set_mediator(esp_eth_phy_t* phy, esp_eth_mediator_t* eth) {
  if (phy == NULL || eth == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  ext_phy->mediator = eth;
  return ESP_OK;
}

static esp_err_t
w5500_phy_reset_hw(esp_eth_phy_t* phy) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);

  if (ext_phy->config.reset_gpio_num < 0) {
    return ESP_OK;
  }

  return w5500_driver_hard_reset();
}

static esp_err_t
w5500_phy_reset(esp_eth_phy_t* phy) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  return w5500_driver_soft_reset();
}

static esp_err_t
w5500_phy_init(esp_eth_phy_t* phy) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);

  ext_phy->power_enabled = true;
  ext_phy->link = ETH_LINK_DOWN;
  ext_phy->speed = ETH_SPEED_10M;
  ext_phy->duplex = ETH_DUPLEX_HALF;
  ext_phy->autoneg_enabled = true;

  return w5500_phy_apply_mode(ext_phy);
}

static esp_err_t
w5500_phy_deinit(esp_eth_phy_t* phy) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

// autonegotiation/link management
static esp_err_t
w5500_phy_autonego_ctrl(esp_eth_phy_t* phy, eth_phy_autoneg_cmd_t cmd, bool* autnego_enable_status) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  esp_err_t err;

  switch (cmd) {
    case ESP_ETH_PHY_AUTONEGO_G_STAT:
      if (autnego_enable_status == NULL) {
        return ESP_ERR_INVALID_ARG;
      }
      *autnego_enable_status = ext_phy->autoneg_enabled;
      return ESP_OK;

    case ESP_ETH_PHY_AUTONEGO_EN:
      ext_phy->autoneg_enabled = true;
      err = w5500_phy_apply_mode(ext_phy);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable autoneg failed");
        return err;
      }

      if (autnego_enable_status) {
        *autnego_enable_status = true;
      }
      return ESP_OK;

    case ESP_ETH_PHY_AUTONEGO_DIS:
      ext_phy->autoneg_enabled = false;
      err = w5500_phy_apply_mode(ext_phy);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "disable autoneg failed");
        return err;
      }

      if (autnego_enable_status) {
        *autnego_enable_status = false;
      }
      return ESP_OK;

    case ESP_ETH_PHY_AUTONEGO_RESTART:
      err = w5500_phy_apply_mode(ext_phy);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "restart autoneg failed");
        return err;
      }

      if (autnego_enable_status) {
        *autnego_enable_status = ext_phy->autoneg_enabled;
      }
      return ESP_OK;

    default:
      return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t
w5500_phy_get_link(esp_eth_phy_t* phy) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);

  if (ext_phy->mediator == NULL) {
    ESP_LOGE(TAG, "mediator not set");
    return ESP_ERR_INVALID_STATE;
  }

  eth_link_t new_link = ETH_LINK_DOWN;
  eth_speed_t new_speed = ETH_SPEED_10M;
  eth_duplex_t new_duplex = ETH_DUPLEX_HALF;

  esp_err_t err = w5500_phy_read_status(ext_phy, &new_link, &new_speed, &new_duplex);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read PHY status failed");
    return err;
  }

  if (new_link == ETH_LINK_UP) {
    if (ext_phy->speed != new_speed) {
      ext_phy->speed = new_speed;
      esp_err_t err = ext_phy->mediator->on_state_changed(ext_phy->mediator, ETH_STATE_SPEED, (void *)(uintptr_t)new_speed);

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "notify speed failed");
        return err;
      }
    }

    if (ext_phy->duplex != new_duplex) {
      ext_phy->duplex = new_duplex;
      esp_err_t err = ext_phy->mediator->on_state_changed(ext_phy->mediator,  ETH_STATE_DUPLEX,  (void *)(uintptr_t)new_duplex);

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "notify duplex failed");
        return err;
      }
    }

    if (ext_phy->link != ETH_LINK_UP) {
      ext_phy->link = ETH_LINK_UP;
      esp_err_t err = ext_phy->mediator->on_state_changed(ext_phy->mediator,  ETH_STATE_LINK,  (void *)(uintptr_t)ETH_LINK_UP);

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "notify link up failed");
        return err;
      }
    }
  } else {
    if (ext_phy->link != ETH_LINK_DOWN) {
      ext_phy->link = ETH_LINK_DOWN;
      esp_err_t err = ext_phy->mediator->on_state_changed(ext_phy->mediator, ETH_STATE_LINK, (void *)(uintptr_t)ETH_LINK_DOWN);

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "notify link down failed");
        return err;
      }
    }

    ext_phy->speed = new_speed;
    ext_phy->duplex = new_duplex;
  }

  return ESP_OK;
}

static esp_err_t
w5500_phy_set_link(esp_eth_phy_t* phy, eth_link_t link) {
  // Link is just status
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  ext_phy->link = link;
  return ESP_OK;
}

// PHY config hooks
static esp_err_t
w5500_phy_pwrctl(esp_eth_phy_t* phy, bool enable) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  ext_phy->power_enabled = enable;
  return w5500_phy_apply_mode(ext_phy);
}

static esp_err_t
w5500_phy_set_addr(esp_eth_phy_t* phy, uint32_t addr) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  ext_phy->addr = addr;
  return ESP_OK;
}

static esp_err_t
w5500_phy_get_addr(esp_eth_phy_t* phy, uint32_t* addr) {
  if (phy == NULL || addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  *addr = ext_phy->addr;
  return ESP_OK;
}

static esp_err_t
w5500_phy_advertise_pause_ability(esp_eth_phy_t* phy, uint32_t ability) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  ext_phy->pause_ability = ability;
  return ESP_OK;
}

static esp_err_t
w5500_phy_loopback(esp_eth_phy_t* phy, bool enable) {
  (void)phy;
  (void)enable;
  return ESP_ERR_NOT_SUPPORTED;
}

// spee/duplex state
static esp_err_t
w5500_phy_set_speed(esp_eth_phy_t* phy, eth_speed_t speed) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  ext_phy->speed = speed;

  if (!ext_phy->autoneg_enabled && ext_phy->power_enabled) {
    return w5500_phy_apply_mode(ext_phy);
  }

  return ESP_OK;
}

static esp_err_t
w5500_phy_set_duplex(esp_eth_phy_t* phy, eth_duplex_t duplex) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  ext_phy->duplex = duplex;

  if (!ext_phy->autoneg_enabled && ext_phy->power_enabled) {
    return w5500_phy_apply_mode(ext_phy);
  }

  return ESP_OK;
}

// optional hooks
static esp_err_t
w5500_phy_custom_ioctl(esp_eth_phy_t* phy, int cmd, void* data) {
  (void)phy;
  (void)cmd;
  (void)data;
  return ESP_ERR_NOT_SUPPORTED;
}

// destructor
static esp_err_t
w5500_phy_del(esp_eth_phy_t* phy) {
  if (phy == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  w5500_eth_phy_t* ext_phy = w5500_phy_from_parent(phy);
  free(ext_phy);
  return ESP_OK;
}

// read PHY from PHYCFGR
static esp_err_t
w5500_phy_read_status(w5500_eth_phy_t* ext_phy, eth_link_t* link, eth_speed_t* speed, eth_duplex_t* duplex) {
  if (ext_phy == NULL || link == NULL || speed == NULL || duplex == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t phycfgr = 0;
  esp_err_t err = w5500_read_phycfgr(&phycfgr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed to read PHYCFGR: %s", esp_err_to_name(err));
    return err;
  }

  *link = (phycfgr & W5500_PHYCFGR_LNK) ? ETH_LINK_UP : ETH_LINK_DOWN;
  *speed = (phycfgr & W5500_PHYCFGR_SPD) ? ETH_SPEED_100M : ETH_SPEED_10M;
  *duplex = (phycfgr & W5500_PHYCFGR_DPX) ? ETH_DUPLEX_FULL : ETH_DUPLEX_HALF;

  return ESP_OK;
}
