// Original Crystal front-picture frame scripts. No gameplay or hidden clock.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "assets.h"

#define POKEMON_ANIM_BUFFER_BYTES (56u * 56u / 4u)

typedef struct {
    uint8_t size, frame_count;
    uint16_t duration_ticks;  // Original normal-speed 60 Hz script, including loop-end holds.
    uint8_t x, y, w, h;       // Union of nonwhite pixels in every played frame + base, in source pixels.
} pokemon_anim_info_t;

typedef struct {
    uint8_t frame;            // Original PNG square-frame index. Zero is the resting picture.
    bool finished;
} pokemon_anim_sample_t;

// Invalid species clear out and return false. All 151 Kanto fronts are covered.
bool pokemon_anim_info(uint16_t species, pokemon_anim_info_t *out);

// Sample the original species-specific anim.asm. At/after the end, frame=0 and
// finished=true. Source ticks are floor(elapsed_ms*60/1000); multiplication is
// widened so long pauses cannot wrap. Repeated redraws return the same frame.
pokemon_anim_sample_t pokemon_anim_sample(uint16_t species, uint32_t elapsed_ms);

// Decode one original front frame using the current FRNT base and generated
// 8x8 tile patches. buffer belongs to the caller; out->data points into it.
// Requires assets_init(). Invalid ID/frame/base/short buffer clears out and
// returns false. Cache until sample.frame changes, then use the normal/shiny
// species palette and the existing integer 2bpp renderer. No allocation.
bool pokemon_anim_decode(uint16_t species, uint8_t frame, uint8_t *buffer,
                          size_t capacity, sprite_asset_t *out);

// A page-owned, allocation-free idle loop. Timer callbacks update it; drawing
// only reads it. The 2 second resting interval keeps original actions distinct.
typedef struct {
    uint16_t species;
    uint8_t frame;
    uint32_t elapsed_ms;
    sprite_asset_t sprite;
    uint8_t buffer[POKEMON_ANIM_BUFFER_BYTES];
} pokemon_idle_t;
void pokemon_idle_reset(pokemon_idle_t *idle, uint16_t species);
bool pokemon_idle_step(pokemon_idle_t *idle, uint32_t delta_ms);

// Back pictures have no original frame scripts: use a small entrance sway.
int pokemon_back_entrance_offset(uint32_t elapsed_ms);
