// Original tile provenance: pret/pokecrystal, commit
// 7a7881d0d62e0ddbd82dcf10e7116807487ac651.
// gfx/font/font_battle_extra.png (first 12 tiles, GB low/high bitplanes):
// SHA256 23b8791b24648789e284d69d2e7f6c14e6b98c08cb56a783f67e1eca0ca6b6a1
// gfx/battle/enemy_hp_bar_border.png (first tile, GB 1bpp):
// SHA256 218cce2af1967ad07fc6083895476baa4e7f30d9f3980ce87e1d36ff3abfbcdc
// gfx/frames/1.png (six tiles in row-major order, GB 1bpp):
// SHA256 34cb93e39aacdc78c43c2f1af85939b8d19be4fc648d7c315f1de2ac11f05973
// gfx/battle/expbar.png (first seven tiles, GB low/high bitplanes):
// SHA256 61303808ff6b2e7168807489cbb3ddfd9ecb2e0798f5ee01a00058e3321ca355
// Tile selection: home/pokemon.asm DrawBattleHPBar; home/text.asm TextboxBorder.
// Palette: gfx/battle/hp_bar.pal, engine/gfx/cgb_layouts.asm _CGB_BattleColors.
// These are tile assets and presentation rules; battle damage is unchanged.
#include "battle_hud.h"

static const uint8_t s_hp_tiles[12][16] = {
    {0x00, 0x00, 0xff, 0xff, 0xff, 0xca, 0xff, 0xca, 0xff, 0xc2, 0xff, 0xca, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0xff, 0xff, 0xff, 0x1b, 0xff, 0x5f, 0xff, 0x1f, 0xff, 0x7b, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0xc0, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0xe0, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0xf0, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x00, 0xf8, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x00, 0xfc, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0xfe, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0x00, 0x00},
};

static const uint8_t s_pet_cap[8] = {
    0x00, 0xf0, 0xf8, 0xfc, 0xfe, 0xfe, 0xfe, 0x1e,
};

// DrawEnemyHUDBorder uses $5d: LoadHPBar maps expbar.png tile 8 here.
static const uint8_t s_caught_ball[8] = {
    0x00, 0x78, 0xdc, 0xfc, 0x84, 0x84, 0x78, 0x00,
};

// $79 top-left, $7a horizontal, $7b top-right, $7c vertical,
// $7d bottom-left, $7e bottom-right. Top/bottom use the SAME horizontal tile.
static const uint8_t s_frame_tiles[6][8] = {
    {0x00, 0x00, 0x00, 0x0f, 0x10, 0x27, 0x2f, 0x2c},
    {0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00},
    {0x00, 0x00, 0x00, 0xe0, 0x10, 0xc8, 0xe8, 0x68},
    {0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28},
    {0x28, 0x28, 0x28, 0x27, 0x30, 0x1f, 0x0f, 0x00},
    {0x28, 0x28, 0x28, 0xc8, 0x18, 0xf0, 0xe0, 0x00},
};

static const uint8_t s_exp_partial[7][16] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x3f, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x7f, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
};

static bool valid_scale(uint8_t scale) { return scale >= 1 && scale <= 4; }

int battle_hud_hp_width(uint8_t fill_tiles, uint8_t scale)
{
    if (!valid_scale(scale) || fill_tiles < 1 || fill_tiles > 16) return 0;
    return (fill_tiles + 3) * BATTLE_HUD_TILE_SIZE * scale;
}

static int hp_pixels(uint16_t cur, uint16_t max, int width)
{
    if (!max) return 0;
    if (cur >= max) return width;
    return (int)((uint32_t)cur * (unsigned)width / max);
}

uint16_t battle_hud_hp_color(uint16_t cur, uint16_t max)
{
    int pixels = hp_pixels(cur, max, 48);
    return pixels >= 24 ? BATTLE_HUD_HP_GREEN
         : pixels >= 10 ? BATTLE_HUD_HP_YELLOW : BATTLE_HUD_HP_RED;
}

// Shade zero is already provided by the component's background clear.
// Coalesce adjacent equal source pixels; no allocation or frame buffer needed.
static void draw_tile_2bpp(battle_hud_rect_fn rect, void *ctx, int x, int y,
                           const uint8_t tile[16], uint8_t scale,
                           const uint16_t palette[4])
{
    for (int sy = 0; sy < 8; sy++) {
        uint8_t low = tile[sy * 2], high = tile[sy * 2 + 1];
        for (int sx = 0; sx < 8;) {
            int start = sx;
            int shade = ((low >> (7 - sx)) & 1) | (((high >> (7 - sx)) & 1) << 1);
            do { sx++; } while (sx < 8 &&
                (((low >> (7 - sx)) & 1) | (((high >> (7 - sx)) & 1) << 1)) == shade);
            if (shade) rect(ctx, x + start * scale, y + sy * scale,
                            (sx - start) * scale, scale, palette[shade]);
        }
    }
}

static void draw_tile_1bpp(battle_hud_rect_fn rect, void *ctx, int x, int y,
                           const uint8_t tile[8], uint8_t scale)
{
    for (int sy = 0; sy < 8; sy++) {
        for (int sx = 0; sx < 8;) {
            if (!(tile[sy] & (0x80u >> sx))) { sx++; continue; }
            int start = sx++;
            while (sx < 8 && (tile[sy] & (0x80u >> sx))) sx++;
            rect(ctx, x + start * scale, y + sy * scale,
                 (sx - start) * scale, scale, C_INK);
        }
    }
}

bool battle_hud_draw_caught(battle_hud_rect_fn rect, void *ctx, int x, int y,
                            uint8_t scale, uint16_t background)
{
    if (!rect || !valid_scale(scale)) return false;
    rect(ctx, x, y, 8 * scale, 8 * scale, background);
    draw_tile_1bpp(rect, ctx, x, y, s_caught_ball, scale);
    return true;
}

bool battle_hud_draw_hp(battle_hud_rect_fn rect, void *ctx, int x, int y,
                        uint8_t fill_tiles, uint8_t scale,
                        battle_hud_side_t side, uint16_t cur, uint16_t max,
                        uint16_t background)
{
    int width = battle_hud_hp_width(fill_tiles, scale);
    if (!rect || !width || (side != BATTLE_HUD_WILD && side != BATTLE_HUD_PET)) return false;
    uint16_t palette[4] = {background, BATTLE_HUD_LABEL_COLOR,
                           battle_hud_hp_color(cur, max), C_INK};
    const int tile_width = BATTLE_HUD_TILE_SIZE * scale;
    rect(ctx, x, y, width, tile_width, background);
    draw_tile_2bpp(rect, ctx, x, y, s_hp_tiles[0], scale, palette);
    draw_tile_2bpp(rect, ctx, x + tile_width, y, s_hp_tiles[1], scale, palette);
    int remaining = hp_pixels(cur, max, fill_tiles * BATTLE_HUD_TILE_SIZE);
    // Original DrawBattleHPBar keeps an alive Pokémon visible at <1 source px.
    if (!remaining && cur && max) remaining = 1;
    x += 2 * tile_width;
    for (int i = 0; i < fill_tiles; i++, x += tile_width) {
        int filled = remaining >= 8 ? 8 : remaining;
        draw_tile_2bpp(rect, ctx, x, y, s_hp_tiles[2 + filled], scale, palette);
        remaining -= filled;
    }
    if (side == BATTLE_HUD_PET) draw_tile_1bpp(rect, ctx, x, y, s_pet_cap, scale);
    else draw_tile_2bpp(rect, ctx, x, y, s_hp_tiles[11], scale, palette);
    return true;
}

bool battle_hud_draw_message_box(battle_hud_rect_fn rect, void *ctx,
                                 int x, int y, uint8_t cols, uint8_t rows,
                                 uint8_t scale, uint16_t background)
{
    if (!rect || !valid_scale(scale) || cols < 3 || rows < 3) return false;
    int tile_width = BATTLE_HUD_TILE_SIZE * scale;
    rect(ctx, x, y, cols * tile_width, rows * tile_width, background);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int tile;
            if (!row) tile = !col ? 0 : col == cols - 1 ? 2 : 1;
            else if (row == rows - 1) tile = !col ? 4 : col == cols - 1 ? 5 : 1;
            else if (!col || col == cols - 1) tile = 3;
            else continue;
            draw_tile_1bpp(rect, ctx, x + col * tile_width,
                           y + row * tile_width, s_frame_tiles[tile], scale);
        }
    }
    return true;
}

bool battle_hud_draw_exp(battle_hud_rect_fn rect, void *ctx, int x, int y,
                         uint8_t fill_tiles, uint8_t scale,
                         uint32_t cur, uint32_t max, uint16_t background)
{
    if (!rect || !battle_hud_hp_width(fill_tiles, scale)) return false;
    const int tile_width = BATTLE_HUD_TILE_SIZE * scale;
    const int source_width = fill_tiles * BATTLE_HUD_TILE_SIZE;
    const uint16_t palette[4] = {background, BATTLE_HUD_LABEL_COLOR,
                                 BATTLE_HUD_EXP_COLOR, C_INK};
    int remaining = !max ? 0 : cur >= max ? source_width
        : (int)((uint64_t)cur * (unsigned)source_width / max);
    rect(ctx, x, y, fill_tiles * tile_width, tile_width, background);
    for (int i = fill_tiles - 1; i >= 0; i--) {
        int filled = remaining >= 8 ? 8 : remaining;
        const uint8_t *tile = !filled ? s_hp_tiles[2]
            : filled == 8 ? s_hp_tiles[10] : s_exp_partial[filled - 1];
        draw_tile_2bpp(rect, ctx, x + i * tile_width, y, tile, scale, palette);
        remaining -= filled;
    }
    return true;
}
