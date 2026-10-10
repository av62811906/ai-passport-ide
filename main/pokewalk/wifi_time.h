#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { WIFI_TIME_UNCONFIGURED, WIFI_TIME_CONNECTING, WIFI_TIME_WAITING,
 WIFI_TIME_READY, WIFI_TIME_OFFLINE, WIFI_TIME_ERROR } wifi_time_state_t;
typedef struct {
 wifi_time_state_t state;
 bool setup;
 char ssid[16],password[9]; // Temporary provisioning AP only, never the home password.
 uint8_t recovered;
} wifi_time_view_t;
// Called after the game's STA interface is initialized. Poll runs in world_task.
void wifi_time_start(void);
void wifi_time_poll(void);
bool wifi_time_scan_allowed(void);
void wifi_time_view(wifi_time_view_t *);
void wifi_time_setup_request(bool start);
void wifi_time_retry(void);
