#pragma once
#include "bsp_button.h"
#include "exp.h"

void growth_ui_start(void);
void growth_ui_stop(void);
bool growth_ui_key(bsp_btn_t,bsp_btn_ev_t);
bool growth_ui_active(void);
// Read-only acceptance state. Skill availability still comes from combat.
bool growth_ui_view(exp_growth_t *event,unsigned *page,unsigned *moves);
