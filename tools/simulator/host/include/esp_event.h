// tools/simulator/host/include/esp_event.h
// Host-side shim of the default event loop. Only creation is needed: the game's
// Wi-Fi time-sync (excluded from the host build) is the sole consumer.
#pragma once

#include "esp_err.h"

esp_err_t esp_event_loop_create_default(void);
