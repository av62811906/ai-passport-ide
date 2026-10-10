// Shared scene recipe: integer screen coordinates and logical RGB565 colors.
// Backends consume rectangles/text; they do not calculate layout or fallback.
#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "render.h"
#include "screen.h"
#include "render_layout_budget.h"
#include "battle_hud.h"

#define SCENE_P3_BG BATTLE_HUD_ORIGINAL_BACKGROUND
#define SCENE_P3_WILD_TOP 30
#define SCENE_P3_WILD_RIGHT 232
#define SCENE_P3_WILD_SCALE 2
#define SCENE_P3_WILD_MAX_SIZE 112
#define SCENE_P3_WILD_HUD_RIGHT 120
#define SCENE_P3_PET_HP_X 120
#define SCENE_P3_PET_HP_Y 192
#define SCENE_P3_PET_HP_W 112
#define SCENE_P3_PET_HP_H 16

#define SCENE_P3_PET_BACK_X 8
#define SCENE_P3_PET_BACK_Y 140
#define SCENE_P3_PET_BACK_SIZE 96

// Coordinates belong to the recipe; width budget comes from VAR_ELEMENTS.
#define SCENE_P3_PET_NAME_X 112
#define SCENE_P3_PET_NAME_Y 168
#define SCENE_P3_PET_NAME_RIGHT (SCENE_P3_PET_NAME_X + LAYOUT_P3_PET_NAME_BUDGET)

typedef struct { int x, y, w, h; } scene_bounds_t;
typedef struct {
    int x, y;                  // Original asset's drawing origin.
    scene_bounds_t visible;    // Half-open nontransparent bounds on screen.
} scene_sprite_layout_t;

// Align the visible artwork, not its transparent source padding. Preserve all
// source pixels and integer scale; the renderer clips only transparent margins.
// Call once on page entry. Sprite sizes are validated against the asset format.
static inline bool scene_p3_wild_layout(const uint8_t *data, int size,
                                         scene_sprite_layout_t *out)
{
    if (!data || !out || (size != 32 && size != 40 && size != 48 && size != 56)) return false;
    int left = size, top = size, right = 0, bottom = 0;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            unsigned shade = (data[y * (size / 4) + x / 4] >> (6 - 2 * (x % 4))) & 3u;
            if (shade == 3u) continue;
            if (x < left) left = x;
            if (y < top) top = y;
            if (x + 1 > right) right = x + 1;
            if (y + 1 > bottom) bottom = y + 1;
        }
    }
    if (right <= left || bottom <= top) return false;
    int width = (right - left) * SCENE_P3_WILD_SCALE;
    *out = (scene_sprite_layout_t){
        SCENE_P3_WILD_RIGHT - right * SCENE_P3_WILD_SCALE,
        SCENE_P3_WILD_TOP - top * SCENE_P3_WILD_SCALE,
        {SCENE_P3_WILD_RIGHT - width, SCENE_P3_WILD_TOP,
         width, (bottom - top) * SCENE_P3_WILD_SCALE},
    };
    return true;
}

// Text is borrowed for the duration of this synchronous callback only.
typedef void (*scene_text_fn)(void *ctx, int x, int y, const char *text,
                              uint16_t color);

static inline void scene_p3_pet_name_sized(scene_text_fn text, void *ctx,
                                      const char *name, uint8_t name_len,
                                      uint8_t level, uint16_t font_size)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*s Lv%u", (int)name_len, name, (unsigned)level);
    // Preserve D53: only remove the space, never abbreviate the species name.
    if (render_text_width_sized(buf, font_size) > SCENE_P3_PET_NAME_RIGHT - SCENE_P3_PET_NAME_X) {
        snprintf(buf, sizeof(buf), "%.*sLv%u", (int)name_len, name, (unsigned)level);
    }
    text(ctx, SCENE_P3_PET_NAME_X, SCENE_P3_PET_NAME_Y, buf, C_INK);
}

// Firmware compatibility entry: keep existing call sites and font state.
static inline void scene_p3_pet_name(scene_text_fn text, void *ctx,
                                      const char *name, uint8_t name_len,
                                      uint8_t level)
{
    scene_p3_pet_name_sized(text, ctx, name, name_len, level, render_font_size());
}

typedef void (*scene_rect_fn)(void *ctx, int x, int y, int w, int h,
                              uint16_t color);

// Input is a snapshot of the same cur/max pair used by the battle page.
// Rectangles use half-open bounds; later rectangles overwrite earlier ones.
static inline void scene_p3_pet_hp(scene_rect_fn rect, void *ctx,
                                    uint16_t cur, uint16_t max)
{
    battle_hud_draw_hp(rect, ctx, SCENE_P3_PET_HP_X, SCENE_P3_PET_HP_Y,
                       BATTLE_HUD_HP_COMPACT_FILL_TILES, BATTLE_HUD_SCALE,
                       BATTLE_HUD_PET, cur, max, SCENE_P3_BG);
}

// Decode once in the shared recipe, including scale, transparency and palette.
// Backends still consume only rectangles, never source pixels or scale rules.
// The caller supplies a validated w*h 2bpp asset (ceil(w/4) bytes per row).
static inline bool scene_p3_pet_back(scene_rect_fn rect, void *ctx,
                                      const uint8_t *data, int w, int h,
                                      int shake_dx, const uint16_t palette[4])
{
    if (!data || !palette || w != h || (w != 32 && w != 48)) return false;
    const int scale = SCENE_P3_PET_BACK_SIZE / w;
    const int row_bytes = (w + 3) / 4;
    for (int sy = 0; sy < h; sy++) {
        // Coalesce adjacent equal shades into a rectangle; no extra buffer.
        for (int sx = 0; sx < w;) {
            int start = sx;
            uint8_t shade = (data[sy * row_bytes + sx / 4] >> (6 - 2 * (sx % 4))) & 3;
            do {
                sx++;
            } while (sx < w &&
                     ((data[sy * row_bytes + sx / 4] >> (6 - 2 * (sx % 4))) & 3) == shade);
            if (shade != 3) {
                rect(ctx, SCENE_P3_PET_BACK_X + shake_dx + start * scale,
                     SCENE_P3_PET_BACK_Y + sy * scale,
                     (sx - start) * scale, scale, palette[shade]);
            }
        }
    }
    return true;
}
