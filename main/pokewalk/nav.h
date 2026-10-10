// main/nav.h —— 页面导航。
//
// ## 为什么不用 main.c 的 DEMOS 表
//
// 那张表是**菜单式**的：进一项、长按 OK 退回菜单。上游 demo 就该这样。
// 但玩法是**链式**的：P1 →C→ P2 →A→ P3 →A→ P4 →抓到→ P6，
// 每一步都要带参数（P3 要知道打哪一只，P4 要知道打残到什么程度）。
//
// 塞进 DEMOS 表要么给每页一个全局变量传参（下一个人看不出数据从哪来），
// 要么把跳转逻辑摊到各页的 key 回调里（改一条路径要动三个文件）。
// 这里用一张显式的页面表 + 一个 nav_go()，跳转关系集中在一处。
//
// ## 生命周期契约
//
// nav_go(P) 会：当前页 exit() → 切 s_cur → 新页 enter()。
// **exit 必须先停掉自己的 lv_timer 再删屏** —— 反过来定时器会访问
// 已删除的对象（上游 AGENTS.md 专门点了这条，P1 的 exit 有示范）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bsp_button.h"
#include "encounter.h"

typedef enum {
    PAGE_IDLE = 0,      // P1 待机
    PAGE_ENCOUNTER,     // P2 遭遇列表
    PAGE_BATTLE,        // P3 战斗
    PAGE_CAPTURE,       // P4 捕获
    PAGE_DEX,           // P6 图鉴
    PAGE_OPENING,       // P0 开场（只在首次冷启动进入）
    PAGE_CARE,          // P5 照料
    PAGE_STARTER,       // P9 初始伙伴（P7/P8 尚未实现）
    PAGE_BAG,           // P10 道具背包
    PAGE_MENU,          // P11 主菜单与选项
    PAGE_PARTY,         // P12 随行队伍
    PAGE_TRAINER,       // P13 道馆与联盟挑战
    PAGE_ACHIEVEMENTS,   // P14 成就与奖励
    PAGE_EXPLORATION,    // P15 路线探索
    PAGE_DUNGEON,       // P16 isolated roguelike playtest
    PAGE_COUNT,
} page_id_t;

#define NAV_NOTE_NONE     0
#define NAV_NOTE_CAUGHT   1
#define NAV_NOTE_EVOLVED  2

// 切页。在 LVGL 任务里调（按键回调已经持锁，直接调即可）。
void nav_go(page_id_t p);
page_id_t nav_current(void);
// Menu drill-down keeps a bounded return path. nav_go starts a new path;
// nav_open pushes the displayed page, nav_back restores it or uses fallback.
void nav_open(page_id_t p);
void nav_back(page_id_t fallback);
bool nav_is_returning(void); // During enter(): retain the previous selection.

// All game pages use the same physical controls. DOWN long is exclusive of
// DOWN click (the BSP suppresses the click after a completed hold).
static inline int nav_direction(bsp_btn_t button, bsp_btn_ev_t event) {
    return event == BSP_BTN_CLICK ? (button == BSP_BTN_UP ? -1 : button == BSP_BTN_DOWN ? 1 : 0) : 0;
}
static inline bool nav_confirm(bsp_btn_t button, bsp_btn_ev_t event) {
    return button == BSP_BTN_OK && event == BSP_BTN_CLICK;
}
static inline bool nav_return(bsp_btn_t button, bsp_btn_ev_t event) {
    return button == BSP_BTN_DOWN && event == BSP_BTN_LONG;
}
// Every list uses A/B navigation and C confirmation, independent of length.
static inline unsigned nav_list_selection(bsp_btn_t button, bsp_btn_ev_t event, unsigned count, unsigned current) {
    return count ? (current + count + nav_direction(button, event)) % count : 0;
}
static inline bool nav_list_activate(bsp_btn_t button, bsp_btn_ev_t event, unsigned count) {
    return count && nav_confirm(button, event);
}

// 分发按键给当前页
void nav_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 进 P1（开机用）
void nav_start(void);

// 退出当前页（要去 demo 菜单时用）—— 不进新页，只做清理。
void nav_exit_current(void);
// User-triggered exit (including C-long in main) must finish locked animations.
// nav_exit_current itself remains unconditional for shutdown/cleanup.
bool nav_can_leave(void);
// Finite animations and timed outcomes keep the display on. Input choices,
// idle breathing and background refreshes do not prevent the idle timeout.
bool nav_screen_busy(void);

// ---------------------------------------------------------------------------
// 页面间传参
//
// 用一个显式的「当前处理中的遭遇」而不是各页自己存 —— P3 打完把 HP
// 写回这里，P4 读它算捕获窗口。链路上只有一条，不需要栈。
// ---------------------------------------------------------------------------

typedef struct {
    encounter_t enc;        // 正在处理的这一只（值拷贝，页面读它画）

    // **用 uid 而不是队列下标**认这一条。
    // 玩家在 P3/P4 期间后台还在往队列塞，淘汰会让下标整体左移 ——
    // 实测症状是「打的是 #64，抓到的是 #23」。见 encounter.h。
    uint16_t uid;
    bool valid;
    bool exploring; // Return route discoveries to P15 after the encounter.

    // P3 打完留下的：给 P4 用
    bool battled;
    bool battle_won;

    // 完成页留下的摘要类型。来源页在短暂停留期显示，随后由自己的
    // LVGL timer 自动导航；新完成事件覆盖旧值。
    uint8_t done_note;
} nav_ctx_t;

nav_ctx_t *nav_ctx(void);

void nav_end_encounter(void);
