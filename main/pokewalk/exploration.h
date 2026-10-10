#pragma once
#include "encounter_refresh.h"
#include "items.h"
#define EXPLORATION_ROUTES 4
#define EXPLORATION_REGIONS 8
#define EXPLORATION_MAPS (EXPLORATION_ROUTES + EXPLORATION_REGIONS)
#define EXPLORATION_CAPACITY 24
#define EXPLORATION_CLUES 3
#define EXPLORATION_REGION_PITY 9

typedef struct {
 // energy is a legacy V14 byte, retained only for save compatibility; ignored by gameplay.
 uint8_t route, energy, clues[EXPLORATION_ROUTES], pulse[EXPLORATION_ROUTES];
 uint8_t tracked_species, research_flags; // Low 4: claimed; high 4: target traced.
 uint32_t steps;
} exploration_state_t;
static inline void exploration_init(exploration_state_t *s) {
 *s=(exploration_state_t){.energy=0};
}
static inline bool exploration_valid(const exploration_state_t *s) {
 if(s->route>=EXPLORATION_ROUTES||s->energy>EXPLORATION_CAPACITY||s->tracked_species>151)return false;
 for(unsigned i=0;i<EXPLORATION_ROUTES;i++)if(s->clues[i]>3||s->pulse[i]>1)return false;
 return true;
}
typedef struct {
 const char *name,*description,*clue[3];
 uint16_t target;
 uint16_t color;
} exploration_route_t;
const exploration_route_t *exploration_route(unsigned id);
typedef enum { EXPLORE_NONE, EXPLORE_ENCOUNTER, EXPLORE_CLUE, EXPLORE_TARGET,
 EXPLORE_RESEARCH_LOCKED, EXPLORE_RESEARCH_CLAIMED, EXPLORE_NO_ENERGY, EXPLORE_NO_STAMINA, EXPLORE_BLOCKED, EXPLORE_BUSY, EXPLORE_SAVE_FAILED } exploration_kind_t;
typedef struct {
 exploration_kind_t kind;
 uint16_t uid,species;
 uint8_t route,clues,rarity,level;
 bool shiny;
 uint8_t item,quantity;
 bool item_full;
 uint32_t chain_roll; // Ephemeral independent draw; never persisted or re-rolled on display.
 uint16_t exp; // Actual leader gain after the durable discovery settlement.
 uint8_t special,extra_quantity; // Presentation only; awards commit with the discovery.
 bool visitor;
} exploration_event_t;
typedef enum { EXPLORE_SPECIAL_NONE, EXPLORE_SPECIAL_SUPPLY,
 EXPLORE_SPECIAL_TRAINING, EXPLORE_SPECIAL_SPARKLE } exploration_special_t;
#define EXPLORATION_ROTATION_STEPS 12
const char *exploration_special_name(unsigned special);
void exploration_special_apply(exploration_event_t *,enc_queue_t *,dex_t *,inventory_t *,const nurture_t *,uint32_t steps);
// Separate save extension: never enlarge the V11-V15 exploration prefix.
#define EXPLORATION_ACTIVITIES 8
typedef struct {
 uint32_t rounds[4];
 uint8_t targets[4], chapters[4];
 uint8_t activity_progress[8], activity_claimed;
 uint16_t activity_runs[8];
 uint16_t activity_uid[8]; // Outstanding encounter or zero; stale queue entries are reconciled.
} exploration_updates_t;

// V18 append-only extension. The four legacy route arrays never grow.
typedef struct {
 uint32_t steps;
 uint16_t clears;
 uint8_t clues,pulse,target,pity,deep,traced,claimed,challenge_clear;
} exploration_region_progress_t;
typedef struct {
 exploration_region_progress_t region[EXPLORATION_REGIONS];
 uint8_t selected,dungeon_pity;
 uint16_t expedition_clears;
 inventory_t pending_items;
 encounter_t pending_partner;
} exploration_regions_t;
typedef struct {
 exploration_route_t route;
 const char *dungeon,*condition,*directions[3];
 uint16_t gate;
 uint8_t min_level,max_level,biome,item,pool[24],boss[3][3],rewards[3][4];
} exploration_region_t;
const exploration_region_t *exploration_region(unsigned map);
// Read-only habitat configuration, independent of saved region progress and
// dungeon exit rewards. Direction indices here are zero based.
typedef struct {
 const char *name;
 uint8_t species[12];
} exploration_trail_t;
const exploration_trail_t *exploration_region_trail(unsigned map,unsigned direction);
unsigned exploration_trail_examples(unsigned map,unsigned direction,bool deep,uint16_t defeated,uint8_t out[2]);
unsigned exploration_visitors(unsigned map,unsigned direction,uint32_t steps,uint16_t defeated,uint8_t out[2]);
unsigned exploration_guest_candidates(unsigned map,uint8_t out[12]);
static inline unsigned exploration_region_level_min(const exploration_region_t *r,bool deep){
 return r->min_level+(deep?(r->max_level-r->min_level+1)/2:0);
}
bool exploration_map_open(unsigned map,uint16_t defeated,const exploration_regions_t *);
bool exploration_regions_valid(const exploration_regions_t *);
unsigned exploration_region_target(unsigned map,const exploration_regions_t *,uint16_t defeated,const dex_t *);
void exploration_region_research(unsigned map,const dex_t *,uint8_t *,uint8_t *);
exploration_event_t exploration_region_step(exploration_regions_t *,enc_refresh_state_t *,enc_queue_t *,dex_t *,uint16_t,uint16_t,inventory_t *,unsigned direction);
bool exploration_region_partner(unsigned map,unsigned direction,bool challenge,uint32_t seed,uint16_t defeated,exploration_regions_t *,encounter_t *);

typedef struct {
 exploration_state_t state;
 exploration_updates_t updates;
 exploration_regions_t regions;
 uint8_t route,clues,target,deep,deep_unlocked,traced,claimed,pinned;
 uint32_t discoveries,chain_wins;
 uint16_t defeated;
 uint8_t stamina,exp_percent,rare_bonus,party_bonus;
 uint8_t rare_left,elite_left,pending;
 uint8_t research_seen,research_caught;
 uint16_t supply_q10; // Legacy diagnostic field; no longer a spendable resource.
} exploration_view_t;
// Candidate-only operation. Publish all state and the event after NVS commits.
exploration_event_t exploration_step(exploration_state_t *s,enc_refresh_state_t *r,
 enc_queue_t *q,dex_t *dex,uint16_t active_uid);

#define EXPLORATION_CHAPTERS 9
typedef struct {
 const char *name,*condition,*story,*discovery;
 uint8_t badges;
 uint16_t wins;
 uint16_t targets[4];
 uint8_t item;
} exploration_chapter_t;
const exploration_chapter_t *exploration_chapter(unsigned chapter);
bool exploration_chapter_open(unsigned chapter,uint16_t defeated);
unsigned exploration_chapter_current(uint16_t defeated);
uint16_t exploration_target(unsigned route,uint16_t defeated);
bool exploration_species_open(unsigned species,uint16_t defeated);
exploration_event_t exploration_step_progress(exploration_state_t*,enc_refresh_state_t*,enc_queue_t*,dex_t*,uint16_t,uint16_t,inventory_t*);

exploration_event_t exploration_step_nurtured(exploration_state_t*,enc_refresh_state_t*,enc_queue_t*,dex_t*,uint16_t,uint16_t,inventory_t*,const nurture_t*);

int exploration_habitat(unsigned species, unsigned *rarity);
unsigned exploration_unlock_chapter(unsigned species);
uint16_t exploration_focus(const exploration_state_t *,uint16_t defeated);
const char *exploration_story(unsigned species,unsigned clue);

exploration_event_t exploration_step_team(exploration_state_t*,enc_refresh_state_t*,enc_queue_t*,dex_t*,uint16_t,uint16_t,inventory_t*,const nurture_t*,unsigned);

#include "party.h"
unsigned exploration_team_bonus(const party_t *,unsigned route);

void exploration_research_progress(unsigned route,const dex_t *,uint8_t *seen,uint8_t *caught);
exploration_kind_t exploration_research_claim(exploration_state_t *,const dex_t *,unsigned route);

// Snapshot sync is pure on the caller's copy; world persists it with the next action.
void exploration_targets_sync(exploration_state_t *,exploration_updates_t *,const dex_t *,const enc_queue_t *,uint16_t);
uint16_t exploration_current_target(const exploration_state_t *,const exploration_updates_t *,uint16_t);
void exploration_target_completed(exploration_state_t *,exploration_updates_t *,const dex_t *,const enc_queue_t *,uint16_t,unsigned);
bool exploration_updates_valid(const exploration_updates_t *);
typedef struct {
 const char *name,*story;
 uint8_t route,level,item,species[8];
} exploration_activity_t;
const exploration_activity_t *exploration_activity(unsigned);
bool exploration_activity_open(unsigned,uint16_t);
exploration_event_t exploration_activity_spawn(unsigned,exploration_updates_t *,enc_queue_t *,dex_t *,uint16_t,uint32_t,uint16_t);
void exploration_activity_credit(exploration_updates_t *,const encounter_t *);
exploration_kind_t exploration_activity_claim(unsigned,exploration_updates_t *,inventory_t *,exploration_event_t *);

exploration_event_t exploration_step_with_target(exploration_state_t*,enc_refresh_state_t*,enc_queue_t*,dex_t*,uint16_t,uint16_t,inventory_t*,const nurture_t*,unsigned,unsigned);

// Forest dungeon clear reward. Pure candidate: no queue, dex or RNG mutation.
bool exploration_dungeon_partner(uint32_t seed,uint32_t run_id,uint16_t defeated,
                                 const enc_queue_t *queue,encounter_t *out);

// Only tagged map/badge exploration encounters participate. Passive discoveries,
// dungeon partners and trainer battles never touch this separately saved chain.
static inline bool exploration_chain_encounter(const encounter_t *e) {
 return e&&e->activity>=1&&e->activity<=ENC_ACTIVITY_EXPLORATION;
}
static inline void exploration_chain_settle(uint32_t *wins,const encounter_t *e,bool won) {
 if(!wins||!exploration_chain_encounter(e))return;
 if(!won)*wins=0;else if(*wins<UINT32_MAX)(*wins)++;
}
unsigned exploration_chain_shiny_bp(uint32_t wins,shiny_source_t source);
void exploration_chain_discovery(exploration_event_t *,enc_queue_t *,dex_t *,uint32_t wins);
unsigned exploration_legacy_level_min(unsigned route);
