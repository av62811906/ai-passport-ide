// tools/simulator/host/sim.c
// Simulator entry points: boot LVGL, start the application's home page, drive
// its clock, and forward virtual key events. See sim.h for the ABI contract.
#include "sim.h"
#include "sim_internal.h"

#include "app_home.h"
#include "app_pokemon.h"
#include "app_tuner.h"
#include "bsp_display.h"
#include "lvgl.h"

// 页面路由与固件 main.c 一致:首页返回选中项下标(0=调音器,1=宝可梦),
// 调音器"长按确定"返回首页,宝可梦"双击确定"返回首页。
typedef enum {
    SIM_PAGE_HOME = 0,
    SIM_PAGE_TUNER,
    SIM_PAGE_POKEMON,
} sim_page_t;

#define SIM_HOME_ITEM_TUNER   0
#define SIM_HOME_ITEM_POKEMON 1

static int s_initialized;
static sim_page_t s_page = SIM_PAGE_HOME;
static bool s_audio_ready;

int sim_init(int audio_ready) {
    if (s_initialized) return 0;

    lv_init();
    if (sim_display_init() != 0) return -1;

    // audio_ready != 0 starts the tuner's capture task (fed by sim_audio_push);
    // otherwise the tuner runs its on-device "no signal" path.
    s_audio_ready = audio_ready != 0;
    s_page = SIM_PAGE_HOME;
    app_home_start();

    s_initialized = 1;
    return 0;
}

void sim_shutdown(void) {
    if (!s_initialized) return;
    // Intentionally do NOT call lv_deinit(): when audio is enabled the capture
    // task keeps running on its own thread and may touch LVGL concurrently. The
    // IDE stops the microphone first and then exits the process, which reclaims
    // everything; tearing LVGL down here would race that thread and crash.
    s_initialized = 0;
}

void sim_step(uint32_t elapsed_ms) {
    if (!s_initialized) return;
    // The IDE owns time: advancing the tick here makes the UI pause/freeze when
    // the refresh loop stops. The capture task touches LVGL on its own thread,
    // so the handler runs under the same recursive lock the app uses.
    if (!bsp_lvgl_lock(1000)) return;
    lv_tick_inc(elapsed_ms);
    lv_timer_handler();
    bsp_lvgl_unlock();
}

void sim_push_button(int btn, int ev) {
    if (!s_initialized) return;
    const bsp_btn_t button = (bsp_btn_t)btn;
    const bsp_btn_ev_t event = (bsp_btn_ev_t)ev;

    if (s_page == SIM_PAGE_HOME) {
        // 换页约束与固件 main.c 完全一致(见那里的两条 LVGL 硬约束注释):
        // 调音器是有控件的界面,必须「同一把锁内先拆首页、再建调音器」;
        // 游戏页不建 LVGL 控件、不占池子,可以先建后拆。
        const int item = app_home_handle_key(button, event);
        if (item == SIM_HOME_ITEM_TUNER) {
            if (!bsp_lvgl_lock(1000)) return;
            app_home_exit();
            app_tuner_start(s_audio_ready);
            bsp_lvgl_unlock();
            s_page = SIM_PAGE_TUNER;
        } else if (item == SIM_HOME_ITEM_POKEMON) {
            s_page = SIM_PAGE_POKEMON;
            if (!app_pokemon_enter()) {
                s_page = SIM_PAGE_HOME;   // 首页从未被销毁,留在原地即可
                return;
            }
            app_home_exit();
        }
        return;
    }

    if (s_page == SIM_PAGE_POKEMON) {
        // 双击"确定"返回首页,与固件 main.c 的宝可梦分支一致。
        // 游戏页不删共用屏,活动屏始终有效,按常规顺序退出再建首页。
        if (app_pokemon_handle_key(button, event)) {
            app_pokemon_exit();
            s_page = SIM_PAGE_HOME;
            app_home_start();
        }
        return;
    }

    if (button == BSP_BTN_OK && event == BSP_BTN_LONG) {
        // 与固件一致:同一把锁内先拆调音器(腾空 LVGL 池),再建首页。
        if (bsp_lvgl_lock(1000)) {
            app_tuner_exit();
            app_home_start();
            bsp_lvgl_unlock();
        }
        s_page = SIM_PAGE_HOME;
        return;
    }
    app_tuner_handle_key(button, event);
}
