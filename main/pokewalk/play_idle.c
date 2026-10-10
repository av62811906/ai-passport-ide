// main/play_idle.c —— P1 待机页（F8：最小可玩闭环）。
//
// 对应 docs/pages/P1-idle.md。
//
// ## 为什么整页自己画，不用 LVGL 的 label
//
// 第一版用 lv_label_set_text 画文字，真机上**所有汉字都是空心方框** ——
// LVGL 内置字体没有中文字形。而我明明写了 render_text（读 font16.bin
// 的 650 个字形）并跑了自检，自检还通过了 —— 但页面一次都没调它。
//
// 「自检通过 ≠ 画对了」。教训记在这里：**渲染类的东西必须看屏幕**，
// 串口日志与单元自检都证明不了像素对。
//
// 现在整页是一张 canvas，文字与 sprite 全部走 render.c。
//
// ## 分块横带渲染
//
// 整屏 240×320×2 = 150KB，占 231KB 可用堆的 65% —— 太冒险
// （WiFi 栈与 LVGL 还要用）。改成 240×80 的横带（37.5KB），
// 画 4 次推 4 次。
//
// 这正是 docs/01-constitution.md 那条「无法整帧缓冲，必须分块渲染」
// 的落地 —— 那条约束一直写在纸上，到这里才第一次真的执行。
//
// 金银白底：完整名称/等级在标题行，心情/亲密度在 y40 两端。
// 96px 背图居中于 y58；呼吸只影响带 0/1。三条养成轴完整落在
// 带 2，体能恢复提示与 y280 消息框落在带 3，文字不跨脏带边界。

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"    // 只用 lv_timer 与空屏对象，绘制全走 screen.c

#include "assets.h"
#include "game_ui.h"
#include "nav.h"
#include "world.h"
#include "exp.h"
#include "bsp_display.h"
#include "play.h"
#include "render.h"
#include "screen.h"
#include "screen_idle.h"

static const char *TAG = "idle";

// 横带尺寸走 screen.h —— 那里也是截图与字节序的归属地
#define BAND_H SCREEN_BAND_H
#define SCR_W SCREEN_W
#define SCR_H SCREEN_H

// 呼吸序列 —— 与 sim/effects.py 的 breath_sequence(8, rise=2) 逐值相同。
// 只向上浮动：实测 151 只 back 的下方留白 137 只都只有 4 行。
static const int8_t BREATH[] = {0, 0, -1, -2, -2, -2, -1, 0};
#define BREATH_FRAMES (sizeof(BREATH) / sizeof(BREATH[0]))

static lv_timer_t *s_tick;
static uint8_t s_breath_i;

#define SPRITE_Y 58
#define SPRITE_SIZE 96
#define MOOD_Y 40
#define AXIS_Y0 168
#define AXIS_STEP 24
#define AXIS_Y(i) (AXIS_Y0 + (i) * AXIS_STEP)
#define EXP_Y 240
#define PROGRESS_Y 260
#define TEXT_H 16
#define BAR_H 16
#define AXIS_BAR_X 88

// 呼吸的完整范围 y52..153 位于带 0/1；两条带始终一起刷新。
SCREEN_ASSERT_ALLOW_CROSS_BAND(idle_sprite, SPRITE_Y - 6, SPRITE_SIZE + 6);
SCREEN_ASSERT_WITHIN_BAND(idle_mood, MOOD_Y, TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(idle_satiety_label, AXIS_Y(0), TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(idle_satiety_bar, AXIS_Y(0), BAR_H);
SCREEN_ASSERT_WITHIN_BAND(idle_mood_label, AXIS_Y(1), TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(idle_mood_bar, AXIS_Y(1), BAR_H);
SCREEN_ASSERT_WITHIN_BAND(idle_stamina_label, AXIS_Y(2), TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(idle_stamina_bar, AXIS_Y(2), BAR_H);
SCREEN_ASSERT_WITHIN_BAND(idle_progress_label, PROGRESS_Y, TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(idle_progress_bar, PROGRESS_Y, BAR_H);
SCREEN_ASSERT_WITHIN_BAND(idle_exp, EXP_Y, TEXT_H);
SCREEN_ASSERT_WITHIN_BAND(idle_footer, 280, 40);

// 世界快照。每次重画前刷一次 —— **一帧之内不再变**，
// 否则同一帧里四条轴可能读到不同时刻的值（后台任务随时在改）。
static world_t s_w;
static uint8_t s_action;
static bool s_shiny;
// 每条横带使用同一世界快照，所有坐标都相对整屏。
static void draw_band(int band_y, int8_t breath)
{
    screen_band_clear(GAME_UI_BG);
    #define Y(v) ((v) - band_y)

    char buf[64], level[16];
    species_t sp;
    bool has_species = assets_species(s_w.species, &sp);
    if (has_species) {
        snprintf(buf, sizeof(buf), "%.*s", sp.name_zh_len, sp.name_zh);
    } else {
        snprintf(buf, sizeof(buf), "#%03u", s_w.species);
    }
    snprintf(level, sizeof(level), "Lv%u", s_w.level);
    game_ui_title(band_y, buf, level);

    // 亲密度与长名称分行；两端的小信息不进入居中的精灵区域。
    snprintf(buf, sizeof(buf), "%u", nurture_pct(s_w.pet.intimacy));
    int width = render_text_width(buf);
    render_text(228 - width, Y(MOOD_Y), buf, GAME_UI_INK);
    ui_art_t heart;
    if (assets_ui("heart", &heart)) {
        static const uint16_t HEART_PAL[4] = {
            GAME_UI_INK, GAME_UI_ACCENT, GAME_UI_BG, 0,
        };
        render_sprite_2bpp_wh(228 - width - 4 - heart.w,
                              Y(MOOD_Y + (TEXT_H - heart.h) / 2),
                              heart.data, heart.w, heart.h, 1, HEART_PAL);
    }
    static const char *MOOD[4] = {"愉快", "平静", "低落", "消沉"};
    render_text(12, Y(MOOD_Y), MOOD[nurture_mood(&s_w.pet)], GAME_UI_MUTED);

    sprite_asset_t spr;
    if (has_species && assets_back_sprite_info(s_w.species, &spr) &&
        spr.w == spr.h && (spr.w == 32 || spr.w == 48)) {
        uint16_t pal[4];
        assets_palette_variant(sp.palette, s_shiny, pal);
        // RBY 32@3 / GSC 48@2 share the same 96px display box.
        game_ui_sprite_centered(band_y, (SCR_W - SPRITE_SIZE) / 2,
                                 SPRITE_Y + breath * 3, SPRITE_SIZE, SPRITE_SIZE,
                                 spr.data, spr.w, spr.h, SPRITE_SIZE / spr.w, pal);
    }

    static const char *AXIS[3] = {"饱食", "心情", "体能"};
    const uint8_t value[3] = {
        nurture_pct(s_w.pet.satiety), nurture_pct(s_w.pet.mood),
        nurture_stamina_points(&s_w.pet),
    };
    for (int i = 0; i < 3; i++) {
        int y = AXIS_Y(i);
        render_text(12, Y(y), AXIS[i], GAME_UI_INK);
        game_ui_meter(band_y, AXIS_BAR_X, y, 228 - AXIS_BAR_X, value[i]);
    }

    uint32_t got, need; exp_progress(s_w.exp, s_w.level, &got, &need);
    unsigned xp = s_w.level >= LEVEL_MAX ? 100 : (unsigned)((uint64_t)got * 100 / need);
    if (xp > 100) xp = 100;
    render_text(12, Y(EXP_Y), "经验", GAME_UI_INK);
    game_ui_meter(band_y, AXIS_BAR_X, EXP_Y, 80, xp);
    if (s_w.level >= LEVEL_MAX) snprintf(buf, sizeof(buf), "满级");
    else snprintf(buf, sizeof(buf), "%u%%", xp);
    render_text(228-render_text_width(buf), Y(EXP_Y), buf, GAME_UI_INK);
    unsigned wait=nurture_wait_minutes(&s_w.pet,100);
    if(!wait)snprintf(buf,sizeof(buf),"体能已满 可以去探索");
    else snprintf(buf,sizeof(buf),"体能回满约%u分钟",wait);
    game_ui_text_centered(band_y,12,PROGRESS_Y,216,16,buf,GAME_UI_MUTED);

    static const char *const actions[] = {"照料", "菜单", "遭遇"};
    game_ui_actions(band_y, actions, 3, s_action);
    #undef Y
    screen_push_band(band_y);
}

static void draw_all(int8_t breath)
{
    for (int y = 0; y < SCR_H; y += BAND_H) draw_band(y, breath);
}

// 截图用的重画 —— screen_dump 会调它，每条带 push 时被吐到串口。
static void redraw_for_dump(void)
{
    draw_all(BREATH[s_breath_i]);
}

// 呼吸只影响精灵所在的带。精灵占 y=52~154（含向上浮动 6px），
// 落在第 0 条带（0~79）与第 1 条带（80~159）——
// 页面文档说「状态栏与三条轴是静态的，不参与逐帧重绘」。
static void draw_sprite_bands(int8_t breath)
{
    draw_band(0, breath);
    draw_band(BAND_H, breath);
}

static void tick(lv_timer_t *t)
{
    (void)t;

    // 刷快照。**养成结算不在这里** —— 它挪进了 world 任务，
    // 因为页面不在前台时 lv_timer 根本不跑，宠物的时间不该因此停住。
    // 这里只是把后台算好的值取一份出来画。
    world_t before = s_w;
    world_snapshot(&s_w);
    bool axes_changed = nurture_pct(before.pet.satiety) != nurture_pct(s_w.pet.satiety) ||
                        nurture_pct(before.pet.mood) != nurture_pct(s_w.pet.mood) ||
                        nurture_stamina_points(&before.pet) != nurture_stamina_points(&s_w.pet);

    s_breath_i = (uint8_t)((s_breath_i + 1) % BREATH_FRAMES);

    // 精灵带每拍都画（呼吸）。
    draw_sprite_bands(BREATH[s_breath_i]);

    // 底部带（240~319）也每拍画 —— 遭遇角标要闪，
    // 它的相位跟着 s_breath_i 走（见 draw_band 里那段）。
    // 没有待处理遭遇时这一带是静态的，但每拍重画 37.5KB 的代价
    // 与精灵带同量级，不值得为省它加一层判断。
    draw_band(BAND_H * 3, BREATH[s_breath_i]);

    // 三条养成轴的显示值变化时立即补带 2，使局部刷新与当前快照一致。
    if (axes_changed) draw_band(BAND_H * 2, BREATH[s_breath_i]);

    // 每 2 分钟打一行三条轴 —— **长跑观测用**。
    //
    // 为什么不用截图观测：截图靠开机 1 秒后那次自动触发，
    // 而要再截一张就得复位，复位就 nurture_init 了 ——
    // 把要测的状态本身清掉。日志没这个问题，设备自己跑就行。
    //
    // 2 分钟的间隔够看出变化：饱食 4/小时 = 0.13/2分钟，
    // 一小时后累计 4 格，用 tools/device/decay.py 拟合斜率。
    static uint16_t log_n;
    if (++log_n >= 480) {                // 480 × 250ms = 2 分钟
        log_n = 0;
        ESP_LOGI(TAG, "@@AXES %lld %u %u %u %u",
                 (long long)esp_timer_get_time(),
                 nurture_pct(s_w.pet.satiety), nurture_pct(s_w.pet.mood),
                 nurture_stamina_points(&s_w.pet), nurture_pct(s_w.pet.intimacy));
    }
}

void play_idle_enter(void)
{
    // **不建屏**。LVGL 那张空屏由 screen_own_display() 建一次，
    // 五个页面共用 —— 每页新建/载入会让 LVGL 刷一遍它自己的空背景，
    // 那正是切页时闪的那一下（见 screen.h）。

    if (!nav_is_returning()) s_action = 0;
    s_breath_i = 0;
    world_snapshot(&s_w);          // 先取一份，别用零值画第一帧
    world_party_t party;
    world_party_snapshot(&party);
    s_shiny = party.count && (party.members[0].flags & 1u);
    screen_set_redraw(redraw_for_dump);
    draw_all(0);
    s_tick = lv_timer_create(tick, 250, NULL);   // 4 fps

    // 进页面 1 秒后自动截一张 —— 让 PC 侧不用等人按键就能看到画面。
    //
    // 渲染类问题必须看屏幕，而拍照要人在场。自动截图让这个环节
    // 完全自助：烧写 → 等 10 秒 → screenshot.py 收图 → 我自己判断。
    // 一次性的（lv_timer_create 后立刻 set_repeat_count 1）。
    // **不做自动截图** —— P1 那个是在只有一页时加的，
    // 现在有了 dbg.c 的按键注入，截图由 walk.py 显式发 's' 触发。
    // 页面自己再截一张只会与之交错，让 PC 侧收到半张（踩过一次）。

    ESP_LOGI(TAG, "P1：#%u Lv%u  提示行 %d px  横带 %dx%d×%d 条",
             s_w.species, s_w.level,
             render_text_width("上下选择 确认执行"),
             SCR_W, BAND_H, SCR_H / BAND_H);
}

void play_idle_exit(void)
{
    // 停定时器。**不删屏** —— 那张 LVGL 空屏是五页共用的
    // （screen_own_display 建的），删了下一页就没得载。
    //
    // 顺序仍然重要：tick 会调 draw_all() 碰帧缓冲，
    // 离开页面前必须先停掉（上游 AGENTS.md 那条的实质是
    // 「别让回调在它依赖的东西之后还活着」）。
    if (s_tick) { lv_timer_delete(s_tick); s_tick = NULL; }
}

void play_idle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (nav_direction(btn, ev)) {
        s_action = nav_list_selection(btn, ev, 3, s_action);
        draw_band(240, BREATH[s_breath_i]); return;
    }
    if (!nav_confirm(btn, ev)) return;
    if (s_action == 0) nav_open(PAGE_CARE);
    else if (s_action == 1) nav_open(PAGE_MENU);
    else nav_go(PAGE_ENCOUNTER);
}
