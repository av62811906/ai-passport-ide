// main/opening.c —— 与 sim/opening.py::OpeningFlow 逐帧一致。

#include <stddef.h>
#include <string.h>

#include "opening.h"

typedef struct {
    const char *lines[OPENING_LINES_PER_BOX];
    uint8_t line_count;
    bool show_oak;
    uint8_t show_mon;
    uint8_t pause_after;
} opening_box_t;

static const opening_box_t SCRIPT[OPENING_BOXES] = {
    {
        .lines = {"你好！", "欢迎来到", "宝可梦的世界！"},
        .line_count = 3,
        .show_oak = true,
    },
    {
        .lines = {"我叫大木", "人们都叫我", "宝可梦博士"},
        .line_count = 3,
        .show_oak = true,
    },
    {
        .lines = {"这个世界里", "生活着一种", "被称为宝可梦的生物"},
        .line_count = 3,
        .show_oak = true,
        .show_mon = 25,
    },
    {
        .lines = {"有人把它们当作伙伴", "也有人", "让它们互相切磋"},
        .line_count = 3,
        .show_oak = true,
        .show_mon = 25,
    },
    {
        .lines = {"而我", "把研究宝可梦", "当作毕生的事业"},
        .line_count = 3,
        .show_oak = true,
        .pause_after = 20,
    },
    {
        .lines = {"你的宝可梦传奇", "就要开始了！"},
        .line_count = 2,
        .show_oak = true,
    },
    {
        .lines = {"一个充满", "梦想与冒险的世界", "正在等着你", "出发吧！"},
        .line_count = 4,
    },
};

static uint16_t utf8_len(const char *s)
{
    uint16_t n = 0;
    if (!s) return 0;
    while (*s) {
        // 每个 UTF-8 码点只有首字节不以 10 开头。
        if (((uint8_t)*s & 0xc0) != 0x80) n++;
        s++;
    }
    return n;
}

void opening_init(opening_t *o)
{
    if (o) memset(o, 0, sizeof(*o));
}

uint8_t opening_line_count(const opening_t *o)
{
    if (!o || o->box >= OPENING_BOXES) return 0;
    return SCRIPT[o->box].line_count;
}

const char *opening_line(const opening_t *o, uint8_t line)
{
    if (!o || o->box >= OPENING_BOXES ||
        line >= SCRIPT[o->box].line_count) return NULL;
    return SCRIPT[o->box].lines[line];
}

uint16_t opening_full_len(const opening_t *o)
{
    uint16_t n = 0;
    uint8_t lines = opening_line_count(o);
    for (uint8_t i = 0; i < lines; i++) n += utf8_len(opening_line(o, i));
    return n;
}

bool opening_typing(const opening_t *o)
{
    return o && o->typed < opening_full_len(o);
}

void opening_tick(opening_t *o)
{
    if (!o || o->done) return;
    o->frame++;
    if (opening_typing(o) &&
        o->frame % OPENING_TYPE_FRAMES_PER_CHAR == 0) {
        o->typed++;
    }
}

uint8_t opening_press(opening_t *o, char key)
{
    if (!o || o->done) return 0;

    if (key == 'C') {
        o->skipped = true;
        o->done = true;
        return 4;
    }
    if (key != 'A') return 0;       // B 刻意无操作

    if (opening_typing(o)) {
        o->typed = opening_full_len(o);
        return 1;
    }

    o->box++;
    o->typed = 0;
    o->frame = 0;
    if (o->box >= OPENING_BOXES) {
        o->done = true;
        return 3;
    }
    return 2;
}

uint16_t opening_total_frames(void)
{
    uint16_t frames = 0;
    opening_t o;
    opening_init(&o);
    for (uint8_t i = 0; i < OPENING_BOXES; i++) {
        o.box = i;
        frames += OPENING_BOX_APPEAR_FRAMES;
        frames += opening_full_len(&o) * OPENING_TYPE_FRAMES_PER_CHAR;
        frames += SCRIPT[i].pause_after;
    }
    return frames;
}

uint8_t opening_show_mon(const opening_t *o)
{
    if (!o || o->box >= OPENING_BOXES) return 0;
    return SCRIPT[o->box].show_mon;
}

bool opening_show_oak(const opening_t *o)
{
    if (!o || o->box >= OPENING_BOXES) return false;
    return SCRIPT[o->box].show_oak;
}
