#include "music.h"
#include <stddef.h>
#include <string.h>

typedef struct { uint16_t frames; int8_t midi; uint8_t volume, duty; int8_t envelope; } music_note_t;
typedef struct { const music_note_t *notes; uint16_t count, loop; uint8_t channel; } music_track_t;
typedef struct { const char *name; const music_track_t *tracks; uint8_t count; } music_score_t;
#include "music_assets.h"
_Static_assert(sizeof(MUSIC_SCORES)/sizeof(MUSIC_SCORES[0])==MUSIC_COUNT,"music ID/catalog mismatch");
const char *music_name(music_id_t id) { return id>0 && id<MUSIC_COUNT ? MUSIC_SCORES[id].name : "none"; }
void music_player_start(music_player_t *p, music_id_t id) {
 memset(p,0,sizeof(*p));p->id=(unsigned)id<MUSIC_COUNT?id:MUSIC_NONE;
 for(unsigned i=0;i<4;i++)p->voice[i].noise=0x7fff;
}
static int32_t voice_sample(music_voice_t *v,const music_track_t *t) {
 if(v->length && v->position>=v->length) { v->note++;v->position=v->length=0; }
 if(v->note>=t->count) { if(t->loop>=t->count)return 0;v->note=t->loop; }
 const music_note_t *n=&t->notes[v->note];
 if(!v->length) {
  // Original 70224-clock frames, retaining fractional samples across loop edges.
  uint64_t samples=(uint64_t)n->frames*70224u*AUDIO_SAMPLE_RATE+v->duration_remainder;
  v->length=(uint32_t)(samples/4194304u);v->duration_remainder=samples%4194304u;
  v->increment=n->midi<0?0:(uint32_t)(((uint64_t)audio_note_hz_q8(n->midi)<<24)/AUDIO_SAMPLE_RATE);
 }
 uint32_t position=v->position++;
 if(n->midi<0)return 0;
 uint32_t old=v->phase;v->phase+=v->increment;
 int32_t wave;
 if(t->channel==4) {
  if(v->phase<old) {unsigned bit=(v->noise^(v->noise>>1))&1;v->noise=(v->noise>>1)|(bit<<14);}
  wave=(v->noise&1)?1024:-1024;
 } else if(t->channel==3) {
  unsigned x=v->phase>>21;
  wave=x<1024?(int)x*2-1024:3071-(int)x*2;
 } else {
  static const uint32_t threshold[]={0x20000000,0x40000000,0x80000000,0xc0000000};
  wave=v->phase<threshold[n->duty&3]?1024:-1024;
 }
 int volume=n->volume;
 if(t->channel==3) volume=volume==1?12:volume==2?6:volume==3?3:0;
 else if(n->envelope) {
  unsigned step=(unsigned)(n->envelope<0?-n->envelope:n->envelope);
  int change=(int)((uint64_t)position*64u/(AUDIO_SAMPLE_RATE*step));
  volume+=n->envelope<0?change:-change;
  if(volume<0)volume=0;
 if(volume>15)volume=15;
 }
 return wave*volume/12;
}
void music_player_render(music_player_t *p,uint32_t count,int16_t *out) {
 if(!p||!out)return;
 const music_score_t *score=&MUSIC_SCORES[p->id];
 for(uint32_t i=0;i<count;i++) {
  int32_t mixed=0;
  for(unsigned ch=0;ch<score->count;ch++)mixed+=voice_sample(&p->voice[ch],&score->tracks[ch]);
  out[i]=(int16_t)mixed; // Four voices bounded below 5120; mixer retains SFX headroom.
 }
}
