#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp_button.h"

// All calls run with the LVGL lock held. Only the display sleeps: world tasks
// and their monotonic clock keep running. Busy animations defer the timeout.
bool screen_idle_init(bool (*busy)(void));
// 停止熄屏计时器并（若屏幕已熄）唤醒面板，交还宿主界面用。
// 与 screen_idle_init() 成对；宿主界面不接管时应调用本函数清除全局计时器。
void screen_idle_deinit(void);
// State notifications run on the UI task, after a real sleep/wake transition.
void screen_idle_set_state_callback(void (*changed)(bool off));
uint32_t screen_idle_timeout_ms(void);
bool screen_idle_is_off(void);
void screen_idle_note_activity(void);
void screen_idle_request_off(void);
// True consumes the input, including every event in the wake-up gesture.
bool screen_idle_filter_key(bsp_btn_t button, bsp_btn_ev_t event);
// Lock-free cleanup mailbox for a physical event whose LVGL lock timed out.
// Preserves release/end edges so a dropped callback cannot leave a key held.
void screen_idle_input_dropped(bsp_btn_t button, bsp_btn_ev_t event);
