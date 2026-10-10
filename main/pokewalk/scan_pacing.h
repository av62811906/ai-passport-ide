#pragma once
#include <stdbool.h>
#include <stdint.h>

// Only confirmed, unchanged fingerprints while screen-off qualify for backoff.
// Failures/empty scans/foreground/movement reset it, so stale state cannot hide
// resumed movement indefinitely. Cap at 60s to bound extra detection latency.
typedef struct { uint8_t stable; } scan_pacing_t;
static inline uint32_t scan_pacing_next(scan_pacing_t *p, bool off, bool stable) {
    if (!off || !stable) p->stable = 0;
    else if (p->stable < 3) p->stable++;
    return p->stable >= 3 ? 60000u : 30000u;
}
