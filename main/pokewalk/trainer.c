#include "trainer.h"
#include <string.h>
#include "trainer_assets.h"
#include "combat.h"
#include "items.h"
#include "exp.h"
#include "route_trainer_assets.h"
static const trainer_info_t CATALOG[TRAINER_COUNT] = {
 {"小刚","灰色徽章",{74,95},{10,12},2,0},
 {"小霞","蓝色徽章",{120,121},{17,19},2,0},
 {"马志士","橙色徽章",{100,25,26},{23,24,26},3,0},
 {"莉佳","彩虹徽章",{114,70,45},{29,30,32},3,0},
 {"阿桔","粉红徽章",{110,89,42,49},{36,37,38,39},4,0},
 {"娜姿","金色徽章",{64,122,97,65},{40,41,42,43},4,0},
 {"夏伯","深红徽章",{38,78,126,59},{45,46,47,48},4,0},
 {"坂木","绿色徽章",{51,31,34,112},{50,51,52,53},4,0},
 {"科拿","四天王",{87,91,80,124,131},{54,54,55,55,56},5,1},
 {"希巴","四天王",{95,107,106,95,68},{55,55,56,56,57},5,1},
 {"菊子","四天王",{94,42,93,24,94},{56,56,57,57,58},5,1},
 {"阿渡","四天王",{130,148,148,142,149},{58,58,59,59,60},5,1},
 {"青绿","联盟冠军",{18,65,112,6,130,103},{60,60,61,61,62,63},6,2},
 {"赤红","最终挑战",{25,131,143,3,6,9},{81,75,75,77,77,77},6,3},
};
static const trainer_info_t ROUTE_TRAINERS[TRAINER_ROUTE_COUNT]={
 {"捕虫少年","森林切磋",{10},{0},1,4},{"森林巡护员","林间伙伴",{12,17},{0},2,4},{"森林高手","森林特训",{123,45,127},{0},3,4},
 {"登山少年","山洞切磋",{74},{0},1,4},{"登山客","岩壁伙伴",{75,42},{0},2,4},{"山地高手","山地特训",{76,95,68},{0},3,4},
 {"钓鱼少年","海边切磋",{60},{0},1,4},{"钓鱼好手","潮汐伙伴",{61,117},{0},2,4},{"海边高手","海边特训",{62,131,130},{0},3,4},
 {"电站学徒","电站切磋",{81},{0},1,4},{"电站研究员","电流伙伴",{82,101},{0},2,4},{"电站高手","电站特训",{125,135,110},{0},3,4}
};
bool trainer_is_route(unsigned id){return id>=TRAINER_ROUTE_FIRST&&id<TRAINER_TOTAL;}
const trainer_info_t *trainer_info(uint8_t id) {return id<TRAINER_COUNT?&CATALOG[id]:trainer_is_route(id)?&ROUTE_TRAINERS[id-TRAINER_ROUTE_FIRST]:NULL;}
void trainer_art(uint8_t id,const uint8_t **data,uint16_t palette[4]) {
 if(trainer_is_route(id)){unsigned r=(id-TRAINER_ROUTE_FIRST)/3;*data=ROUTE_TRAINER_ART[r];memcpy(palette,ROUTE_TRAINER_PAL[r],8);return;}
 if(id>=TRAINER_COUNT){*data=0;return;}*data=TRAINER_ART[id];memcpy(palette,TRAINER_PAL[id],8);
}
const uint8_t *trainer_badge_art(uint8_t id) { return id<8?BADGE_ART[id]:0; }
const uint8_t *trainer_player_art(void) { return TRAINER_ART[14]; }
bool trainer_move(uint16_t id,move_t *out) {return combat_move(id,out);}
static trainer_mon_t *actor(trainer_session_t *s,unsigned side) { return &s->sides[side].mons[s->sides[side].active]; }
static void init_mon(trainer_mon_t *mon,uint8_t species,uint8_t level) {
 combat_init(mon,species,level,combat_max_hp(species,level));
}
bool trainer_unlocked(const trainer_store_t *st,uint8_t id) {
 if(!st||id>=TRAINER_TOTAL)return false;
 if(trainer_is_route(id)){unsigned badges=0;for(unsigned i=0;i<8;i++)badges+=!!(st->defeated&(1u<<i));return !st->league_active&&badges>=(unsigned[]){0,2,5}[(id-TRAINER_ROUTE_FIRST)%3];}
 if(st->league_active)return id==st->league_stage;
 if(id<8)return id==0?st->wild_wins>0:(st->defeated&(1u<<(id-1)))!=0;
 if(id<13)return st->league_active?st->league_stage==id:id==8&&(st->defeated&255u)==255u;
 return (st->defeated&(1u<<12))!=0;
}
unsigned trainer_stamina_cost(const trainer_store_t *st,uint8_t id) {
 if(!st||id>=TRAINER_TOTAL)return 0;
 if(trainer_is_route(id))return 5;
 if(id>=8&&id<=12)return st->league_active&&id==st->league_stage&&id>8?0:20;
 return 10;
}
static void choose_first(trainer_session_t *s,const move_policy_t *policies) {
 s->planned[0]=combat_choose_filtered(actor(s,0),actor(s,1),&s->rng,policies?&policies[s->sides[0].active]:NULL);
 s->planned[1]=combat_choose(actor(s,1),actor(s,0),&s->rng);
 unsigned ps=combat_speed(actor(s,0)),es=combat_speed(actor(s,1));
 int priority=combat_priority(s->planned[0])-combat_priority(s->planned[1]);
 s->next=priority?priority>0?0:1:ps==es?(s->rng&1):ps>es?0:1;s->acted=0;
}
bool trainer_rematch(const trainer_store_t *st,uint8_t id){return st&&id<8&&(st->defeated&(1u<<12))&&(st->defeated&(1u<<id));}
static const uint8_t REMATCH[8][6]={
 {76,95,112,142,139,141},{121,130,131,134,73,9},{26,101,82,125,135,25},{45,71,103,114,3,47},
 {110,89,42,49,73,94},{65,97,122,103,80,124},{59,78,126,136,6,38},{112,31,34,51,105,76}
};
unsigned trainer_route_level(unsigned id,const trainer_store_t *st,const mon_t *party,unsigned count){
 if(!trainer_is_route(id)||!st||!party||!count)return 0;
 unsigned highest=1,badges=0;for(unsigned i=0;i<count;i++)if(party[i].level>highest)highest=party[i].level;
 for(unsigned i=0;i<8;i++)badges+=!!(st->defeated&(1u<<i));
 unsigned cap=(st->defeated&(1u<<12))?100:12+badges*7;
 unsigned level=highest*9/10+2*((id-TRAINER_ROUTE_FIRST)%3);
 return level<2?2:level>cap?cap:level;
}
bool trainer_begin(trainer_store_t *st,uint8_t id,const mon_t *party,uint8_t count,uint16_t ability,uint32_t seed) {return trainer_begin_filtered(st,id,party,count,ability,seed,NULL);}
bool trainer_begin_filtered(trainer_store_t *st,uint8_t id,const mon_t *party,uint8_t count,uint16_t ability,uint32_t seed,const move_policy_t *policies) {
 if(!st||!party||!count||count>6||st->session.active||!trainer_unlocked(st,id))return false;
 for(unsigned i=0;i<count;i++)if(!party[i].species_id||party[i].species_id>151||!party[i].level||party[i].level>100)return false;
 trainer_side_t retained=st->session.sides[0];bool continuing=st->league_active&&id>8&&id<=12;
 trainer_session_t *s=&st->session;memset(s,0,sizeof(*s));s->trainer=id;s->active=1;s->rng=seed?seed:1;s->ability=ability;
 s->sides[0].count=count;
 for(unsigned i=0;i<count;i++)init_mon(&s->sides[0].mons[i],party[i].species_id,party[i].level);
 if(continuing){
  s->sides[0]=retained;s->sides[0].reflect=s->sides[0].light_screen=0;
  for(unsigned i=0;i<retained.count;i++){trainer_mon_t *m=&s->sides[0].mons[i];combat_reset_volatile(m);}
 }
 if(id==8){st->league_active=1;st->league_stage=8;}
 const trainer_info_t *t=trainer_info(id);s->sides[1].count=t->count;
 if(trainer_is_route(id)){
  unsigned level=trainer_route_level(id,st,party,count);
  for(unsigned i=0;i<t->count;i++)init_mon(&s->sides[1].mons[i],t->species[i],level);
 }else if(trainer_rematch(st,id)){
  s->sides[1].count=6;unsigned rotate=s->rng%6;
  for(unsigned i=0;i<6;i++)init_mon(&s->sides[1].mons[i],REMATCH[id][(i+rotate)%6],65+id+i/2);
 }else for(unsigned i=0;i<t->count;i++)init_mon(&s->sides[1].mons[i],t->species[i],t->levels[i]);
 s->participated=1u<<s->sides[0].active;choose_first(s,policies);
 return true;
}
static unsigned living(const trainer_side_t *side) { for(unsigned i=0;i<side->count;i++)if(side->mons[i].hp)return i;
 return 6; }
static void finish(trainer_store_t *st,bool won) { st->session.finished=1;st->session.won=won; }
bool trainer_step(trainer_store_t *st,trainer_event_t *e) {return trainer_step_filtered(st,e,NULL);}
bool trainer_step_filtered(trainer_store_t *st,trainer_event_t *e,const move_policy_t *policies) {
 if(!st||!e||!st->session.active||st->session.finished)return false;
 memset(e,0,sizeof(*e));trainer_session_t *s=&st->session;
 for(unsigned side=0;side<2;side++)if(!actor(s,side)->hp) {
  unsigned next=living(&s->sides[side]);
  if(next==6){finish(st,side==1);e->kind=TRAINER_FINISHED;
 return true;}
  if(side==0){s->awaiting_replacement=1;e->kind=TRAINER_SWITCH_NEEDED;
 return true;}
  s->sides[side].active=next;choose_first(s,policies);e->kind=TRAINER_SENDOUT;e->side=side;e->slot=next;
 return true;
 }
 if(s->turns>=400){finish(st,false);e->kind=TRAINER_FINISHED;
 return true;}
 if(s->awaiting_replacement)return false;
 if(s->acted==3)choose_first(s,policies);
 unsigned side=s->next;s->next^=1;s->acted|=1u<<side;s->turns++;
 trainer_mon_t *a=actor(s,side),*d=actor(s,side^1);
 e->kind=TRAINER_ATTACK;e->side=side;e->before_hp[0]=actor(s,0)->hp;e->before_hp[1]=actor(s,1)->hp;
 battle_round_t *r=&e->attack;r->by_pet=side==0;
 a->reflect=s->sides[side].reflect;a->light_screen=s->sides[side].light_screen;
 d->reflect=s->sides[side^1].reflect;d->light_screen=s->sides[side^1].light_screen;
 combat_turn_filtered(a,d,side?1024:s->ability,&s->rng,50,s->planned[side],r,side||!policies?NULL:&policies[s->sides[0].active]);
 if(s->acted==3)combat_finish_round(actor(s,0),actor(s,1),r);
 if(s->acted&(1u<<(side^1)))d->flinch=0;
 s->sides[side].reflect=a->reflect;s->sides[side].light_screen=a->light_screen;
 s->pending_move=0;
 if(r->skipped){e->kind=TRAINER_STATUS;e->status=a->status;}
 r->pet_hp=actor(s,0)->hp;r->wild_hp=actor(s,1)->hp;
 return true;
}
// Retained ABI for old debug clients. Automatic combat never accepts manual moves.
bool trainer_choose_move(trainer_store_t *st,uint8_t slot) {(void)st;(void)slot;return false;}
bool trainer_switch(trainer_store_t *st,uint8_t slot,bool forced) {return trainer_switch_filtered(st,slot,forced,NULL);}
bool trainer_switch_filtered(trainer_store_t *st,uint8_t slot,bool forced,const move_policy_t *policies) {
 trainer_session_t *s=&st->session;
 if(!s->active||s->finished||slot>=s->sides[0].count||!s->sides[0].mons[slot].hp||slot==s->sides[0].active)return false;
 if(forced!=!!s->awaiting_replacement)return false;
 trainer_mon_t *old=actor(s,0);combat_reset_volatile(old);
 s->pending_move=0;s->sides[0].active=slot;s->participated|=1u<<slot;s->awaiting_replacement=0;
 if(forced)choose_first(s,policies);else {s->next=1;s->acted=1;}
 return true;
}
void trainer_retire(trainer_store_t *st) { if(st->session.active){st->session.retired=1;finish(st,false);} }
uint16_t trainer_reward(const trainer_store_t *st) {
 if(!st->session.finished||st->session.retired)return 0;
 const trainer_info_t *t=trainer_info(st->session.trainer);unsigned reward=0;
 for(unsigned i=0;i<st->session.sides[1].count;i++)
  if(st->session.won||i<=st->session.sides[1].active)reward+=exp_battle_base(st->session.sides[1].mons[i].level);
 return st->session.won?reward:reward*30/100;
}
void trainer_settle(trainer_store_t *st) {
 trainer_session_t *s=&st->session;
 if(!s->active||!s->finished)return;
 if(s->won&&!trainer_is_route(s->trainer))st->defeated|=1u<<s->trainer;
 if(s->trainer>=8&&s->trainer<=12) {
  if(s->won&&s->trainer<12)st->league_stage=s->trainer+1;
  else {st->league_active=0;st->league_stage=0;}
 }
 s->active=0;
}
bool trainer_store_valid(const trainer_store_t *st) {
 if(!st||st->defeated>=1u<<TRAINER_COUNT||st->league_active>1||st->league_stage>12)return false;
 const trainer_session_t *s=&st->session;
 move_t planned;for(unsigned i=0;i<2;i++)if(s->planned[i]&&!combat_move(s->planned[i],&planned))return false;
 if(st->league_active&&(st->league_stage<8||st->league_stage>12))return false;
 if(s->active>1||s->finished>1||s->won>1||s->awaiting_replacement>1||s->retired>1||s->acted>3||s->pending_move>4)return false;
 if(!s->active&&!st->league_active&&!s->finished)return true;
 if(s->trainer>=TRAINER_TOTAL||(trainer_is_route(s->trainer)&&st->league_active)||s->active>1||s->finished>1||s->won>1||s->next>1||s->ability>2048||s->turns>400)return false;
 for(unsigned side=0;side<2;side++) {
  const trainer_side_t *t=&s->sides[side];
 if(!t->count||t->count>6||t->active>=t->count||t->reflect>5||t->light_screen>5)return false;
  for(unsigned i=0;i<t->count;i++) {
   const trainer_mon_t *m=&t->mons[i];
 if(!combat_valid(m))return false;
  }
 }

 return true;
}

uint8_t trainer_rematch_prize(const trainer_store_t *st){
 static const uint8_t prizes[8]={ITEM_MOON_STONE,ITEM_WATER_STONE,ITEM_THUNDER_STONE,ITEM_LEAF_STONE,ITEM_LINK_MACHINE,ITEM_GROWTH_MACHINE,ITEM_FIRE_STONE,ITEM_LINK_MACHINE};
 if(!st||!st->session.finished||!st->session.won||!trainer_rematch(st,st->session.trainer))return ITEM_NONE;
 return prizes[st->session.trainer];
}

const char *trainer_victory_line(uint8_t id){
 static const char *lines[]={"你的意志比岩石还坚定！","你们配合得真好！","好一场充满力量的对战！","我感受到了伙伴的信赖。","你的判断突破了我的战术。","你与伙伴的心意相通。","你们的热情胜过火焰！","这份实力，值得我认可。"};
 return trainer_is_route(id)?"下次再与伙伴一起来切磋！":id<8?lines[id]:id==13?"……！": "你已经证明了自己的实力。";
}
void trainer_grant_items(const trainer_store_t *st,inventory_t *bag){
 if(!st||!bag||!st->session.active||!st->session.finished||!st->session.won)return;
 bool route=trainer_is_route(st->session.trainer);
 bool first=!route&&!(st->defeated&(1u<<st->session.trainer));
 unsigned gift[ITEM_COUNT]={0};gift[ITEM_POKE]=route?1:first?5:2;gift[ITEM_BERRY]=first?3:2;gift[ITEM_MILK]=first?2:1;
 static const uint8_t stones[]={ITEM_MOON_STONE,ITEM_WATER_STONE,ITEM_THUNDER_STONE,ITEM_LEAF_STONE,ITEM_LINK_MACHINE,ITEM_GROWTH_MACHINE,ITEM_FIRE_STONE,ITEM_MOON_STONE};
 if(first&&st->session.trainer<8)gift[stones[st->session.trainer]]=1;
 uint8_t prize=trainer_rematch_prize(st);if(prize!=ITEM_NONE)gift[prize]++;
 for(unsigned i=0;i<ITEM_COUNT;i++){unsigned n=bag->quantity[i]+gift[i];bag->quantity[i]=n>items_capacity(i)?items_capacity(i):n;}
}
