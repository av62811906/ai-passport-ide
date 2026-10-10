#include "sound_mixer.h"
#include <string.h>
void sound_mixer_init(sound_mixer_t *m) { memset(m,0,sizeof(*m));music_player_start(&m->music,MUSIC_NONE); }
void sound_mixer_music(sound_mixer_t *m,music_id_t id) {
 if(m->music.id==id)return;
 music_player_start(&m->music,id);m->fade=0;
}
void sound_mixer_effect(sound_mixer_t *m,sfx_id_t id) { if((unsigned)id>=SFX_COUNT)return;m->effect=id;m->effect_at=0;m->active=true;m->is_move=false;m->is_cry=false; }
void sound_mixer_move(sound_mixer_t *m,uint16_t id,uint8_t type,bool missed) {
 m->is_cry=false;m->move_id=id;m->move_type=type%17;m->missed=missed;m->is_move=m->active=true;m->effect_at=0;
}
void sound_mixer_cry(sound_mixer_t *m,uint16_t species) { cry_start(&m->cry,species);m->is_cry=true;m->active=species>0&&species<=151;m->is_move=false; }
static int16_t move_sample(const sound_mixer_t *m,uint32_t at) {
 uint32_t ms=at*1000u/AUDIO_SAMPLE_RATE;
 if(ms>=700)return 0;
 // Type-colored square/noise layers; adapted effects, not GB SFX emulation.
 uint32_t hash=at*747796405u+2891336453u;hash=((hash>>((hash>>28)+4))^hash)*277803737u;
 int noise=(int)((hash>>20)&2047)-1024;
 unsigned frequency=90u+m->move_type*43u+(m->move_id%7)*17u;
 uint64_t cycles=(uint64_t)at*(frequency*1000u+(m->move_type&1?ms:700-ms));
 int square=(cycles/(AUDIO_SAMPLE_RATE*500u))&1?1024:-1024;
 int value=(m->move_type==1||m->move_type==2||m->move_type==10)?noise*3:square*2+noise;
 if(m->missed)value=noise;
 return (int16_t)(value*(int)(700-ms)/700);
}
void sound_mixer_render(sound_mixer_t *m,uint32_t count,int16_t *out) {
 music_player_render(&m->music,count,out);
 for(uint32_t i=0;i<count;i++) {
  int32_t background=out[i];
  if(m->fade<2048)m->fade++;
  background=background*m->fade/2048;
  int16_t effect=0;
  if(m->active) {
   if(m->is_cry) {
    if(!cry_sample(&m->cry,&effect))m->active=false;
   } else if(m->is_move) {
    effect=move_sample(m,m->effect_at++);
    if(m->effect_at>=AUDIO_SAMPLE_RATE*7u/10u)m->active=false;
   } else {
    if(audio_render(m->effect,m->effect_at,1,&effect))m->effect_at++;
    else m->active=false;
   }
   background/=3;
  }
  int32_t input=background+effect/2;
  // DC blocker (approximately 18 Hz at 22050 Hz), including asymmetric duties.
  int32_t mixed=input-m->previous_input+(int32_t)((int64_t)m->previous_output*32600/32768);
  m->previous_input=input;m->previous_output=mixed;
  if(mixed>24000)mixed=24000;
 if(mixed<-24000)mixed=-24000;
  out[i]=(int16_t)mixed;
 }
}
