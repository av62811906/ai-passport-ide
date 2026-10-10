#include "audio.h"

#include <stddef.h>

typedef enum {
    WAVE_SQUARE = 0,
    WAVE_TABLE,
    WAVE_NOISE,
} wave_kind_t;

typedef enum {
    ENV_FLAT = 0,
    ENV_PLUCK,
    ENV_FADE,
    ENV_HIT,
} env_t;

typedef enum {
    VOL_100 = 0,
    VOL_70,
    VOL_60,
    VOL_55,
    VOL_50,
} volume_t;

typedef enum {
    SWEEP_NONE = 0,
    SWEEP_DOWN,
    SWEEP_UP,
} sweep_t;

/* Four bytes per note, matching sim/audio.py's storage budget. */
typedef struct __attribute__((packed)) {
    int8_t midi;
    uint8_t duration;
    uint8_t duty_env;
    uint8_t volume;
} note_t;

typedef struct {
    const note_t *notes;
    uint8_t count;
    uint8_t kind;
    uint8_t sweep;
    uint8_t noise_short;
} track_t;

typedef struct {
    const track_t *tracks;
    uint8_t count;
    uint16_t duration_ms;
} effect_t;

#define REST (-1)
#define NOTE(midi_, duration_, duty_, env_, volume_) \
    { (midi_), (duration_), (uint8_t)((duty_) | ((env_) << 2)), (volume_) }

enum {
    DUR_24 = 0, DUR_34, DUR_50, DUR_55, DUR_60, DUR_70, DUR_90,
    DUR_100, DUR_120, DUR_140, DUR_160, DUR_200, DUR_220, DUR_420, DUR_900,
};

static const uint16_t DURATION_MS[] = {
    24, 34, 50, 55, 60, 70, 90, 100, 120, 140, 160, 200, 220, 420, 900,
};

static const note_t NOTES_BOOT_1[] = {
    NOTE(84, DUR_90, 2, ENV_PLUCK, VOL_100),
};
static const note_t NOTES_BOOT_2[] = {
    NOTE(79, DUR_420, 2, ENV_FADE, VOL_100),
};
static const note_t NOTES_ENCOUNTER[] = {
    NOTE(76, DUR_60, 1, ENV_PLUCK, VOL_100),
    NOTE(79, DUR_60, 1, ENV_PLUCK, VOL_100),
    NOTE(84, DUR_120, 1, ENV_FADE, VOL_100),
};
// Project-composed rare encounter arpeggio, distinct from the shiny sparkle.
static const note_t NOTES_RARE[] = {
 NOTE(72,DUR_70,1,ENV_PLUCK,VOL_100), NOTE(79,DUR_70,1,ENV_PLUCK,VOL_100),
 NOTE(84,DUR_70,1,ENV_PLUCK,VOL_100), NOTE(91,DUR_220,1,ENV_FADE,VOL_100),
};
static const note_t NOTES_BALL_SQUARE[] = {
    NOTE(81, DUR_160, 0, ENV_PLUCK, VOL_100),
};
static const note_t NOTES_BALL_NOISE[] = {
    NOTE(REST, DUR_140, 2, ENV_FLAT, VOL_100),
    NOTE(60, DUR_50, 2, ENV_HIT, VOL_100),
};
static const note_t NOTES_CAUGHT[] = {
    NOTE(72, DUR_70, 2, ENV_PLUCK, VOL_100),
    NOTE(76, DUR_70, 2, ENV_PLUCK, VOL_100),
    NOTE(79, DUR_70, 2, ENV_PLUCK, VOL_100),
    NOTE(84, DUR_220, 2, ENV_FADE, VOL_100),
};
static const note_t NOTES_ESCAPED_SQUARE[] = {
    NOTE(67, DUR_100, 1, ENV_PLUCK, VOL_100),
    NOTE(62, DUR_200, 1, ENV_FADE, VOL_100),
};
static const note_t NOTES_ESCAPED_NOISE[] = {
    NOTE(REST, DUR_90, 2, ENV_FLAT, VOL_100),
    NOTE(48, DUR_60, 2, ENV_HIT, VOL_50),
};
static const note_t NOTES_SHINY[] = {
    NOTE(88, DUR_34, 0, ENV_FLAT, VOL_100),
    NOTE(95, DUR_34, 0, ENV_FLAT, VOL_100),
    NOTE(88, DUR_34, 0, ENV_FLAT, VOL_100),
    NOTE(95, DUR_34, 0, ENV_FLAT, VOL_100),
    NOTE(88, DUR_34, 0, ENV_FLAT, VOL_100),
    NOTE(95, DUR_34, 0, ENV_FLAT, VOL_100),
    NOTE(88, DUR_34, 0, ENV_FLAT, VOL_100),
    NOTE(95, DUR_34, 0, ENV_FLAT, VOL_100),
    NOTE(100, DUR_160, 0, ENV_FADE, VOL_100),
};
static const note_t NOTES_EVOLVE_SQUARE[] = {
    NOTE(60, DUR_900, 2, ENV_FLAT, VOL_100),
};
static const note_t NOTES_EVOLVE_WAVE[] = {
    NOTE(48, DUR_900, 2, ENV_FADE, VOL_70),
};
static const note_t NOTES_LEVEL_UP[] = {
    NOTE(79, DUR_55, 3, ENV_PLUCK, VOL_100),
    NOTE(81, DUR_55, 3, ENV_PLUCK, VOL_100),
    NOTE(83, DUR_55, 3, ENV_PLUCK, VOL_100),
    NOTE(86, DUR_160, 3, ENV_FADE, VOL_100),
};
static const note_t NOTES_CARE[] = {
    NOTE(76, DUR_50, 2, ENV_PLUCK, VOL_60),
    NOTE(81, DUR_90, 2, ENV_FADE, VOL_60),
};
static const note_t NOTES_MENU[] = {
    NOTE(84, DUR_24, 0, ENV_HIT, VOL_55),
};

#define TRACK(notes_, kind_, sweep_) \
    { (notes_), (uint8_t)(sizeof(notes_) / sizeof((notes_)[0])), (kind_), (sweep_), 0 }

static const track_t TRACKS_BOOT_1[] = { TRACK(NOTES_BOOT_1, WAVE_SQUARE, SWEEP_NONE) };
static const track_t TRACKS_BOOT_2[] = { TRACK(NOTES_BOOT_2, WAVE_SQUARE, SWEEP_NONE) };
static const track_t TRACKS_RARE[] = { TRACK(NOTES_RARE, WAVE_SQUARE, SWEEP_NONE) };
static const track_t TRACKS_ENCOUNTER[] = { TRACK(NOTES_ENCOUNTER, WAVE_SQUARE, SWEEP_NONE) };
static const track_t TRACKS_BALL[] = {
    TRACK(NOTES_BALL_SQUARE, WAVE_SQUARE, SWEEP_DOWN),
    TRACK(NOTES_BALL_NOISE, WAVE_NOISE, SWEEP_NONE),
};
static const track_t TRACKS_CAUGHT[] = { TRACK(NOTES_CAUGHT, WAVE_SQUARE, SWEEP_NONE) };
static const track_t TRACKS_ESCAPED[] = {
    TRACK(NOTES_ESCAPED_SQUARE, WAVE_SQUARE, SWEEP_NONE),
    TRACK(NOTES_ESCAPED_NOISE, WAVE_NOISE, SWEEP_NONE),
};
static const track_t TRACKS_SHINY[] = { TRACK(NOTES_SHINY, WAVE_SQUARE, SWEEP_NONE) };
static const track_t TRACKS_EVOLVE[] = {
    TRACK(NOTES_EVOLVE_SQUARE, WAVE_SQUARE, SWEEP_UP),
    TRACK(NOTES_EVOLVE_WAVE, WAVE_TABLE, SWEEP_NONE),
};
static const track_t TRACKS_LEVEL_UP[] = { TRACK(NOTES_LEVEL_UP, WAVE_SQUARE, SWEEP_NONE) };
static const track_t TRACKS_CARE[] = { TRACK(NOTES_CARE, WAVE_SQUARE, SWEEP_NONE) };
static const track_t TRACKS_MENU[] = { TRACK(NOTES_MENU, WAVE_SQUARE, SWEEP_NONE) };

#define EFFECT(tracks_, duration_) \
    { (tracks_), (uint8_t)(sizeof(tracks_) / sizeof((tracks_)[0])), (duration_) }

static const effect_t EFFECTS[SFX_COUNT] = {
    EFFECT(TRACKS_BOOT_1, 90),
    EFFECT(TRACKS_BOOT_2, 420),
    EFFECT(TRACKS_ENCOUNTER, 240),
    EFFECT(TRACKS_BALL, 190),
    EFFECT(TRACKS_CAUGHT, 430),
    EFFECT(TRACKS_ESCAPED, 300),
    EFFECT(TRACKS_SHINY, 432),
    EFFECT(TRACKS_EVOLVE, 900),
    EFFECT(TRACKS_LEVEL_UP, 325),
    EFFECT(TRACKS_CARE, 140),
    EFFECT(TRACKS_MENU, 24),
    EFFECT(TRACKS_RARE, 430),
};

static const uint32_t MIDI_HZ_Q8[128] = {
    2093u, 2217u, 2349u, 2489u, 2637u, 2794u, 2960u, 3136u,
    3322u, 3520u, 3729u, 3951u, 4186u, 4435u, 4699u, 4978u,
    5274u, 5588u, 5920u, 6272u, 6645u, 7040u, 7459u, 7902u,
    8372u, 8870u, 9397u, 9956u, 10548u, 11175u, 11840u, 12544u,
    13290u, 14080u, 14917u, 15804u, 16744u, 17740u, 18795u, 19912u,
    21096u, 22351u, 23680u, 25088u, 26580u, 28160u, 29834u, 31609u,
    33488u, 35479u, 37589u, 39824u, 42192u, 44701u, 47359u, 50175u,
    53159u, 56320u, 59669u, 63217u, 66976u, 70959u, 75178u, 79649u,
    84385u, 89402u, 94719u, 100351u, 106318u, 112640u, 119338u, 126434u,
    133952u, 141918u, 150356u, 159297u, 168769u, 178805u, 189437u, 200702u,
    212636u, 225280u, 238676u, 252868u, 267905u, 283835u, 300713u, 318594u,
    337539u, 357610u, 378874u, 401403u, 425272u, 450560u, 477352u, 505737u,
    535809u, 567670u, 601425u, 637188u, 675077u, 715219u, 757749u, 802807u,
    850544u, 901120u, 954703u, 1011473u, 1071618u, 1135340u, 1202851u, 1274376u,
    1350154u, 1430439u, 1515497u, 1605613u, 1701088u, 1802240u, 1909407u, 2022946u,
    2143237u, 2270680u, 2405702u, 2548752u, 2700309u, 2860878u, 3030994u, 3211227u,
};

/* Q48 cycles/sample for MIDI 0..11; higher octaves are exact shifts. */
static const uint64_t PHASE_BASE_Q48[12] = {
    104366567318ULL, 110572526359ULL, 117147510927ULL, 124113464424ULL,
    131493635070ULL, 139312653500ULL, 147596614960ULL, 156373166403ULL,
    165671598752ULL, 175522944661ULL, 185960082081ULL, 197017843989ULL,
};

static const uint8_t WAVE_TRIANGLE[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
};

#define Q15_ONE 32768
#define PHASE_MASK ((1ULL << 48) - 1ULL)
#define SWEEP_DOWN_DELTA_Q48 44237699430ULL
#define SWEEP_UP_DELTA_Q48   20646639365ULL

uint32_t audio_note_hz_q8(uint8_t midi_note)
{
    return MIDI_HZ_Q8[midi_note & 0x7fu];
}

uint32_t audio_sfx_samples(sfx_id_t id)
{
    if ((unsigned)id >= SFX_COUNT) return 0;
    return (uint32_t)EFFECTS[id].duration_ms * AUDIO_SAMPLE_RATE / 1000u + 1u;
}

static uint64_t phase_increment(uint8_t midi)
{
    return PHASE_BASE_Q48[midi % 12u] << (midi / 12u);
}

/* Rounded (value * delta_q48) >> 48 without a 128-bit runtime helper. */
static uint64_t mul_delta_q48(uint64_t value, uint64_t delta)
{
    uint64_t hi_product = (value >> 16) * delta;
    uint64_t remainder = (hi_product & 0xffffffffULL) << 16;
    remainder += (value & 0xffffULL) * delta;
    return (hi_product >> 32) + (remainder >> 48) +
           ((remainder & ((1ULL << 48) - 1ULL)) >= (1ULL << 47));
}

static uint64_t sweep_next(uint64_t increment, uint8_t sweep)
{
    if (sweep == SWEEP_DOWN) {
        return increment - mul_delta_q48(increment, SWEEP_DOWN_DELTA_Q48);
    }
    if (sweep == SWEEP_UP) {
        return increment + mul_delta_q48(increment, SWEEP_UP_DELTA_Q48);
    }
    return increment;
}

static int32_t div_round_nearest(int64_t value, int32_t divisor)
{
    if (value >= 0) return (int32_t)((value + divisor / 2) / divisor);
    return (int32_t)((value - divisor / 2) / divisor);
}

static int32_t wave_q15(uint8_t kind, uint8_t duty, uint64_t phase,
                        uint16_t *noise, uint8_t noise_short)
{
    if (kind == WAVE_NOISE) {
        uint16_t bit = (uint16_t)((*noise ^ (*noise >> 1)) & 1u);
        *noise >>= 1;
        *noise |= (uint16_t)(bit << 14);
        if (noise_short) {
            *noise = (uint16_t)((*noise & ~(1u << 6)) | (bit << 6));
        }
        return (*noise & 1u) ? -Q15_ONE : Q15_ONE;
    }
    if (kind == WAVE_TABLE) {
        uint8_t index = (uint8_t)((phase * 32u) >> 48);
        return div_round_nearest((int32_t)(2 * WAVE_TRIANGLE[index] - 15) * Q15_ONE, 15);
    }

    switch (duty) {
    case 0:
        return phase < (1ULL << 45) ? Q15_ONE : -4681;
    case 1:
        return phase < (1ULL << 46) ? Q15_ONE : -10923;
    case 2:
        return phase < (1ULL << 47) ? Q15_ONE : -Q15_ONE;
    default:
        return phase < (3ULL << 46) ? 10923 : -Q15_ONE;
    }
}

static uint8_t envelope_percent(uint8_t env, uint32_t sample_in_note)
{
    uint32_t steps = sample_in_note * 1000u / (AUDIO_SAMPLE_RATE * 16u);
    int32_t value = 100;
    if (env == ENV_PLUCK) value -= (int32_t)steps * 10;
    if (env == ENV_FADE) value -= (int32_t)steps * 3;
    if (env == ENV_HIT) value -= (int32_t)steps * 25;
    return value > 0 ? (uint8_t)value : 0;
}

static uint8_t volume_percent(uint8_t volume)
{
    static const uint8_t values[] = {100, 70, 60, 55, 50};
    return values[volume];
}

typedef struct {
    const track_t *track;
    uint64_t phase;
    uint64_t increment;
    uint32_t note_sample;
    uint16_t noise;
    uint8_t note_index;
} track_state_t;

typedef struct {
    sfx_id_t id;
    uint32_t position;
    uint8_t track_count;
    track_state_t tracks[2];
    uint8_t valid;
} render_state_t;

/* Playback has one consumer task. The cache also makes host-side random reads
 * deterministic: a non-contiguous request simply resets and advances. */
static render_state_t s_render;

static void render_reset(sfx_id_t id)
{
    const effect_t *effect = &EFFECTS[id];
    s_render.id = id;
    s_render.position = 0;
    s_render.track_count = effect->count;
    s_render.valid = 1;
    for (uint8_t i = 0; i < effect->count; i++) {
        track_state_t *state = &s_render.tracks[i];
        state->track = &effect->tracks[i];
        state->phase = 0;
        state->increment = 0;
        state->note_sample = 0;
        state->noise = 0x7fffu;
        state->note_index = 0;
    }
}

static int32_t track_sample(track_state_t *state)
{
    const track_t *track = state->track;
    while (state->note_index < track->count) {
        const note_t *note = &track->notes[state->note_index];
        uint32_t samples = (uint32_t)DURATION_MS[note->duration] *
                           AUDIO_SAMPLE_RATE / 1000u;
        if (state->note_sample < samples) break;
        state->note_index++;
        state->note_sample = 0;
        state->increment = 0;
    }
    if (state->note_index >= track->count) return 0;

    const note_t *note = &track->notes[state->note_index];
    if (note->midi == REST) {
        state->note_sample++;
        return 0;
    }
    if (state->increment == 0) {
        state->increment = phase_increment((uint8_t)note->midi);
    }

    uint8_t duty = note->duty_env & 3u;
    uint8_t env = (note->duty_env >> 2) & 3u;
    int32_t wave = wave_q15(track->kind, duty, state->phase, &state->noise,
                            track->noise_short);
    int32_t amp = envelope_percent(env, state->note_sample) *
                  volume_percent(note->volume);
    int32_t sample = div_round_nearest((int64_t)wave * amp, 10000);

    state->phase = (state->phase + state->increment) & PHASE_MASK;
    state->increment = sweep_next(state->increment, track->sweep);
    state->note_sample++;
    return sample;
}

static int16_t render_next(void)
{
    int32_t mix = 0;
    for (uint8_t i = 0; i < s_render.track_count; i++) {
        mix += track_sample(&s_render.tracks[i]);
    }
    s_render.position++;

    int64_t scaled = (int64_t)mix * 22 * 32767;
    int32_t pcm = (int32_t)(scaled / (100LL * Q15_ONE));
    if (pcm > 32767) pcm = 32767;
    if (pcm < -32767) pcm = -32767;
    return (int16_t)pcm;
}

uint32_t audio_render(sfx_id_t id, uint32_t from, uint32_t count, int16_t *out)
{
    uint32_t total = audio_sfx_samples(id);
    if (!out || from >= total || count == 0) return 0;
    if (count > total - from) count = total - from;

    if (!s_render.valid || s_render.id != id || s_render.position != from) {
        render_reset(id);
        while (s_render.position < from) (void)render_next();
    }
    for (uint32_t i = 0; i < count; i++) out[i] = render_next();
    return count;
}

sfx_id_t audio_encounter_alert(uint8_t rarity, bool shiny) { return shiny ? SFX_SHINY : rarity >= 4 ? SFX_RARE : SFX_ENCOUNTER; }
