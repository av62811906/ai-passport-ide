#include "items.h"
#include "assets.h"
#include "evolution.h"
#include <string.h>

static const item_info_t INFO[ITEM_COUNT] = {
    {"精灵球", "基本捕获球", ITEM_KIND_BALL},
    {"超级球", "捕获窗口增至一点五倍", ITEM_KIND_BALL},
    {"高级球", "捕获窗口增至两倍", ITEM_KIND_BALL},
    {"大师球", "必定捕获", ITEM_KIND_BALL},
    {"速度球", "对方基础速度达一百时四倍", ITEM_KIND_BALL},
    {"沉重球", "百公斤两倍，两百公斤三倍", ITEM_KIND_BALL},
    {"等级球", "等级较高一点五倍，达两倍则两倍", ITEM_KIND_BALL},
    {"友友球", "捕获后亲密度为四十", ITEM_KIND_BALL},
    {"火之石", "使对应宝可梦进化", ITEM_KIND_STONE},
    {"水之石", "使对应宝可梦进化", ITEM_KIND_STONE},
    {"雷之石", "使对应宝可梦进化", ITEM_KIND_STONE},
    {"叶之石", "使对应宝可梦进化", ITEM_KIND_STONE},
    {"月之石", "使对应宝可梦进化", ITEM_KIND_STONE},
    {"通讯机器", "替代通讯交换完成进化", ITEM_KIND_MACHINE},
    {"成长机器", "达到原进化等级即可使用", ITEM_KIND_MACHINE},
    {"树果", "饱食加三十，心情加五", ITEM_KIND_CARE},
    {"活力根", "饱食加十，心情加二十", ITEM_KIND_CARE},
    {"哞哞牛奶", "饱食加二十，心情加十", ITEM_KIND_CARE},
    {"开心饼干", "心情加二十五，亲密加二", ITEM_KIND_CARE},
};
static const uint8_t CAPACITY[ITEM_COUNT] = {
    99,30,20,1,20,20,20,20, 9,9,9,9,9, 5,5, 30,30,30,30,
};

const item_info_t *items_info(uint8_t id) { return id < ITEM_COUNT ? &INFO[id] : NULL; }
uint16_t items_capacity(uint8_t id) { return id < ITEM_COUNT ? CAPACITY[id] : 0; }

void items_inventory_init(inventory_t *inventory)
{
    if (!inventory) return;
    memset(inventory, 0, sizeof(*inventory));
    inventory->quantity[ITEM_POKE] = 12;
    inventory->quantity[ITEM_GREAT] = 3;
    inventory->quantity[ITEM_ULTRA] = 1;
    inventory->quantity[ITEM_BERRY] = 2;
    inventory->quantity[ITEM_MILK] = 2;
}

bool items_inventory_valid(const inventory_t *inventory)
{
    if (!inventory) return false;
    for (unsigned i = 0; i < ITEM_COUNT; i++)
        if (inventory->quantity[i] > CAPACITY[i]) return false;
    return true;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}

uint8_t items_drop_chance(uint8_t rarity)
{
    return rarity >= 1 && rarity <= 5 ? (uint8_t)(15 + rarity * 10) : 0;
}

uint32_t items_loot_seed(uint16_t uid, uint32_t ts, uint16_t species, uint8_t rarity)
{
    uint32_t value = ts ^ ((uint32_t)uid << 16) ^ ((uint32_t)species * 0x9E3779B9u)
        ^ ((uint32_t)rarity * 0x85EBCA6Bu);
    value ^= value >> 16; value *= 0x7FEB352Du;
    value ^= value >> 15; value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

item_loot_t items_roll_loot(uint8_t rarity, uint32_t seed)
{
    item_loot_t out = {.item_id = ITEM_NONE};
    if (rarity < 1 || rarity > 5) return out;
    // Separately mixed stream, unrelated to the battle's xorshift state.
    uint32_t rng = seed ^ 0xA511E9B3u;
    if (!rng) rng = 0x6D2B79F5u;
    bool special = next_random(&rng) % 100 < items_drop_chance(rarity);
    unsigned category = next_random(&rng) % 100;
    if (category < 70) {
        // Keep balls at 70% of drops. Rarity upgrades the mix rather than
        // reducing ball supplies; specialty balls remain situational tools.
        static const uint8_t balls[5][ITEM_BALL_COUNT] = {
            {80,15, 1,0, 1, 1, 1, 1},
            {55,28, 5,0, 3, 3, 3, 3},
            {25,35,20,0, 5, 5, 5, 5},
            {10,25,33,0, 8, 8, 8, 8},
            { 4,14,41,1,10,10,10,10},
        };
        unsigned pick = next_random(&rng) % 100;
        for (unsigned i = 0; i < ITEM_BALL_COUNT; i++) {
            if (pick < balls[rarity-1][i]) { out.item_id = i; break; }
            pick -= balls[rarity-1][i];
        }
    } else if (special && category < 85) {
        // Evolution rewards rise from 3.75% to 9.75% overall. Machines become
        // more common at higher rarities; one-star opponents only drop stones.
        static const uint8_t machine_chance[] = {0,5,15,30,40};
        if (next_random(&rng) % 100 < machine_chance[rarity-1])
            out.item_id = ITEM_LINK_MACHINE + next_random(&rng) % 2;
        else out.item_id = ITEM_FIRE_STONE + next_random(&rng) % 5;
    } else {
        unsigned pick = next_random(&rng) % 100;
        out.item_id = pick < 55 ? ITEM_BERRY : pick < 85 ? ITEM_MILK
                     : pick < 93 ? ITEM_ENERGY_ROOT : ITEM_JOY_COOKIE;
    }
    out.quantity = out.item_id == ITEM_POKE ? (uint8_t)(2 + next_random(&rng) % 2)
                   : out.item_id == ITEM_BERRY ? 2 : 1;
    return out;
}

// A full rolled stack redirects to the least stocked basic supply. Rare items
// are never minted by overflow, and the caller commits the actual result once.
item_loot_t items_fit_loot(item_loot_t loot,const inventory_t *bag,uint32_t seed)
{
    if(!bag||loot.item_id>=ITEM_COUNT||!loot.quantity)return loot;
    if(bag->quantity[loot.item_id]>=items_capacity(loot.item_id)) {
        static const uint8_t supplies[]={ITEM_BERRY,ITEM_MILK,ITEM_ENERGY_ROOT,ITEM_JOY_COOKIE,ITEM_POKE,ITEM_GREAT};
        unsigned best=ITEM_NONE;
        for(unsigned j=0;j<sizeof(supplies);j++) {
            unsigned id=supplies[(j+seed%sizeof(supplies))%sizeof(supplies)];
            if(bag->quantity[id]>=items_capacity(id))continue;
            if(best==ITEM_NONE||(uint32_t)bag->quantity[id]*items_capacity(best)<(uint32_t)bag->quantity[best]*items_capacity(id))best=id;
        }
        if(best!=ITEM_NONE){loot.item_id=best;loot.quantity=(best==ITEM_BERRY||best==ITEM_POKE)?2:1;}
    }
    unsigned room=items_capacity(loot.item_id)-bag->quantity[loot.item_id];
    loot.full=room<loot.quantity;if(loot.quantity>room)loot.quantity=room;
    return loot;
}

static int32_t add_axis(int32_t before, int amount)
{
    int64_t result = (int64_t)before + amount * NURT_Q;
    return result < 0 ? 0 : result > NURT_MAX ? NURT_MAX : (int32_t)result;
}

item_use_status_t items_apply(uint8_t id, uint16_t species, uint8_t level,
                              const nurture_t *before, item_use_result_t *out)
{
    if (!before || !out || id >= ITEM_COUNT || species < 1 || species > 151)
        return ITEM_USE_INVALID;
    *out = (item_use_result_t){.species_before = species, .species_after = species,
                             .before = *before, .after = *before};
    if (id < ITEM_BALL_COUNT) return ITEM_USE_NOT_APPLICABLE;
    // Gen1 stone targets are explicit, including all three Eevee branches.
    static const uint8_t stone_targets[][3] = {
        {ITEM_FIRE_STONE,37,38},{ITEM_FIRE_STONE,58,59},{ITEM_FIRE_STONE,133,136},
        {ITEM_WATER_STONE,61,62},{ITEM_WATER_STONE,90,91},{ITEM_WATER_STONE,120,121},{ITEM_WATER_STONE,133,134},
        {ITEM_THUNDER_STONE,25,26},{ITEM_THUNDER_STONE,133,135},
        {ITEM_LEAF_STONE,44,45},{ITEM_LEAF_STONE,70,71},{ITEM_LEAF_STONE,102,103},
        {ITEM_MOON_STONE,30,31},{ITEM_MOON_STONE,33,34},{ITEM_MOON_STONE,35,36},{ITEM_MOON_STONE,39,40},
    };
    if (id <= ITEM_MOON_STONE) {
        for (unsigned i = 0; i < sizeof(stone_targets)/sizeof(stone_targets[0]); i++)
            if (stone_targets[i][0] == id && stone_targets[i][1] == species)
                out->species_after = stone_targets[i][2];
        if (out->species_after == species) return ITEM_USE_NOT_APPLICABLE;
    } else if (id == ITEM_LINK_MACHINE) {
        if (species == 64 || species == 67 || species == 75 || species == 93)
            out->species_after = species + 1;
        else return ITEM_USE_NOT_APPLICABLE;
    } else if (id == ITEM_GROWTH_MACHINE) {
        species_t sp;
        if (!assets_species(species, &sp) || sp.evolve_trigger != EVO_TRIGGER_LEVEL ||
            !sp.evolve_to || !sp.evolve_level) return ITEM_USE_NOT_APPLICABLE;
        if (level < sp.evolve_level) return ITEM_USE_LEVEL_TOO_LOW;
        out->species_after = sp.evolve_to;
    } else {
        int satiety = 0, mood = 0, stamina = 0, intimacy = 0;
        switch (id) {
        case ITEM_BERRY: satiety = 30; mood = 5; break;
        case ITEM_ENERGY_ROOT: satiety = 10; mood = 20; break;
        case ITEM_MILK: satiety = 20; mood = 10; break;
        case ITEM_JOY_COOKIE: mood = 25; intimacy = 2; break;
        default: return ITEM_USE_INVALID;
        }
        // Do not spend food with no positive benefit (or only a side effect).
        bool useful = (satiety > 0 && before->satiety < NURT_MAX) ||
            (mood > 0 && before->mood < NURT_MAX) ||
            (stamina > 0 && before->stamina < NURT_MAX) ||
            (intimacy > 0 && before->intimacy < NURT_MAX);
        if (!useful) return ITEM_USE_NOT_APPLICABLE;
        out->after.satiety = add_axis(before->satiety, satiety);
        out->after.mood = add_axis(before->mood, mood);
        out->after.stamina = add_axis(before->stamina, stamina);
        out->after.intimacy = add_axis(before->intimacy, intimacy);
    }
    if (out->species_after != species)
        out->after.mood = add_axis(before->mood, 15); // Existing evolution bonus.
    return ITEM_USE_OK;
}

uint16_t items_milk_heal(uint16_t max_hp)
{
    unsigned half=((unsigned)max_hp+1)/2;
    unsigned heal=half>50?half:50;
    return heal>max_hp?max_hp:(uint16_t)heal;
}
