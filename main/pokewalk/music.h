#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "audio.h"

typedef enum {
 MUSIC_NONE, MUSIC_OAK, MUSIC_LAB, MUSIC_HOME, MUSIC_CENTER, MUSIC_ROUTE,
 MUSIC_GYM, MUSIC_WILD, MUSIC_TRAINER, MUSIC_LEADER, MUSIC_CHAMPION,
 MUSIC_WILD_WIN, MUSIC_TRAINER_WIN, MUSIC_LEADER_WIN, MUSIC_EVOLUTION,
 MUSIC_ENCOUNTER, MUSIC_CAUGHT, MUSIC_EVOLVED, MUSIC_COUNT
} music_id_t;

typedef struct {
 uint16_t note, noise;
 uint32_t position, length, phase, increment;
 uint32_t duration_remainder;
} music_voice_t;
typedef struct { music_id_t id; music_voice_t voice[4]; } music_player_t;
void music_player_start(music_player_t *player, music_id_t id);
void music_player_render(music_player_t *player, uint32_t count, int16_t *out);
const char *music_name(music_id_t id);
