// tools/simulator/host/sim_display.c
// LVGL display driver for the desktop simulator: renders into a static
// full-screen RGB565 framebuffer and applies the same rounded-corner mask the
// device uses, so the IDE mirrors the on-device look.
#include "sim.h"
#include "sim_internal.h"

#include "bsp_display.h"            // BSP_LVGL_SCREEN_RADIUS
#include "bsp_display_rounding.h"   // bsp_display_rounded_row_span
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
