#include "battle_presentation.h"

#include "exp.h"

uint16_t battle_presentation_hit_frame(uint16_t fx_frames)
{
    if (fx_frames <= 2) return 0;
    uint16_t active = fx_frames - 2;
    return (uint16_t)((active + 1u) / 2u);
}

uint16_t battle_presentation_hp(uint16_t from_hp, uint16_t to_hp,
                                bool missed, uint16_t frame)
{
    if (missed || to_hp >= from_hp) return from_hp;
    if (frame >= BATTLE_PRESENTATION_HP_FRAMES) return to_hp;
    uint32_t lost = (uint32_t)(from_hp - to_hp) * frame /
                    BATTLE_PRESENTATION_HP_FRAMES;
    return (uint16_t)(from_hp - lost);
}

battle_presentation_exp_t battle_presentation_exp(uint32_t from_total,
                                                  uint32_t to_total,
                                                  uint8_t cap,
                                                  uint16_t frame)
{
    if (to_total < from_total) to_total = from_total;
    battle_presentation_exp_t out;
    out.total = frame >= BATTLE_PRESENTATION_EXP_FRAMES ? to_total
        : from_total + (uint32_t)((uint64_t)(to_total - from_total) * frame /
                                  BATTLE_PRESENTATION_EXP_FRAMES);
    if (cap < 1) cap = 1;
    if (cap > LEVEL_MAX) cap = LEVEL_MAX;
    out.level = exp_to_level(out.total, cap);
    if (out.level == cap) {
        out.got = 1;
        out.need = 1;
    } else {
        exp_progress(out.total, out.level, &out.got, &out.need);
    }
    return out;
}

static int16_t entry_offset(int16_t start, uint16_t frame)
{
    if (frame >= BATTLE_PRESENTATION_ENTRY_FRAMES) return 0;
    // int32_t also covers INT16_MIN; signed division truncates toward zero.
    return (int16_t)(start - (int32_t)start * frame /
                            BATTLE_PRESENTATION_ENTRY_FRAMES);
}

battle_presentation_entry_t battle_presentation_entry(uint16_t frame,
                                                      int16_t pet_start_dx,
                                                      int16_t wild_start_dx)
{
    return (battle_presentation_entry_t){
        entry_offset(pet_start_dx, frame),
        entry_offset(wild_start_dx, frame),
    };
}
