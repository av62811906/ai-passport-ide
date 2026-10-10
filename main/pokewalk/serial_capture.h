// Reserve the complete publisher request before legacy single-key dispatch.
// A malformed F-prefixed line must never inject A/C presses into the game.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { uint8_t matched; bool active, invalid; } serial_capture_parser_t;
enum { SERIAL_CAPTURE_OTHER, SERIAL_CAPTURE_CONSUMED, SERIAL_CAPTURE_READY };
static inline int serial_capture_feed(serial_capture_parser_t *p, char c)
{
    static const char request[] = "FAP_SCREENSHOT_V1";
    if (!p->active) {
        if (c != request[0]) return SERIAL_CAPTURE_OTHER;
        *p = (serial_capture_parser_t){.matched=1, .active=true};
        return SERIAL_CAPTURE_CONSUMED;
    }
    if (c == '\r' || c == '\n') {
        bool complete = !p->invalid && p->matched == sizeof(request)-1;
        *p = (serial_capture_parser_t){0};
        return complete ? SERIAL_CAPTURE_READY : SERIAL_CAPTURE_CONSUMED;
    }
    if (!p->invalid && p->matched < sizeof(request)-1 && c == request[p->matched]) p->matched++;
    else p->invalid = true;
    return SERIAL_CAPTURE_CONSUMED;
}
