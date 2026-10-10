#pragma once

#include <stddef.h>

// Shared inline battery widget for the menu and care identity rows.
// right/y are logical screen coordinates; drawing never polls the hardware.
void battery_ui_draw(int band_y, int right, int y);
void battery_ui_start(void);
void battery_ui_stop(void);

// Right-aligned title above the battery; still visible when battery is hidden.
void battery_ui_playtime_text(char *out, size_t size);
