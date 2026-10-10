// main/app_pokemon.c —— 宝可梦像素游戏(PokeWalk)接入层实现(见 app_pokemon.h)。
//
// 线程模型:
//   * enter/exit 在按键分发任务里调用,内部自行加解锁;
//   * 一次性全局初始化(assets/world/sfx)在非 LVGL 上下文完成,避免和锁交叉;
//   * 游戏按键必须在持有 bsp_lvgl_lock() 时走 nav_key(),与游戏原本的
//     on_key→nav_key 契约一致。
#include "app_pokemon.h"

#include "bsp_display.h"
#include "esp_log.h"

#include "assets.h"
#include "audio_settings.h"
#include "display_settings.h"
#include "dungeon.h"
#include "encounter.h"
#include "nav.h"
#include "nurture.h"
#include "render.h"
#include "save.h"
#include "screen.h"
#include "screen_idle.h"
#include "sensing.h"
#include "sfx.h"
#include "world.h"

static const char *TAG = "pokemon";

static bool s_active;   // 游戏是否在前台
static bool s_booted;   // 一次性全局初始化是否已完成

// 熄屏判定的“忙”回调:只有游戏在前台时才让游戏页阻止超时。
static bool display_busy(void) { return s_active && nav_screen_busy(); }

// 只做一次:资产、渲染自检、后台世界(WiFi 唯一所有者)、显示/音频设置与音效。
// 不属于 LVGL 上下文,故放在锁外调用。
static void pokemon_boot_once(void) {
    if (s_booted) return;
    s_booted = true;

    assets_init();
    assets_selftest();
    sens_selftest();
    nurture_selftest();
    enc_selftest();
    render_init();
    render_selftest();

    bool world_ok = world_start();
    if (world_ok && !world_needs_starter()) dungeon_load();

    display_settings_init();
    audio_settings_init();
    sfx_start();
}

bool app_pokemon_enter(void) {
    pokemon_boot_once();

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败,宝可梦界面未创建");
        return false;
    }
    screen_own_display();
    s_active = true;
    if (!world_needs_starter()) {
        nav_start();                                  // 老档:回到上次进度
    } else {
        nav_go(save_opening_seen() ? PAGE_STARTER : PAGE_OPENING);  // 新档:开场/选伙伴
    }
    screen_idle_set_state_callback(world_playtime_set_paused);
    if (!screen_idle_init(display_busy)) {
        ESP_LOGE(TAG, "自动熄屏计时器创建失败");
    }
    bsp_lvgl_unlock();

    bsp_display_backlight(display_settings_brightness());
    ESP_LOGI(TAG, "进入 PokeWalk");
    return true;
}

void app_pokemon_exit(void) {
    if (bsp_lvgl_lock(1000)) {
        nav_exit_current();
        screen_set_overlay(NULL);
        sfx_music_play(MUSIC_NONE);
        screen_idle_deinit();
        screen_release_display();
        s_active = false;
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "获取 LVGL 锁失败,宝可梦界面未清理");
        s_active = false;
    }
    ESP_LOGI(TAG, "退出 PokeWalk");
}

bool app_pokemon_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!s_active) return false;
    // 双击“确定”:任何页面都能返回首页,与常规操作互不冲突。
    if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) return true;
    if (!bsp_lvgl_lock(500)) return false;
    // 待机页(P1,游戏主页)长按“确定”也返回首页。双击在不少宿主上不易触发
    // (桌面模拟器的虚拟按键只能发 CLICK/LONG),主页需要一个可达的出口。
    // 其它页面仍保留长按“确定”熄屏(见 screen_idle_filter_key)。
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG && nav_current() == PAGE_IDLE) {
        bsp_lvgl_unlock();
        return true;
    }
    nav_key(btn, ev);
    bsp_lvgl_unlock();
    return false;
}
