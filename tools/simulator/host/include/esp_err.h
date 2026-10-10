// tools/simulator/host/include/esp_err.h
// Host-side shim of ESP-IDF <esp_err.h> for the desktop simulator.
// Provides only the surface used by the compiled application code and the
// simulator's own bsp/esp shims; it is NOT a full ESP-IDF replacement.
#pragma once

#include "esp_log.h"

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL (-1)

#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NVS_NOT_FOUND 0x110A

const char *esp_err_to_name(esp_err_t error);

// On a device ESP_ERROR_CHECK aborts. On the host it must not: the PokeWalk boot
// path wraps its WiFi bring-up in it, and a desktop has no radio. Log and carry
// on so the game still starts (WiFi then stays unavailable, exactly as it does
// when the on-device radio fails to come up).
#define ESP_ERROR_CHECK(x)                                                     \
    do {                                                                       \
        esp_err_t sim_rc_ = (x);                                               \
        if (sim_rc_ != ESP_OK) {                                               \
            sim_log_emit(SIM_LOG_WARN, "esp_err",                             \
                         "ESP_ERROR_CHECK(%s) -> %s", #x,                      \
                         esp_err_to_name(sim_rc_));                            \
        }                                                                      \
    } while (0)
