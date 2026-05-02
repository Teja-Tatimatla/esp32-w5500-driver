#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include "demo.h"
#include "w5500_driver.h"

static const char *TAG = "main";

typedef struct {
  esp_netif_t* eth_netif;
} app_context_t;

static void
eth_event_handler(void* args, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  (void)event_base;

  app_context_t* app_context = (app_context_t *)args;
  esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

  switch(event_id) {
    case ETHERNET_EVENT_CONNECTED: {
      uint8_t mac_addr[6] = {0};
      esp_err_t err = esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
      if(err != ESP_OK) {
        return;
      }

      ESP_LOGI(TAG, "Ethernet Link Up");
      ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
               mac_addr[0], mac_addr[1], mac_addr[2],
               mac_addr[3], mac_addr[4], mac_addr[5]);
      break;
    }

    case ETHERNET_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "Ethernet Link Down");

      if (app_context != NULL && app_context->eth_netif != NULL) {
        esp_err_t err = demo_on_network_down();
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "demo_on_network_down failed: %s", esp_err_to_name(err));
        }
      }
      break;

    case ETHERNET_EVENT_START:
      ESP_LOGI(TAG, "Ethernet Started");
      break;

    case ETHERNET_EVENT_STOP:
      ESP_LOGI(TAG, "Ethernet Stopped");

      if (app_context != NULL && app_context->eth_netif != NULL) {
        esp_err_t err = demo_on_network_down();
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "demo_on_network_down failed: %s", esp_err_to_name(err));
        }
      }
      break;

    default:
      break;
  }
}

static void
got_ip_event_handler(void* args, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_id;

  app_context_t* app_context = (app_context_t *)args;

  ip_event_got_ip_t* event = (ip_event_got_ip_t *)event_data;
  const esp_netif_ip_info_t* ip_info = &event->ip_info;

  ESP_LOGI(TAG, "Ethernet Got IP Address");
  ESP_LOGI(TAG, "ETHIP: " IPSTR, IP2STR(&ip_info->ip));
  ESP_LOGI(TAG, "ETHMASK: " IPSTR, IP2STR(&ip_info->netmask));
  ESP_LOGI(TAG, "ETHGW: " IPSTR, IP2STR(&ip_info->gw));

  if (app_context == NULL || app_context->eth_netif == NULL) {
    ESP_LOGE(TAG, "demo network ready skipped: missing app context");
    return;
  }

  esp_err_t err = demo_on_network_ready(app_context->eth_netif);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "demo_on_network_ready failed: %s", esp_err_to_name(err));
  }
}

void
app_main(void) {
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

  ESP_LOGI(TAG, "Starting W5500 Ethernet bring-up");

  ESP_ERROR_CHECK(w5500_driver_init(&w5500_driver_config));
  ESP_ERROR_CHECK(w5500_driver_hard_reset());

  uint8_t version = 0;
  ESP_ERROR_CHECK(w5500_driver_get_version(&version));

  if (version != 0x04) {
    ESP_LOGE(TAG, "Unexpected W5500 version: 0x%02X", version);
    return;
  }

  ESP_ERROR_CHECK(w5500_driver_soft_reset());

  version = 0;
  ESP_ERROR_CHECK(w5500_driver_get_version(&version));

  if (version != 0x04) {
    ESP_LOGE(TAG, "Unexpected W5500 version after reset: 0x%02X", version);
    return;
  }

  ESP_LOGI(TAG, "W5500 detected");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  ESP_ERROR_CHECK(demo_init());
  static app_context_t app_context = {0};

  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, &app_context));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, &app_context));

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  mac_config.rx_task_stack_size = 4096;
  mac_config.rx_task_prio = 15;

  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 0;
  phy_config.reset_gpio_num = w5500_driver_config.pin_reset;

  esp_eth_mac_t* mac = w5500_eth_mac_new(&mac_config);
  esp_eth_phy_t* phy = w5500_eth_phy_new(&phy_config);

  if (mac == NULL || phy == NULL) {
    ESP_LOGE(TAG, "Failed to create MAC/PHY objects");
    return;
  }

  // Install driver
  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = NULL;
  ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));

  uint8_t eth_mac_addr[6] = {0};
  ESP_ERROR_CHECK(esp_read_mac(eth_mac_addr, ESP_MAC_ETH));
  ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac_addr));

  // Create and attach netif
  esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t* eth_netif = esp_netif_new(&netif_config);
  if (eth_netif == NULL) {
    ESP_LOGE(TAG, "esp_netif_new failed");
    return;
  }

  esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
  if (glue == NULL) {
    ESP_LOGE(TAG, "esp_eth_new_netif_glue failed");
    return;
  }
  app_context.eth_netif = eth_netif;

  ESP_ERROR_CHECK(esp_netif_attach(eth_netif, glue));

  // start
  ESP_ERROR_CHECK(esp_eth_start(eth_handle));

  ESP_LOGI(TAG, "Ethernet driver started; waiting for link/DHCP");
}
