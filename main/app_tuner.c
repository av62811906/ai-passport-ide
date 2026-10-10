// main/app_tuner.c —— 尤克里里调音器界面 + 麦克风采集任务(见 app_tuner.h)。
//
// 界面是针对 240x320 竖屏【重新设计】的,没有沿用基线 demo 的菜单、测试页或像素风外壳:
//   上/下 = 手动选弦,确定 = 在自动/手动之间切换;自动模式按识别结果高亮对应弦。
//
// 线程模型(遵守仓库运行期约束):
//   * 采集任务(capture_task)负责 bsp_audio_read 阻塞读 + 音高检测(纯逻辑);
//   * 任何 lv_* 调用都在 bsp_lvgl_lock() 保护下进行;
//   * 按键回调只入队,真正的状态更新在按键分发任务里完成。
// 采集任务与按键任务共享的界面状态统一在持锁区内读写,不在锁外碰共享变量。
#include "app_tuner.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "tuner_pitch.h"
#include "tuner_texts.h"

static const char *TAG = "tuner";

// ---------------------------------------------------------------------------
// 采集参数
// ---------------------------------------------------------------------------
// 16kHz 采样对 261.6~440Hz 的四根弦余量充足,且与 demo_audio 走同一条已验证的
// ES8311 16bit 单声道通路。
#define TUNER_SAMPLE_RATE    16000
// 分析窗口 64ms;位置精度足够分辨约 0.3 音分。
#define TUNER_WINDOW         1024
// 每次滑动半窗 -> 约 32ms 出一次读数;检测本身约 1~2ms,余量充足。
#define TUNER_HOP            512
#define TUNER_TASK_STACK     4096
#define TUNER_TASK_PRIORITY  4

// 连续多少帧没有有效读数后才收起读数(约 0.5s),避免拨弦间隙指针乱跳。
#define TUNER_HOLD_FRAMES    15
// 电池读数间隔(帧):电量变化很慢,不必每帧走 I2C。
#define TUNER_BATTERY_FRAMES 64
// 无按键也无声音时降低背光,避免亮屏干耗电池(约 45s);任何按键/读数都会恢复。
#define TUNER_DIM_FRAMES     1400
#define TUNER_BRIGHT_PERCENT 100
#define TUNER_DIM_PERCENT    8

// 指针平滑:跟得快、回落慢,读数才不会像噪声一样抖。
#define CENTS_ALPHA_UP       0.45f
#define CENTS_ALPHA_DOWN     0.18f

// ---------------------------------------------------------------------------
// 布局(240x320 竖屏)
// 注意:BSP_LVGL_SCREEN_RADIUS = 30,四角会被圆角遮罩裁掉,
// 所以顶部/底部靠边的文字都做了水平内缩,避免被切掉。
// ---------------------------------------------------------------------------
#define SCREEN_W          240

#define HEADER_TITLE_X    18
#define HEADER_TITLE_Y    6
#define HEADER_BATT_RIGHT 222
#define HEADER_BATT_Y     10

#define SIGN_X            18
#define SIGN_RIGHT        222
#define SIGN_Y            42

#define NOTE_ROW_Y        26
#define NOTE_ROW_H        60
#define STATE_ROW_Y       88

#define SCALE_ROW_Y       118
#define TRACK_X           20
#define TRACK_Y           146
#define TRACK_W           200
#define TRACK_H           8
#define TRACK_TICKS       3     /* 左/中/右三处刻线 */
#define CENTER_TICK_H     22
#define CENTER_TICK_W     4
#define SIDE_TICK_H       10
#define SIDE_TICK_W       3

#define NEEDLE_W          4
#define NEEDLE_H          30
#define NEEDLE_Y          135

#define CENTS_ROW_Y       168
#define CENTS_ROW_H       40

#define CHIP_Y            214
#define CHIP_W            46
#define CHIP_H            30
#define CHIP_GAP          6
#define MODE_CHIP_W       62

#define STATUS_ROW_Y      248
#define FOOTER_ROW_Y      280
#define EDGE_X            12

// ---------------------------------------------------------------------------
// 配色
// ---------------------------------------------------------------------------
#define COLOR_BG         0x14161A
#define COLOR_TRACK      0x2A2F36
#define COLOR_INK        0xF2F5F8
#define COLOR_MUTED      0x7C828C
#define COLOR_IN_TUNE    0x3DDC97
#define COLOR_OFF        0xF5A623
#define COLOR_CHIP_ON    0x1E6F5C
#define COLOR_CHIP_ON_TX 0x8CF3C9
#define COLOR_CHIP_OFF   0x22262C

// 四根弦的显示字母(与 tuner_string_t 顺序一致)。
static const char STRING_LETTERS[TUNER_STRING_COUNT] = { 'G', 'C', 'E', 'A' };
static const char *const STRING_NOTE_LABELS[TUNER_STRING_COUNT] = { "G4", "C4", "E4", "A4" };

// ---------------------------------------------------------------------------
// 共享状态:界面对象与下面这些变量都只在持 bsp_lvgl_lock() 时读写。
// ---------------------------------------------------------------------------
static lv_obj_t *s_scr;
static lv_obj_t *s_battery;
static lv_obj_t *s_sign_flat;
static lv_obj_t *s_sign_sharp;
static lv_obj_t *s_note;
static lv_obj_t *s_octave;
static lv_obj_t *s_state;
static lv_obj_t *s_needle;
static lv_obj_t *s_cents_value;
static lv_obj_t *s_cents_unit;
static lv_obj_t *s_chips[TUNER_STRING_COUNT];
static lv_obj_t *s_chip_labels[TUNER_STRING_COUNT];
static lv_obj_t *s_mode_chip;
static lv_obj_t *s_mode_label;

static bool s_audio_ready;                  // 音频通路是否可用
static bool s_auto_mode = true;             // 自动识别 / 手动选弦
static tuner_string_t s_selected = TUNER_STRING_G;

// 当前展示的读数状态。
static bool s_have_reading;
static tuner_string_t s_shown_string = TUNER_STRING_G;
static float s_shown_cents;

// 背光节流状态。
static int s_idle_frames;
static bool s_dimmed;

// 采集任务句柄与运行标志:app_tuner_exit() 靠它们先停任务、再删界面。
static TaskHandle_t s_capture_task;
static volatile bool s_capture_run;

// 应用自有字体描述符:tuner_font_20 只有 ASCII + 本项目用到的汉字,
// 缺的字形(如 LV_SYMBOL_*)回落到已启用的 Montserrat 20。
LV_FONT_DECLARE(tuner_font_20);
static lv_font_t s_font_20;

// 采集任务私有:只被 capture_task 访问,放静态区省下 2.8KB 任务栈。
static int16_t s_window[TUNER_WINDOW];
static tuner_pitch_scratch_t s_scratch;

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
static void style_plain(lv_obj_t *obj) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

static void set_label_color(lv_obj_t *label, uint32_t color) {
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

// 指针 x 坐标:把 0..1 的比例映射到轨道内,并让指针以自身中心对齐。
static int needle_x_from_ratio(float ratio, int track_w) {
    float center = (float)TRACK_X + ratio * (float)track_w;
    return (int)lroundf(center) - NEEDLE_W / 2;
}

static void format_cents(char *buffer, size_t size, float cents) {
    // 展示前先钳位:手动模式弦没调好时偏差可能远超量程,再大的数字也没意义。
    float clamped = cents;
    if (clamped > 99.0f) clamped = 99.0f;
    if (clamped < -99.0f) clamped = -99.0f;

    int rounded = (int)lroundf(clamped);
    if (rounded == 0) {
        snprintf(buffer, size, "0");
    } else {
        snprintf(buffer, size, "%+d", rounded);
    }
}

// ---------------------------------------------------------------------------
// 界面构建
// ---------------------------------------------------------------------------
static void build_screen(void) {
    s_font_20 = tuner_font_20;
    s_font_20.fallback = &lv_font_montserrat_20;

    s_scr = lv_obj_create(NULL);
    style_plain(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    // 顶栏:左标题,右电量(读不到时显示占位符而不是假数字)。
    lv_obj_t *title = make_label(s_scr, &s_font_20, COLOR_INK, TUNER_TEXT_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, HEADER_TITLE_X, HEADER_TITLE_Y);

    s_battery = make_label(s_scr, &lv_font_montserrat_14, COLOR_MUTED, "--");
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -(SCREEN_W - HEADER_BATT_RIGHT), HEADER_BATT_Y);

    // 升降记号:只在读数偏离时点亮,给"往哪边调"一个方向感。
    s_sign_flat = make_label(s_scr, &s_font_20, COLOR_MUTED, "b");
    lv_obj_align(s_sign_flat, LV_ALIGN_TOP_LEFT, SIGN_X, SIGN_Y);
    s_sign_sharp = make_label(s_scr, &s_font_20, COLOR_MUTED, "#");
    lv_obj_align(s_sign_sharp, LV_ALIGN_TOP_RIGHT, -(SCREEN_W - SIGN_RIGHT), SIGN_Y);

    // 音名:48px 大字 + 八度数字,用 flex 行保证两者始终贴合。
    lv_obj_t *note_row = lv_obj_create(s_scr);
    style_plain(note_row);
    lv_obj_set_size(note_row, SCREEN_W, NOTE_ROW_H);
    lv_obj_align(note_row, LV_ALIGN_TOP_MID, 0, NOTE_ROW_Y);
    lv_obj_set_flex_flow(note_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(note_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(note_row, 2, 0);

    s_note = make_label(note_row, &lv_font_montserrat_48, COLOR_MUTED, "-");
    s_octave = make_label(note_row, &s_font_20, COLOR_MUTED, "4");

    // 中央提示/判定。
    s_state = make_label(s_scr, &s_font_20, COLOR_MUTED, TUNER_TEXT_PLUCK);
    lv_obj_align(s_state, LV_ALIGN_TOP_MID, 0, STATE_ROW_Y);

    // 刻度值。
    lv_obj_t *scale_min = make_label(s_scr, &lv_font_montserrat_14, COLOR_MUTED, "-50");
    lv_obj_align(scale_min, LV_ALIGN_TOP_LEFT, TRACK_X - 6, SCALE_ROW_Y);

    lv_obj_t *scale_mid = make_label(s_scr, &lv_font_montserrat_14, COLOR_MUTED, "0");
    lv_obj_align(scale_mid, LV_ALIGN_TOP_MID, 0, SCALE_ROW_Y);

    lv_obj_t *scale_max = make_label(s_scr, &lv_font_montserrat_14, COLOR_MUTED, "+50");
    lv_obj_align(scale_max, LV_ALIGN_TOP_RIGHT, -(SCREEN_W - (TRACK_X + TRACK_W) - 6), SCALE_ROW_Y);

    // 轨道 + 刻线 + 指针。
    lv_obj_t *track = lv_obj_create(s_scr);
    style_plain(track);
    lv_obj_set_size(track, TRACK_W, TRACK_H);
    lv_obj_set_pos(track, TRACK_X, TRACK_Y);
    lv_obj_set_style_radius(track, TRACK_H / 2, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);

    // 起/中/终三处刻线,中点更高更亮,方便一眼看出"准"在哪。
    for (int i = 0; i < TRACK_TICKS; ++i) {
        bool center = (i == TRACK_TICKS / 2);
        int w = center ? CENTER_TICK_W : SIDE_TICK_W;
        int h = center ? CENTER_TICK_H : SIDE_TICK_H;
        int cx = TRACK_X + (TRACK_W * i) / (TRACK_TICKS - 1);

        lv_obj_t *tick = lv_obj_create(s_scr);
        style_plain(tick);
        lv_obj_set_size(tick, w, h);
        lv_obj_set_pos(tick, cx - w / 2, TRACK_Y + TRACK_H / 2 - h / 2);
        lv_obj_set_style_radius(tick, w / 2, 0);
        lv_obj_set_style_bg_color(tick, lv_color_hex(center ? COLOR_INK : COLOR_MUTED), 0);
        lv_obj_set_style_bg_opa(tick, center ? LV_OPA_70 : LV_OPA_40, 0);
    }

    s_needle = lv_obj_create(s_scr);
    style_plain(s_needle);
    lv_obj_set_size(s_needle, NEEDLE_W, NEEDLE_H);
    lv_obj_set_style_radius(s_needle, NEEDLE_W / 2, 0);
    lv_obj_set_style_bg_color(s_needle, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_bg_opa(s_needle, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_needle, needle_x_from_ratio(0.5f, TRACK_W), NEEDLE_Y);

    // 音分读数:数字 + 单位,flex 行整体居中。
    lv_obj_t *cents_row = lv_obj_create(s_scr);
    style_plain(cents_row);
    lv_obj_set_size(cents_row, SCREEN_W, CENTS_ROW_H);
    lv_obj_align(cents_row, LV_ALIGN_TOP_MID, 0, CENTS_ROW_Y);
    lv_obj_set_flex_flow(cents_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cents_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cents_row, 6, 0);

    s_cents_value = make_label(cents_row, &lv_font_montserrat_28, COLOR_MUTED, "0");
    s_cents_unit = make_label(cents_row, &s_font_20, COLOR_MUTED, TUNER_TEXT_CENTS_UNIT);

    // 弦块:选中/识别到的那根高亮。
    int chips_w = TUNER_STRING_COUNT * CHIP_W + (TUNER_STRING_COUNT - 1) * CHIP_GAP;
    int chips_x = (SCREEN_W - chips_w) / 2;
    for (int i = 0; i < TUNER_STRING_COUNT; ++i) {
        lv_obj_t *chip = lv_obj_create(s_scr);
        style_plain(chip);
        lv_obj_set_size(chip, CHIP_W, CHIP_H);
        lv_obj_set_pos(chip, chips_x + i * (CHIP_W + CHIP_GAP), CHIP_Y);
        lv_obj_set_style_radius(chip, 8, 0);
        lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_CHIP_OFF), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);

        s_chips[i] = chip;
        s_chip_labels[i] = make_label(chip, &s_font_20, COLOR_MUTED, STRING_NOTE_LABELS[i]);
        lv_obj_center(s_chip_labels[i]);
    }

    // 乐器名 + 当前模式。
    lv_obj_t *instrument = make_label(s_scr, &s_font_20, COLOR_MUTED, TUNER_TEXT_INSTRUMENT);
    lv_obj_align(instrument, LV_ALIGN_TOP_LEFT, EDGE_X + 6, STATUS_ROW_Y);

    s_mode_chip = lv_obj_create(s_scr);
    style_plain(s_mode_chip);
    lv_obj_set_size(s_mode_chip, MODE_CHIP_W, CHIP_H);
    lv_obj_align(s_mode_chip, LV_ALIGN_TOP_RIGHT, -(EDGE_X + 6), STATUS_ROW_Y - 4);
    lv_obj_set_style_radius(s_mode_chip, 8, 0);
    s_mode_label = make_label(s_mode_chip, &s_font_20, COLOR_CHIP_ON_TX, TUNER_TEXT_MODE_AUTO);
    lv_obj_center(s_mode_label);

    // 底部按键提示。
    lv_obj_t *key_string = make_label(s_scr, &s_font_20, COLOR_MUTED, TUNER_TEXT_KEY_STRING);
    lv_obj_align(key_string, LV_ALIGN_TOP_LEFT, EDGE_X, FOOTER_ROW_Y);
    lv_obj_t *key_mode = make_label(s_scr, &s_font_20, COLOR_MUTED, TUNER_TEXT_KEY_MODE);
    lv_obj_align(key_mode, LV_ALIGN_TOP_RIGHT, -EDGE_X, FOOTER_ROW_Y);

    lv_screen_load(s_scr);
}

// 让某根弦的弦块高亮(持锁调用)。
static void highlight_chip_locked(tuner_string_t string) {
    for (int i = 0; i < TUNER_STRING_COUNT; ++i) {
        bool on = (i == (int)string);
        lv_obj_set_style_bg_color(s_chips[i], lv_color_hex(on ? COLOR_CHIP_ON : COLOR_CHIP_OFF), 0);
        set_label_color(s_chip_labels[i], on ? COLOR_CHIP_ON_TX : COLOR_MUTED);
    }
}

// 刷新模式芯片(持锁调用)。
static void refresh_mode_locked(void) {
    lv_label_set_text(s_mode_label, s_auto_mode ? TUNER_TEXT_MODE_AUTO : TUNER_TEXT_MODE_MANUAL);
    lv_obj_set_style_bg_color(s_mode_chip,
                              lv_color_hex(s_auto_mode ? COLOR_CHIP_ON : COLOR_CHIP_OFF), 0);
    set_label_color(s_mode_label, s_auto_mode ? COLOR_CHIP_ON_TX : COLOR_MUTED);
}

// 没有读数时收起结论,只提示拨弦(持锁调用)。
static void show_idle_locked(void) {
    char note_text[2] = { STRING_LETTERS[s_selected], '\0' };

    lv_label_set_text(s_note, note_text);
    lv_label_set_text(s_octave, "4");
    set_label_color(s_note, COLOR_MUTED);
    set_label_color(s_octave, COLOR_MUTED);

    lv_label_set_text(s_state, s_audio_ready ? TUNER_TEXT_PLUCK : TUNER_TEXT_NO_AUDIO);
    set_label_color(s_state, COLOR_MUTED);

    lv_label_set_text(s_cents_value, "0");
    set_label_color(s_cents_value, COLOR_MUTED);
    set_label_color(s_cents_unit, COLOR_MUTED);

    set_label_color(s_sign_flat, COLOR_MUTED);
    set_label_color(s_sign_sharp, COLOR_MUTED);

    lv_obj_set_style_bg_color(s_needle, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_x(s_needle, needle_x_from_ratio(0.5f, TRACK_W));

    highlight_chip_locked(s_selected);
    refresh_mode_locked();
}

// 有读数时更新音名/音分/指针(持锁调用)。
static void show_reading_locked(tuner_string_t string, float cents) {
    bool in_tune = fabsf(cents) <= TUNER_IN_TUNE_CENTS;
    uint32_t accent = in_tune ? COLOR_IN_TUNE : COLOR_OFF;

    char note_text[2] = { STRING_LETTERS[string], '\0' };
    lv_label_set_text(s_note, note_text);
    lv_label_set_text(s_octave, "4");
    set_label_color(s_note, accent);
    set_label_color(s_octave, accent);

    const char *state_text = TUNER_TEXT_IN_TUNE;
    if (cents < -TUNER_IN_TUNE_CENTS) state_text = TUNER_TEXT_FLAT;
    else if (cents > TUNER_IN_TUNE_CENTS) state_text = TUNER_TEXT_SHARP;
    lv_label_set_text(s_state, state_text);
    set_label_color(s_state, accent);

    char cents_text[8];
    format_cents(cents_text, sizeof(cents_text), cents);
    lv_label_set_text(s_cents_value, cents_text);
    set_label_color(s_cents_value, accent);
    set_label_color(s_cents_unit, accent);

    // 偏低点亮 b,偏高点亮 #,准了都变暗。
    set_label_color(s_sign_flat, (cents < -TUNER_IN_TUNE_CENTS) ? accent : COLOR_MUTED);
    set_label_color(s_sign_sharp, (cents > TUNER_IN_TUNE_CENTS) ? accent : COLOR_MUTED);

    lv_obj_set_style_bg_color(s_needle, lv_color_hex(accent), 0);
    lv_obj_set_x(s_needle, needle_x_from_ratio(tuner_meter_ratio(cents), TRACK_W));

    highlight_chip_locked(string);
    refresh_mode_locked();
}

// ---------------------------------------------------------------------------
// 麦克风采集任务
// ---------------------------------------------------------------------------
// 背光节流:有读数/有按键就点亮并重置计时,长时间安静无操作才压暗(持锁调用)。
static void update_backlight_locked(bool active) {
    if (active) {
        s_idle_frames = 0;
        if (s_dimmed) {
            s_dimmed = false;
            bsp_display_backlight(TUNER_BRIGHT_PERCENT);
        }
        return;
    }
    if (s_idle_frames < TUNER_DIM_FRAMES) {
        ++s_idle_frames;
        if (s_idle_frames >= TUNER_DIM_FRAMES) {
            s_dimmed = true;
            bsp_display_backlight(TUNER_DIM_PERCENT);
        }
    }
}

static void capture_task(void *arg) {
    (void)arg;

    esp_err_t err = bsp_audio_set_format(TUNER_SAMPLE_RATE, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "麦克风格式设置失败: %s", esp_err_to_name(err));
        if (bsp_lvgl_lock(200)) {
            s_audio_ready = false;
            show_idle_locked();
            bsp_lvgl_unlock();
        }
        s_capture_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    bsp_audio_set_volume(0);   // 只录音不放音,顺手把功放输出压到 0,减少底噪

    const size_t half_bytes = (size_t)TUNER_HOP * sizeof(int16_t);

    // 先把后半窗填满;之后每次只读半窗,形成 50% 重叠的滑动窗口。
    if (bsp_audio_read(s_window + TUNER_HOP, half_bytes) != ESP_OK) {
        ESP_LOGE(TAG, "麦克风首次读取失败,停止采集");
        if (bsp_lvgl_lock(200)) {
            s_audio_ready = false;
            show_idle_locked();
            bsp_lvgl_unlock();
        }
        s_capture_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int hold = TUNER_HOLD_FRAMES;
    unsigned frame = 0;

    while (s_capture_run) {
        if (bsp_audio_read(s_window + TUNER_HOP, half_bytes) != ESP_OK) {
            ESP_LOGW(TAG, "麦克风读取失败,停止采集");
            break;
        }

        tuner_pitch_result_t result;
        bool got_pitch = tuner_pitch_detect(s_window, TUNER_WINDOW, TUNER_SAMPLE_RATE,
                                            TUNER_SEARCH_MIN_HZ, TUNER_SEARCH_MAX_HZ,
                                            TUNER_A4_HZ, &s_scratch, &result);

        // 检测本身在锁外做(纯计算,不碰共享状态);模式/选弦/展示状态都在锁内读写,
        // 与按键任务严格串行。
        if (!bsp_lvgl_lock(100)) {
            memmove(s_window, s_window + TUNER_HOP, half_bytes);
            ++frame;
            continue;
        }

        // 决定本帧的目标弦与偏差。手动模式下只认选中的那根弦,
        // 避免用户拨错弦时给出一个几百音分的"读数"。
        bool accepted = false;
        tuner_string_t target = s_selected;
        float cents = 0.0f;
        if (got_pitch && result.valid) {
            target = tuner_resolve_target(s_auto_mode, s_selected, result.string);
            accepted = s_auto_mode || (result.string == s_selected);
            if (accepted) {
                cents = tuner_cents_between(result.frequency_hz,
                                            tuner_string_frequency_hz(target, TUNER_A4_HZ));
            }
        }

        // 目标弦切换时重置平滑,免得指针从上一根弦的位置滑过去。
        if (accepted) {
            if (!s_have_reading || target != s_shown_string) {
                s_shown_cents = cents;
            } else {
                s_shown_cents = tuner_smooth(s_shown_cents, cents,
                                             CENTS_ALPHA_UP, CENTS_ALPHA_DOWN);
            }
            s_shown_string = target;
            s_have_reading = true;
            hold = 0;
        } else if (hold < TUNER_HOLD_FRAMES) {
            ++hold;   // 拨弦间隙短暂没有有效帧,先保留上一次读数
        }

        if (s_have_reading && hold < TUNER_HOLD_FRAMES) {
            show_reading_locked(s_shown_string, s_shown_cents);
        } else {
            s_have_reading = false;
            show_idle_locked();
        }

        if (frame % TUNER_BATTERY_FRAMES == 0) {
            int soc = bsp_battery_soc();
            if (soc < 0) {
                lv_label_set_text(s_battery, "--");
            } else {
                lv_label_set_text_fmt(s_battery, "%d%%", soc);
            }
        }
        update_backlight_locked(accepted);
        bsp_lvgl_unlock();

        // 滑窗左移半窗:后一半的样本成为下一帧的前一半。
        memmove(s_window, s_window + TUNER_HOP, half_bytes);
        ++frame;
    }

    // 只有非主动停止(如读取失败)才把界面降级为"无信号";主动停止时界面即将被删除。
    if (s_capture_run) {
        if (bsp_lvgl_lock(200)) {
            s_audio_ready = false;
            s_have_reading = false;
            show_idle_locked();
            bsp_lvgl_unlock();
        }
    }
    s_capture_task = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
bool app_tuner_start(bool audio_ready) {
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败,调音器界面未创建");
        return false;
    }
    s_audio_ready = audio_ready;
    s_auto_mode = true;
    s_selected = TUNER_STRING_G;
    s_have_reading = false;

    build_screen();
    show_idle_locked();
    bsp_lvgl_unlock();

    bsp_display_backlight(TUNER_BRIGHT_PERCENT);

    if (!audio_ready) {
        ESP_LOGW(TAG, "音频不可用,调音器进入无信号界面");
        return true;
    }

    // 采集任务自己负责首次 bsp_audio_set_format;创建失败则降级为无信号界面。
    s_capture_run = true;
    if (xTaskCreate(capture_task, "tuner_capture", TUNER_TASK_STACK, NULL,
                    TUNER_TASK_PRIORITY, &s_capture_task) != pdPASS) {
        s_capture_task = NULL;
        s_capture_run = false;
        ESP_LOGE(TAG, "采集任务创建失败,退化为无信号界面");
        if (bsp_lvgl_lock(200)) {
            s_audio_ready = false;
            show_idle_locked();
            bsp_lvgl_unlock();
        }
    }
    return true;
}

void app_tuner_exit(void) {
    // 先让采集任务收尾。它只在 bsp_audio_read 上阻塞约 32ms,很快就能返回并自行删除。
    s_capture_run = false;
    for (int i = 0; i < 100 && s_capture_task; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_capture_task) {
        // 兜底:任务未在约 1s 内退出,强制删除,避免它访问即将删除的界面对象。
        ESP_LOGW(TAG, "采集任务未及时退出,强制删除");
        vTaskDelete(s_capture_task);
        s_capture_task = NULL;
    }

    if (!bsp_lvgl_lock(500)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败,调音器界面未删除");
        return;
    }
    if (s_scr) {
        // 删除调音器屏。前置条件:调用方必须在【持 LVGL 锁】的换页序列里调用本函数,
        // 并在释放锁之前载入新屏(见 main.c 的两条约束)。这里必须真的删掉 ——
        // 调音器的控件占 LVGL 池,不释放就装不下下一页的界面。
        lv_obj_del(s_scr);
        s_scr = NULL;
    }
    s_have_reading = false;
    bsp_lvgl_unlock();
}

void app_tuner_handle_key(bsp_btn_t btn, bsp_btn_ev_t event) {
    if (event != BSP_BTN_CLICK) return;   // 长按/双击在本页没有定义,忽略

    // 状态与界面都在锁内更新:采集任务也在锁内读写这些变量。
    if (!bsp_lvgl_lock(200)) return;

    bool changed = true;
    switch (btn) {
    case BSP_BTN_UP:
        s_selected = tuner_string_step(s_selected, -1);
        s_auto_mode = false;   // 手动选弦即表示要锁定这根弦
        break;
    case BSP_BTN_DOWN:
        s_selected = tuner_string_step(s_selected, 1);
        s_auto_mode = false;
        break;
    case BSP_BTN_OK:
        s_auto_mode = !s_auto_mode;
        break;
    default:
        changed = false;
        break;
    }

    if (changed) {
        refresh_mode_locked();
        if (!s_have_reading) show_idle_locked();
    }
    update_backlight_locked(true);   // 按键即重新点亮
    bsp_lvgl_unlock();
}
