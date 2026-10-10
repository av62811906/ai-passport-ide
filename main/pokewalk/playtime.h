#pragma once
#include <stdbool.h>
#include <stdint.h>

// Runtime fractions/clock origin never survive reboot. Only seconds are saved.
typedef struct {
    uint32_t seconds, fraction_us;
    int64_t last_us;
    bool paused;
} playtime_clock_t;

static inline void playtime_init(playtime_clock_t *clock, uint32_t seconds, int64_t now)
{
    *clock = (playtime_clock_t){.seconds = seconds, .last_us = now, .paused = true};
}

static inline void playtime_advance(playtime_clock_t *clock, int64_t now)
{
    if (!clock->paused && now > clock->last_us) {
        uint64_t us = (uint64_t)(now - clock->last_us) + clock->fraction_us;
        uint64_t seconds = (uint64_t)clock->seconds + us / 1000000;
        clock->seconds = seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
        clock->fraction_us = seconds >= UINT32_MAX ? 0 : us % 1000000;
    }
    clock->last_us = now;
}

static inline void playtime_pause(playtime_clock_t *clock, int64_t now, bool paused)
{
    playtime_advance(clock, now);
    clock->paused = paused;
}
