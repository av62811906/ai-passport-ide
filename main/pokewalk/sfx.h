#pragma once

#include "audio.h"
#include "music.h"

/* PCM headroom and a moderate codec level are independent. */
#define SFX_VOLUME_PERCENT 55

/* Start the playback task. Runtime mute/sleep leaves the codec closed. */
/* CONFIG_POKEWALK_SILENT_BOOT makes both functions no-ops for the whole boot. */
void sfx_start(void);

/* Nonblocking: a full or unavailable queue drops the new request. */
void sfx_play(sfx_id_t id);

// Same music ID keeps its current loop position; all calls are nonblocking.
void sfx_music_play(music_id_t id);
void sfx_move(uint16_t move_id, uint8_t type, bool missed);

// Coalesce simultaneous spawns: shiny > rare > ordinary; never wake the screen.
void sfx_encounter(uint8_t rarity, bool shiny);

void sfx_cry(uint16_t species);

// Wake the sound worker after publishing a mute, volume, or screen state change.
void sfx_notify_state(void);
