// tools/simulator/host/sim_pokewalk.c
// Host glue for device-only PokeWalk modules that are left out of the simulator
// build: the dungeon run store, the serial debug-key injection seed, and the
// Wi-Fi time-sync/provisioning page. Each keeps the module's public contract so
// the rest of the game links and runs unchanged.
#include "wifi_time.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Dungeon run store (dungeon.c's HOST_BUILD path calls these instead of NVS).
// ---------------------------------------------------------------------------
static uint8_t *s_dungeon_run;
static size_t s_dungeon_len;

bool dungeon_host_commit(const void *data, size_t len) {
    if (!data || len == 0) return false;
    uint8_t *copy = malloc(len);
    if (!copy) return false;
    memcpy(copy, data, len);
    free(s_dungeon_run);
    s_dungeon_run = copy;
    s_dungeon_len = len;
    return true;
}

bool dungeon_host_load(void *data, size_t len) {
    if (!data || !s_dungeon_run || s_dungeon_len != len) return false;
    memcpy(data, s_dungeon_run, len);
    return true;
}

// ---------------------------------------------------------------------------
// Debug-key injection. dbg.c (which reads keys from the USB serial console) is
// not compiled for the host, but the battle pages read this seed override.
// ---------------------------------------------------------------------------
uint32_t dbg_battle_seed = 0;

// ---------------------------------------------------------------------------
// Wi-Fi time sync (wifi_time.c). The device runs an HTTP captive portal to take
// network credentials and sync the clock; the desktop has no radio and the game
// only tracks power-on elapsed time, so the page reports OFFLINE.
// ---------------------------------------------------------------------------
void wifi_time_start(void) {}

void wifi_time_poll(void) {}

bool wifi_time_scan_allowed(void) {
    return false;
}

void wifi_time_view(wifi_time_view_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->state = WIFI_TIME_OFFLINE;
}

void wifi_time_setup_request(bool start) {
    (void)start;
}

void wifi_time_retry(void) {}
