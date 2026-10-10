// main/play_collect.c —— 采集诊断页。
//
// ## 它曾经是采集器，现在是**观察窗**
//
// 原先这一页自己起 WiFi、自己定时扫描、自己吐 NDJSON。
// F9-① 把扫描收进 world.c 的独立任务之后，这一页不能再扫了 ——
// **两个所有者争同一个射频**：
//   · esp_wifi_init 第二次调返回 ESP_ERR_INVALID_STATE，
//     而这里原本没判那个返回值，会当成初始化失败
//   · 就算初始化绕过去，两边交替 esp_wifi_scan_start
//     会互相打断，谁的结果都不完整
//
// 所以现在它只读 world_snapshot()：显示扫描次数、AP 数、
// 感知判定的状态与地点。**采集本身在后台一直跑**，
// 不再依赖「设备停在这一页」——那正是 F9-① 要解决的问题
// （长跑采集原本必须守着这一页，一晚上的数据可能因为没人按键而全丢）。
//
// NDJSON 也搬去了 world.c，格式一字未改 —— tools/device/collect.py
// 照收不误。格式一致是 F5 对账的前提。

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "demo.h"
#include "ui_pixel.h"
#include "world.h"

static const char *TAG = "collect";

static lv_obj_t *s_scr;
static lv_obj_t *s_title, *s_stat, *s_last;
static lv_timer_t *s_tick;

static const char *state_name(sens_state_t st)
{
    switch (st) {
    case SENS_MOVING:  return "moving";
    case SENS_STAYING: return "staying";
    default:           return "unknown";
    }
}

static void ui_refresh(void)
{
    world_t w;
    world_snapshot(&w);

    // 只在 LVGL 任务里调 —— tick 就是 lv_timer 回调，安全。
    // world 任务那边**绝不碰 LVGL**（非线程安全，见 world.h 的线程契约）。
    lv_label_set_text_fmt(s_stat, "%" PRIu32 " scans  %u APs",
                          w.scans, w.last_ap_count);
    lv_label_set_text_fmt(s_last,
                          "%s\nplace #%u\nprogress %u%%\n"
                          "pet %u/%u/%u",
                          state_name(w.state), w.place_id, w.progress,
                          nurture_pct(w.pet.satiety),
                          nurture_pct(w.pet.mood),
                          nurture_pct(w.pet.stamina));
}

static void tick(lv_timer_t *t)
{
    (void)t;
    ui_refresh();
}

void play_collect_enter(void)
{
    s_scr = ui_pixel_screen_create("Collect");
    s_title = lv_label_create(s_scr);
    lv_label_set_text(s_title, world_wifi_ready() ? "WiFi fingerprint"
                                                  : "WiFi FAILED");
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 8, 40);

    s_stat = lv_label_create(s_scr);
    lv_obj_align(s_stat, LV_ALIGN_TOP_LEFT, 8, 70);

    s_last = lv_label_create(s_scr);
    lv_obj_set_width(s_last, 220);
    lv_obj_align(s_last, LV_ALIGN_TOP_LEFT, 8, 100);

    ui_refresh();
    lv_screen_load(s_scr);

    // 1 秒刷一次就够 —— 扫描 30 秒一次，刷太快只是白烧电。
    s_tick = lv_timer_create(tick, 1000, NULL);
}

void play_collect_exit(void)
{
    // 顺序有讲究：先停定时器再删屏。
    // 反过来的话 tick 可能在屏已删除后触发，访问野指针 ——
    // 上游 AGENTS.md 专门点了这条（「删 screen 前必须停止所有
    // 可能访问其 UI 的任务、定时器、回调」）。
    //
    // 注意**不再需要 esp_wifi_scan_stop** —— 扫描不归这一页管了，
    // 离开页面后台照常扫。
    if (s_tick) { lv_timer_delete(s_tick); s_tick = NULL; }
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

void play_collect_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn != BSP_BTN_OK) return;

    // 采集不再能从这里停 —— 它是后台常驻的。
    // 保留 OK 键只为打一行诊断，方便串口上确认后台还活着。
    world_t w;
    world_snapshot(&w);
    ESP_LOGI(TAG, "后台采集 %" PRIu32 " 次扫描，最近 %u 个 AP，%s，行程 %u%%",
             w.scans, w.last_ap_count, state_name(w.state), w.progress);
}
