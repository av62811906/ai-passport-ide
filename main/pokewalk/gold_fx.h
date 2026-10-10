#pragma once
#include "battle_fx.h"
bool gold_fx_enabled(const battle_round_t *r);
uint8_t gold_fx_frame_count(const battle_round_t *r);
void gold_fx_draw(const battle_round_t *r, uint8_t frame, int band_y,
                  const battle_fx_actor_t *pet, const battle_fx_actor_t *wild);
