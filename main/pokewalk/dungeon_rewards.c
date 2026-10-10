#include "dungeon_rewards.h"
#include <string.h>
bool dungeon_progress_valid(const dungeon_progress_t *p){
 return p&&!(p->paid_nodes&((1u<<3)|(1u<<6)))&&p->receipt.xp<=65535&&p->last_node<8&&p->elite_seen<=1&&p->receipt.first_clear<=1&&p->receipt.first_elite<=1&&p->receipt.full<=1&&p->receipt.partner_species<=151&&p->receipt.partner_shiny<=1&&
 (!p->receipt.partner_shiny||p->receipt.partner_species)&&items_inventory_valid(&p->receipt.items);
}
void dungeon_reward_plan(unsigned node,uint32_t seed,const dungeon_progress_t *p,dungeon_receipt_t *r){
 memset(r,0,sizeof(*r));
 // Experience is a party award budget, scaled by care and distributed by world.
 r->xp=150+40*node;
 uint32_t x=seed^(0x9e3779b9u*(node+1));x^=x<<13;x^=x>>17;x^=x<<5;
 if(node==4){r->items.quantity[ITEM_BERRY]=3;r->items.quantity[ITEM_GREAT]=2;if(!p->elite_seen){r->first_elite=1;r->items.quantity[ITEM_MILK]=1;}}
 else if(node==7){
  r->xp+=300;r->items.quantity[ITEM_ULTRA]=2;
  if(!p->clears){r->first_clear=1;r->items.quantity[ITEM_LEAF_STONE]=1;}
  else if(x%100<30){static const uint8_t rare[]={ITEM_FIRE_STONE,ITEM_WATER_STONE,ITEM_THUNDER_STONE,ITEM_LEAF_STONE,ITEM_MOON_STONE,ITEM_LINK_MACHINE};r->items.quantity[rare[(x/100)%6]]=1;}
  if(p->clears==2)r->items.quantity[ITEM_LINK_MACHINE]++;
  if(p->clears==9)r->items.quantity[ITEM_GROWTH_MACHINE]++;
 }else if(node==5){r->items.quantity[ITEM_BERRY]=3;}
 else if(x%100<35){r->items.quantity[(x/100)%2?ITEM_POKE:ITEM_BERRY]=2;}
}
