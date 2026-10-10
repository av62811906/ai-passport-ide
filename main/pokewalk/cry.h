#pragma once
#include <stdint.h>
#include <stdbool.h>
// Original Gold note programs/parameters, mono GB-style oscillators (not a cycle-exact APU).
typedef struct {
 uint32_t age, length, phase, increment, remainder, duty_frame;
 uint16_t note, lfsr;
 uint8_t envelope, duty, noise;
} cry_voice_t;
typedef struct { uint16_t species; uint32_t position; cry_voice_t voice[3]; } cry_player_t;
void cry_start(cry_player_t *p,uint16_t species);
uint32_t cry_duration_ms(uint16_t species);
bool cry_sample(cry_player_t *p,int16_t *out);
