#include "box_view.h"
#include "assets.h"
#include "battle.h"
#include "evolution.h"

static bool can_evolve(const mon_t *mon, const species_t *species, const inventory_t *inventory)
{
    if (evo_level_ready(mon->level, species->evolve_trigger, species->evolve_to,
                        species->evolve_level)) return true;
    if (!inventory) return false;
    nurture_t nurture = {0};
    item_use_result_t result;
    for (unsigned item = ITEM_FIRE_STONE; item <= ITEM_GROWTH_MACHINE; item++) {
        if (inventory->quantity[item] &&
            items_apply(item, mon->species_id, mon->level, &nurture, &result) == ITEM_USE_OK &&
            result.species_after != mon->species_id) return true;
    }
    return false;
}

static bool precedes(const mon_t *a, const mon_t *b, unsigned sort)
{
    if (sort == BOX_SORT_LEVEL && a->level != b->level) return a->level > b->level;
    if (sort == BOX_SORT_LEVEL_ASC && a->level != b->level) return a->level < b->level;
    if (a->species_id != b->species_id) return a->species_id < b->species_id;
    // Same-species individuals remain stable, including normal/shiny duplicates.
    return false;
}

unsigned box_view_build(const mon_t box[BOX_SPECIES], const inventory_t *inventory,
                        const box_view_options_t *options, uint8_t indices[BOX_SPECIES])
{
    unsigned count = 0;
    for (unsigned slot = 0; slot < BOX_SPECIES; slot++) {
        const mon_t *mon = &box[slot];
        species_t species;
        if (!mon->species_id || !assets_species(mon->species_id, &species)) continue;
        if (options->filter == BOX_FILTER_SHINY && !(mon->flags & 1)) continue;
        if (options->filter == BOX_FILTER_EVOLVABLE && !can_evolve(mon, &species, inventory)) continue;
        if (options->type && species.type1 + 1 != options->type &&
            (species.type2 == TY_NONE || species.type2 + 1 != options->type)) continue;
        unsigned at = count++;
        while (at && precedes(mon, &box[indices[at - 1]], options->sort)) {
            indices[at] = indices[at - 1];
            at--;
        }
        indices[at] = slot;
    }
    return count;
}

unsigned box_view_turn(unsigned row, unsigned count, int direction)
{
    if (!count) return 0;
    unsigned pages = (count + 4) / 5;
    unsigned page = (row < count ? row : 0) / 5;
    return ((page + pages + (direction < 0 ? -1 : 1)) % pages) * 5;
}
