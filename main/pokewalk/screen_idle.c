#include "display_settings.h"
#include <stddef.h>
#include <stdatomic.h>
#include "screen_idle.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "screen.h"
#include "sfx.h"

#define IDLE_MS 60000u
static lv_timer_t *s_timer;
static bool (*s_busy)(void);
static int64_t s_last_activity;
static atomic_bool s_off;
static void (*s_state_changed)(bool off);

void screen_idle_set_state_callback(void (*changed)(bool off)) { s_state_changed = changed; }

static void set_off(bool off)
{
    bool changed = atomic_exchange(&s_off, off) != off;
    if (changed && s_state_changed) s_state_changed(off);
}
static uint8_t s_pressed, s_wake_gesture;
static atomic_uint s_cleanup;

void screen_idle_input_dropped(bsp_btn_t button, bsp_btn_ev_t event)
{
    if ((unsigned)button >= 3) return;
    unsigned bit = 1u << button;
    if (event == BSP_BTN_RELEASE) atomic_fetch_or(&s_cleanup, bit);
    else if (event == BSP_BTN_GESTURE_END) atomic_fetch_or(&s_cleanup, bit << 3);
}

static void apply_cleanup(void)
{
    unsigned pending = atomic_exchange(&s_cleanup, 0);
    s_pressed &= (uint8_t)~((pending & 7u) | (pending >> 3));
    s_wake_gesture &= (uint8_t)~(pending >> 3);
}

uint32_t screen_idle_timeout_ms(void) { return IDLE_MS; }
bool screen_idle_is_off(void) { return s_off; }
void screen_idle_note_activity(void) { s_last_activity = esp_timer_get_time(); }

void screen_idle_request_off(void)
{
    if (!s_timer || s_off) return;
    bsp_display_backlight(0);
    if (bsp_display_sleep(true) != ESP_OK) {
        // A partial DISPOFF must be undone before returning to the active page.
        if (bsp_display_sleep(false) == ESP_OK) bsp_display_backlight(display_settings_brightness());
        else {set_off(true);sfx_notify_state();} // Allow the next key to retry a failed wake.
        ESP_LOGE("screen_idle", "panel sleep failed");
        return;
    }
    set_off(true);
    sfx_notify_state();
    ESP_LOGI("screen_idle", "@@DISPLAY off timeout_ms=%u", IDLE_MS);
}

static void wake(void)
{
    if (bsp_display_sleep(false) != ESP_OK) {
        ESP_LOGE("screen_idle", "panel wake failed; keeping backlight off");
        return;
    }
    set_off(false);
    sfx_notify_state();
    // Repaint while the backlight is still dark, then reveal the current page.
    screen_redraw_current();
    bsp_display_backlight(display_settings_brightness());
    screen_idle_note_activity();
    ESP_LOGI("screen_idle", "@@DISPLAY on");
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    apply_cleanup();
    if (s_off) return;
    int64_t now = esp_timer_get_time();
    if (s_pressed || (s_busy && s_busy())) s_last_activity = now;
    else if (now - s_last_activity >= (int64_t)IDLE_MS * 1000)
        screen_idle_request_off();
}

bool screen_idle_init(bool (*busy)(void))
{
    if (s_timer) return true;
    s_busy = busy;
    s_off = false;
    s_pressed = s_wake_gesture = 0;
    atomic_store(&s_cleanup, 0);
    screen_idle_note_activity();
    s_timer = lv_timer_create(tick, 100, NULL);
    if (s_timer && s_state_changed) s_state_changed(false);
    return s_timer != NULL;
}

// 交还宿主界面前调用：停掉全局熄屏计时器；若屏幕当前是熄的，先唤醒面板，
// 免得宿主首页在「黑屏 + 背光 0」的状态下被建出来。
// 必须先解除 s_off（set_off(false)），否则 screen_push_band 会继续丢弃像素。
void screen_idle_deinit(void)
{
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_off) {
        if (bsp_display_sleep(false) == ESP_OK) {
            set_off(false);
            bsp_display_backlight(display_settings_brightness());
            ESP_LOGI("screen_idle", "@@DISPLAY on (release)");
        } else {
            ESP_LOGE("screen_idle", "panel wake failed while releasing to host");
        }
    }
    s_busy = NULL;
}

bool screen_idle_filter_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (!s_timer || (unsigned)button >= 3) return false;
    apply_cleanup();
    uint8_t bit = (uint8_t)(1u << button);
    screen_idle_note_activity();
    if (event == BSP_BTN_PRESS) {
        s_pressed |= bit;
        if (s_off) { s_wake_gesture |= bit; wake(); }
        return (s_wake_gesture & bit) != 0;
    }
    if (event == BSP_BTN_RELEASE) {
        s_pressed &= (uint8_t)~bit;
        // CLICK/DOUBLE may arrive later. Keep the wake guard until the actual
        // classifier ends the gesture; a guessed timeout can leak late input.
        return true;
    }
    if (event == BSP_BTN_GESTURE_END) {
        s_pressed &= (uint8_t)~bit;
        s_wake_gesture &= (uint8_t)~bit;
        return true;
    }
    if (event != BSP_BTN_CLICK && event != BSP_BTN_DOUBLE && event != BSP_BTN_LONG)
        return false;
    // Serial/preview semantic inputs need no preceding physical PRESS event.
    if (s_off) { wake(); return true; }
    if (s_wake_gesture & bit) return true;
    if (button == BSP_BTN_OK && event == BSP_BTN_LONG) {
        screen_idle_request_off();
        return true;
    }
    return false;
}
