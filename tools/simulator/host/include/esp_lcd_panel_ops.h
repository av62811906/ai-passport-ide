// tools/simulator/host/include/esp_lcd_panel_ops.h
// Host-side shim of the panel drawing API. PokeWalk renders its game pages by
// pushing 240x80 bands straight to the panel (bypassing LVGL), so this must land
// in the simulator framebuffer for the mirror to show the game.
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"

// x/y end coordinates are exclusive, matching esp_lcd_panel_draw_bitmap().
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start,
                                    int y_start, int x_end, int y_end,
                                    const void *color_data);
