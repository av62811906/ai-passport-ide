// Integer-only P3 presentation samples. No gameplay mutation or hidden clock.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Number of timer intervals, not the count of sampled frames: render 0..N.
// With the existing 90 ms battle timer these last 540 / 1620 / 1080 ms.
#define BATTLE_PRESENTATION_HP_FRAMES 6
#define BATTLE_PRESENTATION_EXP_FRAMES 18
#define BATTLE_PRESENTATION_ENTRY_FRAMES 12

typedef struct {
    uint32_t total, got, need;
    uint8_t level;
} battle_presentation_exp_t;

typedef struct {
    int16_t pet_dx, wild_dx;
} battle_presentation_entry_t;

// First frame of phase 6 in battle_fx's 12 authoring phases followed by two
// clean frames. Pass battle_fx_frames(round); before this frame keep old HP.
uint16_t battle_presentation_hit_frame(uint16_t fx_frames);

// Sample 0 keeps from_hp; HP_FRAMES and later reach to_hp exactly. Misses and
// an increasing target keep from_hp. The caller captures BOTH pre-step HPs
// before committing battle_session_step, then samples each independently.
// frame is elapsed since the hit frame, not since the attack began.
uint16_t battle_presentation_hp(uint16_t from_hp, uint16_t to_hp,
                                bool missed, uint16_t frame);

// Animate cumulative EXP, deriving the level and within-level bar at each
// sample with exp.c. cap is clamped to 1..LEVEL_MAX; reaching it displays a
// full 1/1 bar, never progress toward level 101. A decreasing target is ignored.
// uint64_t multiplication handles the entire uint32_t EXP range. Grant the
// gameplay reward once BEFORE starting this display-only animation.
battle_presentation_exp_t battle_presentation_exp(uint32_t from_total,
                                                  uint32_t to_total,
                                                  uint8_t cap,
                                                  uint16_t frame);

// Translate complete sprites by integer pixels, ending at zero displacement.
// GSC direction: player enters from the right (positive start_dx), enemy from
// the left (negative). Geometry, screen clipping and HUD order belong to P3.
battle_presentation_entry_t battle_presentation_entry(uint16_t frame,
                                                      int16_t pet_start_dx,
                                                      int16_t wild_start_dx);

// All functions are pure: repeated redraws do not advance or replay anything.
// The page owns clocks and one-time entry/reward flags. On P4/list re-entry,
// initialize displays from committed HP/EXP and do not replay old endpoints.
