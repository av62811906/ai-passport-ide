// main/app_home.h —— 首页(应用入口页)。属于应用层,不属于 BSP。
//
// 首页是派生应用自定义的界面(暗色科技风),不复用基线 demo 菜单/测试页/像素风外壳。
// 它启动后列出可用功能(调音器、宝可梦游戏);加功能只需在功能表里追加一项。
#pragma once

#include <stdbool.h>

#include "bsp_button.h"

// 创建并载入首页。
//
// 前置条件:bsp_display_init() 与 bsp_lvgl_init() 已成功。
// 内部自行获取/释放 LVGL 锁,可在 app_main 的任务上下文直接调用。
void app_home_start(void);

// 删除首页界面及其定时器。
//
// 只允许在「持 LVGL 锁的换页序列」里调用(main.c 的 page_show_* 是唯一入口):
// 删除活动屏会让 disp->act_scr 变 NULL,若此刻刷新任务能跑,它会在
// lv_obj_update_layout(NULL) 上触发 LV_ASSERT_NULL(处理器是 while(1)),把 LVGL
// 任务永久停住;持锁调用时刷新插不进来,中间态就看不见。
// 内部自行获取/释放 LVGL 锁(锁可重入,可在外层已持锁时调用)。
void app_home_exit(void);

// 按键事件处理,由按键分发任务(非 LVGL 上下文)调用。
//
// 语义:上/下移动选中项;确定(单击)进入选中功能。
// 返回选中项下标(0=调音器,1=宝可梦);-1 表示本次按键不要求进入任何功能。
int app_home_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
