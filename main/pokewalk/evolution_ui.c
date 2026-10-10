#include "evolution_ui.h"
#include "assets.h"
#include "evolution.h"
#include "evolution_light_data.h"
#include "game_ui.h"
#include "world.h"
#include "nav.h"
#include "screen.h"
#include "screen_idle.h"
#include "render.h"
#include "pokemon_animation.h"
#include "music_director.h"
#include "sfx.h"
#include "cry.h"
#include "lvgl.h"
#include "esp_timer.h"
#include <stdio.h>

// Gold: 80-frame lead-in, 8 rounds of (18-2*n) wait + n swaps,
// each frontpic replacement waits 4 frames; final replacement 4, light show 64.
#define REVEAL_FRAME 440u
#define DONE_FRAME 508u
static lv_timer_t *timer;
static uint16_t old_species,new_species;
static uint8_t item_id;
static unsigned frame;
static uint32_t last_ms,remainder,audio_wait;
static uint8_t audio_stage;
static bool active,committed,failed,shiny;
static music_id_t previous_music;
static pokemon_idle_t motion;
static uint16_t shown_species(void){
 if(frame<80)return old_species;
 unsigned t=frame-80;
 for(unsigned n=1;n<=8;n++){
  unsigned wait=18-2*n;
  if(t<wait)return old_species;
  t-=wait;
  if(t<n*8)return (t/4)%2?old_species:new_species;
  t-=n*8;
 }
 return new_species;
}
static void light(int band,unsigned age,unsigned angle){
 if(age>=14)return;
 unsigned radius=16+8*age;
 angle=(angle^((age%2==0)?32:0))&63;
 int x=120+evo_sine[(angle+16)&63]*(int)radius/128;
 int y=132+evo_sine[angle]*(int)radius/128;
 bool big=age>=2&&age<4;unsigned size=big?16:8;
 const uint8_t *tiles=big?evo_light_large:evo_light_small;
 const uint16_t colors[4]={0,0xffdf,0xff00,0xfbef};
 for(unsigned py=0;py<size;py++)for(unsigned px=0;px<size;px++){
  unsigned tile=(py/8)*(size/8)+px/8,row=(py%8)*2,bit=7-px%8;
  unsigned c=((tiles[tile*16+row]>>bit)&1)|(((tiles[tile*16+row+1]>>bit)&1)<<1);
  for(int iy=0;iy<2;iy++)for(int ix=0;ix<2;ix++){
   int dx=x+(int)px*2-(int)size+ix,dy=y+(int)py*2-(int)size+iy;
   if(c&&dx>=0&&dx<240&&dy>=32&&dy<236&&dy>=band&&dy<band+80)screen_px(dx,dy-band,colors[(c+frame/2)%3+1]);
  }
 }
}
static void draw(int band){
 screen_band_clear(GAME_UI_BG);
 game_ui_title(band,committed?"进化成功！":"伙伴正在进化",NULL);
 species_t sp;uint16_t id=committed?new_species:failed?old_species:shown_species();
 if(assets_species(id,&sp)){
  uint8_t size=0;const uint8_t *sprite=assets_front_sprite(id,&size);
  if(committed&&motion.sprite.data){sprite=motion.sprite.data;size=motion.sprite.w;}
  uint16_t pal[4];assets_palette_variant(sp.palette,shiny,pal);
  if(frame>=80&&!committed){pal[0]=0xffff;pal[1]=pal[2]=pal[3]=0;}
  if(sprite)game_ui_sprite_centered(band,24,40,192,184,sprite,size,size,3,pal);
 }
 if(committed&&frame>=REVEAL_FRAME+4&&frame< DONE_FRAME){
  unsigned t=frame-REVEAL_FRAME-4;
  for(unsigned birth=0;birth<32;birth+=2)if(t>=birth){
   unsigned angle=((birth+1)&14)*2;
   light(band,t-birth,angle);light(band,t-birth,angle+16);
  }
 }
 char text[80],name[40];species_t named;
 game_ui_box(band,8,238,224,72);
 if(failed){
  game_ui_text_centered(band,16,248,208,16,"进化未保存",GAME_UI_INK);
  game_ui_text_centered(band,16,278,208,16,"C重试 长按B取消",GAME_UI_MUTED);
 }else if(committed){
  if(assets_species(new_species,&named))snprintf(name,sizeof(name),"%.*s",named.name_zh_len,named.name_zh);else snprintf(name,sizeof(name),"伙伴");
  snprintf(text,sizeof(text),"进化成%s了！",name);game_ui_text_centered(band,16,248,208,16,text,GAME_UI_INK);
  game_ui_text_centered(band,16,278,208,16,frame>=DONE_FRAME&&audio_stage>=3?"C确认继续冒险":"新伙伴诞生了",GAME_UI_MUTED);
 }else{
  game_ui_text_centered(band,16,248,208,16,"伙伴的样子正在变化！",GAME_UI_INK);
  game_ui_text_centered(band,16,278,208,16,"长按B取消进化",GAME_UI_MUTED);
 }
}
static void commit(void){
 bool ok;
 if(item_id==ITEM_NONE)ok=world_evolve_leader(old_species,new_species);
 else{item_use_result_t result;ok=world_item_use(old_species,item_id,&result)==ITEM_USE_OK;}
 if(!ok){failed=true;return;}
 committed=true;failed=false;frame=REVEAL_FRAME;
 pokemon_idle_reset(&motion,new_species);music_director_play(MUSIC_EVOLVED);
}
static void tick(lv_timer_t *unused){
 (void)unused;uint32_t now=(uint32_t)(esp_timer_get_time()/1000),dt=now-last_ms;last_ms=now;
 if(screen_idle_is_off()||failed)return;
 if(audio_wait){
  if(dt<audio_wait){audio_wait-=dt;if(committed&&pokemon_idle_step(&motion,dt))screen_redraw_current();return;}
  dt-=audio_wait;audio_wait=0;
  if(audio_stage==0){audio_stage=1;music_director_play(MUSIC_EVOLUTION);}
  else if(audio_stage==2){audio_stage=3;music_director_play(MUSIC_CAUGHT);screen_redraw_current();}
 }
 if(frame>=DONE_FRAME){if(pokemon_idle_step(&motion,dt))screen_redraw_current();return;}
 // 59.7275 source Hz, independent of display refresh/transport latency.
 uint64_t time=(uint64_t)remainder+(uint64_t)dt*597275;
 unsigned ticks=time/10000000;remainder=time%10000000;
 frame+=ticks;
 if(!committed&&frame>=REVEAL_FRAME)commit();
 if(committed&&frame>=DONE_FRAME){frame=DONE_FRAME;music_director_play(MUSIC_NONE);sfx_cry(new_species);audio_stage=2;audio_wait=cry_duration_ms(new_species)+100;}
 screen_redraw_current();
}
bool evolution_ui_begin(uint16_t from,uint16_t to,uint8_t item){
 if(active||!from||!to||to>151)return false;
 world_t w;world_snapshot(&w);if(w.species!=from)return false;
 timer=lv_timer_create(tick,33,NULL);if(!timer)return false;
 world_party_t party;world_party_snapshot(&party);shiny=party.count&&(party.members[0].flags&1);
 old_species=from;new_species=to;item_id=item;active=true;committed=failed=false;frame=remainder=0;
 last_ms=esp_timer_get_time()/1000;previous_music=music_director_current();
 audio_stage=0;audio_wait=cry_duration_ms(from)+100;music_director_play(MUSIC_NONE);sfx_cry(from);screen_set_overlay(draw);screen_idle_note_activity();screen_redraw_current();return true;
}
bool evolution_ui_active(void){return active;}
unsigned evolution_ui_frame(void){return frame;}
void evolution_ui_stop(void){
 if(timer){lv_timer_delete(timer);timer=NULL;}
 if(!active)return;
 active=false;sfx_cry(0);screen_set_overlay(NULL);music_director_play(previous_music);screen_redraw_current();
}
bool evolution_ui_key(bsp_btn_t b,bsp_btn_ev_t e){
 if(!active)return false;
 if(!committed&&nav_return(b,e))evolution_ui_stop();
 else if(failed&&nav_confirm(b,e)){last_ms=esp_timer_get_time()/1000;commit();screen_redraw_current();}
 else if(committed&&frame>=DONE_FRAME&&audio_stage>=3&&nav_confirm(b,e))evolution_ui_stop();
 return true;
}
