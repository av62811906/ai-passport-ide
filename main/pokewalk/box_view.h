#pragma once
#include "party.h"
#include "items.h"

enum { BOX_FILTER_ALL, BOX_FILTER_SHINY, BOX_FILTER_EVOLVABLE };
enum { BOX_SORT_DEX, BOX_SORT_LEVEL, BOX_SORT_LEVEL_ASC };
// type: 0 means any; other values are TY_* + 1.
typedef struct { uint8_t filter, type, sort; } box_view_options_t;

// Builds display indices only; never reorders or changes saved individuals.
unsigned box_view_build(const mon_t box[BOX_SPECIES], const inventory_t *inventory,
                        const box_view_options_t *options, uint8_t indices[BOX_SPECIES]);
unsigned box_view_turn(unsigned row, unsigned count, int direction);
