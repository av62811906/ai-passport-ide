// tools/simulator/host/sim_internal.h
// Cross-file declarations inside the simulator library (not exposed to Python).
#pragma once

#include <stddef.h>
#include <stdint.h>

// Create the LVGL display (static framebuffer + flush callback). 0 on success.
int sim_display_init(void);

// Copy up to `samples` mono samples out of the audio ring buffer, blocking up to
// timeout_ms for enough data. Always returns `samples` samples (zero-filled on
// timeout) so the application's capture task keeps running through transients.
void sim_audio_read(int16_t *dst, size_t samples, int timeout_ms);
