// Finite inventory and pure item rules. Presentation and NVS live elsewhere.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nurture.h"

typedef enum {
    ITEM_POKE = 0, ITEM_GREAT, ITEM_ULTRA, ITEM_MASTER,
    ITEM_FAST, ITEM_HEAVY, ITEM_LEVEL, ITEM_FRIEND,
    ITEM_FIRE_STONE, ITEM_WATER_STONE, ITEM_THUNDER_STONE, ITEM_LEAF_STONE, ITEM_MOON_STONE,
    ITEM_LINK_MACHINE, ITEM_GROWTH_MACHINE,
    ITEM_BERRY, ITEM_ENERGY_ROOT, ITEM_MILK, ITEM_JOY_COOKIE,
    ITEM_COUNT
} item_id_t;
#define ITEM_BALL_COUNT 8
#define ITEM_NONE 255

typedef enum { ITEM_KIND_BALL, ITEM_KIND_STONE, ITEM_KIND_MACHINE, ITEM_KIND_CARE } item_kind_t;
typedef struct { uint16_t quantity[ITEM_COUNT]; } inventory_t;
typedef struct {
    const char *name;       // NUL-terminated Chinese strings, static storage.
    const char *description;
    item_kind_t kind;
} item_info_t;
typedef struct {
    uint8_t item_id;        // ITEM_NONE for a checked roll with no drop.
    uint8_t quantity;       // Actually added, zero when this item was full.
    bool full;             // Rolled quantity exceeded available capacity.
} item_loot_t;
typedef enum {
    ITEM_USE_OK = 0, ITEM_USE_INVALID, ITEM_USE_EMPTY, ITEM_USE_WRONG_TARGET,
    ITEM_USE_NOT_APPLICABLE, ITEM_USE_LEVEL_TOO_LOW, ITEM_USE_SAVE_FAILED,
    ITEM_USE_STORAGE_UNAVAILABLE, ITEM_USE_BUSY,
} item_use_status_t;
typedef struct {
    uint16_t species_before, species_after;
    nurture_t before, after;
    uint16_t remaining;
} item_use_result_t;

const item_info_t *items_info(uint8_t item_id);
uint16_t items_capacity(uint8_t item_id);
void items_inventory_init(inventory_t *inventory);
bool items_inventory_valid(const inventory_t *inventory);
uint8_t items_drop_chance(uint8_t rarity); // Special-loot gate 25/35/45/55/65%; 15% of that branch grants evolution items.
// Deterministic independent loot stream. Does not access battle's RNG.
item_loot_t items_roll_loot(uint8_t rarity, uint32_t seed);
item_loot_t items_fit_loot(item_loot_t loot, const inventory_t *bag, uint32_t seed);
uint32_t items_loot_seed(uint16_t uid, uint32_t ts, uint16_t species, uint8_t rarity);
// Pure eligibility/application, does not consume inventory. Ball use belongs
// exclusively to the capture transaction. Results preserve level/EXP.
item_use_status_t items_apply(uint8_t item_id, uint16_t species, uint8_t level,
                              const nurture_t *before, item_use_result_t *out);

// Recovery in trainer battles: at least 50 HP, otherwise half maximum (rounded up).
uint16_t items_milk_heal(uint16_t max_hp);
