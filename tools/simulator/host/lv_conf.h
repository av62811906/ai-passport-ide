/**
 * @file lv_conf.h
 * LVGL configuration for the AI Passport desktop simulator.
 *
 * Every option not listed here falls back to LVGL's built-in default
 * (src/lv_conf_internal.h uses `#ifndef` for all of them), so this file only
 * pins the settings that must match the device build in sdkconfig.defaults:
 * 16-bit color and the same Montserrat font sizes the tuner draws with.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Color depth: device uses CONFIG_LV_COLOR_DEPTH_16 (RGB565). */
#define LV_COLOR_DEPTH 16

/* Host uses the C library allocator instead of LVGL's fixed static pool; the
 * pool size is a device RAM concern (no PSRAM) and not meaningful here. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

/* Font sizes enabled on the device (sdkconfig.defaults). The tuner references
 * lv_font_montserrat_14/20/28/48 and uses montserrat_20 as the tuner font's
 * fallback; the home page draws the user name with montserrat_32, so all five
 * must be present. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1

#endif /* LV_CONF_H */
