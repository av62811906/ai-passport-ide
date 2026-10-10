// main/screen.c —— 帧缓冲、推屏、截图。
//
// ## 为什么需要这一层
//
// 直接 esp_lcd_panel_draw_bitmap 有个坑：**ST7789 走 SPI 要大端
// RGB565，而我们内存里是小端**。BSP 的 LVGL 路径靠 `swap_bytes = true`
// 让 esp_lvgl_port 代劳，绕过 LVGL 自己推屏就漏了这一步 ——
// 实测表现是整屏颜色错乱（GB 绿变成紫白 + 亮绿）。
//
// ## 截图：让调试不再靠拍照
//
// 渲染问题拍照看效率极低：拍一张、传一张、我猜一轮。
// `screen_dump()` 把帧缓冲 base64 打到串口，
// `tools/device/screenshot.py` 收下来存成 PNG —— 我自己就能看。
//
// 240×320×2 = 150KB → base64 后 200KB，115200 波特下约 18 秒。
// 慢，但比来回拍照快得多，而且**像素级准确**：
// 照片有反光、偏色、摩尔纹，截图没有。

#include <string.h>
#include <stdio.h>
#ifndef HOST_BUILD
#include "driver/usb_serial_jtag_vfs.h"
#endif

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "lvgl.h"

#include "bsp_display.h"
#include "screen.h"
#include "screen_idle.h"

static const char *TAG = "screen";

// 一条横带的帧缓冲。240×80×2 = 37.5KB。
//
// 不开整屏（150KB）：可用堆 231KB，WiFi 栈与 LVGL 还要用。
// 这也正是 docs/01-constitution.md 那条「无法整帧缓冲，必须分块
// 横带渲染」的落地 —— 那条约束一直写在纸上，到这里才真的执行。
static uint16_t s_band[SCREEN_W * SCREEN_BAND_H];

// 截图**不缓存整屏**。
//
// 第一版开了 240×320×2 = 150KB 的 static 缓冲，直接 DRAM 溢出
// 32624 字节链接失败 —— 可用堆 231KB 是运行时的数字，
// 而 static 数据要在链接期挤进 dram0_0_seg，那里更紧。
//
// 改成**回调式**：dump 时让页面重画一遍，每画完一条带就把它
// base64 吐出去。代价是 dump 期间画面会闪一下（重画），
// 换来零常驻内存。
static screen_redraw_cb_t s_redraw;
static bool s_dumping;
static bool s_fap_capture;

// 唯一的那张 LVGL 屏。**只建一次**，之后页面切换不碰它 ——
// 见 screen.h 里那段「让 LVGL 闭嘴」。
static lv_obj_t *s_lv_scr;
// 当把面板借给游戏时为 true(此时 LVGL 刷新被暂停)。宿主界面回来前必须交还。
static bool s_owned;

void screen_own_display(void)
{
    if (!s_lv_scr) {
        s_lv_scr = lv_obj_create(NULL);
        lv_obj_set_style_pad_all(s_lv_scr, 0, 0);
        lv_obj_set_style_border_width(s_lv_scr, 0, 0);
        lv_obj_set_style_bg_color(s_lv_scr, lv_color_hex(0xffffff), 0);
    }
    // 每次进入游戏都重新载入这张共享屏：宿主界面可能已把它换成自己的屏。
    lv_screen_load(s_lv_scr);
    // Loading an empty screen queues a full LVGL refresh. If it runs after
    // nav_start draws the game, its background overwrites our four bands.
    // Idle animation redraws only selected bands, leaving a persistent stripe.
    // Own pixel output exclusively; keep the LVGL task and gameplay timers alive.
    // Called while holding bsp_lvgl_lock, before the first game frame.
    lv_display_t *display = lv_obj_get_display(s_lv_scr);
    lv_display_enable_invalidation(display, false);
    lv_timer_t *refresh = lv_display_get_refr_timer(display);
    if (refresh) lv_timer_pause(refresh);
    s_owned = true;
    ESP_LOGI(TAG, "Game owns panel; LVGL pixel refresh disabled");
}

// 交还面板给宿主 LVGL 界面：恢复失效通知与刷新定时器。
//
// 宿主首页/调音器页在游戏之后要正常渲染，必须先把这一层恢复，否则
// LVGL 既不刷新也不认脏区。保留 s_lv_scr，再次进入游戏时复用同一张屏，
// 不反复建/删 LVGL 对象。
void screen_release_display(void)
{
    if (!s_lv_scr || !s_owned) return;
    lv_display_t *display = lv_obj_get_display(s_lv_scr);
    if (display) {
        lv_timer_t *refresh = lv_display_get_refr_timer(display);
        if (refresh) lv_timer_resume(refresh);
        lv_display_enable_invalidation(display, true);
    }
    s_owned = false;
    ESP_LOGI(TAG, "Game released panel; LVGL pixel refresh restored");
}

uint16_t *screen_band(void) { return s_band; }

void screen_band_clear(uint16_t rgb565)
{
    for (int i = 0; i < SCREEN_W * SCREEN_BAND_H; i++) s_band[i] = rgb565;
}

void screen_px(int x, int y, uint16_t rgb565)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_BAND_H) return;
    s_band[y * SCREEN_W + x] = rgb565;
}

// 只有一块 s_band，而 **esp_lcd_panel_draw_bitmap 是异步的** ——
// 它把传输排进 SPI 队列（trans_queue_depth=10）就返回，DMA 在后台读。
// 四条带连着画的话：
//   带 0 排队 → 立刻回来 → 带 1 重填 s_band → 带 0 的 DMA 读到带 1 的像素
//
// 实测表现正是用户报的：颜色错乱（紫/绿混杂）、底部闪烁
// （最后一条带最容易撞上，因为它排队后没有下一条带来「顶」它）。
//
// ## 为什么不注册 on_color_trans_done 回调
//
// 试过，直接把设备打进看门狗复位循环 —— **那个回调已经被
// esp_lvgl_port 占了**（esp_lvgl_port_disp.c:124 的
// lvgl_port_flush_io_ready_callback）。我一注册就把 LVGL 的顶掉，
// 它的 flush 永远等不到完成通知，整个 UI 卡死。
// 一个 panel_io 只有一个回调槽，而这块面板是与 LVGL 共用的。
//
// ## 为什么不开双缓冲
//
// 再要 37.5KB DRAM。而下面这招零内存开销。
//
// ## 用一次 tx_param 逼出同步点
//
// 读 esp_lcd_panel_io_spi.c 发现：**任何 polling 传输之前，
// 驱动会先把队列里所有在途传输收干净**（panel_io_spi_rx_param 与
// tx_param 都有那段 `for (num_trans_inflight) get_trans_result`）。
// 所以发一条最便宜的命令就等于「等前面画完」。
//
// 用 NOP（0x00）—— ST7789 收到它什么都不做，代价是几微秒的 SPI 时钟。
static void wait_dma_done(void)
{
    esp_lcd_panel_io_handle_t io = bsp_display_io();
    if (io) esp_lcd_panel_io_tx_param(io, 0x00, NULL, 0);   // NOP
}

static void (*s_overlay)(int);
void screen_set_overlay(void (*draw_band)(int)) { s_overlay = draw_band; }

void screen_push_band(int band_y)
{
    if (s_overlay) s_overlay(band_y);
    if (s_fap_capture) {
        // Logical RGB565 in the existing little-endian band buffer. No extra
        // 150 KiB framebuffer and no panel/backlight writes during capture.
        fwrite(s_band, 1, sizeof(s_band), stdout);
        return;
    }
    if (screen_idle_is_off() && !s_dumping) return;
    esp_lcd_panel_handle_t panel = bsp_display_panel();
    if (!panel) return;

    // 小端 → 大端。ST7789 走 SPI 要大端 RGB565，
    // 而 C 里的 uint16_t 在 RISC-V 上是小端。
    //
    // 原地交换：交换后这块缓冲就不能再当逻辑颜色读了 ——
    // 所以截图要在交换**之后**做（见下面那段）。
    for (int i = 0; i < SCREEN_W * SCREEN_BAND_H; i++) {
        uint16_t v = s_band[i];
        s_band[i] = (uint16_t)((v >> 8) | (v << 8));
    }

    // dump 模式：吐**交换之后**的字节，也就是屏幕真正收到的东西。
    //
    // 第一版在交换前吐，理由是「输出逻辑颜色，PC 侧不用猜字节序」——
    // 那正好让截图**看不见字节序类的错误**。屏幕紫的时候截图还是绿的，
    // 我据此以为渲染没问题，用户看到的才是真相。
    // 观测手段必须能看见故障，否则它只是在确认我的预期。
    if (s_dumping) screen_emit_band(band_y);

    esp_lcd_panel_draw_bitmap(panel, 0, band_y, SCREEN_W,
                              band_y + SCREEN_BAND_H, s_band);

    // **画完就等** —— 下一次调用会立刻重填 s_band。
    wait_dma_done();
}

// ---------------------------------------------------------------------------
// 截图
// ---------------------------------------------------------------------------

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void screen_set_redraw(screen_redraw_cb_t cb) { s_redraw = cb; }
void screen_redraw_current(void) { if (s_redraw) s_redraw(); }

// 把当前横带 base64 吐到串口。由 screen_push_band 在 dump 模式下调用。
void screen_emit_band(int band_y)
{
    const uint8_t *p = (const uint8_t *)s_band;
    size_t n = (size_t)SCREEN_W * SCREEN_BAND_H * 2;

    printf("@@BAND %d\n", band_y);

    // 每行 76 个 base64 字符（57 字节原始）。不用太长：
    // 串口缓冲有限，长行容易丢。
    char line[80];
    size_t li = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16;
        if (i + 1 < n) v |= (uint32_t)p[i + 1] << 8;
        if (i + 2 < n) v |= p[i + 2];

        line[li++] = B64[(v >> 18) & 0x3F];
        line[li++] = B64[(v >> 12) & 0x3F];
        line[li++] = (i + 1 < n) ? B64[(v >> 6) & 0x3F] : '=';
        line[li++] = (i + 2 < n) ? B64[v & 0x3F] : '=';

        if (li >= 76) {
            line[li] = '\0';
            printf("%s\n", line);
            fflush(stdout);
            li = 0;
        }
    }
    if (li) { line[li] = '\0'; printf("%s\n", line); }
}

void screen_dump(void)
{
    if (!s_redraw) {
        ESP_LOGW(TAG, "没注册重画回调 —— 页面要调 screen_set_redraw()");
        return;
    }
    printf("\n@@SHOT %d %d rgb565be %d\n", SCREEN_W, SCREEN_H, SCREEN_BAND_H);
    fflush(stdout);

    // 让页面重画一遍。每条带在 push 时会被 emit 出去。
    s_dumping = true;
    s_redraw();
    s_dumping = false;

    printf("@@SHOTEND\n");
    fflush(stdout);
    ESP_LOGI(TAG, "截图已输出（%d×%d）", SCREEN_W, SCREEN_H);
}

#ifndef HOST_BUILD
static int capture_discard_log(const char *format, va_list args)
{
    (void)format; (void)args; return 0;
}
#endif

void screen_dump_fap(void)
{
    if (!s_redraw || s_fap_capture) return;
#ifndef HOST_BUILD
    vprintf_like_t previous = esp_log_set_vprintf(capture_discard_log);
#endif
    // Keep background telemetry out of the binary payload. Rendering is frozen
    // by the caller's LVGL lock; this callback only replays the current frame.
    flockfile(stdout);
    fflush(stdout);
#ifndef HOST_BUILD
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
#endif
    printf("\nFAP_SCREENSHOT_V1 %d %d RGB565LE %d\n", SCREEN_W, SCREEN_H,
           SCREEN_W * SCREEN_H * 2);
    s_fap_capture = true;
    s_redraw();
    s_fap_capture = false;
    fflush(stdout);
#ifndef HOST_BUILD
#if CONFIG_LIBC_STDOUT_LINE_ENDING_CRLF
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
#elif CONFIG_LIBC_STDOUT_LINE_ENDING_CR
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CR);
#else
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
#endif
#endif
    funlockfile(stdout);
#ifndef HOST_BUILD
    esp_log_set_vprintf(previous);
#endif
}
