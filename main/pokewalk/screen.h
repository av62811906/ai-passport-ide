// main/screen.h —— 帧缓冲、推屏、截图。
//
// 这一层解决两件事：
//   · **字节序** —— ST7789 要大端 RGB565，内存里是小端。
//     BSP 的 LVGL 路径靠 swap_bytes 代劳，自己推屏必须手动交换
//   · **截图** —— 把帧缓冲 base64 打到串口，PC 侧存成 PNG。
//     渲染问题靠拍照调太慢，而且照片有反光偏色，截图是像素级准确的
#pragma once

#include <stdint.h>

#define SCREEN_W 240
#define SCREEN_H 320

// 横带高度。240×80×2 = 37.5KB，在 231KB 可用堆里安全，
// 且 80 整除 320（4 条，不用处理余数）。
#define SCREEN_BAND_H 80
#define SCREEN_BANDS (SCREEN_H / SCREEN_BAND_H)

// Opt-in guard for elements that must be wholly contained in one band.
// Some sprites intentionally span bands, so checking every clipped pixel would
// be noisy. Constant layout coordinates can use this at file scope instead:
// violations fail the build and the normal rendering path pays no runtime cost.
#define SCREEN_ELEMENT_FITS_BAND(y, h) \
    ((h) > 0 && (y) >= 0 && (y) + (h) <= SCREEN_H && \
     (y) / SCREEN_BAND_H == ((y) + (h) - 1) / SCREEN_BAND_H)
#define SCREEN_ASSERT_WITHIN_BAND(name, y, h) \
    _Static_assert(SCREEN_ELEMENT_FITS_BAND((y), (h)), \
                   #name " crosses a screen band boundary")

// 显式声明：此元素**允许**跨横带边界（与 SCREEN_ASSERT_WITHIN_BAND 二选一）。
// 约束：高度 ≤ 2×带高（最多跨一条边界），且在屏幕内。
// 跨带元素的渲染本身没问题（screen_px 越界静默裁剪），
// 但**动画重绘时必须重画所有涉及的带**，否则两半不同帧 = 撕裂（BUG-1）。
// 用这个宏的地方必须在 tick/动画回调里重画全部跨带。
#define SCREEN_ASSERT_ALLOW_CROSS_BAND(name, y, h) \
    _Static_assert((h) > 0 && (h) <= SCREEN_BAND_H * 2 && \
                   (y) >= 0 && (y) + (h) <= SCREEN_H, \
                   #name " cross-band: h must be <= 2*band_h and on-screen")

// 页面重画回调。截图时用 —— 见 screen_dump 的说明。
typedef void (*screen_redraw_cb_t)(void);

// RGB888 → RGB565。编译期能算的就别放运行时。
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | \
                                 (((g) & 0xFC) << 3) | \
                                 ((b) >> 3)))
#define RGB_HEX(h) RGB(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)

// GSC UI colors (RGB565): bg_tiles.pal, hp_bar.pal, exp_bar.pal.
// Keep the shared names for existing pages; sprite palettes stay in assets.
#define C_BG        0xFFF0u   // background
#define C_INK       0x0000u   // primary text / outlines
#define C_MID       0x7240u   // secondary text
#define C_LIGHT     0xAD55u   // generic track / cursor (#a8a8a8 quantized)
#define C_HP_TRACK  0xF6AFu   // HP track
#define C_FOCUS     0x247Fu   // focus / frame
#define C_HP_GREEN  0x05E0u
#define C_HP_YELLOW 0xFD60u
#define C_HP_RED    0xF800u

// Claim exclusive pixel output under bsp_lvgl_lock, before drawing a game page.
// Pause only the LVGL display refresh timer and disable invalidation; gameplay
// timers/input continue running. A blank loaded screen alone still queues a
// background flush that can overwrite directly rendered bands after startup.
void screen_own_display(void);

// 交还面板给宿主 LVGL 界面：恢复失效通知与显示刷新定时器。
// 必须在把控制权交回宿主首页/调音器页之前调用，否则 LVGL 不再重绘。
void screen_release_display(void);

// 当前横带的缓冲。直接写它比逐像素函数快得多 —— 画 sprite 时用。
uint16_t *screen_band(void);

void screen_band_clear(uint16_t rgb565);

// 画一个像素。坐标是**横带内**的（0..SCREEN_BAND_H），
// 越界静默忽略 —— 调用方按整屏坐标算完减去 band_y 就行，
// 不用判断在不在带内。
void screen_px(int x, int y, uint16_t rgb565);

// 把当前横带推到屏幕 band_y 处。内部处理字节序。
void screen_push_band(int band_y);

// 注册页面的重画函数。截图靠它 —— 见 screen_dump。
void screen_set_redraw(screen_redraw_cb_t cb);
// Optional final opaque UI layer, shared by device output and screenshots.
void screen_set_overlay(void (*draw_band)(int band_y));
// Restore the current page before revealing the backlight after screen idle.
void screen_redraw_current(void);

// 截图：让页面重画一遍，每条带 base64 吐到串口。
// PC 侧用 `python3 tools/device/screenshot.py` 收成 PNG。
//
// **不缓存整屏**：那要 150KB static，实测直接 DRAM 溢出
// 32624 字节链接失败。回调式的代价是画面闪一下，换零常驻内存。
void screen_dump(void);

// Official publisher screenshot: current render state, binary RGB565LE.
// Caller holds the LVGL lock. Does not push pixels, wake, or change game state.
void screen_dump_fap(void);

// 内部用：把当前横带吐出去。screen_push_band 在 dump 模式下调它。
void screen_emit_band(int band_y);
