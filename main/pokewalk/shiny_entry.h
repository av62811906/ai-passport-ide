// Display-only Gold shiny send-out sequence. Never serialized into a save.
#pragma once
#include "battle_fx.h"

#define SHINY_ENTRY_MS 1000u
typedef struct { uint16_t elapsed_ms; uint8_t mask; } shiny_entry_t;
// Mask: 1 = player, 2 = enemy. If both are shiny, enemy goes first.
void shiny_entry_begin(shiny_entry_t *entry, uint8_t mask);
unsigned shiny_entry_side(const shiny_entry_t *entry);
// Returns true when the next actor starts (for its one-time sound).
bool shiny_entry_step(shiny_entry_t *entry, unsigned delta_ms);
// Pure band redraw, after battlers/HUD and before message window.
void shiny_entry_draw(const shiny_entry_t *entry, int band_y,
                      battle_fx_rect_t pet, battle_fx_rect_t enemy);
