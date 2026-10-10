// tools/simulator/host/include/esp_wifi.h
// Host-side shim of the ESP-IDF Wi-Fi API used by PokeWalk's world task. Bring-up
// succeeds (so the boot path is exercised unchanged) but scanning returns no
// access points, which the sensing layer already treats as "no data" — the same
// state as standing outside Wi-Fi coverage.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

typedef int wifi_mode_t;
#define WIFI_MODE_NULL 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3

// Security modes as reported in a scan record. world.c maps them to the NDJSON
// "auth" field; the values only need to be distinct, as they are on a device.
typedef int wifi_auth_mode_t;
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WEP 1
#define WIFI_AUTH_WPA_PSK 2
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_AUTH_WPA_WPA2_PSK 4
#define WIFI_AUTH_WPA3_PSK 5
#define WIFI_AUTH_WPA2_WPA3_PSK 6
#define WIFI_AUTH_WPA2_ENTERPRISE 7
#define WIFI_AUTH_WPA3_ENTERPRISE 8
#define WIFI_AUTH_WAPI_PSK 9

typedef struct {
    int dummy;
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){ .dummy = 0 })

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[32];
    uint8_t primary;
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;

typedef struct {
    int dummy;
} wifi_scan_config_t;

esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number,
                                       wifi_ap_record_t *ap_records);
