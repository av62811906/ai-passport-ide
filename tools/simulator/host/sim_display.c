// tools/simulator/host/sim_display.c
// LVGL display driver for the desktop simulator: renders into a static
// full-screen RGB565 framebuffer and applies the same rounded-corner mask the
// device uses, so the IDE mirrors the on-device look.
#include "sim.h"
#include "sim_internal.h"

#include "bsp_display.h"            // BSP_LVGL_SCREEN_RADIUS
#include "bsp_display_rounding.h"   // bsp_display_rounded_row_span
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#include <string.h>

// Screen geometry mirrors bsp_pins.h (BSP_LCD_W / BSP_LCD_H). Kept local so the
// simulator does not pull ESP-IDF driver headers just to read the pin table.
#define SIM_LCD_W 240
#define SIM_LCD_H 320
#define SIM_DRAW_BUFFER_LINES 40

static uint16_t s_fb[SIM_LCD_W * SIM_LCD_H];
static uint8_t s_draw_buf[SIM_LCD_W * SIM_DRAW_BUFFER_LINES * 2];
static lv_display_t *s_disp;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)px_map;
    lv_draw_buf_t *draw_buf = lv_display_get_buf_active(disp);
    const int32_t width = lv_area_get_width(area);

    if (draw_buf && draw_buf->data && width > 0) {
        const uint32_t stride = draw_buf->header.stride;
        for (int32_t y = area->y1; y <= area->y2; ++y) {
            const uint8_t *src = draw_buf->data + (size_t)(y - area->y1) * stride;
            memcpy(&s_fb[(size_t)y * SIM_LCD_W + area->x1], src, (size_t)width * 2);
        }
    }

    // Clear pixels outside the rounded visible span, matching the device's
    // rounded_flush_event() in components/bsp/src/bsp_display_lvgl.c.
    for (int32_t y = area->y1; y <= area->y2; ++y) {
        uint16_t *row = &s_fb[(size_t)y * SIM_LCD_W];
        int32_t x1, x2;
        if (!bsp_display_rounded_row_span(y, SIM_LCD_W, SIM_LCD_H,
                                          BSP_LVGL_SCREEN_RADIUS, &x1, &x2)) {
            memset(row, 0, (size_t)SIM_LCD_W * sizeof(uint16_t));
            continue;
        }
        for (int32_t x = 0; x < SIM_LCD_W; ++x) {
            if (x < x1 || x > x2) row[x] = 0;
        }
    }

    lv_display_flush_ready(disp);
}

int sim_display_init(void) {
    s_disp = lv_display_create(SIM_LCD_W, SIM_LCD_H);
    if (!s_disp) return -1;

    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_draw_buf, NULL, sizeof(s_draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);
    return 0;
}

const uint16_t *sim_framebuffer(int *width, int *height, int *stride_px) {
    if (width) *width = SIM_LCD_W;
    if (height) *height = SIM_LCD_H;
    if (stride_px) *stride_px = SIM_LCD_W;
    return s_fb;
}

// ---------------------------------------------------------------------------
// Direct panel path (PokeWalk game pages)
// ---------------------------------------------------------------------------
// The game owns the panel while it is in the foreground: it pauses the LVGL
// refresh timer and pushes 240x80 bands itself. Its band buffer is already
// byte-swapped to the ST7789's big-endian RGB565, so the bytes read back as the
// little-endian RGB565 the mirror expects only after swapping them again.
void sim_display_blit_be(int x_start, int y_start, int x_end, int y_end,
                         const void *color_data) {
    if (!color_data) return;
    if (x_start < 0) x_start = 0;
    if (y_start < 0) y_start = 0;
    if (x_end > SIM_LCD_W) x_end = SIM_LCD_W;
    if (y_end > SIM_LCD_H) y_end = SIM_LCD_H;
    const int width = x_end - x_start;
    if (width <= 0 || y_end <= y_start) return;

    const uint8_t *src = (const uint8_t *)color_data;
    for (int y = y_start; y < y_end; ++y) {
        uint16_t *row = &s_fb[(size_t)y * SIM_LCD_W + x_start];
        const uint8_t *s = src + (size_t)(y - y_start) * (size_t)width * 2;
        for (int x = 0; x < width; ++x) {
            row[x] = (uint16_t)((s[x * 2] << 8) | s[x * 2 + 1]);
        }
    }
}

// The game asks for the panel/io handles so it can draw without LVGL. On the
// desktop both are backed by the one static framebuffer above, so the handles
// only need to be non-NULL and stable.
static char s_panel_handle;
static char s_io_handle;

esp_lcd_panel_handle_t bsp_display_panel(void) {
    return (esp_lcd_panel_handle_t)&s_panel_handle;
}

esp_lcd_panel_io_handle_t bsp_display_io(void) {
    return (esp_lcd_panel_io_handle_t)&s_io_handle;
}

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd,
                                    const void *param, size_t param_size) {
    (void)io; (void)lcd_cmd; (void)param; (void)param_size;
    return ESP_OK;   // the host blit is synchronous; no DMA to drain
}

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start,
                                    int y_start, int x_end, int y_end,
                                    const void *color_data) {
    (void)panel;
    sim_display_blit_be(x_start, y_start, x_end, y_end, color_data);
    return ESP_OK;
}
