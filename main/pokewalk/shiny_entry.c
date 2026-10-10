#include "shiny_entry.h"
#include "shiny_entry_assets.h"
#include "render.h"
#include "screen.h"

void shiny_entry_begin(shiny_entry_t *entry, uint8_t mask)
{
    *entry = (shiny_entry_t){.mask = mask & 3u};
}

unsigned shiny_entry_side(const shiny_entry_t *entry)
{
    if ((entry->mask & 2u) && entry->elapsed_ms < SHINY_ENTRY_MS) return 2;
    unsigned start = (entry->mask & 2u) ? SHINY_ENTRY_MS : 0;
    if ((entry->mask & 1u) && entry->elapsed_ms < start + SHINY_ENTRY_MS) return 1;
    return 0;
}

bool shiny_entry_step(shiny_entry_t *entry, unsigned delta_ms)
{
    unsigned previous = shiny_entry_side(entry);
    if (!previous) return false;
    unsigned remaining = 2 * SHINY_ENTRY_MS - entry->elapsed_ms;
    entry->elapsed_ms += delta_ms < remaining ? delta_ms : remaining;
    unsigned next = shiny_entry_side(entry);
    return next && previous != next;
}

void shiny_entry_draw(const shiny_entry_t *entry, int band_y,
                      battle_fx_rect_t pet, battle_fx_rect_t enemy)
{
    unsigned side = shiny_entry_side(entry);
    if (!side || band_y < 0 || band_y >= 240) return;
    unsigned ms = entry->elapsed_ms - ((side == 1 && (entry->mask & 2u)) ? SHINY_ENTRY_MS : 0);
    unsigned tick = ms * 60u / 1000u;
    // Gold FLASH_INVERTED: normal / inverted / normal, five ticks each.
    // Same four luminance bands as the shared Gold move renderer.
    if (tick >= 5 && tick < 10) {
        static const uint16_t inverse[] = {0, 0x6b4d, 0xce59, 0xffff};
        uint16_t *pixels = screen_band();
        for (unsigned i = 0; i < SCREEN_W * SCREEN_BAND_H; ++i) {
            uint16_t c = __builtin_bswap16(pixels[i]);
            unsigned light = ((c >> 11) & 31)*3 + ((c >> 5) & 63)*3 + (c & 31);
            unsigned shade = light > 270 ? 0 : light > 175 ? 1 : light > 65 ? 2 : 3;
            pixels[i] = __builtin_bswap16(inverse[shade]);
        }
    }
    battle_fx_rect_t actor = side == 1 ? pet : enemy;
    int cx = actor.x + actor.w/2, cy = actor.y + actor.h/2;
    // Tiny right-aligned fronts (e.g. Diglett) still need a complete ring.
    // Radius 32 + largest half-sprite 16: keep all eight stars on the stage.
    if (cx < 48) cx = 48;
    if (cx > 192) cx = 192;
    if (cy < 48) cy = 48;
    if (cy > 192) cy = 192;
    // Original radius 16 at 2x, source sine truncated before scaling.
    static const int8_t dx[] = {32,22,0,-22,-32,-22,0,22};
    static const int8_t dy[] = {0,22,32,22,0,-22,-32,-22};
    static const uint8_t frames[] = {0,1,2,1,2};
    // Original yellow OB palette, packed renderer order (black -> transparent).
    static const uint16_t palettes[2][4] = {{0,0xfc21,0xffe7,0xffff}, {0xfc21,0xffe7,0xffff,0xffff}};
    for (unsigned i = 0; i < 8; ++i) {
        if (tick < i*4 || tick - i*4 >= 20) continue;
        const battle_fx_art_t *art = &FX_ART[frames[(tick-i*4)/4]];
        int x = cx + dx[i] - art->w, y = cy + dy[i] - art->h;
        // Source stars fit the battler region; clip against the message window.
        for (unsigned py = 0; py < art->h; ++py) for (unsigned px = 0; px < art->w; ++px) {
            unsigned shade = (art->data[py*((art->w+3)/4)+px/4] >> (6-2*(px%4))) & 3;
            if (shade == 3) continue;
            for (int sy = 0; sy < 2; ++sy) for (int sx = 0; sx < 2; ++sx) {
                int yy = y + (int)py*2 + sy;
                if (yy >= 0 && yy < 240 && yy >= band_y && yy < band_y+SCREEN_BAND_H)
                    screen_px(x+(int)px*2+sx, yy-band_y, palettes[(tick/3)&1u][shade]);
            }
        }
    }
}
