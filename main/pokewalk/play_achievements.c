// P14: fixed IDs, explicit claiming; rewards and claims commit in one world save.
#include <stdio.h>
#include "play.h"
#include "nav.h"
#include "world.h"
#include "game_ui.h"
#include "render.h"
#include "screen.h"
static unsigned selected;
static achievement_view_t view;
static const char *feedback;
static void draw_all(void) {
 const achievement_info_t *a=achievement_info(selected);char text[96];
 unsigned progress=achievement_progress(&view,selected),ready=0;
 for(unsigned i=0;i<ACHIEVEMENT_COUNT;i++)if(!(view.store.claimed&(1u<<i))&&achievement_progress(&view,i)==achievement_info(i)->target)ready++;
 for(int y=0;y<SCREEN_H;y+=SCREEN_BAND_H){
  screen_band_clear(GAME_UI_BG);
  snprintf(text,sizeof(text),"可领取 %u",ready);game_ui_title(y,"成就",text);
  game_ui_box(y,8,40,224,136);
  unsigned top=selected/4*4;
  for(unsigned row=0;row<4&&top+row<ACHIEVEMENT_COUNT;row++){
   unsigned id=top+row;const achievement_info_t *r=achievement_info(id);int sy=56+row*28;
   render_text(32,sy-y,r->name,GAME_UI_INK);
   const char *mark=view.store.claimed&(1u<<id)?"已领":achievement_progress(&view,id)==r->target?"可领":"";
   render_text(216-render_text_width(mark),sy-y,mark,GAME_UI_ACCENT);
   if(id==selected)game_ui_cursor(y,16,sy+4);
  }
  game_ui_text_centered(y,16,188,208,16,a->description,GAME_UI_INK);
  snprintf(text,sizeof(text),"%u/%u",progress,a->target);game_ui_text_centered(y,16,210,208,16,text,GAME_UI_MUTED);
  snprintf(text,sizeof(text),"奖励 %s ×%u",items_info(a->item)->name,a->quantity);game_ui_text_centered(y,16,234,208,16,text,GAME_UI_ACCENT);
  game_ui_text_centered(y,16,258,208,16,feedback?feedback:"上下查看 长按B返回",GAME_UI_MUTED);
  game_ui_footer(y,GAME_UI_NAV_HINT);screen_push_band(y);
 }
}
void play_achievements_enter(void){if(!nav_is_returning())selected=0;feedback=0;world_achievements_snapshot(&view);screen_set_redraw(draw_all);draw_all();}
void play_achievements_exit(void){}
void play_achievements_key(bsp_btn_t b,bsp_btn_ev_t e){
 if(nav_direction(b,e)!=0){selected=(selected+ACHIEVEMENT_COUNT+nav_direction(b,e))%ACHIEVEMENT_COUNT;feedback=0;}
 else if(e!=BSP_BTN_CLICK&&!nav_return(b,e))return;
 else if(nav_return(b,e)){nav_back(PAGE_MENU);return;}
 else if(nav_confirm(b,e)){static const char *messages[]={"奖励已放入背包","尚未达成 继续冒险","奖励已经领取","已领取 超量道具未增加","保存失败 按确认重试"};feedback=messages[world_achievement_claim(selected)];}
 world_achievements_snapshot(&view);draw_all();
}
