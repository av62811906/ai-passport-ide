#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "items.h"
#define DUNGEON_ENTRY_COST 20
// Stored atomically WITH party EXP and inventory in the world save.
typedef struct {uint32_t xp;inventory_t items;uint8_t first_clear,first_elite,full;
 // Former zeroed tail padding, without changing any persisted offsets or sizes.
 uint8_t partner_species,partner_shiny;
} dungeon_receipt_t;
_Static_assert(sizeof(dungeon_receipt_t)==48&&offsetof(dungeon_receipt_t,partner_species)==45,
               "Keep legacy receipt layout; old reward_plan zeroed all tail bytes");
typedef struct {
 uint32_t run_id;
 uint16_t clears;
 uint8_t paid_nodes,last_node,elite_seen;
 dungeon_receipt_t receipt;
} dungeon_progress_t;
bool dungeon_progress_valid(const dungeon_progress_t *p);
void dungeon_reward_plan(unsigned node,uint32_t seed,const dungeon_progress_t *p,dungeon_receipt_t *out);
void world_dungeon_progress(dungeon_progress_t *out);
bool world_dungeon_ready(void);
bool world_dungeon_admit(uint32_t run_id);
bool world_dungeon_award(uint32_t run_id,unsigned node,uint32_t seed,dungeon_receipt_t *out);

typedef struct {uint32_t xp;uint8_t theme,map,challenge,trail;} dungeon_award_details_t;
bool dungeon_award_details(uint32_t id,unsigned node,dungeon_award_details_t *out);
