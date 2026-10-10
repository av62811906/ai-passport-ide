// GSC battle HUD tiles, drawn at an integer scale through a clipped backend.
// The caller owns layout, band clipping, and the HP/gameplay snapshot.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "screen.h"

#define BATTLE_HUD_TILE_SIZE 8
#define BATTLE_HUD_HP_ORIGINAL_FILL_TILES 6
#define BATTLE_HUD_HP_COMPACT_FILL_TILES 4
#define BATTLE_HUD_SCALE 2
#define BATTLE_HUD_HP_COMPACT_WIDTH 112
#define BATTLE_HUD_HP_HEIGHT 16

// Original GBC RGB5 colors, with the green channel expanded to RGB565.
// Shade 1 colors the HP letters; the empty track is shade 0 (background).
#define BATTLE_HUD_LABEL_COLOR C_HP_TRACK
#define BATTLE_HUD_HP_GREEN C_HP_GREEN
#define BATTLE_HUD_HP_YELLOW C_HP_YELLOW
#define BATTLE_HUD_HP_RED C_HP_RED
#define BATTLE_HUD_EXP_COLOR C_FOCUS
#define BATTLE_HUD_ORIGINAL_BACKGROUND 0xFFFFu

typedef void (*battle_hud_rect_fn)(void *ctx, int x, int y, int w, int h,
                                   uint16_t color);

typedef enum {
    BATTLE_HUD_WILD,  // $6b: square cap from font_battle_extra.
    BATTLE_HUD_PET,   // $6c: stepped cap from enemy_hp_bar_border.
} battle_hud_side_t;

// Supported scales are 1..4, and fill_tiles is 1..16. Invalid input returns 0.
// Width includes the two HP label tiles, the fill tiles, and one cap tile.
int battle_hud_hp_width(uint8_t fill_tiles, uint8_t scale);

// Color selection retains GetHPPal's original 48-pixel quantization and its
// >=24 green / >=10 yellow thresholds, even when the displayed bar is shorter.
uint16_t battle_hud_hp_color(uint16_t cur, uint16_t max);

// Original wild ownership tile $5d (expbar.png tile 8), distinct from the
// trainer party/status balls. The caller checks the Pokédex caught bit.
bool battle_hud_draw_caught(battle_hud_rect_fn rect, void *ctx, int x, int y,
                            uint8_t scale, uint16_t background);

// DrawBattleHPBar's actual 8x8 tiles, including its minimum live-HP sliver.
// Whole component bounds are cleared to background before drawing the tiles.
// Passing ORIGINAL_FILL_TILES and scale=1 reproduces the original 72x8 bar;
// COMPACT_FILL_TILES and scale=2 gives the project's 112x16 adaptation.
bool battle_hud_draw_hp(battle_hud_rect_fn rect, void *ctx, int x, int y,
                        uint8_t fill_tiles, uint8_t scale,
                        battle_hud_side_t side, uint16_t cur, uint16_t max,
                        uint16_t background);

// Original frame 1, composed exactly as TextboxBorder's six tile patterns.
// cols/rows are total tile counts, including the border (both must be >=3).
// (0,240,15,5,2) covers the bottom 240x80 band. For 16px text use x>=12,
// right<=228 and y=256/274/292, clear of the actual border pixels.
// Whole box bounds are cleared first. Returns false for invalid dimensions.
bool battle_hud_draw_message_box(battle_hud_rect_fn rect, void *ctx,
                                 int x, int y, uint8_t cols, uint8_t rows,
                                 uint8_t scale, uint16_t background);

// Original PlaceExpBar: full tiles and right-aligned partial tiles accumulate
// from the right end. Uses gfx/battle/expbar.png $55..$5b for partial tiles,
// and DrawBattleHPBar's $62/$6a for empty/full. No label or cap is included.
// Bounds are fill_tiles*8*scale by 8*scale; filled count is clamped to max.
// Seven tiles at 2x give 112x16. At x120,y224, colored pixels occupy y230..233
// and the baseline y236..237. Draw before HP digits at y212 because the initial
// background clear includes the whole tile row (or use digits at y208).
bool battle_hud_draw_exp(battle_hud_rect_fn rect, void *ctx, int x, int y,
                         uint8_t fill_tiles, uint8_t scale,
                         uint32_t cur, uint32_t max, uint16_t background);
