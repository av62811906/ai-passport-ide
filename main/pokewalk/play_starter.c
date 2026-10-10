// P9: a fresh game's starter choice. Ownership and persistence live in world.
#include <stdio.h>

#include "assets.h"
#include "lvgl.h"
#include "game_ui.h"
#include "nav.h"
#include "play.h"
#include "render.h"
#include "screen.h"
#include "world.h"

static const uint16_t STARTERS[] = {1, 4, 7, 25};
#define STARTER_COUNT (sizeof(STARTERS) / sizeof(STARTERS[0]))
#define SPRITE_X 64
#define SPRITE_Y 64
#define SPRITE_SIZE 112
#define NAME_Y 184
#define NOTE_Y 225
#define DETAIL_Y 249

SCREEN_ASSERT_WITHIN_BAND(starter_name, NAME_Y, 24);
// This static page always redraws all four bands, including both note halves.
SCREEN_ASSERT_ALLOW_CROSS_BAND(starter_note, NOTE_Y, 16);
SCREEN_ASSERT_WITHIN_BAND(starter_detail, DETAIL_Y, 16);

static uint8_t s_selection;
static world_starter_result_t s_result;
static bool s_saving;
static bool s_intro_requested;
static unsigned s_reveal;
static lv_timer_t *s_intro_timer;

static void draw_band(int band_y)
{
    screen_band_clear(GAME_UI_BG);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)s_selection + 1, (unsigned)STARTER_COUNT);
    game_ui_title(band_y, "选择伙伴", buf);
    game_ui_text_centered(band_y, 12, 40, 216, 16, "让它成为你的伙伴", GAME_UI_MUTED);

    species_t species;
    uint8_t size = 0;
    uint16_t sid = STARTERS[s_selection];
    const uint8_t *front = assets_front_sprite(sid, &size);
    if (assets_species(sid, &species)) {
        snprintf(buf, sizeof(buf), "%.*s", species.name_zh_len, species.name_zh);
        game_ui_text_centered(band_y, 12, NAME_Y, 216, 24, buf, GAME_UI_INK);
        if (front && size && size * 2 <= SPRITE_SIZE) {
            uint16_t palette[4];
            assets_palette(species.palette, palette);
            // Preserve the complete front artwork at integer 2x. The shared
            // helper centers its visible bounds without cropping source ink.
            game_ui_sprite_centered(band_y, SPRITE_X, SPRITE_Y, SPRITE_SIZE, SPRITE_SIZE,
                                    front, size, size, 2, palette);
        }
    }

    const char *note = "一起开始冒险吧";
    const char *detail = "选好后按确认键";
    if (s_saving) { note = "正在保存"; detail = "请稍候"; }
    else if (s_result == WORLD_STARTER_SAVE_FAILED) {
        note = "保存失败"; detail = "确认重试 上下更换";
    } else if (s_result == WORLD_STARTER_STORAGE_UNAVAILABLE) {
        note = "存档不可用"; detail = "请重启后再试";
    } else if (s_result == WORLD_STARTER_INVALID) {
        note = "无法选择伙伴"; detail = "请按B或C更换";
    }
    game_ui_box(band_y, 8, 216, 224, 56);
    game_ui_text_centered(band_y, 16, NOTE_Y, 208, 16, note, GAME_UI_INK);
    game_ui_text_centered(band_y, 16, DETAIL_Y, 208, 16, detail, GAME_UI_MUTED);
    game_ui_footer(band_y, GAME_UI_NAV_HINT);
    if(s_reveal)game_ui_fade_background(band_y,s_reveal);
    screen_push_band(band_y);
}

static void draw_all(void)
{
    for (int y = 0; y < SCREEN_H; y += SCREEN_BAND_H) draw_band(y);
}

static void intro_tick(lv_timer_t *t) {
    if(s_reveal)s_reveal--;
    draw_all();
    if(!s_reveal){s_intro_timer=NULL;lv_timer_delete(t);}
}
void play_starter_prepare_intro(void){s_intro_requested=true;}
void play_starter_enter(void)
{
    s_reveal=s_intro_requested?16:0;s_intro_requested=false;
    if(s_reveal)s_intro_timer=lv_timer_create(intro_tick,33,NULL);
    s_selection = 0;
    s_result = WORLD_STARTER_OK;
    s_saving = false;
    screen_set_redraw(draw_all);
    draw_all();
}

void play_starter_exit(void) { if(s_intro_timer){lv_timer_delete(s_intro_timer);s_intro_timer=NULL;}s_reveal=0; }

bool play_starter_screen_busy(void) { return s_saving || s_reveal; }

void play_starter_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (nav_return(btn, ev) && !s_saving && !s_reveal) { nav_go(PAGE_OPENING); return; }
    if (ev != BSP_BTN_CLICK || s_saving || s_reveal) return;
    if (nav_direction(btn, ev) != 0) {
        s_selection = btn == BSP_BTN_DOWN ? (s_selection + 1) % STARTER_COUNT
                                         : (s_selection + STARTER_COUNT - 1) % STARTER_COUNT;
        s_result = WORLD_STARTER_OK;
        draw_all();
    } else if (nav_confirm(btn, ev)) {
        s_saving = true;
        draw_all();
        s_result = world_choose_starter(STARTERS[s_selection]);
        s_saving = false;
        if (s_result == WORLD_STARTER_OK || s_result == WORLD_STARTER_ALREADY_CHOSEN)
            nav_go(PAGE_IDLE);
        else draw_all();
    }
}
