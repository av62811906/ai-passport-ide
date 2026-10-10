// main/play_opening.c —— P0 大木博士开场。

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "assets.h"
#include "game_ui.h"
#include "nav.h"
#include "opening.h"
#include "play.h"
#include "render.h"
#include "save.h"
#include "screen.h"
#include "world.h"

static const char *TAG = "p0";

#define BAND_H SCREEN_BAND_H
#define SCR_W SCREEN_W
#define SCR_H SCREEN_H

// Crystal intro Oak: gfx/trainers/oak.png -> oak.gbcpal, pinned in fetch_oak.py.
// Original RGB5 in engine shade order: black, (13,16,0), (24,19,11), white.
// UI index 3 preserves source white on GAME_UI_BG; no interior recoloring.
static const uint16_t OAK_PALETTE[4] = {
    0x0000, 0x6c20, 0xc4eb, 0xffff,
};

// Four lines inside the original GSC frame, using the same 16px font.
#define GLYPH        16
#define MARGIN       12
#define USABLE_W     (SCR_W - MARGIN * 2)
#define TEXT_BOX_Y   160
#define OAK_ZOOM_FRAMES 8
#define TEXT_SLIDE_FRAMES OPENING_BOX_APPEAR_FRAMES
#define OAK_BREATH_BOX 3
#define OAK_BREATH_FRAMES 20

// Static lines fit within a single band; slide frames redraw both text bands.
static const uint16_t TEXT_LINE_Y[OPENING_LINES_PER_BOX] = {176, 200, 224, 248};

static opening_t s_opening;
static lv_timer_t *s_tick;
static ui_art_t s_oak_art;
static bool s_oak_ok;
static uint8_t s_oak_zoom_frame;
static uint8_t s_text_slide_frame;
static uint8_t s_oak_breath_frame;
static unsigned s_handoff; // 1..96: invitation, then fade into partner selection.

// 从 UTF-8 字符串复制前 n 个码点。台词最长 11 个汉字，64B 足够。
static void utf8_prefix(char *out, size_t cap, const char *s, uint16_t n)
{
    size_t bytes = 0;
    uint16_t chars = 0;
    if (!out || cap == 0) return;
    if (!s) { out[0] = '\0'; return; }

    while (s[bytes] && chars < n) {
        bytes++;
        while (s[bytes] && ((uint8_t)s[bytes] & 0xc0) == 0x80) bytes++;
        chars++;
    }
    if (bytes >= cap) bytes = cap - 1;
    memcpy(out, s, bytes);
    out[bytes] = '\0';
}

static uint16_t utf8_len(const char *s)
{
    uint16_t n = 0;
    if (!s) return 0;
    while (*s) {
        if (((uint8_t)*s & 0xc0) != 0x80) n++;
        s++;
    }
    return n;
}

static int text_slide_offset(void)
{
    if (s_text_slide_frame >= TEXT_SLIDE_FRAMES) return 0;
    return (TEXT_SLIDE_FRAMES - s_text_slide_frame) * 4;
}

// 与 effects.breath_sequence(20, rise=1) 一致：基线 5 帧、上浮 10 帧、
// 回到基线 5 帧。计数器只跑一轮，状态机仍然继续等玩家按 A。
static int oak_breath_offset(void)
{
    return s_oak_breath_frame >= 6 && s_oak_breath_frame <= 15 ? -1 : 0;
}

// ui.bin 是 2bpp、高位像素在左，色号 3 透明。宽高始终取素材元数据
// （当前 Oak 112×112），且 zoom 需要 1/8..8/8 分数缩放，所以不能复用
// 只支持整数倍的 sprite helper。
static void draw_ui_scaled_centered(int center_x, int center_y,
                                    const ui_art_t *art,
                                    int scale_num, int scale_den,
                                    int offset_y, const uint16_t palette[4])
{
    if (!art || !art->data || art->w == 0 || art->h == 0 ||
        scale_num <= 0 || scale_den <= 0) return;

    int dw = art->w * scale_num / scale_den;
    int dh = art->h * scale_num / scale_den;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = center_x - dw / 2;
    int oy = center_y - dh / 2 + offset_y;
    int row_bytes = (art->w * 2 + 7) / 8;

    for (int dy = 0; dy < dh; dy++) {
        int sy = dy * art->h / dh;
        for (int dx = 0; dx < dw; dx++) {
            int sx = dx * art->w / dw;
            uint8_t b = art->data[sy * row_bytes + (sx * 2) / 8];
            uint8_t shade = (b >> (6 - (sx * 2) % 8)) & 3u;
            if (shade != 3) screen_px(ox + dx, oy + dy, palette[shade]);
        }
    }
}

static void log_fx(void)
{
    ESP_LOGI(TAG,
             "@@OPEN_FX box=%u frame=%u oak_zoom=%u/%u text_slide=%u/%u breath=%u/%u dy=%d",
             s_opening.box, s_opening.frame,
             s_oak_zoom_frame, OAK_ZOOM_FRAMES,
             s_text_slide_frame, TEXT_SLIDE_FRAMES,
             s_oak_breath_frame, OAK_BREATH_FRAMES,
             oak_breath_offset());
}

static void draw_band(int band_y)
{
    screen_band_clear(GAME_UI_BG);
    #define Y(v) ((v) - band_y)

    char buf[64];
    if(s_handoff>12){
        game_ui_title(band_y,"大木博士","");
        if(s_oak_ok)draw_ui_scaled_centered(120,Y(92),&s_oak_art,8,8,0,OAK_PALETTE);
        game_ui_box(band_y,0,TEXT_BOX_Y,240,112);
        render_text(MARGIN,Y(176),"接下来，请选择",GAME_UI_INK);
        render_text(MARGIN,Y(200),"陪你出发的伙伴。",GAME_UI_INK);
        render_text(MARGIN,Y(232),"它们都在等着你！",GAME_UI_INK);
        game_ui_footer(band_y,"即将选择初始伙伴");
        if(s_handoff<=24)game_ui_fade_background(band_y,16-(s_handoff-12)*16/12);
        else if(s_handoff>84)game_ui_fade_background(band_y,(s_handoff-84)*16/12);
        screen_push_band(band_y);return;
    }

    buf[0] = '\0';
    if (s_opening.box < OPENING_BOXES) {
        buf[0] = (char)('1' + s_opening.box);
        buf[1] = '/';
        buf[2] = '7';
        buf[3] = '\0';
    }
    game_ui_title(band_y, "大木博士", buf);

    uint8_t mon = opening_show_mon(&s_opening);
    if (s_oak_ok && opening_show_oak(&s_opening)) {
        int scale_num = s_opening.box == 0 ? s_oak_zoom_frame : OAK_ZOOM_FRAMES;
        int center_x = mon ? SCR_W / 4 : SCR_W / 2;
        draw_ui_scaled_centered(center_x, Y(96), &s_oak_art,
                                scale_num, OAK_ZOOM_FRAMES,
                                oak_breath_offset(), OAK_PALETTE);
    }

    species_t sp;
    uint8_t sprite_size = 0;
    const uint8_t *spr = mon ? assets_front_sprite(mon, &sprite_size) : NULL;
    if (spr && assets_species(mon, &sp)) {
        uint16_t pal[4];
        assets_palette(sp.palette, pal);
        int rendered = sprite_size * 2;
        int y = 96 - rendered / 2;
        int x = SCR_W / 2 + (SCR_W / 2 - rendered) / 2;
        render_sprite_2bpp(x, Y(y), spr,
                           sprite_size, 2, pal);
    }

    // Both text bands refresh together throughout the short slide animation.
    // Keep typed lines within the frame even when A completes a box early.
    int text_offset = text_slide_offset();
    game_ui_box(band_y, 0, TEXT_BOX_Y, 240, 112);
    uint16_t left = s_opening.typed;
    uint8_t lines = opening_line_count(&s_opening);
    for (uint8_t i = 0; i < lines && left; i++) {
        const char *line = opening_line(&s_opening, i);
        uint16_t len = utf8_len(line);
        uint16_t visible = left < len ? left : len;
        utf8_prefix(buf, sizeof(buf), line, visible);
        if (TEXT_LINE_Y[i] + text_offset + GLYPH <= 264)
            render_text(MARGIN, Y(TEXT_LINE_Y[i] + text_offset), buf, GAME_UI_INK);
        left = left > len ? (uint16_t)(left - len) : 0;
    }

    game_ui_footer(band_y, "C继续 长按B跳过");

    #undef Y
    if(s_handoff)game_ui_fade_background(band_y,s_handoff*16/12);
    screen_push_band(band_y);
}

static void draw_all(void)
{
    for (int y = 0; y < SCR_H; y += BAND_H) draw_band(y);
}

static void draw_text_bands(void)
{
    draw_band(BAND_H * 2);
    draw_band(BAND_H * 3);
}

static void draw_top_bands(void)
{
    draw_band(0);
    draw_band(BAND_H);
}

static void redraw_for_dump(void) { draw_all(); }

static void tick(lv_timer_t *t)
{
    (void)t;
    if(s_handoff){
        if(s_handoff>=96){play_starter_prepare_intro();nav_go(PAGE_STARTER);return;}
        s_handoff++;draw_all();return;
    }
    uint16_t before = s_opening.typed;
    opening_tick(&s_opening);

    bool top_changed = false;
    bool text_changed = s_opening.typed != before;
    bool should_log = false;

    if (s_oak_zoom_frame < OAK_ZOOM_FRAMES) {
        s_oak_zoom_frame++;
        top_changed = true;
        should_log = true;
    }
    if (s_text_slide_frame < TEXT_SLIDE_FRAMES) {
        s_text_slide_frame++;
        text_changed = true;
        if (s_text_slide_frame == 1 ||
            s_text_slide_frame == TEXT_SLIDE_FRAMES) should_log = true;
    }
    if (s_opening.box == OAK_BREATH_BOX && !opening_typing(&s_opening) &&
        s_oak_breath_frame < OAK_BREATH_FRAMES) {
        s_oak_breath_frame++;
        top_changed = true;
        if (s_oak_breath_frame == 1 || s_oak_breath_frame == 6 ||
            s_oak_breath_frame == 15 || s_oak_breath_frame == 16 ||
            s_oak_breath_frame == OAK_BREATH_FRAMES) should_log = true;
    }

    if (top_changed) draw_top_bands();
    if (text_changed) draw_text_bands();
    if (should_log) log_fx();
}

void play_opening_enter(void)
{
    s_handoff=0;
    opening_init(&s_opening);
    s_oak_ok = assets_ui("oak", &s_oak_art);
    s_oak_zoom_frame = 1;
    s_text_slide_frame = 0;
    s_oak_breath_frame = 0;
    screen_set_redraw(redraw_for_dump);
    draw_all();
    log_fx();
    if (!s_oak_ok) ESP_LOGE(TAG, "oak UI asset missing or invalid");
    // 407 帧 / 30fps = 13.6s，与 sim 的时长说明一致。
    s_tick = lv_timer_create(tick, 1000 / 30, NULL);
    ESP_LOGI(TAG, "P0：开场 %u 框 %u 帧，文本宽 %dpx",
             OPENING_BOXES, opening_total_frames(), USABLE_W);
}

void play_opening_exit(void)
{
    if (s_tick) { lv_timer_delete(s_tick); s_tick = NULL; }
}

bool play_opening_screen_busy(void)
{
    return s_handoff || opening_typing(&s_opening) || s_oak_zoom_frame < OAK_ZOOM_FRAMES ||
           s_text_slide_frame < TEXT_SLIDE_FRAMES ||
           (s_opening.box == OAK_BREATH_BOX && s_oak_breath_frame < OAK_BREATH_FRAMES);
}

void play_opening_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{

    if (s_handoff) return;

    char key = 0;
    if (nav_confirm(btn, ev)) key = 'A';
    if (nav_return(btn, ev)) key = 'C';
    if (!key) return;

    opening_t before=s_opening;
    uint8_t before_box = s_opening.box;
    opening_press(&s_opening, key);
    if (s_opening.done) {
        if (!save_mark_opening_seen()) {
            ESP_LOGE(TAG, "opening flag save failed; starter commit will retry it");
        }
        if(world_needs_starter()){s_opening=before;s_handoff=1;draw_all();}
        else nav_go(PAGE_IDLE);
        return;
    }
    if (s_opening.box != before_box) {
        s_text_slide_frame = 0;
        s_oak_breath_frame = 0;
        log_fx();
    }
    draw_all();
}
