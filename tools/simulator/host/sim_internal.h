// tools/simulator/host/sim_internal.h
// Cross-file declarations inside the simulator library (not exposed to Python).
#pragma once

#include <stddef.h>
#include <stdint.h>

// Create the LVGL display (static framebuffer + flush callback). 0 on success.
int sim_display_init(void);

// Blit a rectangle of big-endian RGB565 (the byte order PokeWalk pushes straight
// to the panel) into the little-endian simulator framebuffer. x/y end
// coordinates are exclusive, matching esp_lcd_panel_draw_bitmap().
void sim_display_blit_be(int x_start, int y_start, int x_end, int y_end,
                         const void *color_data);

// Copy up to `samples` mono samples out of the audio ring buffer, blocking up to
// timeout_ms for enough data. Always returns `samples` samples (zero-filled on
// timeout) so the application's capture task keeps running through transients.
void sim_audio_read(int16_t *dst, size_t samples, int timeout_ms);
