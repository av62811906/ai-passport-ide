#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "bsp_button.h"
// Preview only until the reveal; the existing world transaction owns persistence.
bool evolution_ui_begin(uint16_t from,uint16_t to,uint8_t item);
bool evolution_ui_active(void);
bool evolution_ui_key(bsp_btn_t,bsp_btn_ev_t);
void evolution_ui_stop(void);
unsigned evolution_ui_frame(void);
