#include "pokemon_animation.h"

#include <string.h>
#include "pokemon_animation_assets.h"

static const pokemon_anim_record_t *record(uint16_t species)
{
    return species >= 1 && species <= POKEMON_ANIM_SPECIES_COUNT
        ? &POKEMON_ANIM_SPECIES[species - 1] : NULL;
}

bool pokemon_anim_info(uint16_t species, pokemon_anim_info_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    const pokemon_anim_record_t *r = record(species);
    if (!r) return false;
    *out = (pokemon_anim_info_t){r->size, r->frame_count, r->duration_ticks,
                                 r->x, r->y, r->w, r->h};
    return true;
}

pokemon_anim_sample_t pokemon_anim_sample(uint16_t species, uint32_t elapsed_ms)
{
    const pokemon_anim_record_t *r = record(species);
    uint64_t tick = (uint64_t)elapsed_ms * 60u / 1000u;
    if (!r || tick >= r->duration_ticks) return (pokemon_anim_sample_t){0, true};
    unsigned low = 0, high = r->run_count;
    while (low < high) {
        unsigned middle = low + (high - low) / 2;
        if (tick < POKEMON_ANIM_RUNS[r->run_start + middle].end_tick) high = middle;
        else low = middle + 1;
    }
    return (pokemon_anim_sample_t){POKEMON_ANIM_RUNS[r->run_start + low].frame, false};
}

bool pokemon_anim_decode(uint16_t species, uint8_t frame, uint8_t *buffer,
                          size_t capacity, sprite_asset_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    const pokemon_anim_record_t *r = record(species);
    if (!r || frame >= r->frame_count || !buffer) return false;
    uint8_t size;
    const uint8_t *base = assets_front_sprite(species, &size);
    size_t bytes = (size_t)r->size * r->size / 4;
    if (!base || size != r->size || capacity < bytes) return false;
    memcpy(buffer, base, bytes);
    const pokemon_anim_frame_record_t *f = &POKEMON_ANIM_FRAMES[r->frame_start + frame];
    size_t start = (size_t)f->patch_start * 3u;
    if (start + (size_t)f->patch_count * 3u > sizeof(POKEMON_ANIM_PATCHES)) return false;
    unsigned wide = size / 8u;
    for (unsigned i = 0; i < f->patch_count; i++) {
        const uint8_t *patch = &POKEMON_ANIM_PATCHES[start + i * 3u];
        unsigned position = patch[0], tile = patch[1] | ((unsigned)patch[2] << 8);
        if (position >= wide * wide || tile >= POKEMON_ANIM_TILE_COUNT) return false;
        const uint8_t *source = &POKEMON_ANIM_TILES[tile * 16u];
        unsigned x = position % wide, y = position / wide;
        for (unsigned row = 0; row < 8; row++) {
            size_t offset = (y * 8u + row) * (size / 4u) + x * 2u;
            memcpy(buffer + offset, source + row * 2u, 2);
        }
    }
    *out = (sprite_asset_t){buffer, size, size};
    return true;
}

void pokemon_idle_reset(pokemon_idle_t *idle, uint16_t species)
{
    memset(idle, 0, sizeof(*idle));
    idle->species = species;
    pokemon_anim_decode(species, 0, idle->buffer, sizeof(idle->buffer), &idle->sprite);
}

bool pokemon_idle_step(pokemon_idle_t *idle, uint32_t delta_ms)
{
    pokemon_anim_info_t info;
    if (!pokemon_anim_info(idle->species, &info)) return false;
    uint32_t period = ((uint32_t)info.duration_ticks * 1000u + 59u) / 60u + 2000u;
    idle->elapsed_ms = ((uint64_t)idle->elapsed_ms + delta_ms) % period;
    uint8_t frame = pokemon_anim_sample(idle->species, idle->elapsed_ms).frame;
    if (frame == idle->frame) return false;
    idle->frame = frame;
    pokemon_anim_decode(idle->species, frame, idle->buffer, sizeof(idle->buffer), &idle->sprite);
    return true;
}

int pokemon_back_entrance_offset(uint32_t elapsed_ms){
 static const int8_t offsets[]={0,-4,-7,-4,0,4,7,4,0};
 return elapsed_ms<900?offsets[elapsed_ms/100]:0;
}
