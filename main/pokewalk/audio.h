#pragma once

#include <stdint.h>
#include <stdbool.h>

#define AUDIO_SAMPLE_RATE 22050
#define AUDIO_CHANNELS 4

typedef enum {
    SFX_BOOT_1 = 0,
    SFX_BOOT_2,
    SFX_ENCOUNTER,
    SFX_BALL_THROW,
    SFX_CAUGHT,
    SFX_ESCAPED,
    SFX_SHINY,
    SFX_EVOLVE,
    SFX_LEVEL_UP,
    SFX_CARE,
    SFX_MENU,
    SFX_RARE,
    SFX_COUNT,
} sfx_id_t;

uint32_t audio_sfx_samples(sfx_id_t id);
uint32_t audio_render(sfx_id_t id, uint32_t from, uint32_t count, int16_t *out);
uint32_t audio_note_hz_q8(uint8_t midi_note);

sfx_id_t audio_encounter_alert(uint8_t rarity, bool shiny);
