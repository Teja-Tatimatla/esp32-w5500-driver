#ifndef HTTP_DEMO_PRIV_H
#define HTTP_DEMO_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

typedef struct {
  bool ethernet_up;
  char ip[16];
  char driver[32];
  int64_t last_update_seconds;
  uint64_t bytes_transferred;
} demo_data_snapshot_t;

esp_err_t demo_http_server_start(void);
esp_err_t demo_http_server_stop(void);

esp_err_t demo_data_init(void);
esp_err_t demo_data_set_network_ready(esp_netif_t* eth_netif);
esp_err_t demo_data_set_network_down(void);
esp_err_t demo_data_get_snapshot(demo_data_snapshot_t* snapshot);
esp_err_t demo_data_add_bytes(uint64_t bytes);

esp_err_t demo_image_client_stream_jpeg(void* req, uint64_t* bytes_streamed);
#endif
