// Shared, integer-only move presentation. The gameplay result remains read-only.
// Gold machine-code display tracks are sampled offline; portrait compositing
// and non-hit fallbacks are adaptations, not a full Game Boy renderer.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "battle.h"

#define BATTLE_FX_TICK_MS 45

typedef struct {
    int16_t x, y, w, h;   // Absolute unshifted display bounds; wild uses its visible bbox.
} battle_fx_rect_t;

typedef struct {
    int16_t pet_dx, wild_dx;
} battle_fx_pose_t;

typedef struct {
    const uint8_t *data;
    uint8_t w, h;
    uint16_t palette[4];
} battle_fx_actor_t;

// Full-scene entry point for source-derived animation tracks, including
// actor tile masks and background motion. Null actors are valid in FX tests.
void battle_fx_draw_scene_band(const battle_round_t *round, uint8_t frame, int band_y,
                               battle_fx_rect_t pet, battle_fx_rect_t wild,
                               const battle_fx_actor_t *pet_art,
                               const battle_fx_actor_t *wild_art);

// Stable styles are exposed for diagnostics, not stored in saves.
typedef enum {
    BATTLE_FX_IMPACT, BATTLE_FX_FIRE, BATTLE_FX_WATER, BATTLE_FX_ELECTRIC,
    BATTLE_FX_LEAF, BATTLE_FX_ICE, BATTLE_FX_FIGHTING, BATTLE_FX_POISON,
    BATTLE_FX_GROUND, BATTLE_FX_WIND, BATTLE_FX_PSYCHIC, BATTLE_FX_BUG,
    BATTLE_FX_ROCK, BATTLE_FX_GHOST, BATTLE_FX_DRAGON,
    BATTLE_FX_LUNGE, BATTLE_FX_CUT, BATTLE_FX_HORN, BATTLE_FX_BIND,
    BATTLE_FX_NEEDLE, BATTLE_FX_BITE, BATTLE_FX_DRAIN, BATTLE_FX_BUBBLE,
    BATTLE_FX_STARS, BATTLE_FX_THUNDER, BATTLE_FX_PALM, BATTLE_FX_KICK,
    BATTLE_FX_WHIP, BATTLE_FX_ORB, BATTLE_FX_LICK, BATTLE_FX_MULTICUT,
    BATTLE_FX_SONIC_BOOM, BATTLE_FX_SEISMIC_TOSS, BATTLE_FX_DRAGON_RAGE,
    BATTLE_FX_NIGHT_SHADE, BATTLE_FX_SUPER_FANG, BATTLE_FX_HYDRO_PUMP,
    BATTLE_FX_HYPER_BEAM, BATTLE_FX_EXPLOSION,
    BATTLE_FX_SURF,
    BATTLE_FX_STYLE_COUNT
} battle_fx_style_t;

battle_fx_style_t battle_fx_style(const battle_round_t *round);
bool battle_fx_has_dedicated(uint16_t move_id);
// Shared target-only hit flash; HUD and attacker remain visible.
bool battle_fx_actor_visible(const battle_round_t *round, uint8_t frame, bool pet);

// Frame 0..N-1, one frame every BATTLE_FX_TICK_MS. Last two frames are clean
// (zero displacement and no overlay); replay them to remove the previous pose.
uint8_t battle_fx_frames(const battle_round_t *round);
uint8_t battle_fx_hit_frame(const battle_round_t *round);
battle_fx_pose_t battle_fx_pose(const battle_round_t *round, uint8_t frame);

// Use the same unshifted bounds for the actor and draw_band(). Pet movement
// stays in its left-hand area; the wild visible bbox stays in x=120..239.
// Bounds must fit their resting area. Invalid bounds receive zero movement.
battle_fx_pose_t battle_fx_pose_for_rects(const battle_round_t *round, uint8_t frame,
                                         battle_fx_rect_t pet, battle_fx_rect_t wild);

// Draw after actors AND both HUDs, before the message window. Uses the current
// screen band only, no allocation/framebuffer. Effects may cover x=0..239,
// y=0..239. Rebuild bands 0/1/2 from the scene on every phase and recovery;
// animation cleanup must restore HUDs as well as actors.
// Misses have no damage overlay; pose_for_rects() allows attack/evade movement.
void battle_fx_draw_band(const battle_round_t *round, uint8_t frame, int band_y,
                         battle_fx_rect_t pet, battle_fx_rect_t wild);
