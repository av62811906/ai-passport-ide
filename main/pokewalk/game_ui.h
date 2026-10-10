// Shared GSC presentation for the gameplay pages. All coordinates are screen
// coordinates; each call clips through the current 240x80 production band.
#pragma once
#include "move_policy.h"

#include <stdint.h>
#include <stdbool.h>
#include "battle_hud.h"

#define GAME_UI_BG BATTLE_HUD_ORIGINAL_BACKGROUND
#define GAME_UI_INK C_INK
#define GAME_UI_MUTED RGB_HEX(0x686868)
#define GAME_UI_ACCENT C_FOCUS

void game_ui_title(int band_y, const char *title, const char *right_text);
#define GAME_UI_NAV_HINT "A上 B下 C确认 长按B返回"
#define GAME_UI_BACK_HINT "长按B返回"
void game_ui_footer(int band_y, const char *hint);
void game_ui_action_row(int band_y, int x, int y, int width, const char *const *labels, unsigned count, unsigned selected);
void game_ui_actions(int band_y, const char *const *labels, unsigned count, unsigned selected);
void game_ui_list_marker(int band_y, int x, int y, unsigned index, unsigned count, unsigned selected);
const char *game_ui_list_hint(unsigned count);
// Center visible ink, compensating for font bearings and transparent padding.
void game_ui_text_centered(int band_y, int x, int y, int w, int h,
                           const char *text, uint16_t color);
void game_ui_sprite_centered(int band_y, int x, int y, int w, int h,
                             const uint8_t *data, int sw, int sh, int scale,
                             const uint16_t *palette);
void game_ui_thumbnail_centered(int band_y, int x, int y, int w, int h,
                                const uint8_t *data, int size, int dest_size,
                                const uint16_t *palette);
// Original frame-1 tiles at 1x; width/height must be multiples of 8 and >=24.
void game_ui_box(int band_y, int x, int y, int w, int h);
// A 16px-high status gauge, with stepped black ends and a blue fill.
void game_ui_meter(int band_y, int x, int y, int w, uint8_t pct);
void game_ui_cursor(int band_y, int x, int y);

void game_ui_text_fitted(int band_y, int x, int y, int width, const char *text, uint16_t color);

void game_ui_moves(int band_y,uint16_t species,uint8_t level,unsigned selected,bool in_battle);

// Ordered pixel fade to the page background; amount 0..16.
void game_ui_fade_background(int band_y,unsigned amount);

void game_ui_move_settings(int band_y,uint16_t species,uint8_t level,unsigned selected,const move_policy_t *policy,const char *feedback);
