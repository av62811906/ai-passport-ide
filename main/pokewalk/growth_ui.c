#include <stdio.h>
#include "growth_ui.h"
#include "assets.h"
#include "combat.h"
#include "evolution.h"
#include "game_ui.h"
#include "lvgl.h"
#include "nav.h"
#include "play.h"
#include "pokemon_animation.h"
#include "render.h"
#include "screen.h"
#include "screen_idle.h"
#include "sfx.h"
#include "world.h"

static lv_timer_t *s_timer;
static exp_growth_t s_event;
static uint16_t s_moves[COMBAT_MOVE_CAP];
static unsigned s_count,s_page;
static bool s_active, s_evolution_ready;
static pokemon_idle_t s_motion;

static unsigned move_pages(void) { return s_count ? (s_count+2)/3 : 1; }
static unsigned pages(void) { return move_pages() + (s_evolution_ready ? 1 : 0); }
static void draw_band(int y) {
    char text[96],right[24];
    screen_band_clear(GAME_UI_BG);
    snprintf(right,sizeof(right),"%u/%u",s_page+1,pages());
    game_ui_title(y,"等级提升！",right);
    species_t sp;
    if(assets_species(s_event.species,&sp)) {
        uint16_t pal[4];assets_palette_variant(sp.palette,(s_event.flags&1)!=0,pal);
        render_bounds_t ink;
        if(render_sprite_ink_bounds(s_motion.sprite.data,s_motion.sprite.w,s_motion.sprite.h,&ink)) {
            int scale=96/ink.w,vertical=80/ink.h;
            if(scale>vertical)scale=vertical;
            if(scale>2)scale=2;
            if(scale<1)scale=1;
            game_ui_sprite_centered(y,72,36,96,80,s_motion.sprite.data,
                s_motion.sprite.w,s_motion.sprite.h,scale,pal);
        }
        snprintf(text,sizeof(text),"%.*s",sp.name_zh_len,sp.name_zh);
        game_ui_text_centered(y,8,124,224,16,text,GAME_UI_INK);
    }
    snprintf(text,sizeof(text),"Lv%u > Lv%u",s_event.before,s_event.after);
    game_ui_text_centered(y,8,148,224,16,text,GAME_UI_ACCENT);
    game_ui_box(y,8,172,224,104);
    if (s_evolution_ready && s_page == move_pages()) {
        game_ui_text_centered(y,16,182,208,16,"已经可以进化了！",GAME_UI_INK);
        game_ui_text_centered(y,16,212,208,16,"设为队首后进入照料",GAME_UI_INK);
        game_ui_text_centered(y,16,242,208,16,"选择进化并按C确认",GAME_UI_MUTED);
    } else {
    game_ui_text_centered(y,16,182,208,16,s_count?"学会了新招式！":"伙伴变得更强了！",GAME_UI_INK);
    for(unsigned i=0;i<3&&s_page*3+i<s_count;i++) {
        move_t m;if(!combat_move(s_moves[s_page*3+i],&m))continue;
        snprintf(text,sizeof(text),"%.*s",m.name_zh_len,m.name_zh);
        game_ui_text_centered(y,16,204+i*22,208,16,text,GAME_UI_INK);
    }
    if(!s_count)game_ui_text_centered(y,16,224,208,16,"继续冒险，解锁更多招式",GAME_UI_MUTED);
    }
    game_ui_footer(y,pages()>1?"A上 B下 C继续 长按B返回":"C继续 长按B返回");
}
static void close_notice(void) {
    s_active=false;screen_set_overlay(NULL);screen_redraw_current();
}
static bool ready(void) {
    if(screen_idle_is_off()||nav_screen_busy())return false;
    switch(nav_current()) {
    case PAGE_OPENING: case PAGE_STARTER: case PAGE_CAPTURE: return false;
    case PAGE_BATTLE: return play_battle_can_leave();
    case PAGE_TRAINER: return play_trainer_growth_ready();
    default: return true;
    }
}
static void tick(lv_timer_t *timer) {
    (void)timer;
    if(s_active) {
        if(!screen_idle_is_off()&&pokemon_idle_step(&s_motion,120))screen_redraw_current();
        return;
    }
    if(!ready()||!world_growth_pop(&s_event))return;
    s_count=s_page=0;
    species_t evolution;
    s_evolution_ready = assets_species(s_event.species, &evolution) &&
        evo_level_ready(s_event.after, evolution.evolve_trigger, evolution.evolve_to, evolution.evolve_level);
    uint16_t known[COMBAT_MOVE_CAP];int count=combat_known_moves(s_event.species,s_event.after,known,COMBAT_MOVE_CAP);
    for(int i=0;i<count;i++)if(combat_learn_level(s_event.species,known[i])>s_event.before)s_moves[s_count++]=known[i];
    pokemon_idle_reset(&s_motion,s_event.species);
    s_active=true;screen_set_overlay(draw_band);screen_idle_note_activity();
    sfx_play(SFX_LEVEL_UP);screen_redraw_current();
}
void growth_ui_start(void) { if(!s_timer)s_timer=lv_timer_create(tick,120,NULL); }
void growth_ui_stop(void) {
    if(s_timer){lv_timer_delete(s_timer);s_timer=NULL;}
    s_active=false;screen_set_overlay(NULL);
}
bool growth_ui_active(void) { return s_active; }
bool growth_ui_view(exp_growth_t *event,unsigned *page,unsigned *moves) {
    if(!s_active)return false;
    if(event)*event=s_event;
    if(page)*page=s_page;
    if(moves)*moves=s_count;
    return true;
}
bool growth_ui_key(bsp_btn_t b,bsp_btn_ev_t e) {
    if(!s_active)return false;
    if(nav_return(b,e))close_notice();
    else if(nav_direction(b,e)){s_page=nav_list_selection(b,e,pages(),s_page);screen_redraw_current();}
    else if(nav_confirm(b,e)) {
        if(s_page+1<pages()){s_page++;screen_redraw_current();}
        else close_notice();
    }
    return true;
}
