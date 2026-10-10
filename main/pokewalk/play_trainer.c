// P13: persistent trainer campaign and six-on-six battle presentation.
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "nav.h"
#include "play.h"
#include "world.h"
#include "trainer.h"
#include "dungeon.h"
#include "exp.h"
#include "screen.h"
#include "screen_idle.h"
#include "render.h"
#include "game_ui.h"
#include "battle_hud.h"
#include "battle_fx.h"
#include "esp_timer.h"
static uint32_t s_move_started_ms;
#include "battle_presentation.h"
#include "pokemon_animation.h"
#include "shiny_entry.h"
#include "music_director.h"
#include "sfx.h"
#include "ball_assets.h"
#include "party_ball_assets.h"

enum {HALL,INTRO,SENDOUT,FIGHT,SWITCH,RESULT,CONFIRM,TACTICS,RECOVER,MOVES,EXP_GAIN};
static trainer_store_t s_store;
static world_t s_world;
static uint8_t s_requested_route=255,s_route;
static bool s_route_hall;
static unsigned hall_first(void){return s_route_hall?TRAINER_ROUTE_FIRST+s_route*3:0;}

void play_trainer_open_route(uint8_t route){if(route<4){s_requested_route=route;nav_open(PAGE_TRAINER);}}
static world_party_t s_campaign_party;
static uint32_t s_result_exp;
static uint32_t s_exp_from,s_exp_to;
static uint8_t s_exp_frame,s_exp_sound_level;
static bool s_new_chapter;
static uint8_t s_result_stage;
static inventory_t s_result_items;
static trainer_event_t s_event;
static uint8_t s_mode,s_selected,s_slot,s_menu,s_frame,s_hold;
static uint8_t s_sendout_mask;
static shiny_entry_t s_shiny_entry;
static bool s_failed,s_pause,s_quit,s_forced,s_between,s_retire_confirm;
static lv_timer_t *s_tick;
static pokemon_idle_t s_motion;
static char s_hint[80];
static unsigned count_badges(void){unsigned n=0;for(unsigned i=0;i<8;i++)n+=!!(s_store.defeated&(1u<<i));
 return n;}
static trainer_mon_t *mon(unsigned side){trainer_side_t *s=&s_store.session.sides[side];
 return &s->mons[s->active];}
static void refresh(void){world_challenge_snapshot(&s_store);world_party_snapshot(&s_campaign_party);world_snapshot(&s_world);if(dungeon_playing())dungeon_snapshot(&s_store,&s_campaign_party);}
static void name(uint16_t id,char *out,size_t n){species_t sp;
 if(assets_species(id,&sp))snprintf(out,n,"%.*s",sp.name_zh_len,sp.name_zh);else snprintf(out,n,"#%u",id);}
static void rectangle(void *ctx,int x,int y,int w,int h,uint16_t color){int by=*(int*)ctx;
 for(int yy=y;yy<y+h;yy++)if(yy>=by&&yy<by+SCREEN_BAND_H)
  for(int xx=x;xx<x+w;xx++)if(xx>=0&&xx<SCREEN_W)screen_px(xx,yy-by,color);}
static void portrait(int y,uint8_t id,int x,int top,int scale){const uint8_t *data;uint16_t pal[4];trainer_art(id,&data,pal);
 if(data)render_sprite_2bpp(x,top-y,data,56,scale,pal);}
static void title_hall(int y){char b[40];snprintf(b,sizeof(b),"徽章 %u/8",count_badges());game_ui_title(y,"挑战",b);}
static unsigned hall_items(unsigned *ids){
 unsigned count=0;
 for(unsigned id=hall_first();id<hall_first()+(s_route_hall?3:TRAINER_COUNT);id++)
  if(trainer_unlocked(&s_store,id))ids[count++]=id;
 if(!s_route_hall&&s_store.league_active)ids[count++]=TRAINER_COUNT;
 return count;
}
static void hall(int y){
 if(s_route_hall)game_ui_title(y,"训练家",exploration_route(s_route)->name);else title_hall(y);
 if(!s_route_hall)for(unsigned i=0;i<8;i++){
  const uint16_t obtained[4]={0,0x52aa,0xad55,0xffff},locked[4]={0xc618,0xdedb,0xef7d,0xffff};
  render_sprite_2bpp(14+i*27,38-y,trainer_badge_art(i),16,1,s_store.defeated&(1u<<i)?obtained:locked);
 }

 unsigned ids[TRAINER_TOTAL],count=hall_items(ids),selected=0;
 for(unsigned i=0;i<count;i++)if(ids[i]==s_selected)selected=i;
 unsigned top=selected/5*5;
 for(unsigned row=0;row<5&&top+row<count;row++){
  unsigned id=ids[top+row];if(!s_route_hall&&id==TRAINER_COUNT)continue;int sy=76+row*(s_route_hall?48:30);const trainer_info_t *t=trainer_info(id);char level[12];snprintf(level,sizeof(level),trainer_rematch(&s_store,id)?"再战%u":"Lv%u",trainer_is_route(id)?trainer_route_level(id,&s_store,s_campaign_party.members,s_campaign_party.count):trainer_rematch(&s_store,id)?67+id:t->levels[t->count-1]);
  render_text(32,sy-y,t->name,GAME_UI_INK);
  render_text(228-render_text_width(level),sy-y,level,GAME_UI_MUTED);
  if(s_route_hall){char count_text[48];snprintf(count_text,sizeof(count_text),"%u只伙伴 体能%u",t->count,trainer_stamina_cost(&s_store,id));render_text(32,sy+20-y,count_text,GAME_UI_MUTED);}
  game_ui_list_marker(y,8,sy,top+row,count,selected);
 }
 if(s_route_hall)game_ui_text_centered(y,8,44,224,16,"短途切磋 随伙伴等级成长",GAME_UI_MUTED);
 if(!count)game_ui_text_centered(y,16,124,208,16,"赢得一次野战后开放",GAME_UI_MUTED);
 bool supply=!s_route_hall&&s_selected==TRAINER_COUNT&&s_store.league_active;
 unsigned cost=supply?0:trainer_stamina_cost(&s_store,s_selected);
 char balance[64],waiting[64];
 if(count){
  snprintf(balance,sizeof(balance),"体能%u/100 消耗%u",nurture_stamina_points(&s_world.pet),cost);
  game_ui_text_centered(y,8,222,224,16,balance,GAME_UI_INK);
 }
 unsigned wait=nurture_wait_minutes(&s_world.pet,cost);
 snprintf(waiting,sizeof(waiting),"体能不足 约%u分钟后恢复",wait);
 const char *hint=s_hint[0]?s_hint:count&&wait?waiting:supply?"联盟补给 消耗牛奶":s_store.league_active?"本轮已扣体能 保留HP":s_selected==8&&!s_route_hall?"联盟整轮20 后续不再扣除":s_route_hall?(count==1?"两枚徽章解锁下一位":count==2?"五枚徽章解锁下一位":"胜利获得经验与补给"):trainer_unlocked(&s_store,s_selected)?"开始挑战时扣除体能":"先赢上一关或野战";
 game_ui_text_centered(y,8,242,224,16,hint,GAME_UI_MUTED);
 if(!s_route_hall&&s_store.league_active){game_ui_text_centered(y,24,260,200,16,"联盟补给",GAME_UI_INK);game_ui_list_marker(y,0,260,count-1,count,selected);}
 else if(count)game_ui_text_centered(y,8,262,224,16,"熄屏待机约2小时回满",GAME_UI_MUTED);
 game_ui_footer(y,game_ui_list_hint(count));
}
static uint16_t visible_hp(unsigned side){
 trainer_mon_t *m=mon(side);
 if(s_mode!=FIGHT || (s_event.kind!=TRAINER_ATTACK&&s_event.kind!=TRAINER_STATUS))return m->hp;
 unsigned hit=battle_fx_hit_frame(&s_event.attack);
 if(s_frame<=hit)return s_event.before_hp[side];
 unsigned n=s_frame-hit;
 if(n>BATTLE_PRESENTATION_HP_FRAMES)n=BATTLE_PRESENTATION_HP_FRAMES;
 return (uint16_t)((int32_t)s_event.before_hp[side]+((int32_t)m->hp-s_event.before_hp[side])*(int)n/BATTLE_PRESENTATION_HP_FRAMES);
}
static battle_presentation_exp_t visible_exp(void){
 unsigned slot=s_store.session.sides[0].active;
 uint32_t total=slot<s_campaign_party.count?s_campaign_party.members[slot].exp:0;
 return s_mode==EXP_GAIN?battle_presentation_exp(s_exp_from,s_exp_to,LEVEL_MAX,s_exp_frame)
  :battle_presentation_exp(total,total,LEVEL_MAX,BATTLE_PRESENTATION_EXP_FRAMES);
}
static void battle_stage(int y){
 trainer_mon_t *p=mon(0),*e=mon(1);char text[128],label[48];
 name(e->species,label,sizeof(label));render_text(8,8-y,label,GAME_UI_INK);
 static const char *status_names[]={"","中毒","灼伤","麻痹","睡眠","冰冻"};
 snprintf(text,sizeof(text),"Lv%u %s",e->level,status_names[e->status]);render_text(8,26-y,text,GAME_UI_MUTED);
 battle_hud_draw_hp(rectangle,&y,8,48,4,2,BATTLE_HUD_WILD,visible_hp(1),e->max_hp,GAME_UI_BG);
 const trainer_info_t *t=trainer_info(s_store.session.trainer);
 render_text(8,70-y,t->name,GAME_UI_MUTED);
 const trainer_side_t *opponent=&s_store.session.sides[1];
 const uint16_t balls_palette[4]={GAME_UI_INK,0x52aa,0xad55,GAME_UI_BG};
 for(unsigned i=0;i<6;i++) {
  unsigned tile=i>=opponent->count?3:!opponent->mons[i].hp?2:opponent->mons[i].status?1:0;
  render_sprite_2bpp(8+i*16,92-y,PARTY_BALLS[tile],8,2,balls_palette);
 }
 battle_fx_rect_t pet={8,140,96,96},wild={124,24,112,112};battle_fx_pose_t pose={0};
 if(s_mode==FIGHT&&(s_event.kind==TRAINER_ATTACK||s_event.kind==TRAINER_STATUS))pose=battle_fx_pose_for_rects(&s_event.attack,s_frame,pet,wild);
 species_t sp;uint16_t palette[4];uint8_t size;const uint8_t *front=assets_front_sprite(e->transform_species?e->transform_species:e->species,&size);
 if(s_mode==SENDOUT&&(s_sendout_mask&2)&&s_motion.sprite.data){front=s_motion.sprite.data;size=s_motion.sprite.w;}
 const battle_round_t *hit_round=s_mode==FIGHT&&(s_event.kind==TRAINER_ATTACK||s_event.kind==TRAINER_STATUS)?&s_event.attack:NULL;
 bool pet_visible=battle_fx_actor_visible(hit_round,s_frame,true),wild_visible=battle_fx_actor_visible(hit_round,s_frame,false);
 bool pet_revealing=s_mode!=SENDOUT||!(s_sendout_mask&1)||s_hold>=10;
 bool wild_revealing=s_mode!=SENDOUT||!(s_sendout_mask&2)||s_hold>=10;
 if(!pet_revealing||!wild_revealing){
  ui_art_t ball;
  if(assets_ui("ball_24",&ball)){
   if(!wild_revealing)render_sprite_2bpp_wh(wild.x+76-s_hold*4,wild.y+12+s_hold*4-y,ball.data,ball.w,ball.h,1,ball_assets_palette(0));
   if(!pet_revealing)render_sprite_2bpp_wh(pet.x+s_hold*3,pet.y+42-s_hold*3-y,ball.data,ball.w,ball.h,1,ball_assets_palette(0));
  }
 }
 if(wild_revealing&&wild_visible&&front&&assets_species(e->transform_species?e->transform_species:e->species,&sp)){assets_palette_variant(sp.palette,false,palette);game_ui_sprite_centered(y,wild.x+pose.wild_dx,wild.y,112,112,front,size,size,2,palette);}
 sprite_asset_t back;
 if(pet_revealing&&pet_visible&&assets_back_sprite_info(p->transform_species?p->transform_species:p->species,&back)&&assets_species(p->transform_species?p->transform_species:p->species,&sp)){unsigned slot=s_store.session.sides[0].active;bool shiny=slot<s_campaign_party.count&&(s_campaign_party.members[slot].flags&1);assets_palette_variant(sp.palette,shiny,palette);game_ui_sprite_centered(y,pet.x+pose.pet_dx+(s_mode==SENDOUT&&(s_sendout_mask&1)&&s_hold>=10?pokemon_back_entrance_offset((s_hold-10)*90):0),pet.y,96,96,back.data,back.w,back.h,2,palette);}
 battle_presentation_exp_t xp=visible_exp();
 name(p->species,label,sizeof(label));render_text(120,148-y,label,GAME_UI_INK);
 snprintf(text,sizeof(text),"Lv%u %s",s_mode==EXP_GAIN?xp.level:p->level,status_names[p->status]);render_text(120,168-y,text,GAME_UI_MUTED);
 battle_hud_draw_hp(rectangle,&y,120,188,4,2,BATTLE_HUD_PET,visible_hp(0),p->max_hp,GAME_UI_BG);
 snprintf(text,sizeof(text),"%u/%u",visible_hp(0),p->max_hp);
 render_text(232-render_text_width(text),208-y,text,GAME_UI_INK);
 battle_hud_draw_exp(rectangle,&y,120,224,7,2,xp.got,xp.need,GAME_UI_BG);
 if(s_mode==FIGHT&&(s_event.kind==TRAINER_ATTACK||s_event.kind==TRAINER_STATUS)){
  battle_fx_actor_t pa={0},wa={.data=front,.w=size,.h=size};
  sprite_asset_t back_art;species_t art_sp;
  if(assets_back_sprite_info(p->transform_species?p->transform_species:p->species,&back_art)){
   pa.data=back_art.data;pa.w=back_art.w;pa.h=back_art.h;
   if(assets_species(p->transform_species?p->transform_species:p->species,&art_sp)){
    unsigned slot=s_store.session.sides[0].active;
    assets_palette_variant(art_sp.palette,slot<s_campaign_party.count&&(s_campaign_party.members[slot].flags&1),pa.palette);
   }
  }
  if(assets_species(e->transform_species?e->transform_species:e->species,&art_sp))assets_palette_variant(art_sp.palette,false,wa.palette);
  battle_fx_draw_scene_band(&s_event.attack,s_frame,y,pet,wild,&pa,&wa);
 }
 shiny_entry_draw(&s_shiny_entry,y,pet,wild);
 battle_hud_draw_message_box(rectangle,&y,0,240,15,5,2,GAME_UI_BG);
 if(s_mode==EXP_GAIN){
  snprintf(text,sizeof(text),"队伍获得经验 +%lu",(unsigned long)((uint64_t)s_result_exp*s_exp_frame/BATTLE_PRESENTATION_EXP_FRAMES));
  game_ui_text_fitted(y,16,256,208,text,GAME_UI_INK);
  name(p->species,label,sizeof(label));
  snprintf(text,sizeof(text),"%s +%lu",label,(unsigned long)(xp.total-s_exp_from));
  game_ui_text_fitted(y,16,274,208,text,GAME_UI_MUTED);
  game_ui_text_centered(y,16,292,208,16,s_exp_frame<BATTLE_PRESENTATION_EXP_FRAMES?"经验正在增长":"经验已保存 C继续",GAME_UI_INK);
 }else if(s_failed){game_ui_text_fitted(y,16,256,208,"保存失败 按C重试",GAME_UI_INK);}
 else if(s_mode==FIGHT){
  name(mon(s_event.side)->species,label,sizeof(label));
  snprintf(text,sizeof(text),"%s%s",label,s_event.kind==TRAINER_STATUS?"无法行动":"使出");
  game_ui_text_fitted(y,16,256,208,text,GAME_UI_INK);
  if(s_event.kind==TRAINER_STATUS)snprintf(text,sizeof(text),"%s",combat_feedback(&s_event.attack));
  else {
   move_t display;bool named=combat_move(s_event.attack.move_id,&display);
   snprintf(text,sizeof(text),"%.*s%s",named?display.name_zh_len:s_event.attack.move_zh_len,named?display.name_zh:s_event.attack.move_zh?s_event.attack.move_zh:"",s_event.attack.charging?" 蓄力":s_event.attack.missed?" 未命中":"");
  }
  game_ui_text_fitted(y,16,274,208,text,GAME_UI_MUTED);
  game_ui_text_centered(y,16,292,208,16,s_pause?"本招结束后打开战术":dungeon_playing()&&dungeon_feedback()?dungeon_feedback():combat_feedback(&s_event.attack)?combat_feedback(&s_event.attack):"C战术 长按B认输",GAME_UI_INK);
 }else game_ui_text_fitted(y,16,256,208,s_failed?"保存失败 按C重试":dungeon_playing()&&dungeon_feedback()?dungeon_feedback():"伙伴准备出战！",GAME_UI_INK);
}
static void choice(int y){
 char text[64];game_ui_title(y,s_mode==RECOVER?"牛奶回复":"选择伙伴",s_forced?"需要替补":s_mode==RECOVER?(s_between?"连战休息":"回复占一回合"):"换人占一回合");
 trainer_side_t *side=&s_store.session.sides[0];
 for(unsigned i=0;i<side->count;i++){
  char label[48];name(side->mons[i].species,label,sizeof(label));int sy=46+i*34;
  game_ui_text_fitted(y,28,sy,144,label,side->mons[i].hp?GAME_UI_INK:GAME_UI_MUTED);
  snprintf(text,sizeof(text),"Lv%u",side->mons[i].level);render_text(228-render_text_width(text),sy-y,text,GAME_UI_MUTED);
  snprintf(text,sizeof(text),"HP %u/%u",side->mons[i].hp,side->mons[i].max_hp);render_text(28,sy+16-y,text,GAME_UI_MUTED);
  game_ui_list_marker(y,8,sy,i,s_store.session.sides[0].count,s_slot);
 }
 game_ui_text_centered(y,8,254,224,16,s_hint[0]?s_hint:s_mode==RECOVER?"回复一半HP 至少50":"倒下的伙伴无法出场",GAME_UI_MUTED);
 game_ui_footer(y,game_ui_list_hint(s_store.session.sides[0].count));
}
static void draw_all(void){
 for(int y=0;y<SCREEN_H;y+=SCREEN_BAND_H){screen_band_clear(GAME_UI_BG);
  if(s_mode==HALL)hall(y);
  else if(s_mode==INTRO){
   const trainer_info_t *t=trainer_info(s_store.session.trainer);game_ui_title(y,t->name,t->badge);
   portrait(y,s_store.session.trainer,120+(18-s_frame)*4,48,2);
   const uint16_t pal[4]={0,0x6b4d,0xdedb,0xffff};render_sprite_2bpp(8-(18-s_frame)*4,150-y,trainer_player_art(),48,2,pal);
   game_ui_footer(y,"训练家前来挑战！");
  }else if(s_mode==SWITCH||s_mode==RECOVER)choice(y);
  else if(s_mode==MOVES){
   trainer_mon_t *p=mon(0);game_ui_moves(y,p->transform_species?p->transform_species:p->species,p->transform_species?p->transform_level:p->level,s_slot,true);
  }else if(s_mode==RESULT){
   const trainer_info_t *t=trainer_info(s_store.session.trainer);game_ui_title(y,s_store.session.won?"挑战胜利":"挑战结束",t->name);
   if(s_store.session.won&&s_result_stage==2&&s_store.session.trainer<8){
    const uint16_t badge_pal[4]={0,0x52aa,0xad55,0xffff};
    render_sprite_2bpp(88,64-y,trainer_badge_art(s_store.session.trainer),16,4,badge_pal);
   }else portrait(y,s_store.session.trainer,64,42,2);
   if(s_failed){
    game_ui_text_centered(y,8,200,224,16,"保存失败 按C重试",GAME_UI_INK);
   }else if(s_store.session.won && s_result_stage==0){
    game_ui_box(y,8,168,224,100);
    game_ui_text_centered(y,16,182,208,16,trainer_victory_line(s_store.session.trainer),GAME_UI_INK);
    game_ui_text_centered(y,16,212,208,16,"这是你与伙伴努力的证明。",GAME_UI_INK);
    game_ui_text_centered(y,16,242,208,16,"收下奖励，继续前进吧！",GAME_UI_INK);
   }else if(s_store.session.won && s_result_stage==1){
    unsigned row=0;char b[64];
    for(unsigned i=0;i<ITEM_COUNT&&row<5;i++)if(s_result_items.quantity[i]){
     snprintf(b,sizeof(b),"%s ×%u",items_info(i)->name,s_result_items.quantity[i]);
     game_ui_text_centered(y,8,158+row++*22,224,16,b,GAME_UI_INK);
    }
    if(!row)game_ui_text_centered(y,8,194,224,16,"背包已满，奖励无法放入",GAME_UI_MUTED);
   }else{
    game_ui_text_centered(y,8,174,224,16,s_store.session.won?t->badge:s_store.session.retired?"已经认输":"去照料伙伴再来",GAME_UI_INK);
    char b[48];snprintf(b,sizeof(b),"获得经验 %lu",(unsigned long)s_result_exp);
    game_ui_text_centered(y,8,200,224,16,s_store.session.retired?"不扣经验 不降等级":b,GAME_UI_MUTED);
    game_ui_text_centered(y,8,226,224,16,s_new_chapter?"解锁新探索章节":"继续下一段旅程",GAME_UI_INK);
   }
   if(!s_failed&&(!s_store.session.won||s_result_stage==2))game_ui_text_centered(y,8,254,224,16,"经验与奖励已保存",GAME_UI_MUTED);
   game_ui_footer(y,s_store.session.won&&s_result_stage<2?"[C]继续":s_route_hall?"[C]返回训练家":"[C]返回挑战大厅");
   }else if(s_mode==CONFIRM){game_ui_title(y,"结束挑战","");game_ui_text_centered(y,8,112,224,16,s_failed?"保存失败 按C重试":"确定认输吗？",GAME_UI_INK);game_ui_text_centered(y,8,144,224,16,s_route_hall?"本次切磋不会获得经验":s_store.league_active?"联盟连战将重新开始":"本次挑战不会获得经验",GAME_UI_MUTED);game_ui_text_centered(y,8,176,224,16,dungeon_playing()?"已获经验道具都会保留":"入场体能不退还",GAME_UI_MUTED);static const char *const actions[]={"继续", "认输"};game_ui_actions(y,actions,2,s_retire_confirm);}
  else if(s_mode==TACTICS){
   game_ui_title(y,"战术","");static const char *options[]={"继续交锋","查看技能","更换伙伴","牛奶回复","认输"};
   for(unsigned i=0;i<5;i++){render_text(48,56+i*36-y,dungeon_playing()&&i==3?"仅营地可回复":options[i],GAME_UI_INK);
 if(s_menu==i)game_ui_cursor(y,24,60+i*36);}
   game_ui_footer(y,GAME_UI_NAV_HINT);
  }else battle_stage(y);
  if(s_mode==RESULT&&!s_failed&&s_frame<8)game_ui_fade_background(y,16-s_frame*2);
  screen_push_band(y);
 }
}
static void result(void){
 if(dungeon_playing()){if(dungeon_finish()){nav_go(PAGE_DUNGEON);return;}s_mode=RESULT;s_failed=true;draw_all();return;}
 refresh();s_mode=RESULT;s_result_stage=0;s_frame=0;
 unsigned chapter_before=exploration_chapter_current(s_store.defeated);
 inventory_t bag_before,bag_after;world_inventory_snapshot(&bag_before);
 world_party_t before,after;world_party_snapshot(&before);
 s_failed=!world_challenge_settle();world_party_snapshot(&after);s_result_exp=0;
 if(!s_failed)for(unsigned i=0;i<before.count&&i<after.count;i++)
  if(after.members[i].exp>=before.members[i].exp)s_result_exp+=after.members[i].exp-before.members[i].exp;
 world_inventory_snapshot(&bag_after);memset(&s_result_items,0,sizeof(s_result_items));
 if(!s_failed)for(unsigned i=0;i<ITEM_COUNT;i++)s_result_items.quantity[i]=bag_after.quantity[i]>=bag_before.quantity[i]?bag_after.quantity[i]-bag_before.quantity[i]:0;
 refresh();s_new_chapter=!s_failed&&exploration_chapter_current(s_store.defeated)>chapter_before;
 unsigned slot=s_store.session.sides[0].active;
 if(!s_failed&&s_result_exp&&slot<before.count&&slot<after.count){
  s_exp_from=before.members[slot].exp;s_exp_to=after.members[slot].exp;
  s_exp_frame=s_hold=0;s_exp_sound_level=exp_to_level(s_exp_from,LEVEL_MAX);s_mode=EXP_GAIN;
 }
 music_director_play(s_store.session.won?(trainer_info(s_store.session.trainer)->kind==0?MUSIC_LEADER_WIN:MUSIC_TRAINER_WIN):MUSIC_HOME);
 draw_all();
}
static void sendout(uint8_t mask){
 s_sendout_mask=mask;
 shiny_entry_begin(&s_shiny_entry,0);
 const trainer_info_t *t=trainer_info(s_store.session.trainer);
 music_director_play((t->kind==2||t->kind==3)?MUSIC_CHAMPION:t->kind==0?MUSIC_LEADER:MUSIC_TRAINER);
 s_mode=SENDOUT;s_frame=s_hold=0;pokemon_idle_reset(&s_motion,mon(1)->species);draw_all();}
static void next_action(void){
 if(s_quit){s_quit=false;{s_mode=CONFIRM;s_retire_confirm=false;}draw_all();return;}
 if(s_pause&&mon(0)->hp&&mon(1)->hp){s_pause=false;s_mode=TACTICS;s_menu=0;draw_all();return;}
 if(!(dungeon_playing()?dungeon_step(&s_event):world_challenge_step(&s_event))){s_failed=true;draw_all();return;}
 s_failed=false;refresh();s_frame=s_hold=0;
 if(s_event.kind==TRAINER_FINISHED){result();return;}
 if(s_event.kind==TRAINER_SWITCH_NEEDED){s_mode=SWITCH;s_forced=true;s_slot=0;draw_all();return;}
 if(s_event.kind==TRAINER_SENDOUT){sendout(1u<<s_event.side);return;}
 s_mode=FIGHT;s_move_started_ms=(uint32_t)(esp_timer_get_time()/1000);draw_all();
}
static void tick(lv_timer_t *t){
 (void)t;
 static unsigned slow_phase;
 static uint32_t previous_tick;
 uint32_t now=(uint32_t)(esp_timer_get_time()/1000),delta=now-previous_tick;previous_tick=now;
 if(screen_idle_is_off()&&s_mode==FIGHT)s_move_started_ms+=delta;
 if(s_mode!=FIGHT&&!shiny_entry_side(&s_shiny_entry)&&++slow_phase%2)return;
 if(s_failed||screen_idle_is_off())return;
 if(shiny_entry_side(&s_shiny_entry)){
  shiny_entry_step(&s_shiny_entry,BATTLE_FX_TICK_MS);
  if(!shiny_entry_side(&s_shiny_entry))next_action();else draw_all();
  return;
 }
 if(s_mode==EXP_GAIN){
  if(s_exp_frame<BATTLE_PRESENTATION_EXP_FRAMES){
   s_exp_frame++;uint8_t level=visible_exp().level;
   if(level>s_exp_sound_level){sfx_play(SFX_LEVEL_UP);s_exp_sound_level=level;}
  }else if(++s_hold>=12){s_mode=RESULT;s_frame=0;}
  draw_all();return;
 }
 if(s_mode==HALL){
  world_t next;world_snapshot(&next);
  unsigned cost=(!s_route_hall&&s_selected==TRAINER_COUNT&&s_store.league_active)?0:trainer_stamina_cost(&s_store,s_selected);
  bool changed=nurture_stamina_points(&next.pet)!=nurture_stamina_points(&s_world.pet)||nurture_wait_minutes(&next.pet,cost)!=nurture_wait_minutes(&s_world.pet,cost);
  s_world=next;if(changed)draw_all();return;
 }
 if(s_mode==RESULT){if(s_frame<8){s_frame++;draw_all();}return;}
 if(s_mode==INTRO){if(++s_frame>=18)sendout(3);else draw_all();}
 else if(s_mode==SENDOUT){
  if((s_sendout_mask&2)&&s_hold>=10)pokemon_idle_step(&s_motion,90);
  if(++s_hold>=32){
   unsigned slot=s_store.session.sides[0].active;
   // Dungeon snapshots preserve the selected owned partner's shiny flag.
   unsigned mask=(s_sendout_mask&1)&&slot<s_campaign_party.count&&(s_campaign_party.members[slot].flags&1)?1:0;
   shiny_entry_begin(&s_shiny_entry,mask);
   if(mask){sfx_play(SFX_SHINY);draw_all();}else next_action();
  }else draw_all();
 }else if(s_mode==FIGHT){
  unsigned frames=battle_fx_frames(&s_event.attack);
  unsigned hit=battle_fx_hit_frame(&s_event.attack),hp_end=hit+BATTLE_PRESENTATION_HP_FRAMES+1;
  if(frames<hp_end)frames=hp_end;
  if(s_frame+1<frames){
 unsigned elapsed_frame=((uint32_t)(esp_timer_get_time()/1000)-s_move_started_ms)/BATTLE_FX_TICK_MS;
 if(elapsed_frame<=s_frame)return;
 unsigned previous=s_frame;s_frame=elapsed_frame<frames?elapsed_frame:frames-1;
 if(s_event.kind==TRAINER_ATTACK&&previous<hit&&s_frame>=hit)sfx_move(s_event.attack.move_id,s_event.attack.move_type,s_event.attack.missed);
 draw_all();}
  else if(++s_hold>=16)next_action();
 }
}
void play_trainer_enter(void){
 shiny_entry_begin(&s_shiny_entry,0);
 refresh();if(dungeon_playing()&&s_store.session.finished){result();return;}s_route_hall=s_requested_route<4;s_route=s_route_hall?s_requested_route:0;s_requested_route=255;
 if(s_store.session.active&&trainer_is_route(s_store.session.trainer)){s_route_hall=true;s_route=(s_store.session.trainer-TRAINER_ROUTE_FIRST)/3;}
 s_hint[0]=0;s_failed=s_pause=s_quit=s_between=false;s_frame=s_hold=0;s_selected=hall_first();
 if(s_store.session.active){s_mode=INTRO;s_frame=0;
 if(s_store.session.finished)result();}
 else{s_mode=HALL;for(unsigned i=hall_first();i<hall_first()+(s_route_hall?3:TRAINER_COUNT);i++)if(trainer_unlocked(&s_store,i)&&(s_route_hall||!(s_store.defeated&(1u<<i)))){s_selected=i;break;}}
 if(s_mode!=RESULT)music_director_play(s_store.session.active?MUSIC_ENCOUNTER:s_route_hall?MUSIC_ROUTE:MUSIC_GYM);
 screen_set_redraw(draw_all);draw_all();s_tick=lv_timer_create(tick,BATTLE_FX_TICK_MS,NULL);
}
void play_trainer_exit(void){dungeon_set_playing(false);if(s_tick){lv_timer_delete(s_tick);s_tick=NULL;}}
bool play_trainer_screen_busy(void){return !s_failed&&(s_mode==INTRO||s_mode==SENDOUT||s_mode==FIGHT||s_mode==EXP_GAIN);}
void play_trainer_key(bsp_btn_t b,bsp_btn_ev_t e){
 int direction=nav_direction(b,e);bool confirm=nav_confirm(b,e),back=nav_return(b,e);
 if(!direction&&!confirm&&!back)return;
 if(s_mode==EXP_GAIN){
  if(confirm&&s_exp_frame>=BATTLE_PRESENTATION_EXP_FRAMES){s_mode=RESULT;s_frame=0;draw_all();}
  return;
 }
 if(s_mode==HALL){
  unsigned ids[TRAINER_TOTAL],count=hall_items(ids),index=0;
  for(unsigned i=0;i<count;i++)if(ids[i]==s_selected)index=i;
  if(back){nav_back(s_route_hall?PAGE_EXPLORATION:PAGE_MENU);return;}
  if(count)s_selected=ids[nav_list_selection(b,e,count,index)];
  s_hint[0]=0;
  if(nav_list_activate(b,e,count)){
   if(!s_route_hall&&s_selected==TRAINER_COUNT&&s_store.league_active){s_mode=RECOVER;s_between=true;s_forced=false;s_slot=0;s_hint[0]=0;draw_all();return;}
   world_challenge_result_t status=world_challenge_start(s_selected);refresh();
   if(status==WORLD_CHALLENGE_OK){s_mode=INTRO;s_frame=0;music_director_play(MUSIC_ENCOUNTER);}
   else if(status!=WORLD_CHALLENGE_NO_STAMINA)snprintf(s_hint,sizeof(s_hint),"%s",status==WORLD_CHALLENGE_SAVE_FAILED?"保存失败 体能未扣除":status==WORLD_CHALLENGE_BUSY?"请先完成当前对战":status==WORLD_CHALLENGE_LOCKED?"挑战尚未解锁":"存档暂不可用");
  }
 }else if(s_mode==RESULT){if(confirm||back){if(s_failed){result();return;}if(s_store.session.won&&s_result_stage<2){s_result_stage++;s_frame=0;draw_all();return;}if(!world_challenge_settle()){s_failed=true;}else{refresh();if(s_new_chapter)snprintf(s_hint,sizeof(s_hint),"新章节见冒险笔记");else s_hint[0]=0;s_failed=false;s_mode=HALL;s_selected=s_store.league_active?s_store.league_stage:s_selected;music_director_play(s_route_hall?MUSIC_ROUTE:MUSIC_GYM);}}}
 else if(s_mode==CONFIRM){
  if(direction)s_retire_confirm=!s_retire_confirm;
  if(confirm&&s_retire_confirm){if(dungeon_playing()?dungeon_retire():world_challenge_retire())result();else{s_failed=true;snprintf(s_hint,sizeof(s_hint),"保存失败 请重试");}}
  else if(confirm||back){s_mode=TACTICS;s_menu=0;s_failed=false;}
 }else if(s_mode==TACTICS){
  if(direction)s_menu=(s_menu+5+direction)%5;
  else if(back){next_action();return;}
  else if(confirm){
   if(s_menu==0){next_action();return;}
   if(s_menu==4){s_mode=CONFIRM;s_retire_confirm=false;}
   else if(s_menu==1){s_mode=MOVES;s_slot=0;s_hint[0]=0;}
   else{s_mode=s_menu==2?SWITCH:RECOVER;s_forced=false;s_slot=s_store.session.sides[0].active;s_hint[0]=0;}
  }
 }else if(s_mode==MOVES){
  trainer_mon_t *p=mon(0);uint16_t ids[COMBAT_MOVE_CAP];int count=combat_known_moves(p->transform_species?p->transform_species:p->species,p->transform_species?p->transform_level:p->level,ids,COMBAT_MOVE_CAP);
  if(back){s_mode=TACTICS;s_menu=1;}
  else if(confirm){next_action();return;}
  else s_slot=nav_list_selection(b,e,count,s_slot);
 }else if(s_mode==SWITCH||s_mode==RECOVER){
  unsigned count=s_store.session.sides[0].count;
  if(back){if(s_between){s_mode=HALL;s_between=false;}else if(s_forced){s_mode=CONFIRM;s_retire_confirm=false;}else{s_mode=TACTICS;s_menu=0;}}
  else {
   s_slot=nav_list_selection(b,e,count,s_slot);
   if(!nav_list_activate(b,e,count)){draw_all();return;}
   bool ok=dungeon_playing()?(s_mode==SWITCH&&dungeon_switch(s_slot,s_forced)):(s_mode==RECOVER?world_challenge_recover(s_slot):world_challenge_switch(s_slot,s_forced));
   if(ok){refresh();s_forced=false;if(s_between){snprintf(s_hint,sizeof(s_hint),"回复完成");draw_all();}else if(s_mode==RECOVER){next_action();}else sendout(1);return;}else snprintf(s_hint,sizeof(s_hint),"无法使用 或保存失败");
  }
 }else if(s_failed&&confirm){s_failed=false;next_action();return;}
 else if(back)s_quit=true;
 else if(confirm)s_pause=true;
 draw_all();
}

unsigned play_trainer_mode(void) { return s_mode; }
bool play_trainer_growth_ready(void) {
 return s_mode==HALL || (s_mode==RESULT&&!s_failed&&(!s_store.session.won||s_result_stage>=2)&&s_frame>=8);
}

unsigned play_trainer_sendout_mask(void){return s_mode==SENDOUT?s_sendout_mask:0;}

unsigned play_trainer_shiny_side(void){return s_mode==SENDOUT?shiny_entry_side(&s_shiny_entry):0;}
