// tools/simulator/host/include/esp_lcd_types.h
// Host-side shim of ESP-IDF <esp_lcd_types.h>. The simulator never touches a
// real panel; only the opaque handle types are needed to compile bsp_display.h.
#pragma once

typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;
typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
