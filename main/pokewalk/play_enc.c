// main/play_enc.c —— P2 遭遇列表。
//
// 对应 docs/pages/P2-encounter-list.md。
//
// ## 为什么要有列表这一层
//
// 遭遇是**后台累积**的（设备在兜里，屏幕不亮）。掏出来时可能攒了三五只，
// 直接进战斗就没得选。列表让玩家先扫一眼稀有度，决定处理顺序 ——
// 页面文档的原话是「这是 30 秒会话里最值钱的 3 秒」。
//
// ## 无动效
//
// 只在队列内容或选择变化时重画。200ms 检查后台快照，保证 FIFO 淘汰后
// 画面能跟上；绘图和选择始终使用同一份页面快照，不能按新下标误选旧行。
//
// ## 光标为什么是点阵不是字符
//
// 页面文档写的是 `▸`，但**字库里没有这个字形**（PingFang 不含它，
// convert_font.py 顶部记了原因）。用字符会渲染成空白，
// 而 render_text 静默跳过缺字 —— 屏幕上就是「光标不见了」。
// ui.bin 里的 cursor 是 sim/pixelart.py 算法生成的 5×9 三角。

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "assets.h"
#include "game_ui.h"
#include "battle.h"
#include "nav.h"
#include "music_director.h"
#include "play.h"
#include "render.h"
#include "screen.h"
#include "transition.h"
#include "world.h"

static const char *TAG = "p2";

#define BAND_H SCREEN_BAND_H
#define SCR_W SCREEN_W
#define SCR_H SCREEN_H

#define C_BLACK RGB_HEX(0x000000)

// 布局。**每个元素都不跨横带边界**（边界在 y=80/160/240）——
// 跨界会被两条带各画一半，刷新频率不同就撕裂（P1 栽过一次）。
//
// Title y8; eight rows at y40 + i*24; explanatory text y248; the shared
// GSC key frame occupies y280..320. Input and transition redraw every band.
#define ROW0_Y 40
#define ROW_H 24
#define VISIBLE_ROWS 8

static uint8_t s_sel;          // 选中第几条
static uint8_t s_top;          // 滚动窗口的第一条
static enc_queue_t s_view;     // One immutable queue for every band of a frame.
static enc_queue_t s_latest;   // Static scratch; no queue-sized LVGL stack copy.
#define REFRESH_TICK_MS 200
static lv_timer_t *s_refresh_tick;

// P2 → P3 遭遇转场。100ms 一拍推进 6 个原始 60fps 帧，实际时长与
// trans_frames() 一致；LVGL timer 只重画屏幕，不阻塞独立的 world 任务。
#define TRANS_TICK_MS 100
#define TRANS_FRAME_STEP 6
static lv_timer_t *s_trans_tick;
static trans_id_t s_trans_id;
static uint16_t s_trans_frame;
static uint16_t s_trans_q;
static bool s_trans_flash_black;
static bool s_trans_finish_hold;

static void draw_stars(int x, int y, uint8_t rarity, uint16_t fg)
{
    // ★☆ 都在字库里（convert_font.py 明确收了这两个）。
    // 不用点阵星星 —— 那是 S8 闪光特效用的，尺寸与文字不匹配。
    char buf[32];
    int n = 0;
    for (int i = 0; i < 5 && n < 28; i++) {
        const char *s = (i < rarity) ? "★" : "☆";
        memcpy(buf + n, s, 3);
        n += 3;
    }
    buf[n] = '\0';
    render_text(x, y, buf, fg);
}

static void overlay_transition(int band_y)
{
    uint16_t *band = screen_band();
    uint8_t gy0 = (uint8_t)(band_y / TRANS_TILE);
    uint8_t gy1 = (uint8_t)((band_y + BAND_H) / TRANS_TILE);

    for (uint8_t gy = gy0; gy < gy1; gy++) {
        int y0 = gy * TRANS_TILE - band_y;
        for (uint8_t gx = 0; gx < TRANS_GRID_W; gx++) {
            if (!trans_tile_covered(s_trans_id, s_trans_q, gx, gy)) continue;
            int x0 = gx * TRANS_TILE;
            for (int dy = 0; dy < TRANS_TILE; dy++) {
                for (int dx = 0; dx < TRANS_TILE; dx++) {
                    band[(y0 + dy) * SCR_W + x0 + dx] = C_BLACK;
                }
            }
        }
    }
}

static void draw_band(int band_y)
{
    screen_band_clear(GAME_UI_BG);
    #define Y(v) ((v) - band_y)

    const enc_queue_t *q = &s_view;
    char buf[64];
    species_t sp;

    // -- 标题 ----------------------------------------------------------
    snprintf(buf, sizeof(buf), "%u", q->count);
    game_ui_title(band_y, "刚才路上遇到", buf);

    // -- 列表 ----------------------------------------------------------
    if (q->count == 0) {
        render_text(12, Y(96), "队列是空的", GAME_UI_MUTED);
    }
    for (uint8_t i = 0; i < VISIBLE_ROWS; i++) {
        uint8_t idx = (uint8_t)(s_top + i);
        if (idx >= q->count) break;
        const encounter_t *e = &q->items[idx];
        int y = ROW0_Y + i * ROW_H;

        game_ui_list_marker(band_y, 8, y, idx, q->count, s_sel);

        // 物种名
        if (assets_species(e->species_id, &sp)) {
            snprintf(buf, sizeof(buf), "%.*s", sp.name_zh_len, sp.name_zh);
        } else {
            snprintf(buf, sizeof(buf), "#%03u", e->species_id);
        }
        render_text(q->count<=3?32:24, Y(y), buf, GAME_UI_INK);

        // 稀有度星 —— 右对齐到 x=200，名字最长 5 字（80px）不会撞
        draw_stars(112, Y(y), e->rarity, C_INK);

        // 闪光标记：star_7 + star_5 一大一小两颗星，替掉原来的「闪」字。
        // 星星是 ui.bin 的 2bpp 点阵；颜色走调用侧调色板，与素材解耦。
        // 行内垂直居中：24px 行高，7px 星 offset 8、5px 星 offset 9。
        if (e->is_shiny) {
            static const uint16_t STAR_PAL[4] = {
                C_INK, RGB_HEX(0xfff0a0), RGB_HEX(0xffffff), 0,
            };
            ui_art_t s7, s5;
            if (assets_ui("star_7", &s7)) {
                render_sprite_2bpp_wh(200, Y(y + (ROW_H - s7.h) / 2),
                                      s7.data, s7.w, s7.h, 1, STAR_PAL);
                if (assets_ui("star_5", &s5)) {
                    render_sprite_2bpp_wh(200 + s7.w + 2,
                                          Y(y + (ROW_H - s5.h) / 2),
                                          s5.data, s5.w, s5.h, 1, STAR_PAL);
                }
            }
        }
    }

    // -- 提示与三键 ------------------------------------------------------
    render_text(12, Y(248), "稀有度越高越难捕获", GAME_UI_MUTED);
    game_ui_footer(band_y, game_ui_list_hint(q->count));

    if (s_trans_flash_black) {
        screen_band_clear(C_BLACK);
    } else if (s_trans_q) {
        overlay_transition(band_y);
    }

    #undef Y
    screen_push_band(band_y);
}

static void draw_all(void)
{
    for (int y = 0; y < SCR_H; y += BAND_H) draw_band(y);
}

static void redraw_for_dump(void) { draw_all(); }

// Keep the chosen identity when FIFO shifts its index. If it disappeared,
// choose a valid visible row but let the next explicit A select that new row.
static bool refresh_list(void)
{
    world_queue_snapshot(&s_latest);
    if (s_latest.count == s_view.count &&
        memcmp(s_latest.items, s_view.items, s_view.count * sizeof(s_view.items[0])) == 0)
        return false;
    uint16_t selected_uid = s_sel < s_view.count ? s_view.items[s_sel].uid : 0;
    uint32_t selected_ts = s_sel < s_view.count ? s_view.items[s_sel].ts : 0;
    uint8_t next_sel = s_sel < s_latest.count ? s_sel : (s_latest.count ? s_latest.count - 1 : 0);
    for (uint8_t i = 0; i < s_latest.count; i++) {
        if (s_latest.items[i].uid == selected_uid && s_latest.items[i].ts == selected_ts) {
            next_sel = i;
            break;
        }
    }
    s_view = s_latest;
    s_sel = next_sel;
    if (s_top > s_sel) s_top = s_sel;
    if (s_sel >= s_top + VISIBLE_ROWS) s_top = s_sel - VISIBLE_ROWS + 1;
    return true;
}

static void refresh_tick(lv_timer_t *timer)
{
    (void)timer;
    // A transition keeps the selected list frame; P3 checks uid+ts again.
    if (!s_trans_tick && refresh_list()) draw_all();
}

static uint16_t covered_tiles(void)
{
    uint16_t n = 0;
    for (uint8_t gy = 0; gy < TRANS_GRID_H; gy++) {
        for (uint8_t gx = 0; gx < TRANS_GRID_W; gx++) {
            if (trans_tile_covered(s_trans_id, s_trans_q, gx, gy)) n++;
        }
    }
    return n;
}

static void transition_tick(lv_timer_t *timer)
{
    if (s_trans_finish_hold) {
        s_trans_tick = NULL;
        lv_timer_delete(timer);
        s_trans_flash_black = false;
        s_trans_q = 0;
        s_trans_finish_hold = false;
        s_trans_frame = 0;
        // 先清状态再切页；play_enc_exit 不会重复删除当前 timer。
        nav_go(PAGE_BATTLE);
        return;
    }

    uint16_t total = trans_frames(s_trans_id);
    uint16_t next = (uint16_t)(s_trans_frame + TRANS_FRAME_STEP);
    s_trans_frame = next < total ? next : total;

    uint16_t geom_start = trans_has_flash(s_trans_id) ? TRANS_FLASH_FRAMES : 0;
    if (s_trans_frame <= geom_start) {
        s_trans_q = 0;
        // Circle 系的 72 帧闪屏压成 6 帧一档，仍保持 1.2 秒总时长。
        s_trans_flash_black = (((s_trans_frame - 1) / TRANS_FRAME_STEP) & 1u) == 0;
    } else {
        s_trans_flash_black = false;
        s_trans_q = (uint16_t)((uint32_t)(s_trans_frame - geom_start) * 1000u /
                               (total - geom_start));
    }

    draw_all();
    ESP_LOGI(TAG, "@@TRANS id=%u frame=%u/%u q=%u tiles=%u/%u flash=%u",
             (unsigned)s_trans_id, s_trans_frame, total, s_trans_q,
             covered_tiles(), TRANS_GRID_W * TRANS_GRID_H,
             s_trans_flash_black ? 1u : 0u);

    // 满黑保留一拍再进 P3，避免最后一帧被新页面同一回调立刻覆盖。
    if (s_trans_frame == total) s_trans_finish_hold = true;
}

static void start_transition(void)
{
    music_director_play(MUSIC_WILD);
    const nav_ctx_t *c = nav_ctx();
    world_t w;
    world_snapshot(&w);

    uint8_t idx;
    uint8_t wild_level = (c->enc.level ? c->enc.level : battle_wild_level_for_pet(c->enc.rarity, w.level));
    // biome 顺序与 sensing 的 dwell_by_biome 一致：0 野外、4 交通枢纽。
    bool open_biome = c->enc.biome == 0 || c->enc.biome == 4;
    trans_pick(false, wild_level, w.level, open_biome, &idx);
    s_trans_id = trans_pick_encounter(c->enc.uid, c->enc.biome, wild_level >= w.level + 3);
    s_trans_frame = 0;
    s_trans_q = 0;
    s_trans_flash_black = false;
    s_trans_finish_hold = false;
    s_trans_tick = lv_timer_create(transition_tick, TRANS_TICK_MS, NULL);
    if (!s_trans_tick) {
        ESP_LOGE(TAG, "转场 timer 创建失败，直接进入战斗");
        nav_go(PAGE_BATTLE);
        return;
    }

    ESP_LOGI(TAG, "@@TRANS start id=%u idx=%u wild=%u pet=%u open=%u frames=%u",
             (unsigned)s_trans_id, idx, wild_level, w.level,
             open_biome ? 1u : 0u, trans_frames(s_trans_id));
}

void play_enc_enter(void)
{
    s_sel = 0;
    s_top = 0;
    memset(&s_view, 0, sizeof(s_view));
    refresh_list();
    s_refresh_tick = NULL;
    s_trans_tick = NULL;
    s_trans_frame = 0;
    s_trans_q = 0;
    s_trans_flash_black = false;
    s_trans_finish_hold = false;
    screen_set_redraw(redraw_for_dump);
    draw_all();
    s_refresh_tick = lv_timer_create(refresh_tick, REFRESH_TICK_MS, NULL);

    // **不做自动截图** —— P1 那个是在只有一页时加的，
    // 现在有了 dbg.c 的按键注入，截图由 walk.py 显式发 's' 触发。
    // 页面自己再截一张只会与之交错，让 PC 侧收到半张（踩过一次）。

    ESP_LOGI(TAG, "P2：队列 %u 条", s_view.count);
}

void play_enc_exit(void)
{
    if (s_refresh_tick) { lv_timer_delete(s_refresh_tick); s_refresh_tick = NULL; }
    if (s_trans_tick) { lv_timer_delete(s_trans_tick); s_trans_tick = NULL; }
    s_trans_flash_black = false;
    s_trans_q = 0;
    s_trans_finish_hold = false;
}

// A 单击 = 选中进战斗。主流程不依赖提示行没有说明的双击手势。
static void on_select(void)
{
    if (s_sel >= s_view.count) {
        if (refresh_list()) draw_all();
        return;
    }
    const encounter_t *shown = &s_view.items[s_sel];
    encounter_t current;
    if (!world_get_encounter_uid(shown->uid, &current) || current.ts != shown->ts) {
        if (refresh_list()) draw_all();
        return;
    }

    nav_ctx_t *c = nav_ctx();
    c->enc = current;
        c->exploring = false;
    c->uid = current.uid;
    c->valid = true;
    c->battled = false;
    c->battle_won = false;
    start_transition();
}

bool play_enc_screen_busy(void) { return s_trans_tick != NULL; }

void play_enc_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_trans_tick) return;
    if (nav_return(btn, ev)) { nav_go(PAGE_IDLE); return; }
    if (ev != BSP_BTN_CLICK) return;
    s_sel = nav_list_selection(btn, ev, s_view.count, s_sel);
    if (s_sel < s_top) s_top = s_sel;
    if (s_sel >= s_top + VISIBLE_ROWS) s_top = s_sel - VISIBLE_ROWS + 1;
    if (nav_list_activate(btn, ev, s_view.count)) { on_select(); return; }
    draw_all();
}
