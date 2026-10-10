#include "music_director.h"
#include "nav.h"
#include "sfx.h"
static music_id_t s_current;
music_id_t music_director_current(void) { return s_current; }
void music_director_play(music_id_t id) {
 if((unsigned)id>=MUSIC_COUNT || s_current==id)return;
 s_current=id;sfx_music_play(id);
}
void music_director_page(unsigned page) {
 switch(page) {
 case PAGE_OPENING:music_director_play(MUSIC_OAK);break;
 case PAGE_STARTER:music_director_play(MUSIC_LAB);break;
 case PAGE_IDLE:music_director_play(MUSIC_HOME);break;
 case PAGE_CARE:music_director_play(MUSIC_CENTER);break;
 case PAGE_EXPLORATION:case PAGE_ENCOUNTER:music_director_play(MUSIC_ROUTE);break;
 case PAGE_BATTLE:music_director_play(MUSIC_WILD);break;
 case PAGE_CAPTURE:break; // Preserve the battle score while aiming a ball.
 default:if(s_current==MUSIC_NONE)music_director_play(MUSIC_HOME);break;
 }
}
