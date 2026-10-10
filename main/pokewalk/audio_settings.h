#pragma once
#include <stdbool.h>

// Independent NVS preference: old game saves remain byte-compatible.
// Missing/unreadable preferences start muted. A failed write keeps the old value.
void audio_settings_init(void);
bool audio_settings_muted(void);
bool audio_settings_set_muted(bool muted);

#include <stdint.h>
#define AUDIO_VOLUME_DEFAULT 55
uint8_t audio_settings_volume(void);
bool audio_settings_set_volume(uint8_t percent);
