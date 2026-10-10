// P10 inventory: four rows per pocket page, followed by a shared GSC textbox.
// All item application and stock changes belong to world; this page is a reader.
#include <stdio.h>
#include <string.h>

#include "assets.h"
#include "ball_assets.h"
#include "game_ui.h"
#include "items.h"
#include "lvgl.h"
#include "nav.h"
#include "play.h"
#include "render.h"
#include "screen.h"
#include "sfx.h"
#include "world.h"
#include "evolution_ui.h"

#define BAG_ROWS 4
#define BAG_ROW_Y 64
#define BAG_ROW_STEP 24
#define BAG_ICON_SIZE 24
#define BAG_DETAIL_Y 184
#define BAG_DETAIL_H 88
#define BAG_TEXT_X 16
#define BAG_TEXT_W 208
#define BAG_DESCRIPTION_LINES 3
#define BAG_DESCRIPTION_Y 194
#define BAG_DESCRIPTION_STEP 18
#define BAG_FEEDBACK_Y 250
#define BAG_TEXT_H 16

SCREEN_ASSERT_WITHIN_BAND(bag_row_0, BAG_ROW_Y, BAG_TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(bag_row_1, BAG_ROW_Y + BAG_ROW_STEP, BAG_TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(bag_row_2, BAG_ROW_Y + BAG_ROW_STEP * 2, BAG_TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(bag_row_3, BAG_ROW_Y + BAG_ROW_STEP * 3, BAG_TEXT_H);
// Full redraws clear both old and new bounds, including icons and the textbox.
SCREEN_ASSERT_ALLOW_CROSS_BAND(bag_icons, BAG_ROW_Y - 4, BAG_ROW_STEP * BAG_ROWS);
SCREEN_ASSERT_ALLOW_CROSS_BAND(bag_details, BAG_DETAIL_Y, BAG_DETAIL_H);
SCREEN_ASSERT_ALLOW_CROSS_BAND(bag_description, BAG_DESCRIPTION_Y,
                              BAG_DESCRIPTION_STEP * (BAG_DESCRIPTION_LINES - 1) + BAG_TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(bag_feedback, BAG_FEEDBACK_Y, BAG_TEXT_H);

static inventory_t s_inventory;
static world_t s_world;
static uint8_t s_selected;
static lv_timer_t *s_tick;
static char s_description[BAG_DESCRIPTION_LINES][96];
static char s_feedback[96];
static bool s_success;

// Small original drawings for this game's added items. Each source pixel is
// rendered at exactly 2x by the shared 2bpp renderer; no vector or browser path.
static const char STONE[12][13] = {
    "............", "....####....", "...#oo++#...", "..#oo++++#..",
    ".#oo++++++#.", ".#o+++++++#.", ".#+++++++#..", "..#+++++#...",
    "..#++++#....", "...#++#.....", "....##......", "............",
};
static const char MACHINE[12][13] = {
    "...#....#...", "...#....#...", ".##########.", ".#oooooooo#.",
    ".#o######o#.", ".#o#++++#o#.", ".#o######o#.", ".#oooooooo#.",
    ".#o##oo##o#.", ".#oooooooo#.", ".##########.", "............",
};
static const char GROWTH[12][13] = {
    "....####....", "...#++++#...", "..#++++++#..", ".##########.",
    ".#ooo##ooo#.", ".#oo#++#oo#.", ".#o#++++#o#.", ".#oo#++#oo#.",
    ".#oo#++#oo#.", ".#oooooooo#.", ".##########.", "............",
};
static const char BERRY[12][13] = {
    ".....#......", "....#o###...", "....##oo#...", "..###o##....",
    ".#oo+#++##..", ".#o+++++++#.", ".#++++++++#.", ".#++++++++#.",
    "..#++++++#..", "...#++++#...", "....####....", "............",
};
static const char ROOT_ART[12][13] = {
    "...##..##...", "...#+##+#...", "....#++#....", "....####....",
    "...#oo++#...", "...#o+++#...", "....#++#....", "....#++#....",
    ".....##.....", "....#.#.....", "...#..#.....", "............",
};
static const char MILK[12][13] = {
    "....####....", "....#++#....", "....####....", "...#oooo#...",
    "..#oooooo#..", "..#oooooo#..", "..#o####o#..", "..#o#++#o#..",
    "..#o####o#..", "..#oooooo#..", "...######...", "............",
};
static const char COOKIE[12][13] = {
    "............", "....####....", "..##++++##..", ".#++oo++++#.",
    ".#++++++o+#.", "#++o+++++++#", "#++++o+++++#", ".#++++++++#.",
    ".#++o++o++#.", "..##++++##..", "....####....", "............",
};

static uint8_t pocket_start(void)
{
    return s_selected < ITEM_BALL_COUNT ? ITEM_POKE
        : s_selected < ITEM_BERRY ? ITEM_FIRE_STONE : ITEM_BERRY;
}

static uint8_t pocket_end(void)
{
    return s_selected < ITEM_BALL_COUNT ? ITEM_FIRE_STONE
        : s_selected < ITEM_BERRY ? ITEM_BERRY : ITEM_COUNT;
}

static uint8_t first_row(void)
{
    uint8_t start = pocket_start();
    return start + (s_selected - start) / BAG_ROWS * BAG_ROWS;
}

static size_t utf8_bytes(const char *text)
{
    unsigned char c = (unsigned char)*text;
    return c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
}

static void wrap_description(const char *text)
{
    memset(s_description, 0, sizeof(s_description));
    if (!text) return;
    unsigned row = 0;
    size_t used = 0;
    while (*text && row < BAG_DESCRIPTION_LINES) {
        if (*text == '\n') { text++; row++; used = 0; continue; }
        size_t length = utf8_bytes(text);
        if (used + length >= sizeof(s_description[0])) { row++; used = 0; continue; }
        memcpy(s_description[row] + used, text, length);
        s_description[row][used + length] = '\0';
        if (render_text_width(s_description[row]) > BAG_TEXT_W) {
            s_description[row][used] = '\0';
            row++; used = 0;
        } else { used += length; text += length; }
    }
    if (*text) {
        char *last = s_description[BAG_DESCRIPTION_LINES - 1];
        size_t length = strlen(last);
        while (length && render_text_width(last) + render_text_width("...") > BAG_TEXT_W) {
            do { length--; } while (length && ((unsigned char)last[length] & 0xc0) == 0x80);
            last[length] = '\0';
        }
        strcat(last, "...");
    }
}

static void select_description(void)
{
    const item_info_t *info = items_info(s_selected);
    wrap_description(info ? info->description : "道具资料暂不可用");
}

static void draw_item_icon(int band_y, uint8_t item, int y)
{
    if (item < ITEM_BALL_COUNT) {
        ui_art_t art;
        if (assets_ui("ball_24", &art))
            game_ui_sprite_centered(band_y, 24, y - 4, BAG_ICON_SIZE, BAG_ICON_SIZE,
                                     art.data, art.w, art.h, 1, ball_assets_palette(item));
        return;
    }
    const char (*drawing)[13] = STONE;
    uint16_t accent = GAME_UI_ACCENT;
    switch (item) {
    case ITEM_FIRE_STONE: accent = C_HP_RED; break;
    case ITEM_THUNDER_STONE: accent = C_HP_YELLOW; break;
    case ITEM_LEAF_STONE: accent = C_HP_GREEN; break;
    case ITEM_MOON_STONE: accent = GAME_UI_MUTED; break;
    case ITEM_LINK_MACHINE: drawing = MACHINE; break;
    case ITEM_GROWTH_MACHINE: drawing = GROWTH; break;
    case ITEM_BERRY: drawing = BERRY; accent = C_HP_RED; break;
    case ITEM_ENERGY_ROOT: drawing = ROOT_ART; accent = C_HP_YELLOW; break;
    case ITEM_MILK: drawing = MILK; break;
    case ITEM_JOY_COOKIE: drawing = COOKIE; accent = C_HP_YELLOW; break;
    default: break;
    }
    uint8_t pixels[36] = {0};
    for (int py = 0; py < 12; py++) for (int px = 0; px < 12; px++) {
        char ch = drawing[py][px];
        uint8_t shade = ch == '#' ? 0 : ch == '+' ? 1 : ch == 'o' ? 2 : 3;
        pixels[py * 3 + px / 4] |= shade << (6 - 2 * (px % 4));
    }
    uint16_t palette[4] = {GAME_UI_INK, accent, GAME_UI_BG, GAME_UI_BG};
    render_sprite_2bpp_wh(24, y - 4 - band_y, pixels, 12, 12, 2, palette);
}

static void draw_band(int band_y)
{
    screen_band_clear(GAME_UI_BG);
    char text[96];
    species_t sp;
    if (assets_species(s_world.species, &sp))
        snprintf(text, sizeof(text), "%.*s Lv%u", sp.name_zh_len, sp.name_zh, s_world.level);
    else snprintf(text, sizeof(text), "伙伴");
    game_ui_title(band_y, "背包", text);

    uint8_t start = pocket_start(), end = pocket_end(), first = first_row();
    const char *pocket = s_selected < ITEM_BALL_COUNT ? "精灵球"
                        : s_selected < ITEM_BERRY ? "进化道具" : "养成道具";
    render_text(12, 40 - band_y, pocket, GAME_UI_INK);
    snprintf(text, sizeof(text), "%u/%u", (first - start) / BAG_ROWS + 1,
             (end - start + BAG_ROWS - 1) / BAG_ROWS);
    render_text(228 - render_text_width(text), 40 - band_y, text, GAME_UI_MUTED);

    for (uint8_t row = 0; row < BAG_ROWS && first + row < end; row++) {
        uint8_t item = first + row;
        int y = BAG_ROW_Y + row * BAG_ROW_STEP;
        const item_info_t *info = items_info(item);
        uint16_t color = item == s_selected || s_inventory.quantity[item] ? GAME_UI_INK : GAME_UI_MUTED;
        if (item == s_selected) game_ui_cursor(band_y, 12, y + 3);
        draw_item_icon(band_y, item, y);
        render_text(56, y - band_y, info ? info->name : "未知道具", color);
        snprintf(text, sizeof(text), "x%u", s_inventory.quantity[item]);
        render_text(228 - render_text_width(text), y - band_y, text, color);
    }
    render_text(12, 164 - band_y, "长按B返回", GAME_UI_MUTED);
    snprintf(text, sizeof(text), "%02u/%u", s_selected + 1, ITEM_COUNT);
    render_text(228 - render_text_width(text), 164 - band_y, text, GAME_UI_MUTED);

    game_ui_box(band_y, 8, BAG_DETAIL_Y, 224, BAG_DETAIL_H);
    for (unsigned row = 0; row < BAG_DESCRIPTION_LINES; row++)
        render_text(BAG_TEXT_X, BAG_DESCRIPTION_Y + row * BAG_DESCRIPTION_STEP - band_y,
                      s_description[row], GAME_UI_INK);
    const char *feedback = s_feedback[0] ? s_feedback
        : s_selected < ITEM_BALL_COUNT ? "请在捕获页选球使用" : "对当前伙伴使用";
    game_ui_text_centered(band_y, BAG_TEXT_X, BAG_FEEDBACK_Y, BAG_TEXT_W, BAG_TEXT_H,
                          feedback, s_success ? GAME_UI_ACCENT : s_feedback[0] ? GAME_UI_INK : GAME_UI_MUTED);
    game_ui_footer(band_y, s_selected < ITEM_BALL_COUNT
                              ? GAME_UI_NAV_HINT : GAME_UI_NAV_HINT);
    screen_push_band(band_y);
}

static void draw_all(void)
{
    for (int y = 0; y < SCREEN_H; y += SCREEN_BAND_H) draw_band(y);
}

static bool refresh_snapshot(void)
{
    inventory_t inventory;
    world_t world;
    memset(&inventory, 0, sizeof(inventory));
    memset(&world, 0, sizeof(world));
    world_inventory_snapshot(&inventory);
    world_snapshot(&world);
    bool changed = memcmp(&inventory, &s_inventory, sizeof(inventory)) != 0 ||
                   world.species != s_world.species || world.level != s_world.level;
    s_inventory = inventory;
    s_world = world;
    return changed;
}

static void refresh_tick(lv_timer_t *timer)
{
    (void)timer;
    if (refresh_snapshot()) draw_all();
}

static void use_selected(void)
{
    s_success = false;
    if (s_selected < ITEM_BALL_COUNT) {
        snprintf(s_feedback, sizeof(s_feedback), "请在捕获页选球使用");
        return;
    }
    item_use_result_t preview;
    if(items_apply(s_selected,s_world.species,s_world.level,&s_world.pet,&preview)==ITEM_USE_OK &&
       preview.species_after!=preview.species_before) {
        inventory_t inventory;world_inventory_snapshot(&inventory);
        if(!inventory.quantity[s_selected]){snprintf(s_feedback,sizeof(s_feedback),"道具不足");return;}
        if(!evolution_ui_begin(preview.species_before,preview.species_after,s_selected))
            snprintf(s_feedback,sizeof(s_feedback),"无法开始进化");
        return;
    }
    item_use_result_t result;
    item_use_status_t status = world_item_use(s_world.species, s_selected, &result);
    refresh_snapshot();
    const char *message = "道具无效";
    switch (status) {
    case ITEM_USE_OK: {
        s_success = true;
        sfx_play(result.species_before != result.species_after ? SFX_EVOLVE : SFX_CARE);
        species_t evolved;
        if (result.species_before != result.species_after && assets_species(result.species_after, &evolved)) {
            snprintf(s_feedback, sizeof(s_feedback), "进化成%.*s了", evolved.name_zh_len, evolved.name_zh);
            return;
        }
        const char *labels[] = {"饱食", "心情", "体能", "亲密"};
        const int32_t before[] = {result.before.satiety, result.before.mood, result.before.stamina, result.before.intimacy};
        const int32_t after[] = {result.after.satiety, result.after.mood, result.after.stamina, result.after.intimacy};
        size_t used = 0;
        unsigned shown = 0;
        for (unsigned i = 0; i < 4 && shown < 2; i++) {
            int delta = (int)nurture_pct(after[i]) - (int)nurture_pct(before[i]);
            if (!delta) continue;
            used += (size_t)snprintf(s_feedback + used, sizeof(s_feedback) - used,
                                     "%s%s%+d", shown ? " " : "", labels[i], delta);
            shown++;
        }
        if (shown) return;
        message = "使用成功";
        break;
    }
    case ITEM_USE_EMPTY: message = "这个道具用完了"; break;
    case ITEM_USE_WRONG_TARGET: message = "伙伴已更换 请重试"; break;
    case ITEM_USE_NOT_APPLICABLE: message = "对当前伙伴无效"; break;
    case ITEM_USE_LEVEL_TOO_LOW: message = "等级还不够"; break;
    case ITEM_USE_SAVE_FAILED: message = "保存失败 请重试"; break;
    case ITEM_USE_STORAGE_UNAVAILABLE: message = "存档暂不可用"; break;
    case ITEM_USE_BUSY: message = "请先结束当前对战"; break;
    default: break;
    }
    snprintf(s_feedback, sizeof(s_feedback), "%s", message);
}

void play_bag_enter(void)
{
    s_selected = 0;
    s_feedback[0] = '\0';
    s_success = false;
    s_tick = NULL;
    refresh_snapshot();
    select_description();
    screen_set_redraw(draw_all);
    draw_all();
    s_tick = lv_timer_create(refresh_tick, 500, NULL);
}

void play_bag_exit(void)
{
    if (s_tick) { lv_timer_delete(s_tick); s_tick = NULL; }
}

uint8_t play_bag_selected_item(void) { return s_selected; }

void play_bag_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (nav_direction(btn, ev) != 0) {
        s_selected = (s_selected + ITEM_COUNT + nav_direction(btn, ev)) % ITEM_COUNT;
        s_feedback[0] = '\0';
        s_success = false;
        refresh_snapshot();
        select_description();
        draw_all();
    } else if (nav_confirm(btn, ev)) {
        use_selected();
        draw_all();
    } else if (nav_return(btn, ev)) {
        nav_back(PAGE_CARE);
    }
}
