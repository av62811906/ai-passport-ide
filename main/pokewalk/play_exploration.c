// P15: shared route exploration, rendered identically on LCD and native preview.
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_timer.h"
#include "assets.h"
#include "play.h"
#include "nav.h"
#include "world.h"
#include "game_ui.h"
#include "render.h"
#include "screen.h"
#include "battle.h"
#include "dungeon.h"

static exploration_view_t view;
static exploration_event_t event;
static unsigned selected, action_selected;
static uint8_t route_ids[EXPLORATION_MAPS],route_targets[EXPLORATION_MAPS];static unsigned route_count;
static void route_options(void){
 route_count=0;
 for(unsigned i=0;i<EXPLORATION_MAPS;i++)if(exploration_map_open(i,view.defeated,&view.regions)){
  route_ids[route_count++]=i;
  if(i<EXPLORATION_ROUTES){
   exploration_state_t focus=view.state;focus.route=i;
   route_targets[i]=exploration_current_target(&focus,&view.updates,view.defeated);
  }else route_targets[i]=exploration_region_target(i,&view.regions,view.defeated,world_dex());
 }
 selected=0;for(unsigned i=0;i<route_count;i++)if(route_ids[i]==view.route)selected=i;
}
static bool routes, animating, journal, activities, research, paths, chain_rules;
static unsigned activity_selected, badge_selected;
static bool badge_list, badge_detail;
static unsigned badge_ids[8], badge_count;
static void badge_options(void){badge_count=0;for(unsigned i=0;i<8;i++)if(exploration_activity_open(i,view.defeated))badge_ids[badge_count++]=i;if(badge_selected>=badge_count)badge_selected=0;}
static char research_note[64];
static unsigned chapter_selected;
static uint64_t started;
static lv_timer_t *timer;
static const char *feedback;
static uint64_t millis(void){return esp_timer_get_time()/1000;}
static void rect(int band,int x,int y,int w,int h,uint16_t c){
 int lo=y>band?y:band,hi=y+h<band+SCREEN_BAND_H?y+h:band+SCREEN_BAND_H;
 for(int yy=lo;yy<hi;yy++)for(int xx=x;xx<x+w;xx++)screen_px(xx,yy-band,c);
}
static void portrait(int band,unsigned id,bool shiny,int y,int h,int scale){
 species_t sp;uint8_t size;const uint8_t *data=assets_front_sprite(id,&size);
 if(!data||!assets_species(id,&sp))return;
 uint16_t pal[4];assets_palette_variant(sp.palette,shiny,pal);
 game_ui_sprite_centered(band,24,y,192,h,data,size,size,scale,pal);
}
static void center(int band,int y,const char *text,uint16_t color){game_ui_text_centered(band,8,y,224,16,text,color);}
static void trail_examples(int band,unsigned direction,int y){
 uint8_t ids[2];unsigned count=exploration_trail_examples(view.route,direction,view.deep,view.defeated,ids);
 char names[64]={0};size_t used=0;
 for(unsigned i=0;i<count;i++){
  species_t sp;if(!assets_species(ids[i],&sp))continue;
  int n=snprintf(names+used,sizeof(names)-used,"%s%.*s",used?" / ":"",sp.name_zh_len,sp.name_zh);
  if(n<0||(size_t)n>=sizeof(names)-used)break;
  used+=(size_t)n;
 }
 game_ui_text_fitted(band,36,y,188,used?names:"暂未发现伙伴",GAME_UI_MUTED);
}
static void visitor_names(int band,unsigned direction,int y){
 uint8_t ids[2];unsigned n=exploration_visitors(view.route,direction,view.regions.region[view.route-4].steps,view.defeated,ids);
 char text[80]="访客 ";size_t used=strlen(text);
 for(unsigned i=0;i<n;i++){species_t sp;if(!assets_species(ids[i],&sp))continue;
  int wrote=snprintf(text+used,sizeof(text)-used,"%s%.*s",i?" / ":"",sp.name_zh_len,sp.name_zh);
  if(wrote<0||(size_t)wrote>=sizeof(text)-used)break;
  used+=wrote;
 }
 game_ui_text_fitted(band,36,y,188,text,GAME_UI_ACCENT);
}
static void tile(int band,int x,int y,const char *const *rows,int n,const uint16_t *pal){
 for(int yy=0;yy<n;yy++)for(int xx=0;rows[yy][xx];xx++)if(rows[yy][xx]!='.')rect(band,x+xx*2,y+yy*2,2,2,pal[rows[yy][xx]-'0']);
}
static void scene(int band,unsigned route){
 static const char *const tree[]={
 ".....111111.....","...1122222211...","..122233322221..",".12233333222221.",
 "1223333222222221","1223322222232221","1222222222332221",".12222322222221.",
 "..122332222221..",".12233322222221.","1223332222232221","1223322222332221",
 "1222222223332221",".12222222222221.","..111222222111..",".....114411.....",
 "......1441......","......1441......",".....144441.....",".....111111....."};
 static const char *const rock[]={"...111111...",".1122222211.","123333222221","123332222221","122222222221","122222222211",".1222222211.","..11111111.."};
 static const uint16_t leaf[]={0,0x2245,0x4c49,0xaeea,0x9387};
 static const uint16_t stone[]={0,0x4a49,0x9c53,0xd659};
 rect(band,16,72,208,80,route==2?0xaeff:route==1?0xe6f6:0xeef5);
 if(route==0){
  rect(band,16,126,208,18,0xef75);
  for(int i=0;i<6;i++)tile(band,18+i*34,78+(i%2)*4,tree,20,leaf);
  for(int i=0;i<17;i++){int x=18+i*12;rect(band,x,148,2,2,0x4c49);rect(band,x+2,146,2,2,0x4c49);}
 }else if(route==1){
  for(int i=0;i<8;i++){tile(band,18+i*25,74,rock,8,stone);tile(band,18+i*25,134,rock,8,stone);}
  rect(band,108,82,24,20,0x4a49);rect(band,112,88,16,16,0x0000);
  for(int i=0;i<9;i++)rect(band,24+i*22,116+(i%3)*6,4,2,0x9c53);
 }else if(route==2){
  rect(band,16,132,208,20,0xef75);
  for(int i=0;i<7;i++)for(int k=0;k<3;k++){int x=18+i*30+(k%2)*8,y=82+k*18;rect(band,x,y,16,2,0x3d7f);rect(band,x+2,y-2,12,2,0xe79f);}
  for(int i=0;i<6;i++)rect(band,24+i*34,138+(i%2)*6,4,2,0xbd8c);
 }else if(route>=4){
  const exploration_region_t *r=exploration_region(route);
  uint16_t c=r->route.color;rect(band,16,72,208,80,route==5?0x2108:route==8?0x3800:0xe71c);
  if(route==4){
   for(int k=0;k<4;k++)for(int i=0;i<6;i++)rect(band,20+i*36+(k%2)*8,90+k*16,20,2,c);
   for(int i=0;i<2;i++){
    int x=58+i*100;tile(band,x-22,128,rock,8,stone);
    for(int k=0;k<19;k++){rect(band,x-k,100+k,4+k*2,2,0x5bd7);rect(band,x-k+2,100+k,1+k,2,0xe79f);}
    rect(band,x-6,126,16,16,0x4a49);rect(band,x-4,130,12,12,0x2108);
   }
  }else if(route==11){
   rect(band,16,72,208,80,0xaeff);
   for(int i=0;i<3;i++)for(int k=0;k<30;k++){
    int x=48+i*66;rect(band,x-k,80+k*2,4+k*2,2,0x4a73);
    rect(band,x-k+2,80+k*2,1+k,2,k<12?0xffff:0xc71f);
   }
   rect(band,16,140,208,12,0xe79f);
   for(int i=0;i<6;i++){rect(band,96+i*6,140+i*2,16,2,0x9d77);rect(band,30+i*32,136,4,4,0xffff);}
   rect(band,172,128,20,14,0x9387);rect(band,168,124,28,6,0xffff);rect(band,179,134,6,8,0x4a49);
  }else if(route==5){
   rect(band,82,82,80,68,0x1084);rect(band,86,86,72,64,0x630c);
   for(int row=0;row<6;row++){rect(band,86,88+row*10,72,2,0x4a49);for(int col=0;col<4;col++)rect(band,88+col*18+(row%2)*6,90+row*10,2,8,0x4a49);}
   for(int floor=0;floor<2;floor++)for(int i=0;i<3;i++){int x=94+i*22,y=92+floor*24;rect(band,x,y,10,16,0x1084);rect(band,x+2,y+2,6,10,0xfde0);rect(band,x+4,y+2,2,10,c);}
   rect(band,78,80,88,6,0x9b3f);rect(band,112,134,18,16,0x1084);rect(band,104,148,34,4,0x9c53);
   rect(band,184,80,14,14,0xfff0);rect(band,180,76,10,14,0x2108);
   for(int i=0;i<4;i++)rect(band,24+i*48,78+(i%2)*14,2,2,0xfff0);
  }else if(route==6){
   for(int i=0;i<4;i++)tile(band,20+i*56,78,tree,20,leaf);
   for(int i=0;i<10;i++){rect(band,30+i*18,142-(i%2)*8,4,4,0x9387);rect(band,34+i*18,146-(i%2)*8,2,2,0x9387);}
  }else if(route==7){
   for(int k=0;k<6;k++){rect(band,30+k*32,74,6,76,0x9387);rect(band,40,100+k*8,164,2,0x4a49);}rect(band,40,96,164,4,c);rect(band,40,140,164,4,c);
  }else if(route==8||route==9){
   for(int i=0;i<3;i++)for(int k=0;k<28;k++)rect(band,48+i*64-k,84+k*2,4+k*2,2,route==8?0x630c:0x5b4c);
   for(int i=0;i<4;i++)rect(band,20+i*50,140,36,6,c);
  }else{
   for(int i=0;i<4;i++){int x=30+i*50;rect(band,x,84,28,58,0x630c);rect(band,x+4,88,20,40,c);rect(band,x+8,98,8,18,0xe79f);rect(band,x,142,28,6,0x4a49);}
  }
 }else{
  rect(band,16,128,208,24,0xc618);
  for(int i=0;i<4;i++){
   int x=26+i*50;rect(band,x,82,30,46,0x4a49);rect(band,x+2,84,26,42,0x9c53);
   rect(band,x+6,90,18,12,0x2245);rect(band,x+8,92,14,6,0xaeea);
   for(int k=0;k<3;k++)rect(band,x+6,108+k*4,12,2,0x4a49);
   rect(band,x+22,110,4,4,0xfde0);rect(band,x+14,76,4,6,0x4a49);rect(band,x-4,76,54,2,0x4a49);
  }
 }
}

static void progress(int band,unsigned n){
 for(unsigned i=0;i<3;i++){
  int x=66+i*40;rect(band,x,194,28,8,GAME_UI_INK);rect(band,x+2,196,24,4,i<n?exploration_route(view.route)->color:GAME_UI_BG);
 }
}
static const char *error_text(exploration_kind_t kind){
 switch(kind){
 case EXPLORE_RESEARCH_LOCKED:return "研究条件还未完成";
 case EXPLORE_RESEARCH_CLAIMED:return "本路线奖励已领取";
 case EXPLORE_NO_STAMINA:return "体力不足 请等待恢复";
 case EXPLORE_BLOCKED:return "先处理列表中的同类伙伴";
 case EXPLORE_BUSY:return "请先完成当前对战";
 case EXPLORE_SAVE_FAILED:return "保存失败 资源未扣除";
 default:return "暂时无法探索";
 }
}
static void draw_all(void){
 char text[96];unsigned route=event.kind==EXPLORE_ENCOUNTER||event.kind==EXPLORE_TARGET?event.route:view.route;const exploration_route_t *r=exploration_route(route);
 for(int band=0;band<SCREEN_H;band+=SCREEN_BAND_H){
  screen_band_clear(GAME_UI_BG);snprintf(text,sizeof(text),"体能 %u/100",view.stamina);game_ui_title(band,routes?"选择路线":paths?r->name:"探索",text);
  if(chain_rules){
   center(band,44,"探索连胜",GAME_UI_INK);game_ui_box(band,8,72,224,178);
   const char *rules[]={"探索胜利提高闪光概率","探索战败或捕获会清零","逃跑仍然保留连胜","换地图与重启也保留","道馆与秘境不影响连胜"};
   for(unsigned i=0;i<5;i++)center(band,88+i*30,rules[i],GAME_UI_INK);
   center(band,258,"只影响新发现的伙伴",GAME_UI_MUTED);game_ui_footer(band,"长按B返回");
  }else if(paths){
   const exploration_region_t *r=exploration_region(view.route);
   snprintf(text,sizeof(text),"%sLv%u-%u 消耗5体能",view.deep?"深层":"常规",exploration_region_level_min(r,view.deep),r->max_level);center(band,40,text,GAME_UI_MUTED);
   for(unsigned i=0;i<3;i++){
    int y=80+i*64;const exploration_trail_t *trail=exploration_region_trail(view.route,i);
    game_ui_box(band,8,y,224,64);render_text(36,y+8-band,trail->name,GAME_UI_INK);
    trail_examples(band,i,y+24);visitor_names(band,i,y+40);if(i==selected)game_ui_cursor(band,18,y+12);
   }
   snprintf(text,sizeof(text),"访客%u次后轮换",(unsigned)(EXPLORATION_ROTATION_STEPS-view.regions.region[view.route-4].steps%EXPLORATION_ROTATION_STEPS));
   center(band,60,view.clues==3?"本次寻找追踪目标":text,GAME_UI_MUTED);game_ui_footer(band,GAME_UI_NAV_HINT);
  }else if(badge_list){
   center(band,44,"徽章活动",GAME_UI_INK);
   if(!badge_count)center(band,124,"获得徽章后开放新活动",GAME_UI_MUTED);
   unsigned first=badge_selected/4*4;
   for(unsigned row=0;row<4&&first+row<badge_count;row++){
    unsigned idx=first+row,id=badge_ids[idx];int y=76+row*46;
    const exploration_activity_t *a=exploration_activity(id);
    game_ui_box(band,8,y,224,42);if(idx==badge_selected)game_ui_cursor(band,16,y+12);
    render_text(36,y+4-band,a->name,GAME_UI_INK);
    snprintf(text,sizeof(text),"Lv%u-%u 进度%u/3",a->level,a->level+2,view.updates.activity_progress[id]);render_text(36,y+22-band,text,GAME_UI_MUTED);
   }
   center(band,264,"胜利或捕获推进活动",GAME_UI_MUTED);game_ui_footer(band,GAME_UI_NAV_HINT);
  }else if(badge_detail){
   unsigned id=badge_ids[badge_selected];const exploration_activity_t *a=exploration_activity(id);
   center(band,44,a->name,GAME_UI_INK);scene(band,a->route);
   center(band,166,a->story,GAME_UI_INK);
   snprintf(text,sizeof(text),"推荐Lv%u-%u 进度%u/3",a->level,a->level+2,view.updates.activity_progress[id]);center(band,190,text,GAME_UI_INK);
   if(view.updates.activity_claimed&(1u<<id))snprintf(text,sizeof(text),"再挑战：球类与稀有道具");
   else snprintf(text,sizeof(text),"首次奖励：%s",items_info(a->item)->name);
   center(band,216,text,GAME_UI_ACCENT);center(band,242,feedback?feedback:"每次寻找消耗5体能",GAME_UI_MUTED);
   center(band,264,"完成三次胜利或捕获领奖",GAME_UI_MUTED);
   game_ui_footer(band,view.updates.activity_progress[id]==3?"C领取奖励 长按B返回":"C寻找伙伴 长按B返回");
  }else if(activities){
   center(band,44,r->name,GAME_UI_INK);game_ui_box(band,8,72,224,178);
   const char *options[]={route>=4?exploration_region(route)->dungeon:"训练家切磋","路线研究","冒险笔记","徽章活动",view.deep?"切换常规探索":"切换深层调查","领取暂存收获","探索连胜说明"};
   unsigned count=route>=4?7:5,first=activity_selected>=6?1:0;
   for(unsigned row=0;row<6&&first+row<count;row++){unsigned i=first+row;render_text(44,80+row*28-band,options[route<4&&i==4?6:i],GAME_UI_INK);if(i==activity_selected)game_ui_cursor(band,16,84+row*28);}
   snprintf(text,sizeof(text),activity_selected==0?(route>=4?"秘境消耗20体能 当前%u":"切磋消耗5体能 当前%u"):"当前体能%u/100",view.stamina);
   center(band,254,text,GAME_UI_MUTED);game_ui_footer(band,GAME_UI_NAV_HINT);
  }else if(research){
   center(band,44,r->name,GAME_UI_INK);game_ui_box(band,8,72,224,172);
   center(band,88,"路线研究",GAME_UI_INK);
   snprintf(text,sizeof(text),"发现不同伙伴 %u/5",view.research_seen<5?view.research_seen:5);center(band,124,text,GAME_UI_INK);
   snprintf(text,sizeof(text),"捕获不同伙伴 %u/3",view.research_caught<3?view.research_caught:3);center(band,154,text,GAME_UI_INK);
   snprintf(text,sizeof(text),"追踪目标 %u/1",view.traced);center(band,184,text,GAME_UI_INK);
   if(route>=4)center(band,208,view.regions.region[route-4].clears?"主题秘境 已通关":"还需通关主题秘境",GAME_UI_INK);
   center(band,232,"奖励：两场同级战斗经验",GAME_UI_ACCENT);
   center(band,258,research_note[0]?research_note:view.claimed?"奖励已领取":"每条路线可领取一次",GAME_UI_MUTED);
   game_ui_footer(band,"C领取 长按B返回");
  }else if(journal){
   const exploration_chapter_t *c=exploration_chapter(chapter_selected);bool open=exploration_chapter_open(chapter_selected,view.defeated);
   center(band,44,"冒险笔记",GAME_UI_INK);game_ui_box(band,8,72,224,176);
   center(band,88,c->name,GAME_UI_INK);center(band,120,open?"已解锁":"下一段冒险",GAME_UI_ACCENT);
   center(band,152,c->condition,GAME_UI_INK);center(band,184,c->story,GAME_UI_MUTED);
   center(band,216,c->discovery,GAME_UI_INK);
   center(band,258,"线索事件有机会找到道具",GAME_UI_MUTED);game_ui_footer(band,"A上页 B下页 长按B返回");
  }else if(routes){
   unsigned first=selected/4*4;
   for(unsigned row=0;row<4&&first+row<route_count;row++){
    unsigned index=first+row,id=route_ids[index];int y=40+row*52;
    const exploration_route_t *r=exploration_route(id);const exploration_region_t *region=exploration_region(id);
    game_ui_box(band,8,y,224,48);if(index==selected)game_ui_cursor(band,16,y+13);
    render_text(32,y+8-band,r->name,GAME_UI_INK);
    if(region)snprintf(text,sizeof(text),"野生Lv%u-%u",exploration_region_level_min(region,view.regions.region[id-EXPLORATION_ROUTES].deep),region->max_level);
    else snprintf(text,sizeof(text),"Lv%u起 随队成长",exploration_legacy_level_min(id));
    game_ui_text_fitted(band,32,y+26,144,text,GAME_UI_MUTED);
    uint8_t size;species_t sp;const uint8_t *data=assets_front_sprite(route_targets[id],&size);
    if(data&&assets_species(route_targets[id],&sp)){
     uint16_t pal[4];assets_palette_variant(sp.palette,false,pal);
     game_ui_thumbnail_centered(band,182,y+6,40,36,data,size,32,pal);
    }
   }
   const char *next="全部地区已开放";
   for(unsigned i=4;i<EXPLORATION_MAPS;i++)if(!exploration_map_open(i,view.defeated,&view.regions)){next=exploration_region(i)->condition;break;}
   center(band,254,feedback?feedback:next,GAME_UI_MUTED);
   game_ui_footer(band,GAME_UI_NAV_HINT);
  }else if(animating){
   center(band,44,r->name,GAME_UI_INK);scene(band,route);
   unsigned phase=(millis()-started)/120;for(unsigned i=0;i<=phase&&i<4;i++)rect(band,80+i*24,176,8,8,r->color);
   center(band,218,"寻找伙伴的踪迹",GAME_UI_INK);game_ui_footer(band,"正在探索……");
  }else if(event.kind==EXPLORE_ENCOUNTER||event.kind==EXPLORE_TARGET){
   center(band,44,event.special?exploration_special_name(event.special):event.visitor?"迁徙访客出现了！":event.kind==EXPLORE_TARGET?"追踪目标出现了！":"发现野生宝可梦！",GAME_UI_INK);
   portrait(band,event.species,event.shiny,68,144,2);
   species_t sp;world_t w;world_snapshot(&w);
   if(assets_species(event.species,&sp))snprintf(text,sizeof(text),"%.*s Lv%u",sp.name_zh_len,sp.name_zh,(event.level ? event.level : battle_wild_level_for_pet(event.rarity,w.level)));else snprintf(text,sizeof(text),"#%03u",event.species);
   center(band,220,text,GAME_UI_INK);snprintf(text,sizeof(text),"稀有度 %u%s",event.rarity,event.shiny?" 闪光":event.special==EXPLORE_SPECIAL_SPARKLE?" 闪光率提升":"");center(band,244,text,GAME_UI_ACCENT);
   if(event.exp){snprintf(text,sizeof(text),event.special==EXPLORE_SPECIAL_TRAINING?"伙伴特训 经验+%u":"发现新种 经验+%u",event.exp);center(band,264,text,GAME_UI_INK);}
   static const char *const choices[]={"查看", "继续"};game_ui_actions(band,choices,2,action_selected);
  }else if(event.kind==EXPLORE_CLUE){
   center(band,44,r->name,GAME_UI_INK);scene(band,route);
   center(band,164,event.special?exploration_special_name(event.special):"发现新的线索",GAME_UI_INK);progress(band,event.clues);
   center(band,218,exploration_story(view.target,event.clues-1),GAME_UI_INK);
   if(event.item!=ITEM_NONE){snprintf(text,sizeof(text),event.item_full&&!event.quantity?"%s已满 未拾取":"发现 %s ×%u",items_info(event.item)->name,event.quantity);center(band,248,text,GAME_UI_ACCENT);}
   if(event.special==EXPLORE_SPECIAL_SUPPLY){snprintf(text,sizeof(text),event.extra_quantity?"伙伴额外找到树果 ×%u":"树果已满 额外补给未领取",event.extra_quantity);center(band,264,text,GAME_UI_MUTED);}
   else if(event.item==ITEM_NONE)center(band,248,event.clues==3?"下次探索必定找到目标":"继续探索 追踪伙伴",GAME_UI_MUTED);
   static const char *const choices[]={"继续", "路线", "活动"};game_ui_actions(band,choices,3,action_selected);
  }else{
   center(band,36,r->name,GAME_UI_INK);
   const exploration_region_t *region=exploration_region(route);
   if(region)snprintf(text,sizeof(text),"野生Lv%u-%u",exploration_region_level_min(region,view.deep),region->max_level);
   else snprintf(text,sizeof(text),"野生Lv%u起 随队成长",exploration_legacy_level_min(route));
   center(band,54,text,GAME_UI_MUTED);scene(band,route);
   unsigned tier=0;exploration_habitat(view.target,&tier);
   bool pinned=view.pinned;
   snprintf(text,sizeof(text),"%s 稀有度%u",pinned?"指定追踪":"随机追踪",tier);
   center(band,164,text,GAME_UI_INK);progress(band,view.clues);
   species_t target;uint16_t target_id=view.target;
   if(assets_species(target_id,&target))snprintf(text,sizeof(text),"%.*s 线索%u/3",target.name_zh_len,target.name_zh,view.clues);else snprintf(text,sizeof(text),"线索 %u/3",view.clues);center(band,214,text,GAME_UI_INK);
   const char *hint=feedback?feedback:(view.pending==5?"遭遇已满 将替换最早一只":"目标出现后更新追踪");
   center(band,238,hint,GAME_UI_MUTED);
   unsigned bp=exploration_chain_shiny_bp(view.chain_wins,SHINY_EXPLORATION);
   snprintf(text,sizeof(text),"连胜%lu 闪光%u.%02u%%",(unsigned long)view.chain_wins,bp/100,bp%100);
   center(band,258,text,GAME_UI_ACCENT);
   static const char *const choices[]={"探索", "路线", "活动"};game_ui_actions(band,choices,3,action_selected);
  }
  screen_push_band(band);
 }
}
static void tick(lv_timer_t *t){
 (void)t;
 if(animating){if(millis()-started>=600)animating=false;draw_all();}
 else {exploration_view_t next;world_exploration_snapshot(&next);if(next.pending!=view.pending||next.stamina!=view.stamina){view=next;draw_all();}}
}
void play_exploration_enter(void){
 world_exploration_snapshot(&view);route_options();action_selected=0;routes=animating=journal=activities=research=badge_list=badge_detail=paths=chain_rules=false;badge_selected=0;badge_options();research_note[0]=0;event=(exploration_event_t){0};feedback=NULL;
 screen_set_redraw(draw_all);timer=lv_timer_create(tick,120,NULL);draw_all();
}
void play_exploration_exit(void){if(timer){lv_timer_delete(timer);timer=NULL;}animating=false;}
bool play_exploration_screen_busy(void){return animating;}
void play_exploration_key(bsp_btn_t btn,bsp_btn_ev_t ev){
 if(animating)return;
 int direction=nav_direction(btn,ev);bool confirm=nav_confirm(btn,ev),back=nav_return(btn,ev);
 if(!direction&&!confirm&&!back)return;
 if(chain_rules){if(back){chain_rules=false;activities=true;}}
 else if(paths){
  if(direction)selected=nav_list_selection(btn,ev,3,selected);
  if(back)paths=false;
  else if(confirm){event=world_explore_path(selected+1);paths=false;action_selected=0;
   if(event.kind==EXPLORE_ENCOUNTER||event.kind==EXPLORE_CLUE||event.kind==EXPLORE_TARGET){animating=true;started=millis();feedback=NULL;}
   else {feedback=error_text(event.kind);event=(exploration_event_t){0};}
  }
 }else if(badge_list){
  if(direction&&badge_count)badge_selected=nav_list_selection(btn,ev,badge_count,badge_selected);
  if(back){badge_list=false;activities=true;activity_selected=3;}
  else if(confirm&&badge_count){badge_list=false;badge_detail=true;feedback=NULL;}
 }else if(badge_detail){
  if(back){badge_detail=false;badge_list=true;feedback=NULL;}
  else if(confirm){
   unsigned id=badge_ids[badge_selected];bool claim=view.updates.activity_progress[id]==3;
   exploration_event_t result=world_exploration_activity(id,claim);
   if(result.kind==EXPLORE_ENCOUNTER){
    event=result;badge_detail=false;action_selected=0;feedback=NULL;
   }else if(claim&&result.kind==EXPLORE_NONE){snprintf(research_note,sizeof(research_note),"获得 %s ×%u",items_info(result.item)->name,result.quantity);feedback=research_note;}
   else if(result.item_full)feedback="背包已满 请先使用道具";
   else if(result.kind==EXPLORE_BLOCKED){
    // Reopen an outstanding activity encounter instead of charging another entry.
    encounter_t enc;unsigned uid=view.updates.activity_uid[id];
    if(uid&&world_get_encounter_uid(uid,&enc)){
     nav_ctx_t *ctx=nav_ctx();*ctx=(nav_ctx_t){.enc=enc,.uid=enc.uid,.valid=true,.exploring=true};nav_go(PAGE_BATTLE);return;
    }
    feedback=error_text(result.kind);
   }else feedback=error_text(result.kind);
  }
 }else if(activities){
  if(direction)activity_selected=nav_list_selection(btn,ev,view.route>=4?7:5,activity_selected);
  if(back)activities=false;
  else if(confirm){
   activities=false;
   if(activity_selected==0){if(view.route>=4)play_dungeon_open_region(view.route);else play_trainer_open_route(view.route);return;}
   if(activity_selected==1){research=true;research_note[0]=0;}
   else if(activity_selected==2){journal=true;chapter_selected=exploration_chapter_current(view.defeated);}
   else if(activity_selected==3){badge_options();badge_list=true;}
   else if(activity_selected==(view.route>=4?6u:4u)){chain_rules=true;}
   else if(activity_selected==4){exploration_kind_t k=world_exploration_depth();feedback=k==EXPLORE_NONE?"探索方式已切换":"发现五种伙伴后开放深层";}
   else {feedback=world_region_collect()?"已领取可放入的收获":"先腾出遭遇或背包空间";}
  }
 }else if(research){
  if(confirm){uint16_t gain=0;exploration_kind_t k=world_research_claim(view.route,&gain);if(k==EXPLORE_NONE)snprintf(research_note,sizeof(research_note),"已保存 经验+%u",gain);else snprintf(research_note,sizeof(research_note),"%s",error_text(k));}
  else if(back){research=false;activities=true;activity_selected=1;}
 }else if(journal){
  unsigned limit=exploration_chapter_current(view.defeated)+2;if(limit>EXPLORATION_CHAPTERS)limit=EXPLORATION_CHAPTERS;
  if(direction)chapter_selected=(chapter_selected+limit+direction)%limit;
  else if(back){journal=false;activities=true;activity_selected=2;}
 }else if(routes){
  if(direction){selected=(selected+route_count+direction)%route_count;feedback=NULL;}
  else if(back){routes=false;feedback=NULL;}
  else if(confirm){exploration_kind_t k=world_exploration_select(route_ids[selected]);if(k==EXPLORE_NONE){routes=false;feedback=NULL;event=(exploration_event_t){0};action_selected=0;}else feedback=error_text(k);}
 }else{
  bool found=event.kind==EXPLORE_ENCOUNTER||event.kind==EXPLORE_TARGET;
  unsigned count=found?2:3;
  if(direction)action_selected=nav_list_selection(btn,ev,count,action_selected);
  if(back){if(event.kind){event=(exploration_event_t){0};feedback=NULL;action_selected=0;}else {nav_back(PAGE_MENU);return;}}
  else if(confirm){
   if(found){
    if(action_selected==0){
     encounter_t enc;if(world_get_encounter_uid(event.uid,&enc)){
      nav_ctx_t *ctx=nav_ctx();*ctx=(nav_ctx_t){.enc=enc,.uid=enc.uid,.valid=true,.exploring=true};nav_go(PAGE_BATTLE);return;
     }feedback="伙伴已离开 请继续探索";
    }
    event=(exploration_event_t){0};action_selected=0;
   }else if(action_selected==1){routes=true;route_options();feedback=NULL;}
   else if(action_selected==2){activities=true;activity_selected=0;}
   else{
    if(view.route>=4){paths=true;selected=0;draw_all();return;}
    event=world_explore();action_selected=0;
    if(event.kind==EXPLORE_ENCOUNTER||event.kind==EXPLORE_CLUE||event.kind==EXPLORE_TARGET){animating=true;started=millis();feedback=NULL;}
    else {feedback=error_text(event.kind);event=(exploration_event_t){0};}
   }
  }
 }
 world_exploration_snapshot(&view);draw_all();
}
