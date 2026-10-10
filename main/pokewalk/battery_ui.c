#include "battery_ui.h"
#include "display_settings.h"
#include "bsp_battery.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "render.h"
#include "game_ui.h"
#include "screen.h"
#include "screen_idle.h"
#include "world.h"
#include <stdio.h>

static lv_timer_t *timer;
static int percent = -1;
static int64_t sampled_at;
static bool sampled;
static uint32_t playtime_minutes;

void battery_ui_playtime_text(char *out, size_t size)
{
    snprintf(out, size, "时长 %lu:%02lu", (unsigned long)(playtime_minutes / 60),
             (unsigned long)(playtime_minutes % 60));
}

static bool sample(void)
{
    if (screen_idle_is_off()) return false;
    uint32_t minutes = world_playtime_seconds() / 60;
    bool changed = minutes != playtime_minutes;
    playtime_minutes = minutes;
    if (!display_settings_battery_visible()) return changed;
    int64_t now = esp_timer_get_time();
    if (sampled && now - sampled_at < 10000000) return changed;
    int value = bsp_battery_soc();
    if (value < 0 || value > 100) value = -1;
    changed = changed || !sampled || value != percent;
    percent = value;
    sampled_at = now;
    sampled = true;
    return changed;
}

static void rect(int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++) screen_px(col, row, color);
}

void battery_ui_draw(int band_y, int right, int y)
{
    if (!display_settings_battery_visible()) return;
    char text[16];
    if (percent < 0) snprintf(text, sizeof(text), "--%%");
    else snprintf(text, sizeof(text), "%d%%", percent);
    int text_x = right - render_text_width(text);
    int x = text_x - 30;
    int top = y + 3 - band_y;
    uint16_t color = percent < 0 ? GAME_UI_MUTED : percent <= 20 ? C_HP_RED : GAME_UI_INK;
    rect(x, top, 22, 11, color);
    rect(x + 2, top + 2, 18, 7, GAME_UI_BG);
    rect(x + 22, top + 3, 2, 5, color);
    if (percent > 0) rect(x + 3, top + 3, (percent * 16 + 99) / 100, 5, color);
    render_text(text_x, y - band_y, text, color);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (sample()) screen_redraw_current();
}

void battery_ui_start(void)
{
    sample();
    if (!timer) timer = lv_timer_create(tick, 1000, NULL);
}

void battery_ui_stop(void)
{
    if (timer) lv_timer_delete(timer);
    timer = NULL;
    sampled = false;
}
