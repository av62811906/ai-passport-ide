// tools/simulator/host/include/esp_err.h
// Host-side shim of ESP-IDF <esp_err.h> for the desktop simulator.
// Provides only the surface used by the compiled application code and the
// simulator's own bsp/esp shims; it is NOT a full ESP-IDF replacement.
#pragma once

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

const char *esp_err_to_name(esp_err_t error);
