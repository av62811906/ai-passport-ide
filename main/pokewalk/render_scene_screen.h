// Firmware backend: consume absolute commands through the existing band API.
#pragma once

#include "render_scene.h"

static inline void scene_screen_text(void *ctx, int x, int y, const char *text,
                                      uint16_t color)
{
    render_text(x, y - *(const int *)ctx, text, color);
}

static inline void scene_screen_p3_pet_name(int band_y, const char *name,
                                            uint8_t name_len, uint8_t level)
{
    scene_p3_pet_name(scene_screen_text, &band_y, name, name_len, level);
}

static inline void scene_screen_rect(void *ctx, int x, int y, int w, int h,
                                      uint16_t color)
{
    const int band_y = *(const int *)ctx;
    int top = y > band_y ? y : band_y;
    int bottom = y + h < band_y + SCREEN_BAND_H
        ? y + h : band_y + SCREEN_BAND_H;
    for (int py = top; py < bottom; py++) {
        for (int px = x; px < x + w; px++) {
            screen_px(px, py - band_y, color);
        }
    }
}

static inline void scene_screen_p3_pet_hp(int band_y, uint16_t cur, uint16_t max)
{
    scene_p3_pet_hp(scene_screen_rect, &band_y, cur, max);
}

static inline bool scene_screen_p3_pet_back(int band_y, const uint8_t *data,
                                            int w, int h, int shake_dx,
                                            const uint16_t palette[4])
{
    return scene_p3_pet_back(scene_screen_rect, &band_y, data, w, h,
                             shake_dx, palette);
}
