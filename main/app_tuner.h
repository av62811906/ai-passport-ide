// main/app_tuner.h —— 尤克里里调音器应用入口(应用层,不属于 BSP)。
//
// 这是一个【派生应用】:它有自己的界面与交互,不复用基线 demo 菜单/测试页/像素风外壳。
// 硬件能力仍全部走 BSP(bsp_display/bsp_lvgl/bsp_button/bsp_audio/bsp_battery)。
#pragma once

#include <stdbool.h>

#include "bsp_button.h"

// 启动调音器:创建界面并(音频可用时)启动麦克风采集任务。
//
// 前置条件:bsp_display_init() 与 bsp_lvgl_init() 已成功。
// audio_ready 为 false 时进入"无信号"降级显示,不启动采集任务,界面其余部分照常可用。
// 内部自行获取/释放 LVGL 锁,可在 app_main 的任务上下文直接调用。
void app_tuner_start(bool audio_ready);

// 按键事件处理,由按键分发任务(非 LVGL 上下文)调用。
//
// 语义:上/下 = 手动选弦;确定 = 在自动/手动之间切换。
// 函数只改状态并加锁刷新界面,不做任何阻塞操作。
void app_tuner_handle_key(bsp_btn_t btn, bsp_btn_ev_t event);
