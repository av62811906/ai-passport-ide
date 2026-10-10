// main/transition.h —— S15 遭遇转场的纯函数。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TRANS_TILE 8
#define TRANS_GRID_W 30
#define TRANS_GRID_H 40
#define TRANS_FLASH_FRAMES 72

// 顺序来自 pokered 的 BattleTransitions 表，不能重排。
typedef enum {
    TRANS_DOUBLE_CIRCLE = 0,
    TRANS_SPIRAL_IN,
    TRANS_CIRCLE,
    TRANS_SPIRAL_OUT,
    TRANS_H_STRIPES,
    TRANS_SHRINK,
    TRANS_V_STRIPES,
    TRANS_SPLIT,
    TRANS_WAVE,
    TRANS_SPECKLE,
    TRANS_COUNT,
} trans_id_t;

trans_id_t trans_pick(bool is_trainer, uint8_t wild_level,
                      uint8_t pet_level, bool open_biome, uint8_t *idx);
// Presentation-only variation. Does not consume combat/capture RNG.
trans_id_t trans_pick_encounter(uint16_t uid, uint8_t biome, bool stronger);
uint16_t trans_frames(trans_id_t id);
bool trans_has_flash(trans_id_t id);
bool trans_tile_covered(trans_id_t id, uint16_t progress_q10,
                        uint8_t gx, uint8_t gy);
