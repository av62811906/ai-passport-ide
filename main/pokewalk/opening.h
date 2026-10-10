// main/opening.h —— S16 开场剧情的纯状态机。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define OPENING_BOXES                7
#define OPENING_TYPE_FRAMES_PER_CHAR 3
#define OPENING_BOX_APPEAR_FRAMES    6
#define OPENING_LINES_PER_BOX        4

typedef struct {
    uint8_t  box;
    uint16_t typed;
    uint16_t frame;
    bool     done;
    bool     skipped;
} opening_t;

void     opening_init(opening_t *o);
uint16_t opening_full_len(const opening_t *o);
bool     opening_typing(const opening_t *o);
void     opening_tick(opening_t *o);
uint8_t  opening_press(opening_t *o, char key);
uint16_t opening_total_frames(void);
bool     opening_show_oak(const opening_t *o);
uint8_t  opening_show_mon(const opening_t *o);

// 页面取当前框台词用。返回的字符串为静态只读数据；越界返回 NULL。
uint8_t     opening_line_count(const opening_t *o);
const char *opening_line(const opening_t *o, uint8_t line);
