// tools/simulator/host/include/nvs_flash.h
// Host-side shim of the NVS flash initialisation. The in-memory store needs no
// mounting, so init is idempotent and always succeeds.
#pragma once

#include "esp_err.h"

esp_err_t nvs_flash_init(void);
