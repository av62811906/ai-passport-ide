#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_BRIGHTNESS_DEFAULT 100
#define DISPLAY_BRIGHTNESS_MIN 10

void display_settings_init(void);
uint8_t display_settings_brightness(void);
// Persist before applying. A sleeping screen must stay dark.
bool display_settings_set_brightness(uint8_t percent);
bool display_settings_battery_visible(void);
bool display_settings_set_battery_visible(bool visible);
