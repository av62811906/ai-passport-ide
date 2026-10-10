// tools/simulator/host/include/esp_netif.h
// Host-side shim of the network interface layer. A desktop has no Wi-Fi, so the
// game's default-STA creation is a no-op; the world task then simply never sees
// access points (see world.c's HOST_BUILD scan paths).
#pragma once

typedef struct esp_netif_t esp_netif_t;

#include "esp_err.h"

esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
