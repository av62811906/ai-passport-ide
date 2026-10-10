#include "achievements.h"
enum { DEX, SHINY, EVOLUTION, BADGES, CHAMPION, RED };
static const achievement_info_t INFO[ACHIEVEMENT_COUNT] = {
 {"新的伙伴","图鉴捕获种类达到2",DEX,2,ITEM_GREAT,5},
 {"小小收藏家","图鉴捕获种类达到5",DEX,5,ITEM_THUNDER_STONE,1},
 {"旅行收藏家","图鉴捕获种类达到10",DEX,10,ITEM_WATER_STONE,1},
 {"图鉴研究员","图鉴捕获种类达到25",DEX,25,ITEM_LEAF_STONE,1},
 {"图鉴专家","图鉴捕获种类达到50",DEX,50,ITEM_LINK_MACHINE,1},
 {"百种相遇","图鉴捕获种类达到100",DEX,100,ITEM_GROWTH_MACHINE,2},
 {"关都全图鉴","图鉴捕获种类达到151",DEX,151,ITEM_MASTER,1},
 {"闪耀的相遇","捕获1种闪光宝可梦",SHINY,1,ITEM_MOON_STONE,1},
 {"追逐星光","捕获3种闪光宝可梦",SHINY,3,ITEM_MASTER,1},
 {"成长的瞬间","完成1次宝可梦进化",EVOLUTION,1,ITEM_FIRE_STONE,1},
 {"进化研究家","完成5次宝可梦进化",EVOLUTION,5,ITEM_LINK_MACHINE,1},
 {"第一枚徽章","获得1枚道馆徽章",BADGES,1,ITEM_MILK,5},
 {"道馆挑战者","获得4枚道馆徽章",BADGES,4,ITEM_MOON_STONE,1},
 {"八枚徽章","获得8枚道馆徽章",BADGES,8,ITEM_GROWTH_MACHINE,1},
 {"联盟冠军","战胜联盟冠军青绿",CHAMPION,1,ITEM_MASTER,1},
 {"白银山之巅","战胜白银山的赤红",RED,1,ITEM_LINK_MACHINE,2},
};
const achievement_info_t *achievement_info(unsigned id) { return id<ACHIEVEMENT_COUNT?&INFO[id]:0; }
void achievement_view(achievement_view_t *out,const achievement_store_t *store,const dex_t *dex,uint16_t defeated) {
 *out=(achievement_view_t){.store=*store,.defeated=defeated};
 out->caught=dex_count_caught(dex);
 for(unsigned i=0;i<DEX_SPECIES;i++)out->shiny+=!!(dex->shiny_caught[i/8]&(1u<<(i%8)));
}
unsigned achievement_progress(const achievement_view_t *v,unsigned id) {
 const achievement_info_t *a=achievement_info(id);if(!a||!v)return 0;
 unsigned n=0;
 switch(a->kind){case DEX:n=v->caught;break;case SHINY:n=v->shiny;break;case EVOLUTION:n=v->store.evolutions;break;
 case BADGES:for(unsigned i=0;i<8;i++)n+=!!(v->defeated&(1u<<i));break;
 case CHAMPION:n=!!(v->defeated&(1u<<12));break;case RED:n=!!(v->defeated&(1u<<13));break;}
 return n>a->target?a->target:n;
}
achievement_claim_t achievement_claim(achievement_store_t *s,inventory_t *bag,const achievement_view_t *v,unsigned id) {
 const achievement_info_t *a=achievement_info(id);if(!a)return ACH_CLAIM_LOCKED;
 if(s->claimed&(1u<<id))return ACH_CLAIM_ALREADY;
 if(achievement_progress(v,id)<a->target)return ACH_CLAIM_LOCKED;
 unsigned total=(unsigned)bag->quantity[a->item]+a->quantity;
 unsigned cap=items_capacity(a->item);
 bag->quantity[a->item]=total>cap?cap:total;
 s->claimed|=1u<<id;
 return total>cap?ACH_CLAIM_CAPPED:ACH_CLAIM_OK;
}
