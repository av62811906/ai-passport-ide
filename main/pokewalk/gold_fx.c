// Source-derived display tracks. Original script/motion runs offline; firmware
// replays immutable tiles, OAM, battler tile masks and scanline background state.
#include "gold_fx.h"
#include "gold_fx_data.h"
#include "screen.h"

// One bit per current-band pixel; keep OBJ-behind-BG priority independent of
// other OAM entries without allocating another RGB frame buffer.
static uint8_t background_ink[240*80/8];

static const gold_span_t *clip_for(const battle_round_t *r) {
    if (!r || !r->move_id || r->move_id > 250 || r->missed || r->no_effect || r->skipped) return 0;
    unsigned id = gold_lookup[r->move_id][!r->by_pet][!!r->charging];
    return id == 65535 ? 0 : &gold_clips[id];
}
bool gold_fx_enabled(const battle_round_t *r) { return clip_for(r) != 0; }
uint8_t gold_fx_frame_count(const battle_round_t *r) {
    const gold_span_t *clip=clip_for(r);
    return clip ? clip->count + (r->damage ? 12 : 0) + 2 : 0;
}
static void pixel(int x,int y,int band,uint16_t color) {
    if(x>=0&&x<240&&y>=0&&y<240&&y>=band&&y<band+80)screen_px(x,y-band,color);
}
static unsigned raw_index(unsigned ref,unsigned x,unsigned y) {
    const uint8_t *tile=gold_tiles[ref];
    return ((tile[y*2]>>(7-x))&1)|(((tile[y*2+1]>>(7-x))&1)<<1);
}
static uint16_t rgb555(const uint8_t *p) {
    unsigned c=p[0]|p[1]<<8,r=c&31,g=(c>>5)&31,b=(c>>10)&31;
    return (r<<11)|((g<<1|g>>4)<<5)|b;
}
static unsigned actor_mapping(const gold_frame_t *frame,unsigned slot) {
    unsigned mapping=0;static const uint16_t gray[4]={32767,25368,12684,0};
    for(unsigned k=0;k<4;k++){
        const uint8_t *p=&gold_palettes[frame->palette][slot*8+k*2];
        unsigned value=p[0]|p[1]<<8,mapped=k;
        for(unsigned n=0;n<4;n++)if(value==gray[n])mapped=n;
        mapping|=mapped<<(2*k);
    }
    return mapping;
}
static bool actor_pixel(const battle_fx_actor_t *art,unsigned tile,unsigned x,unsigned y,uint16_t *color,unsigned bgp) {
    if(!art||!art->data)return false;
    unsigned n=tile<49?7:6;unsigned id=tile<49?tile:tile-49;
    int px=(id/n)*8+x-((int)n*8-art->w)/2;
    int py=(id%n)*8+y-((int)n*8-art->h)/2;
    if(px<0||py<0||px>=art->w||py>=art->h)return false;
    unsigned index=(art->data[py*((art->w+3)/4)+px/4]>>(6-2*(px%4)))&3;
    if(index==3)return false;
    unsigned mapped=(bgp>>(2*(3-index)))&3;
    *color=art->palette[3-mapped];return true;
}
static void clear_actor(int band,bool pet,uint16_t background) {
    int x0=pet?8:124,y0=pet?140:24,size=pet?96:112;
    for(int y=y0>band?y0:band;y<y0+size&&y<band+80;y++)for(int x=x0;x<x0+size;x++)pixel(x,y,band,background);
}
static int map_x(int x){return 56+(x-40)*124/84;}
static int map_y(int y){return 188+(y-72)*108/44;}
// BattleAnimSub_Beam joins four separate objects into one continuous beam.
// Project their pixels in one coordinate space: mapping each object's origin
// while keeping its tiles at 2x introduces overlapping, offset beam segments.
static bool joined_beam(const battle_round_t *r) {
    return r->move_id==62||r->move_id==63||(r->move_id==76&&!r->charging);
}
void gold_fx_draw(const battle_round_t *r,uint8_t frame,int band,
                  const battle_fx_actor_t *pet,const battle_fx_actor_t *wild) {
    const gold_span_t *clip=clip_for(r);
    if(!clip||frame>=clip->count||band<0||band>=240)return;
    const gold_frame_t *f=&gold_frames[gold_clip_frames[clip->offset+frame]];
    // DMG palette effects also apply to the HUD/background. Preserve existing
    // colors for unchanged shade mappings; blackout/whiteout use exact ends.
    if(f->bgp!=0xe4) {
        uint16_t *buf=screen_band();
        for(int i=0;i<240*80;i++){
            uint16_t c=__builtin_bswap16(buf[i]);
            unsigned light=((c>>11)&31)*3+((c>>5)&63)*3+(c&31);
            unsigned shade=light>270?0:light>175?1:light>65?2:3;
            unsigned mapped=(f->bgp>>(2*shade))&3;
            if(mapped!=shade)buf[i]=__builtin_bswap16(mapped==0?0xffff:mapped==3?0:mapped==1?0xce59:0x6b4d);
        }
    }
    unsigned bgshade=f->bgp&3;
    uint16_t background=bgshade==0?0xffff:bgshade==3?0:bgshade==1?0xce59:0x6b4d;
    if(pet)clear_actor(band,true,background);
    if(wild)clear_actor(band,false,background);
    // Reconstruct the original battler tilemap using the selected species.
    // Original column-major 7x7 front / 6x6 back tiles retain their masks.
    unsigned pet_map=actor_mapping(f,1),wild_map=actor_mapping(f,0);
    for(int gy=0;gy<96;gy++){
     if(gy*2+45<band||gy*2+24>=band+80)continue;
     for(int gx=0;gx<160;gx++) {
        int dx=f->lcd==0x43?(int8_t)gold_lines[f->lines][gy]:(int8_t)f->scx;
        int dy=f->lcd==0x42?(int8_t)gold_lines[f->lines][gy]:(int8_t)f->scy;
        unsigned sx=(gx+dx)&255,sy=(gy+dy)&255;
        if(sx>=160||sy>=96)continue;
        unsigned ref=gold_maps[f->map][(sy/8)*20+sx/8];
        if(ref==65535)continue;
        bool is_pet=ref>=0x8000?(ref&0x7fff)>=49:sy>=48&&sx<80;
        const battle_fx_actor_t *art=is_pet?pet:wild;
        if(!art)continue;
        uint16_t color;
        if(ref>=0x8000){if(!actor_pixel(art,ref&0x7fff,sx%8,sy%8,&color,is_pet?pet_map:wild_map))continue;}
        else{unsigned index=raw_index(ref,sx%8,sy%8);if(!index)continue;color=art->palette[3-((f->bgp>>(2*index))&3)];}
        int x=(is_pet?8:124)+(gx-(is_pet?16:96))*2;
        int y=(is_pet?140:24)+(gy-(is_pet?48:0))*2;
        for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++)pixel(x+xx,y+yy,band,color);
    }
    }
    const uint16_t *band_pixels=screen_band();
    uint16_t blank=__builtin_bswap16(background);
    for(unsigned byte=0;byte<sizeof(background_ink);byte++){
        unsigned bits=0;for(unsigned bit=0;bit<8;bit++)if(band_pixels[byte*8+bit]!=blank)bits|=1u<<bit;
        background_ink[byte]=bits;
    }
    const gold_span_t *span=&gold_object_spans[f->objects];
    // Earlier OAM entries have priority. Tile offsets are relative to each
    // original object origin, preserving the shape of multi-tile composites.
    for(int j=(int)span->count-1;j>=0;j--){
        const gold_object_t *o=&gold_objects[span->offset+j];
        bool project=joined_beam(r)&&o->tile<0x8000;
        int x=map_x((int)o->x-o->dx-8)+o->dx*2;
        int y=map_y((int)o->y-o->dy-16)+o->dy*2;
        if(o->tile>=0x8000){
            bool is_pet=(o->tile&0x7fff)>=49;
            x=(is_pet?8:124)+((int)o->x-8-(is_pet?16:96))*2;
            y=(is_pet?140:24)+((int)o->y-16-(is_pet?48:0))*2;
        }
        if(project){x=map_x((int)o->x-8);y=map_y((int)o->y-16);}
        int right=project?map_x(o->x):x+16;
        int bottom=project?map_y((int)o->y-8):y+16;
        if(bottom<=band||y>=band+80||right<=0||x>=240)continue;
        for(unsigned yy=0;yy<8;yy++)for(unsigned xx=0;xx<8;xx++){
            unsigned sx=o->flags&0x20?7-xx:xx,sy=o->flags&0x40?7-yy:yy;
            uint16_t color;
            if(o->tile>=0x8000){unsigned tile=o->tile&0x7fff;const battle_fx_actor_t *art=tile<49?wild:pet;unsigned mapping=actor_mapping(f,o->flags&7);
                if(!actor_pixel(art,tile,sx,sy,&color,mapping))continue;}
            else{unsigned index=raw_index(o->tile,sx,sy);if(!index)continue;color=rgb555(&gold_palettes[f->palette][(o->flags&7)*8+index*2]);}
            int left=project?map_x((int)o->x-8+(int)xx):x+xx*2;
            int top=project?map_y((int)o->y-16+(int)yy):y+yy*2;
            int end_x=project?map_x((int)o->x-7+(int)xx):left+2;
            int end_y=project?map_y((int)o->y-15+(int)yy):top+2;
            for(int dy=top;dy<end_y;dy++)for(int dx=left;dx<end_x;dx++){
                if(dx<0||dx>=240||dy<band||dy>=band+80||dy>=240)continue;
                unsigned at=(dy-band)*240+dx;
                if((o->flags&0x80)&&(background_ink[at/8]&(1u<<(at%8))))continue;
                pixel(dx,dy,band,color);
            }
        }
    }
}
