#ifndef HTTP_DEMO_H
#define HTTP_DEMO_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif.h"

esp_err_t demo_init(void);
esp_err_t demo_on_network_ready(esp_netif_t* eth_netif);
esp_err_t demo_on_network_down(void);
bool demo_is_available(void);

#endif
