// Frozen V7/V8 save representation. Never use for active combat.
#pragma once
#include <stdint.h>
typedef struct {
 uint16_t hp,max_hp,moves[4];
 uint8_t species,level,pp[4],status,sleep,seeded;
 int8_t attack,defense,special,speed;
} trainer_mon_v8_t;
typedef struct {
 trainer_mon_v8_t mons[6];
 uint8_t count,active,reflect,light_screen;
} trainer_side_v8_t;
typedef struct {
 trainer_side_v8_t sides[2];
 uint32_t rng;
 uint16_t turns,ability;
 uint8_t trainer,active,finished,won,next,participated;
 uint8_t awaiting_replacement,retired,acted,pending_move;
} trainer_session_v8_t;
typedef struct {
 uint16_t wild_wins,defeated; // stable trainer IDs; bits 0..7 are badges
 uint8_t league_stage,league_active;
 trainer_session_v8_t session;
} trainer_store_v8_t;
