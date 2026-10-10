// main/app_pokemon.h —— 宝可梦像素游戏(PokeWalk)的入口封装。
//
// 游戏源码整体移植在 main/pokewalk/ 下,本文件只负责把它接进宿主的页面路由:
//   * 首次进入时才做全局初始化(资产/渲染/后台世界/音效),避免拖慢只玩调音器的启动;
//   * 进入时接管面板像素输出(screen_own_display),并启动游戏自己的熄屏计时器;
//   * 退出时先停页、停熄屏计时器,再把面板交还 LVGL(screen_release_display)。
#pragma once

#include <stdbool.h>

#include "bsp_button.h"

// 进入游戏并显示首个玩法页(新档走开场/选伙伴,老档回到上次进度)。
// 内部自行获取/释放 LVGL 锁,可在 app_main/按键分发任务上下文调用。
// 返回 false 表示界面未能建立(例如 LVGL 锁超时),游戏不在前台。
// 此时调用方必须回退到首页 —— 否则会停在「旧画面 + 按键全失效」的死路上
// (app_pokemon_handle_key 在 !s_active 时丢弃一切按键,连退出都触发不了)。
bool app_pokemon_enter(void);

// 退出游戏:停当前页、交还面板与熄屏计时器。切换到首页前必须调用。
void app_pokemon_exit(void);

// 按键处理,由按键分发任务(非 LVGL 上下文)调用。
// 返回 true 表示用户要求退出游戏回到首页(双击“确定”)。
bool app_pokemon_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
