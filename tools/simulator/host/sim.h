// tools/simulator/host/sim.h
// C ABI exposed to the Python IDE through ctypes.
//
// sim_step() and sim_push_button() are called from the IDE's main thread. When
// audio is enabled the application's capture task runs on its own thread; both
// threads share LVGL through the same recursive lock the application uses
// (bsp_lvgl_lock/unlock), which sim_step() also takes.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One formatted log line (already NUL-terminated, no trailing newline).
typedef void (*sim_log_cb_t)(const char *line, void *user);

// Boot LVGL, create the 240x320 display, then start the tuner app.
// audio_ready != 0 starts the tuner's microphone capture task, which blocks in
// bsp_audio_read() and is fed through sim_audio_push(); pass 0 to run the
// on-device "no signal" path instead. Returns 0 on success.
int sim_init(int audio_ready);

// Feed 16 kHz / 16-bit / mono PCM into the capture task's ring buffer.
// Thread-safe; call it from the host audio callback thread.
void sim_audio_push(const int16_t *samples, size_t count);

// Tear down (currently releases nothing beyond LVGL state). Safe to call once.
void sim_shutdown(void);

// Advance the simulated clock and run LVGL timers. The IDE owns time: LVGL's
// tick is driven by elapsed_ms, so pausing the loop freezes the UI.
void sim_step(uint32_t elapsed_ms);

// Inject a key event. btn/ev use the bsp_button.h encoding:
//   btn: 0=UP, 1=DOWN, 2=OK      ev: 0=PRESS, 1=CLICK, 2=DOUBLE, 3=LONG
void sim_push_button(int btn, int ev);

// Full-screen RGB565 little-endian framebuffer (owned by the simulator).
// Returns NULL before sim_init(). width/height/stride_px are written when non-NULL.
const uint16_t *sim_framebuffer(int *width, int *height, int *stride_px);

// Last value passed to bsp_display_backlight() (0..100).
int sim_backlight_percent(void);

// Register (or clear with NULL) the log sink used by ESP_LOGx.
void sim_set_log_callback(sim_log_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif
