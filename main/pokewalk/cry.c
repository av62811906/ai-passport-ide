#include "cry.h"
#include "audio.h"
#include <string.h>
typedef struct {uint8_t frames,envelope;uint16_t frequency;uint8_t duty,reset;} cry_note_t;
typedef struct {const cry_note_t *notes;uint16_t count;} cry_track_t;
typedef cry_track_t cry_score_t[3];
typedef struct {uint8_t base;uint16_t pitch,length;} cry_species_t;
#include "cry_data.h"
static uint32_t samples(unsigned frames){return (uint64_t)frames*AUDIO_SAMPLE_RATE*10000/597275;}
static unsigned duration(const cry_note_t *n,unsigned tempo,uint32_t *remainder){
 unsigned t=n->frames*tempo+*remainder;*remainder=t%256;return t/256?t/256:1;
}
uint32_t cry_duration_ms(uint16_t id){
 if(!id||id>151)return 0;
 const cry_species_t *sp=&cry_species[id];unsigned max=0;
 for(unsigned ch=0;ch<3;ch++){
  const cry_track_t *t=&cry_scores[sp->base][ch];unsigned frames=0;uint32_t rem=0;
  for(unsigned n=0;n<t->count;n++)frames+=duration(&t->notes[n],ch==2?256:sp->length,&rem);
  if(frames>max)max=frames;
 }
 return (samples(max)*1000u+AUDIO_SAMPLE_RATE-1)/AUDIO_SAMPLE_RATE;
}
void cry_start(cry_player_t *p,uint16_t id){memset(p,0,sizeof(*p));p->species=id<=151?id:0;}
bool cry_sample(cry_player_t *p,int16_t *out){
 *out=0;if(!p->species)return false;
 const cry_species_t *sp=&cry_species[p->species];bool active=false;int mix=0;
 unsigned source_frame=(uint64_t)p->position*597275/(AUDIO_SAMPLE_RATE*10000u);
 for(unsigned ch=0;ch<3;ch++){
  cry_voice_t *v=&p->voice[ch];const cry_track_t *t=&cry_scores[sp->base][ch];
  if(v->age>=v->length){
   if(v->note>=t->count)continue;
   const cry_note_t *n=&t->notes[v->note++];
   v->age=0;v->length=samples(duration(n,ch==2?256:sp->length,&v->remainder));
   v->envelope=n->envelope;v->duty=n->duty;if(n->reset)v->duty_frame=source_frame;
   unsigned frequency=n->frequency+sp->pitch;
   if(ch==2){
    unsigned reg=frequency&255,div=reg&7;v->noise=reg&8;v->lfsr=0x7fff;v->phase=0;
    unsigned hz=(div?524288u/div:1048576u) >> ((reg>>4)+1);
    v->increment=(uint64_t)hz*65536/AUDIO_SAMPLE_RATE;
   }else v->increment=(uint64_t)131072*65536/((2048-(frequency&2047))*AUDIO_SAMPLE_RATE);
  }
  active=true;int volume=v->envelope>>4;unsigned period=v->envelope&7;
  if(period){int delta=(uint64_t)v->age*64/(AUDIO_SAMPLE_RATE*period);volume+=(v->envelope&8)?delta:-delta;}
  if(volume<0)volume=0;
  if(volume>15)volume=15;
  int sign;
  if(ch==2){
   v->phase+=v->increment;
   while(v->phase>=65536){v->phase-=65536;unsigned bit=(v->lfsr^(v->lfsr>>1))&1;v->lfsr=(v->lfsr>>1)|(bit<<14);if(v->noise)v->lfsr=(v->lfsr&~64u)|(bit<<6);}
   sign=(v->lfsr&1)?-1:1;
  }else{
   v->phase=(v->phase+v->increment)&65535;
   unsigned duty=(v->duty>>(6-2*((source_frame-v->duty_frame)%4)))&3;
   static const uint8_t pattern[4]={0x01,0x81,0x87,0x7e};
   sign=(pattern[duty]>>(v->phase>>13))&1?1:-1;
  }
  mix+=sign*volume*180;v->age++;
 }
 p->position++;*out=(int16_t)mix;return active;
}
