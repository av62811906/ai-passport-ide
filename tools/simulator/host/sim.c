// tools/simulator/host/sim.c
// Simulator entry points: boot LVGL, start the tuner app, drive its clock, and
// forward virtual key events. See sim.h for the ABI contract.
#include "sim.h"
#include "sim_internal.h"

#include "app_tuner.h"
#include "bsp_display.h"
#include "lvgl.h"

static int s_initialized;

int sim_init(int audio_ready) {
    if (s_initialized) return 0;

    lv_init();
    if (sim_display_init() != 0) return -1;

    // audio_ready != 0 starts the tuner's capture task (fed by sim_audio_push);
    // otherwise the tuner runs its on-device "no signal" path.
    app_tuner_start(audio_ready != 0);

    s_initialized = 1;
    return 0;
}

void sim_shutdown(void) {
    if (!s_initialized) return;
    // Intentionally do NOT call lv_deinit(): when audio is enabled the capture
    // task keeps running on its own thread and may touch LVGL concurrently. The
    // IDE stops the microphone first and then exits the process, which reclaims
    // everything; tearing LVGL down here would race that thread and crash.
    s_initialized = 0;
}

void sim_step(uint32_t elapsed_ms) {
    if (!s_initialized) return;
    // The IDE owns time: advancing the tick here makes the UI pause/freeze when
    // the refresh loop stops. The capture task touches LVGL on its own thread,
    // so the handler runs under the same recursive lock the app uses.
    if (!bsp_lvgl_lock(1000)) return;
    lv_tick_inc(elapsed_ms);
    lv_timer_handler();
    bsp_lvgl_unlock();
}

void sim_push_button(int btn, int ev) {
    if (!s_initialized) return;
    app_tuner_handle_key((bsp_btn_t)btn, (bsp_btn_ev_t)ev);
}
