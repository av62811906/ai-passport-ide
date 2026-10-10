#pragma once
#include "music.h"
#include "cry.h"
typedef struct {
 music_player_t music;
 cry_player_t cry;
 uint32_t effect_at;
 int32_t previous_input,previous_output;
 uint16_t move_id;
 uint8_t move_type;
 sfx_id_t effect;
 uint16_t fade;
 bool active, is_move, is_cry, missed;
} sound_mixer_t;
void sound_mixer_init(sound_mixer_t *m);
void sound_mixer_music(sound_mixer_t *m,music_id_t id);
void sound_mixer_effect(sound_mixer_t *m,sfx_id_t id);
void sound_mixer_move(sound_mixer_t *m,uint16_t id,uint8_t type,bool missed);
void sound_mixer_render(sound_mixer_t *m,uint32_t count,int16_t *out);

void sound_mixer_cry(sound_mixer_t *m,uint16_t species);
