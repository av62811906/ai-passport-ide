// tools/simulator/host/include/esp_timer.h
// Host-side shim of the one ESP-IDF timer API PokeWalk uses: the monotonic
// microsecond clock. The game reads it for animation pacing, playtime and the
// world scan schedule; the simulator backs it with CLOCK_MONOTONIC.
#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);
