#pragma once
#include <stdbool.h>
#include <stdint.h>
// Persistent V21 bit assignments: IDs 1..165, then this append-only extra list.
// Never reorder/reuse bits; expanding past 192 requires an explicit save migration.
#define MOVE_POLICY_BYTES 24
typedef struct { uint8_t disabled[MOVE_POLICY_BYTES]; } move_policy_t;
static inline int move_policy_bit(unsigned id) {
 if(id>=1 && id<=165)return (int)id-1;
 static const uint8_t extra[]={172,181,183,185,186,188,189,192,196,198,200,202,204,206,211,223,225,231,238,239,242,245,246,247,249,250};
 for(unsigned i=0;i<sizeof(extra);i++)if(id==extra[i])return 165+i;
 return -1;
}
static inline bool move_policy_allows(const move_policy_t *p,unsigned id) {
 int bit=move_policy_bit(id);
 return id==165||!p||bit<0||!(p->disabled[bit/8]&(1u<<(bit%8)));
}
static inline void move_policy_set(move_policy_t *p,unsigned id,bool enabled) {
 int bit=move_policy_bit(id);if(!p||bit<0)return;
 if(enabled)p->disabled[bit/8]&=~(1u<<(bit%8));else p->disabled[bit/8]|=1u<<(bit%8);
}
static inline bool move_policy_valid(const move_policy_t *p) {return p && !(p->disabled[23]&0x80);}
