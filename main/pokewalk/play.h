// main/play.h —— 本项目玩法的入口声明。
//
// 与 demo.h 分开：demo.h 是上游 BSP 参考示例的接口，
// 跟着上游走；play.h 是我们自己的东西。混在一起的话
// 下次同步上游会冲突。
#pragma once


#include <stdbool.h>
#include <stdint.h>
#include "bsp_button.h"

bool play_trainer_growth_ready(void);

// P0 开场页（S11/S16）—— 仅首次冷启动进入，大木博士 7 框台词。
void play_opening_enter(void);
void play_opening_exit(void);
void play_opening_key(bsp_btn_t btn, bsp_btn_ev_t ev);
bool play_opening_screen_busy(void);

// P9 选择初始伙伴：御三家或皮卡丘，保存成功后才进入 P1。
void play_starter_prepare_intro(void);
void play_starter_enter(void);
void play_starter_exit(void);
void play_starter_key(bsp_btn_t btn, bsp_btn_ev_t ev);
bool play_starter_screen_busy(void);

// WiFi 指纹采集器（play_collect.c）。
//
// 先做采集而不是游戏，因为两件事卡在缺数据上：
// 野外 biome 死代码需要真实户外采集、口袋 RSSI 基线未标定。
// 详见 play_collect.c 顶部。
void play_collect_enter(void);
void play_collect_exit(void);
void play_collect_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// P1 待机页（play_idle.c）—— roadmap 的 F8「最小可玩闭环」。
// 主宠 back sprite + 呼吸动效 + 三条轴 + 三键提示。
void play_idle_enter(void);
void play_idle_exit(void);
void play_idle_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// P5 照料页（play_care.c）—— 喂食 / 玩耍 / 休息三级线性菜单。
void play_care_enter(void);
void play_care_exit(void);
void play_care_key(bsp_btn_t btn, bsp_btn_ev_t ev);
bool play_care_screen_busy(void);

// P10 道具背包：A 使用 / B 下一项 / C 返回来源页 / 长按 B 上一项。
void play_bag_enter(void);
void play_bag_exit(void);
void play_bag_key(bsp_btn_t btn, bsp_btn_ev_t ev);
uint8_t play_bag_selected_item(void); // Read-only native/test presentation state.

// P11 金银式主菜单；选项只修改展示偏好，不含开发调试入口。
void play_menu_enter(void);
void play_menu_exit(void);
void play_menu_key(bsp_btn_t btn, bsp_btn_ev_t ev);
typedef struct {
    uint8_t selected, option_selected;
    bool options;
} play_menu_view_t;
void play_menu_presentation_snapshot(play_menu_view_t *out);

// P12 六人队伍与详情。成员交换由 world 保存成功后发布。
void play_party_enter(void);
void play_party_exit(void);
void play_party_key(bsp_btn_t btn, bsp_btn_ev_t ev);
typedef struct {
    uint8_t selected;
    bool details;
    bool box, skills;
    uint8_t box_row;
    uint8_t box_matches, box_slot, box_menu, box_filter, box_type, box_sort;
    bool box_paging;
    bool release_confirm;
    uint8_t release_choice;
    uint16_t skill_selected, skill_id, skill_count, skill_enabled_count;
    bool skill_enabled;
    uint16_t species;
    const char *feedback;
} play_party_view_t;
void play_party_presentation_snapshot(play_party_view_t *out);

// P2 遭遇列表（play_enc.c）—— 队列的出口。
// 遭遇是后台攒的，这一页让玩家先扫一眼稀有度再决定处理顺序。
void play_enc_enter(void);
void play_enc_exit(void);
void play_enc_key(bsp_btn_t btn, bsp_btn_ev_t ev);
bool play_enc_screen_busy(void);

// P3 战斗（play_battle.c）—— 自动战斗，逐回合播放。
// 它是决策点不是走廊：打残了捕获窗口才宽（「先打再抓」的策略性）。
void play_battle_enter(void);
void play_battle_exit(void);
void play_battle_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// Read-only presentation values; the renderer and native inspector share this
// sample so tests observe the displayed HP/EXP, not a duplicate approximation.
typedef struct {
    const char *phase;
    uint16_t pet_hp, wild_hp;
    uint32_t exp;
    uint8_t level;
    int16_t pet_dx, wild_dx;
    uint8_t wild_frame, shiny_side;
    uint16_t move_id;
    bool by_pet;
} play_battle_view_t;
void play_battle_presentation_snapshot(play_battle_view_t *out);
bool play_battle_can_leave(void);
bool play_battle_screen_busy(void);

// P4 捕获（play_capture.c）—— 时机判定。
// 四个乘数（种族/心情/球种/打残）在这一页汇合。
void play_capture_enter(void);
void play_capture_exit(void);
void play_capture_key(bsp_btn_t btn, bsp_btn_ev_t ev);
bool play_capture_can_leave(void);
bool play_capture_screen_busy(void);

// P6 图鉴（play_dex.c）—— 收集的展示面。未捕获画剪影。
void play_dex_enter(void);
void play_dex_exit(void);
void play_dex_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void play_trainer_enter(void);
void play_trainer_exit(void);
void play_trainer_key(bsp_btn_t button,bsp_btn_ev_t event);
bool play_trainer_screen_busy(void);
unsigned play_trainer_mode(void);

void play_achievements_enter(void);
void play_achievements_exit(void);
void play_achievements_key(bsp_btn_t, bsp_btn_ev_t);

void play_exploration_enter(void);
void play_exploration_exit(void);
void play_exploration_key(bsp_btn_t,bsp_btn_ev_t);
bool play_exploration_screen_busy(void);

unsigned play_trainer_sendout_mask(void);
unsigned play_trainer_shiny_side(void);

void play_trainer_open_route(uint8_t route);
