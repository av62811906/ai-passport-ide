// tools/simulator/host/include/nvs.h
// Host-side shim of the ESP-IDF NVS key/value store. The game persists its save
// slot, display/audio settings and dungeon run through it. The simulator keeps
// the data in memory, so it survives page/task restarts but not the IDE process
// exiting (documented desktop degradation).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY = 0,
    NVS_READWRITE,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out);
void nvs_close(nvs_handle_t handle);

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);

// Mirrors the device contract: with out == NULL only the stored length is
// written back through *len.
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out,
                       size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data,
                       size_t len);

esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
