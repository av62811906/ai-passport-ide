// P12: six visible party slots and a member detail view.
// World transactions save each requested change before publishing it to the UI.
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "assets.h"
#include "game_ui.h"
#include "dungeon.h"
#include "nav.h"
#include "play.h"
#include "render.h"
#include "screen.h"
#include "screen_idle.h"
#include "pokemon_animation.h"
#include "world.h"
#include "combat.h"
#include "exp.h"
#include "box_view.h"
#include "battle.h"

#define ROW_Y 36
#define ROW_H 36
#define THUMB_SIZE 32

SCREEN_ASSERT_WITHIN_BAND(party_title, 8, 16);
SCREEN_ASSERT_ALLOW_CROSS_BAND(party_detail_front, 64, 112);
SCREEN_ASSERT_WITHIN_BAND(party_detail_stats, 184, 16);
SCREEN_ASSERT_WITHIN_BAND(party_detail_exp, 208, 16);
SCREEN_ASSERT_WITHIN_BAND(party_detail_feedback, 244, 16);
SCREEN_ASSERT_WITHIN_BAND(party_box_count, 260, 16);

static world_party_t s_party;
static uint8_t s_selected, s_action;
static bool s_details,s_skills;
static unsigned s_skill;
static move_policy_t s_policy;
static bool s_box_mode,s_box_details;
static bool s_end_dungeon;
static bool s_release_confirm;
static unsigned s_release_choice, s_release_slot;
static mon_t s_release_expected;
static unsigned s_end_choice;
static mon_t s_box[BOX_SPECIES];
static uint8_t s_box_ids[BOX_SPECIES],s_box_count,s_box_row;
static box_view_options_t s_box_options;
enum { BOX_MENU_CLOSED, BOX_MENU_OPTIONS, BOX_MENU_FILTER, BOX_MENU_TYPE, BOX_MENU_SORT };
static unsigned s_box_menu, s_box_menu_row;
static bool s_box_paging;
static const char *const s_box_filters[] = {"全部", "闪光", "可进化"};
static const char *const s_box_sorts[] = {"图鉴编号", "等级从高到低", "等级从低到高"};
static const char *const s_box_types[] = {
    "不限", "一般", "火", "水", "电", "草", "冰", "格斗", "毒", "地面",
    "飞行", "超能力", "虫", "岩石", "幽灵", "龙", "恶", "钢"
};
_Static_assert(sizeof(s_box_types) / sizeof(s_box_types[0]) == BATTLE_TYPE_COUNT + 1, "box types");
static char s_feedback[72];
static bool s_success;
static lv_timer_t *s_tick;
static pokemon_idle_t s_motion;
static uint8_t s_sway_tick;
static int s_sway;

static const mon_t *view_member(void)
{
    if (s_box_mode) return s_box_count ? &s_box[s_box_ids[s_box_row]] : NULL;
    return s_selected < s_party.count ? &s_party.members[s_selected] : NULL;
}

static unsigned policy_slot(void){return s_box_mode?PARTY_MAX+s_box_ids[s_box_row]:s_selected;}
static void refresh_policy(void){
 memset(&s_policy,0,sizeof(s_policy));
 if(view_member())world_move_policy(policy_slot(),view_member(),&s_policy);
}
static void reset_motion(void)
{
    const mon_t *member = view_member();
    pokemon_idle_reset(&s_motion, member ? member->species_id : 0);
    s_sway_tick = 0;
    s_sway = 0;
}

static void member_name(const mon_t *member, char *out, size_t capacity)
{
    species_t species;
    if (assets_species(member->species_id, &species))
        snprintf(out, capacity, "%.*s", species.name_zh_len, species.name_zh);
    else snprintf(out, capacity, "#%03u", member->species_id);
}

static void draw_front(int band_y, const mon_t *member, int x, int y, int w, int h, bool thumbnail)
{
    uint8_t size;
    const uint8_t *front = assets_front_sprite(member->species_id, &size);
    if (member == view_member() && s_motion.species == member->species_id && s_motion.sprite.data) {
        front = s_motion.sprite.data;
        size = s_motion.sprite.w;
        if (thumbnail) x += s_sway;
    }
    species_t species;
    if (!front || !assets_species(member->species_id, &species)) return;
    uint16_t palette[4];
    assets_palette_variant(species.palette, (member->flags & 1) != 0, palette);
    if (thumbnail)
        game_ui_thumbnail_centered(band_y, x, y, w, h, front, size, THUMB_SIZE, palette);
    else game_ui_sprite_centered(band_y, x, y, w, h, front, size, size, 2, palette);
}

static void draw_list(int band_y)
{
    char text[64], name[48];
    snprintf(text, sizeof(text), "%u/%u", s_party.count, PARTY_MAX);
    game_ui_title(band_y, "队伍", text);
    unsigned highest=1;for(unsigned i=0;i<s_party.count;i++)if(s_party.members[i].level>highest)highest=s_party.members[i].level;
    for (unsigned index = 0; index < PARTY_MAX; index++) {
        int y = ROW_Y + index * ROW_H;
        if (index >= s_party.count) {
            render_text(64, y + 10 - band_y, "空位", GAME_UI_MUTED);
            continue;
        }
        const mon_t *member = &s_party.members[index];
        int sprite_x = 24;
        int text_x = 64;
        draw_front(band_y, member, sprite_x, y + 2, THUMB_SIZE, THUMB_SIZE, true);
        member_name(member, name, sizeof(name));
        render_text(text_x, y + 2 - band_y, name, GAME_UI_INK);
        snprintf(text, sizeof(text), "Lv%u", member->level);
        render_text(228 - render_text_width(text), y + 2 - band_y, text, GAME_UI_INK);
        if(index)snprintf(text,sizeof(text),"分享%u%%",exp_party_percent(member->level,highest,false));
        else snprintf(text,sizeof(text),"亲密 %u",member->intimacy);
        render_text(text_x, y + 20 - band_y, text, GAME_UI_MUTED);
        if (index == 0) render_text(196, y + 20 - band_y, "出战", GAME_UI_ACCENT);
        game_ui_list_marker(band_y, 8, y + 10, index, s_party.count, s_selected);
    }
    if(!s_feedback[0]) { render_text(12, 260 - band_y, "C查看详情", GAME_UI_MUTED);
    snprintf(text, sizeof(text), "仓库%u只", s_party.box_count);
    render_text(228 - render_text_width(text), 260 - band_y, text, GAME_UI_MUTED);
    } else game_ui_text_centered(band_y,8,260,224,16,s_feedback,GAME_UI_ACCENT);
    game_ui_footer(band_y, game_ui_list_hint(s_party.count));
}

static void draw_detail(int band_y)
{
    const mon_t *member = view_member();
    if (!member) return;
    char name[48], text[64], level[16];
    member_name(member, name, sizeof(name));
    snprintf(level, sizeof(level), "Lv%u", member->level);
    game_ui_title(band_y, name, level);
    snprintf(text, sizeof(text), "#%03u", member->species_id);
    render_text(12, 40 - band_y, text, GAME_UI_MUTED);
    if (s_box_mode) snprintf(text, sizeof(text), "仓库伙伴");
    else if (s_selected) snprintf(text, sizeof(text), "队员 %u", s_selected + 1);
    else snprintf(text, sizeof(text), "出战伙伴");
    render_text(228 - render_text_width(text), 40 - band_y, text, GAME_UI_INK);
    draw_front(band_y, member, 64, 64, 112, 112, false);
    snprintf(text, sizeof(text), "亲密 %u", member->intimacy);
    render_text(12, 184 - band_y, text, GAME_UI_INK);
    snprintf(text, sizeof(text), "探索 %u", member->explore_value);
    render_text(228 - render_text_width(text), 184 - band_y, text, GAME_UI_INK);
    snprintf(text, sizeof(text), "经验 %lu", (unsigned long)member->exp);
    render_text(12, 208 - band_y, text, GAME_UI_MUTED);
    if (member->flags & 1) render_text(196, 208 - band_y, "闪光", GAME_UI_ACCENT);
    game_ui_box(band_y, 8, 232, 224, 40);
    const char *hint = s_feedback[0] ? s_feedback : s_box_mode ? GAME_UI_BACK_HINT : GAME_UI_NAV_HINT;
    game_ui_text_centered(band_y, 16, 244, 208, 16, hint,
                         s_success ? GAME_UI_ACCENT : GAME_UI_INK);
    static const char *const party_actions[] = {"出战", "道具", "技能", "仓库"};
    static const char *const box_actions[] = {"换入", "技能", "放生"};
    game_ui_actions(band_y, s_box_mode ? box_actions : party_actions,
                    s_box_mode ? 3 : 4, s_action);

}

static void load_box(void){
 unsigned previous = s_box_row < s_box_count ? s_box_ids[s_box_row] : BOX_SPECIES;
 inventory_t inventory;
 world_box_snapshot(s_box);
 world_inventory_snapshot(&inventory);
 s_box_count = box_view_build(s_box, &inventory, &s_box_options, s_box_ids);
 if (s_box_row >= s_box_count) s_box_row = s_box_count ? s_box_count - 1 : 0;
 for (unsigned i = 0; i < s_box_count; i++) if (s_box_ids[i] == previous) s_box_row = i;
 if (!s_box_count) s_box_details = s_skills = false;
}
static void draw_box(int band){
 char text[72],name[48];snprintf(text,sizeof(text),"%u只",s_party.box_count);game_ui_title(band,"仓库",text);
 snprintf(text,sizeof(text),"%s / %s / %s",s_box_filters[s_box_options.filter],s_box_types[s_box_options.type],s_box_options.sort==BOX_SORT_LEVEL?"等级降序":s_box_options.sort==BOX_SORT_LEVEL_ASC?"等级升序":"编号");
 game_ui_text_centered(band,8,36,224,16,text,GAME_UI_MUTED);
 game_ui_box(band,8,56,224,168);
 if(!s_box_count){
  game_ui_text_centered(band,16,112,208,16,s_party.box_count?"没有符合条件的伙伴":"仓库暂无伙伴",GAME_UI_MUTED);
  game_ui_text_centered(band,16,144,208,16,"长按A调整筛选",GAME_UI_MUTED);
 }
 for(unsigned i=0,top=s_box_row/5*5;i<5&&top+i<s_box_count;i++){
  mon_t *m=&s_box[s_box_ids[top+i]];int y=68+i*32;member_name(m,name,sizeof(name));render_text(54,y-band,name,GAME_UI_INK);
  if(m->flags&1)render_text(34,y-band,"★",GAME_UI_ACCENT);
  snprintf(text,sizeof(text),"Lv%u",m->level);render_text(216-render_text_width(text),y-band,text,GAME_UI_INK);
  game_ui_list_marker(band,20,y,top+i,s_box_count,s_box_row);
 }
 member_name(&s_party.members[s_selected],name,sizeof(name));snprintf(text,sizeof(text),"换出 %s",name);game_ui_text_centered(band,8,236,224,16,text,GAME_UI_INK);
 game_ui_text_centered(band,8,260,224,16,s_feedback[0]?s_feedback:s_box_paging?"快速翻页 C恢复逐只选择":"长按A筛选排序与翻页",s_box_paging?GAME_UI_ACCENT:GAME_UI_MUTED);
 game_ui_footer(band,s_box_paging?"A上页 B下页 C选择":game_ui_list_hint(s_box_count));
}

static unsigned box_menu_count(void)
{
    return s_box_menu == BOX_MENU_OPTIONS ? 6 : s_box_menu == BOX_MENU_FILTER ? 3 :
           s_box_menu == BOX_MENU_TYPE ? BATTLE_TYPE_COUNT + 1 : 3;
}

static void draw_box_menu(int band)
{
    char text[80];
    snprintf(text, sizeof(text), "%u只匹配", s_box_count);
    const char *title = s_box_menu == BOX_MENU_OPTIONS ? "仓库选项" :
        s_box_menu == BOX_MENU_FILTER ? "筛选伙伴" : s_box_menu == BOX_MENU_TYPE ? "按属性筛选" : "排序方式";
    game_ui_title(band, title, text);
    unsigned top = s_box_menu == BOX_MENU_OPTIONS ? 0 : s_box_menu_row / 5 * 5;
    for (unsigned i = top; i < box_menu_count() && i < top + (s_box_menu == BOX_MENU_OPTIONS ? 6 : 5); i++) {
        const char *label;
        if (s_box_menu == BOX_MENU_OPTIONS) {
            switch (i) {
            case 0: snprintf(text, sizeof(text), "范围 %s", s_box_filters[s_box_options.filter]); break;
            case 1: snprintf(text, sizeof(text), "属性 %s", s_box_types[s_box_options.type]); break;
            case 2: snprintf(text, sizeof(text), "排序 %s", s_box_options.sort == BOX_SORT_LEVEL ? "等级从高到低" : "图鉴编号"); break;
            case 3: snprintf(text, sizeof(text), "快速翻页"); break;
            case 4: snprintf(text, sizeof(text), "清除筛选"); break;
            default: snprintf(text, sizeof(text), "返回仓库"); break;
            }
            label = text;
        } else label = s_box_menu == BOX_MENU_FILTER ? s_box_filters[i] :
                       s_box_menu == BOX_MENU_TYPE ? s_box_types[i] : s_box_sorts[i];
        int y = 52 + (i - top) * 32;
        game_ui_text_fitted(band, 36, y, 192, label, GAME_UI_INK);
        game_ui_list_marker(band, 16, y, i, box_menu_count(), s_box_menu_row);
    }
    const char *hint = s_box_menu == BOX_MENU_FILTER ? "进化需等级达标或已有道具" :
                       s_box_menu == BOX_MENU_TYPE ? "双属性任一匹配即可" : "只调整显示 不改变伙伴";
    game_ui_text_fitted(band, 8, 252, 224, hint, GAME_UI_MUTED);
    game_ui_footer(band, GAME_UI_NAV_HINT);
}
static void draw_all(void)
{
    for (int y = 0; y < SCREEN_H; y += SCREEN_BAND_H) {
        screen_band_clear(GAME_UI_BG);
        if (s_release_confirm) {
            char name[48], level[24];
            member_name(&s_release_expected, name, sizeof(name));
            snprintf(level, sizeof(level), "Lv%u", s_release_expected.level);
            game_ui_title(y, "放生伙伴？", level);
            draw_front(y, &s_release_expected, 64, 44, 112, 112, false);
            game_ui_text_centered(y, 8, 168, 224, 16, name, GAME_UI_INK);
            game_ui_box(y, 8, 196, 224, 44);
            game_ui_text_centered(y, 16, 210, 208, 16,
                s_release_expected.flags & 1 ? "闪光伙伴 放生无法撤销" : "放生后无法撤销", GAME_UI_ACCENT);
            game_ui_text_centered(y, 8, 252, 224, 16,
                s_feedback[0] ? s_feedback : GAME_UI_BACK_HINT, GAME_UI_INK);
            static const char *const actions[] = {"取消", "确认放生"};
            game_ui_actions(y, actions, 2, s_release_choice);
        }
        else if (s_end_dungeon) {
            game_ui_title(y, "结束秘境？", "换入伙伴");
            game_ui_box(y, 8, 56, 224, 176);
            game_ui_text_centered(y, 16, 76, 208, 16, "换入需要结束本局", GAME_UI_INK);
            game_ui_text_centered(y, 16, 108, 208, 16, "已得经验和道具保留", GAME_UI_ACCENT);
            game_ui_text_centered(y, 16, 140, 208, 16, "本局无法继续", GAME_UI_MUTED);
            game_ui_text_centered(y, 16, 172, 208, 16, "入场体能不退还", GAME_UI_MUTED);
            game_ui_text_centered(y, 8, 252, 224, 16, s_feedback, GAME_UI_INK);
            static const char *const actions[] = {"取消", "结束并换入"};
            game_ui_actions(y, actions, 2, s_end_choice);
        }
        else if (s_box_mode && s_box_menu) draw_box_menu(y);
        else if(s_box_mode && s_skills && view_member())game_ui_move_settings(y,view_member()->species_id,view_member()->level,s_skill,&s_policy,s_feedback);
        else if(s_box_mode && s_box_details)draw_detail(y);
        else if(s_box_mode)draw_box(y);
        else if(s_skills && s_selected<s_party.count)game_ui_move_settings(y,s_party.members[s_selected].species_id,s_party.members[s_selected].level,s_skill,&s_policy,s_feedback);
        else if (s_details && s_selected < s_party.count) draw_detail(y);
        else draw_list(y);
        screen_push_band(y);
    }
}

static bool refresh_snapshot(void)
{
    world_party_t fresh;
    memset(&fresh, 0, sizeof(fresh));
    world_party_snapshot(&fresh);
    bool changed = memcmp(&fresh, &s_party, sizeof(fresh)) != 0;
    s_party = fresh;
    if (s_selected >= s_party.count) { s_selected = s_action = 0; s_details = false; s_skills = false; }
    return changed;
}

static void refresh_tick(lv_timer_t *timer)
{
    (void)timer;
    bool changed = refresh_snapshot();
    const mon_t *member = view_member();
    if (member && s_motion.species != member->species_id) reset_motion();
    if (screen_idle_is_off()) return;
    static const int8_t sway[] = {0, 1, 1, 0, -1, -1, 0, 0};
    int previous = s_sway;
    s_sway_tick = (s_sway_tick + 1) % 40;
    s_sway = sway[s_sway_tick / 5];
    if (pokemon_idle_step(&s_motion, 80) || changed || previous != s_sway) draw_all();
}

void play_party_enter(void)
{
    if (!nav_is_returning()) { s_selected = s_action = 0; s_details = false; s_skills = false; }
    s_feedback[0] = '\0';
    s_box_mode=s_box_details=false;
    s_box_menu = BOX_MENU_CLOSED;
    s_box_paging = false;
    s_end_dungeon = false;
    s_release_confirm = false;
    s_release_choice = 0;
    s_end_choice = 0;
    s_success = false;
    s_tick = NULL;
    refresh_snapshot();
    reset_motion();
    screen_set_redraw(draw_all);
    draw_all();
    s_tick = lv_timer_create(refresh_tick, 80, NULL);
}

void play_party_exit(void)
{
    if (s_tick) { lv_timer_delete(s_tick); s_tick = NULL; }
}

void play_party_presentation_snapshot(play_party_view_t *out)
{
    if (out) *out = (play_party_view_t){.selected = s_selected, .details = s_box_mode ? s_box_details : s_details,
        .box = s_box_mode, .box_row = s_box_row, .skills = s_skills,
        .box_matches = s_box_count, .box_slot = s_box_row < s_box_count ? s_box_ids[s_box_row] : 255,
        .box_menu = s_box_menu, .box_filter = s_box_options.filter, .box_type = s_box_options.type,
        .box_sort = s_box_options.sort, .box_paging = s_box_paging,
        .release_confirm = s_release_confirm, .release_choice = s_release_choice,
        .species = view_member() ? view_member()->species_id : 0,
        .feedback = s_feedback};
    if(s_skills&&view_member()){
        uint16_t ids[COMBAT_MOVE_CAP];out->skill_selected=s_skill;
        out->skill_count=combat_known_moves(view_member()->species_id,view_member()->level,ids,COMBAT_MOVE_CAP);
        out->skill_id=s_skill<out->skill_count?ids[s_skill]:0;
        out->skill_enabled=move_policy_allows(&s_policy,out->skill_id);
        out->skill_enabled_count=combat_enabled_moves(view_member()->species_id,view_member()->level,&s_policy);
    }

}

static void select_leader(void)
{
    world_switch_result_t result = world_set_leader(s_selected, &s_party.members[s_selected], &s_party);
    s_success = result == WORLD_SWITCH_OK || result == WORLD_SWITCH_ALREADY_LEADER;
    const char *message = "伙伴已变 请重试";
    switch (result) {
    case WORLD_SWITCH_OK: s_selected = 0; message = "已设为出战伙伴"; break;
    case WORLD_SWITCH_ALREADY_LEADER: s_selected = 0; message = "已经是出战伙伴"; break;
    case WORLD_SWITCH_BUSY: message = dungeon_party_locked()?"请先结束秘境旅程":"请先结束当前对战"; break;
    case WORLD_SWITCH_SAVE_FAILED: message = "保存失败 请重试"; break;
    case WORLD_SWITCH_STORAGE_UNAVAILABLE: message = "存档暂不可用"; break;
    default: break;
    }
    if (!s_success) refresh_snapshot();
    snprintf(s_feedback, sizeof(s_feedback), "%s", message);
}

static void exchange_member(void)
{
    world_switch_result_t result = world_box_exchange(s_selected, &s_party.members[s_selected], &s_box[s_box_ids[s_box_row]]);
    if (result == WORLD_SWITCH_BUSY && dungeon_party_locked()) {
        s_end_dungeon = true;
        s_end_choice = 0;
        s_feedback[0] = 0;
        return;
    }
    const char *message = result == WORLD_SWITCH_OK ? "队伍已更换" : result == WORLD_SWITCH_BUSY ? "请先结束当前对战" : result == WORLD_SWITCH_SAVE_FAILED ? "保存失败 请重试" : result == WORLD_SWITCH_INVALID ? "仓库选择无效 请重试" : "伙伴已变 请重试";
    snprintf(s_feedback, sizeof(s_feedback), "%s", message);
    refresh_snapshot();
    load_box();
    if (result == WORLD_SWITCH_OK) {
        s_box_mode = s_box_details = false;
        s_action = 3;
        reset_motion();
    }
}

void play_party_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    int direction = nav_direction(btn, ev);
    bool confirm = nav_confirm(btn, ev), back = nav_return(btn, ev);
    if (s_release_confirm) {
        s_release_choice = nav_list_selection(btn, ev, 2, s_release_choice);
        if (back || (confirm && s_release_choice == 0)) {
            s_release_confirm = false; s_feedback[0] = 0;
        } else if (confirm) {
            world_switch_result_t result = world_box_release(s_release_slot, &s_release_expected);
            s_success = result == WORLD_SWITCH_OK;
            const char *message = s_success ? "伙伴已放生" :
                result == WORLD_SWITCH_SAVE_FAILED ? "保存失败 请重试" :
                result == WORLD_SWITCH_BUSY ? "请先结束对战或秘境" :
                result == WORLD_SWITCH_STORAGE_UNAVAILABLE ? "存档暂不可用" : "伙伴已变 请重试";
            snprintf(s_feedback, sizeof(s_feedback), "%s", message);
            if (s_success || result == WORLD_SWITCH_STALE || result == WORLD_SWITCH_INVALID) {
                s_release_confirm = s_box_details = false; s_action = 0;
                refresh_snapshot(); load_box(); reset_motion();
            }
        }
        draw_all(); return;
    }
    if (s_box_mode && !s_box_details && !s_skills && !s_end_dungeon &&
        btn == BSP_BTN_UP && ev == BSP_BTN_LONG) {
        s_box_menu = BOX_MENU_OPTIONS;
        s_box_menu_row = 0;
        s_box_paging = false;
        s_feedback[0] = 0;
        draw_all();
        return;
    }
    if (!direction && !confirm && !back) return;
    if (s_box_mode && s_box_menu) {
        if (back) {
            s_box_menu = s_box_menu == BOX_MENU_OPTIONS ? BOX_MENU_CLOSED : BOX_MENU_OPTIONS;
            s_box_menu_row = 0;
        } else if (direction) s_box_menu_row = nav_list_selection(btn, ev, box_menu_count(), s_box_menu_row);
        else if (confirm) {
            if (s_box_menu == BOX_MENU_OPTIONS) {
                switch (s_box_menu_row) {
                case 0: s_box_menu = BOX_MENU_FILTER; s_box_menu_row = s_box_options.filter; break;
                case 1: s_box_menu = BOX_MENU_TYPE; s_box_menu_row = s_box_options.type; break;
                case 2: s_box_menu = BOX_MENU_SORT; s_box_menu_row = s_box_options.sort; break;
                case 3: s_box_paging = s_box_count > 0; s_box_menu = BOX_MENU_CLOSED; break;
                case 4: s_box_options.filter = BOX_FILTER_ALL; s_box_options.type = 0; load_box(); s_box_menu = BOX_MENU_CLOSED; break;
                default: s_box_menu = BOX_MENU_CLOSED; break;
                }
            } else {
                if (s_box_menu == BOX_MENU_FILTER) s_box_options.filter = s_box_menu_row;
                else if (s_box_menu == BOX_MENU_TYPE) s_box_options.type = s_box_menu_row;
                else s_box_options.sort = s_box_menu_row;
                load_box();
                s_box_menu = BOX_MENU_CLOSED;
            }
        }
        reset_motion(); draw_all(); return;
    }
    if (s_box_mode && s_box_paging && !s_box_details && !s_skills && !s_end_dungeon) {
        if (back || confirm) s_box_paging = false;
        else if (direction) s_box_row = box_view_turn(s_box_row, s_box_count, direction);
        reset_motion(); draw_all(); return;
    }
    if (s_end_dungeon) {
        s_end_choice = nav_list_selection(btn, ev, 2, s_end_choice);
        if (back || (confirm && s_end_choice == 0)) {
            s_end_dungeon = false;
            s_feedback[0] = 0;
        } else if (confirm) {
            if (!dungeon_abandon()) {
                snprintf(s_feedback, sizeof(s_feedback), "保存失败 请重试");
            } else {
                s_end_dungeon = false;
                // Pending dungeon rewards may change the outgoing member's EXP.
                // Refresh it after settlement, keeping the chosen warehouse row.
                refresh_snapshot();
                exchange_member();
            }
        }
        draw_all();
        return;
    }
    if (s_skills) {
        if (back) {s_skills = false;s_feedback[0]=0;}
        else if (ev==BSP_BTN_CLICK && view_member()) {
            uint16_t ids[COMBAT_MOVE_CAP];
            int count = combat_known_moves(view_member()->species_id, view_member()->level, ids, COMBAT_MOVE_CAP);
            if(direction){s_skill=nav_list_selection(btn,ev,count+1,s_skill);s_feedback[0]=0;}
            else if(confirm){
                unsigned id=s_skill<(unsigned)count?ids[s_skill]:0;
                world_switch_result_t r=world_move_set(policy_slot(),view_member(),id,!move_policy_allows(&s_policy,id));
                const char *message=r==WORLD_SWITCH_OK?(id?(move_policy_allows(&s_policy,id)?"已禁用 自动战斗不选用":"已启用"):"已全部启用"):
                    r==WORLD_SWITCH_LAST_MOVE?"至少保留一招":r==WORLD_SWITCH_BUSY?"对战或秘境中无法修改":
                    r==WORLD_SWITCH_SAVE_FAILED?"保存失败 请重试":r==WORLD_SWITCH_STORAGE_UNAVAILABLE?"存档暂不可用":"伙伴已变 请重试";
                snprintf(s_feedback,sizeof(s_feedback),"%s",message);refresh_policy();
            }
        }
        draw_all(); return;
    }
    if (back) {
        if (s_box_details) { s_box_details = false; s_action = 0; }
        else if (s_box_mode) { s_box_mode = false; s_action = 3; reset_motion(); }
        else if (s_details) s_details = false;
        else { nav_back(PAGE_MENU); return; }
        s_feedback[0] = 0; draw_all(); return;
    }
    bool detail = s_box_mode ? s_box_details : s_details;
    unsigned count = detail ? (s_box_mode ? 3 : 4) : s_box_mode ? s_box_count : s_party.count;
    unsigned choice = detail ? s_action : s_box_mode ? s_box_row : s_selected;
    choice = nav_list_selection(btn, ev, count, choice);
    if (detail) s_action = choice; else if (s_box_mode) s_box_row = choice; else s_selected = choice;
    if (!nav_list_activate(btn, ev, count)) {
        s_feedback[0] = 0; s_success = false; reset_motion(); draw_all(); return;
    }
    if (!view_member()) return;
    if (!detail) {
        if (s_box_mode) s_box_details = true; else s_details = true;
        s_action = 0; s_feedback[0] = 0; reset_motion(); draw_all(); return;
    }
    if (s_box_mode) {
        if (s_action == 1) { s_skills = true; s_skill = 0; s_feedback[0]=0; refresh_policy(); }
        else if (s_action == 2) {
            s_release_expected = *view_member(); s_release_slot = s_box_ids[s_box_row];
            s_release_choice = 0; s_release_confirm = true; s_feedback[0] = 0;
        }
        else exchange_member();
    } else if (s_action == 0) select_leader();
    else if (s_action == 1) {
        if (s_selected) { s_success = false; snprintf(s_feedback, sizeof(s_feedback), "请先设为出战伙伴"); }
        else { nav_open(PAGE_BAG); return; }
    } else if (s_action == 2) { s_skills = true; s_skill = 0; s_feedback[0]=0; refresh_policy(); }
    else { s_box_mode = true; s_box_details = false; s_box_menu = BOX_MENU_CLOSED; s_box_paging = false; s_box_row = s_action = 0; s_feedback[0] = 0; load_box(); reset_motion(); }
    draw_all();
}
