// main/nav.c —— 页面导航。设计说明见 nav.h。

#include <stddef.h>

#include "esp_log.h"

#include "nav.h"
#include "play.h"
#include "dungeon.h"
#include "world.h"
#include "screen.h"
#include "screen_idle.h"
#include "music_director.h"
#include "growth_ui.h"
#include "evolution_ui.h"
#include "battery_ui.h"

static const char *TAG = "nav";

typedef struct {
    const char *name;
    void (*enter)(void);
    void (*exit)(void);
    void (*key)(bsp_btn_t, bsp_btn_ev_t);
} page_t;

// **顺序必须与 page_id_t 一致** —— 用下标索引，错位不会报错，
// 只会「按 C 进了图鉴」。加页面时两处一起改。
static const page_t PAGES[PAGE_COUNT] = {
    [PAGE_DUNGEON] = {"P16 秘境", play_dungeon_enter, play_dungeon_exit, play_dungeon_key},
    [PAGE_EXPLORATION] = {"P15 探索", play_exploration_enter, play_exploration_exit, play_exploration_key},
    [PAGE_ACHIEVEMENTS] = {"P14 成就", play_achievements_enter, play_achievements_exit, play_achievements_key},
    [PAGE_TRAINER] = {"P13 挑战", play_trainer_enter, play_trainer_exit, play_trainer_key},
    [PAGE_IDLE]      = {"P1 待机", play_idle_enter, play_idle_exit,
                        play_idle_key},
    [PAGE_ENCOUNTER] = {"P2 遭遇", play_enc_enter, play_enc_exit,
                        play_enc_key},
    [PAGE_BATTLE]    = {"P3 战斗", play_battle_enter, play_battle_exit,
                        play_battle_key},
    [PAGE_CAPTURE]   = {"P4 捕获", play_capture_enter, play_capture_exit,
                        play_capture_key},
    [PAGE_DEX]       = {"P6 图鉴", play_dex_enter, play_dex_exit,
                        play_dex_key},
    [PAGE_OPENING]   = {"P0 开场", play_opening_enter, play_opening_exit,
                        play_opening_key},
    [PAGE_CARE]      = {"P5 照料", play_care_enter, play_care_exit,
                        play_care_key},
    [PAGE_STARTER]   = {"P9 初始伙伴", play_starter_enter, play_starter_exit,
                        play_starter_key},
    [PAGE_BAG]       = {"P10 背包", play_bag_enter, play_bag_exit,
                        play_bag_key},
    [PAGE_MENU]      = {"P11 菜单", play_menu_enter, play_menu_exit,
                        play_menu_key},
    [PAGE_PARTY]     = {"P12 队伍", play_party_enter, play_party_exit,
                        play_party_key},
};

static page_id_t s_cur = PAGE_IDLE;
static bool s_entered;
static nav_ctx_t s_ctx;
#define RETURN_DEPTH 6
static page_id_t s_return[RETURN_DEPTH];
static uint8_t s_return_count;
static bool s_returning;

static bool encounter_page(page_id_t p)
{
    return p == PAGE_BATTLE || p == PAGE_CAPTURE;
}

nav_ctx_t *nav_ctx(void) { return &s_ctx; }
page_id_t nav_current(void) { return s_cur; }

bool nav_is_returning(void) { return s_returning; }

static void transition(page_id_t p, bool returning)
{
    if ((unsigned)p >= PAGE_COUNT) return;
    if (world_needs_starter() && p != PAGE_OPENING && p != PAGE_STARTER)
        p = PAGE_STARTER;
    if (s_entered && PAGES[s_cur].exit) PAGES[s_cur].exit();
    if (s_entered && encounter_page(s_cur) && !encounter_page(p))
        world_end_active_encounter();
    s_cur = p;
    screen_idle_note_activity();
    s_entered = true;
    s_returning = returning;
    ESP_LOGI(TAG, "→ %s", PAGES[p].name);
    music_director_page(p);
    if (p == PAGE_MENU || p == PAGE_CARE) battery_ui_start();
    else battery_ui_stop();
    if (PAGES[p].enter) PAGES[p].enter();
    growth_ui_start();
    s_returning = false;
}

void nav_go(page_id_t p)
{
    if ((unsigned)p >= PAGE_COUNT) return;
    s_return_count = 0;
    transition(p, false);
}

void nav_open(page_id_t p)
{
    if ((unsigned)p >= PAGE_COUNT || (s_entered && p == s_cur)) return;
    if (world_needs_starter()) { nav_go(PAGE_STARTER); return; }
    // Six entries exceed the deepest supported path: idle/menu/party/bag.
    // Keep the existing path intact if an accidental recursive link is added.
    if (s_entered && s_return_count == RETURN_DEPTH) return;
    if (s_entered) s_return[s_return_count++] = s_cur;
    transition(p, false);
}

void nav_back(page_id_t fallback)
{
    if (s_return_count) transition(s_return[--s_return_count], true);
    else nav_go(fallback);
}

void nav_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (screen_idle_filter_key(btn, ev)) return;
    if (evolution_ui_key(btn, ev)) return;
    if (growth_ui_key(btn, ev)) return;
    if (s_entered && PAGES[s_cur].key) PAGES[s_cur].key(btn, ev);
}

void nav_start(void) { trainer_store_t c; world_challenge_snapshot(&c); nav_go(c.session.active ? PAGE_TRAINER : PAGE_IDLE); }

bool nav_can_leave(void)
{
    if (!s_entered) return true;
    if (s_cur == PAGE_BATTLE) return play_battle_can_leave();
    if (s_cur == PAGE_CAPTURE) return play_capture_can_leave();
    if (s_cur == PAGE_EXPLORATION) return !play_exploration_screen_busy();
    return true;
}

bool nav_screen_busy(void)
{
    if(evolution_ui_active())return true;
    if (!s_entered) return false;
    switch (s_cur) {
    case PAGE_DUNGEON: return play_dungeon_screen_busy();
    case PAGE_EXPLORATION: return play_exploration_screen_busy();
    case PAGE_OPENING: return play_opening_screen_busy();
    case PAGE_STARTER: return play_starter_screen_busy();
    case PAGE_ENCOUNTER: return play_enc_screen_busy();
    case PAGE_BATTLE: return play_battle_screen_busy();
    case PAGE_CAPTURE: return play_capture_screen_busy();
    case PAGE_CARE: return play_care_screen_busy();
    case PAGE_TRAINER: return play_trainer_screen_busy();
    default: return false;
    }
}

void nav_exit_current(void)
{
    battery_ui_stop();
    evolution_ui_stop();
    growth_ui_stop();
    if (s_entered && PAGES[s_cur].exit) PAGES[s_cur].exit();
    if (s_entered && encounter_page(s_cur)) world_end_active_encounter();
    s_entered = false;
    s_return_count = 0;
    s_returning = false;
    screen_set_redraw(NULL);
}

void nav_end_encounter(void) {
 page_id_t next=s_ctx.exploring?PAGE_EXPLORATION:PAGE_ENCOUNTER;
 s_ctx.exploring=false;nav_go(next);
}
