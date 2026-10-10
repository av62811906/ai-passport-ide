// main/render.c —— 中文点阵与 2bpp sprite 的绘制（F2 + F3）。
//
// ## 半宽 ASCII：规格冲突的解法
//
// `sim/strings.py` 的 text_px() 按「汉字 16px、ASCII 半宽 8px」算，
// 八份页面文档的「184px / 232px」全基于它。但 font16.bin 是**定长
// per=32（16×16），头部没有任何 advance 字段**——按 16px 步进的话
// 九处三键提示行全部溢出 40~64px。
//
// 三条路（docs/00-handoff.md P1-③）里选了第一条：
// **渲染器按字符类型决定步进**。依据是实测的墨迹范围：
//
//     A  x= 2~11    [  x=6~9     0  x=4~11    %  x=1~13
//
// ASCII 字形全部落在 x=1~13，**按 8px 步进只会切掉右侧空白**，
// 不动任何墨迹。而 `%` 的 13 列是唯一接近边界的，它只出现在
// 电量显示里且后面通常是空格。
//
// 这条路零字库改动、零格式变更，代价是渲染器要知道「什么是 ASCII」——
// 一个 `cp < 0x80` 的判断，比给 650 个字形各加 1 字节 advance 划算。
//
// ## 为什么不用 LVGL 的字体系统
//
// LVGL 能加载自定义字体，但那要把 font16.bin 转成它的格式，
// 多一层转换就多一处可能漂移的地方。而我们只需要「把 1bpp 点阵
// 画到画布上」——直接操作 lv_canvas 的像素缓冲更直接，
// 也与固件将来脱离 LVGL 走裸横带渲染的方向一致。

#include <string.h>

#include "esp_log.h"

#include "render.h"
#include "screen.h"

static const char *TAG = "render";

// 字库。格式见 tools/pipeline/convert_font.py：
//   头部 14 B: magic "FNT1" | ver u16 | size u16 | per u16 | count u32
//   码点索引: count × u16，**升序**（可二分）
//   字形数据: count × per，每行 2 字节高位在左
extern const uint8_t font_bin_start[] asm("_binary_font16_bin_start");
extern const uint8_t font_bin_end[] asm("_binary_font16_bin_end");

static struct {
    const uint16_t *codes;
    const uint8_t *glyphs;
    uint16_t size;
    uint16_t per;
    uint32_t count;
    bool ok;
} s_font;

bool render_init(void)
{
    const uint8_t *d = font_bin_start;
    size_t len = (size_t)(font_bin_end - font_bin_start);
    if (len < 14 || memcmp(d, "FNT1", 4) != 0) {
        ESP_LOGE(TAG, "font16.bin magic 不对");
        return false;
    }
    uint16_t size = (uint16_t)(d[6] | (d[7] << 8));
    uint16_t per = (uint16_t)(d[8] | (d[9] << 8));
    uint32_t cnt = (uint32_t)d[10] | ((uint32_t)d[11] << 8) |
                   ((uint32_t)d[12] << 16) | ((uint32_t)d[13] << 24);
    if (14u + cnt * 2 + cnt * per != len) {
        ESP_LOGE(TAG, "font16.bin 长度不自洽: 14+%u*2+%u*%u != %u",
                 (unsigned)cnt, (unsigned)cnt, per, (unsigned)len);
        return false;
    }
    // 码点索引是 u16 数组，但文件里未必 2 字节对齐 —— 头部 14 字节
    // 是偶数所以这里恰好对齐。若将来改头部长度，这个强转要跟着改成
    // 逐字节读（RISC-V 未对齐访问是异常，不是静默慢速）。
    s_font.codes = (const uint16_t *)(const void *)(d + 14);
    s_font.glyphs = d + 14 + cnt * 2;
    s_font.size = size;
    s_font.per = per;
    s_font.count = cnt;
    s_font.ok = true;
    return true;
}

// 二分查码点 → 字形下标。码点索引是升序的（convert_font.py 保证）。
static int glyph_index(uint16_t cp)
{
    if (!s_font.ok) return -1;
    int lo = 0, hi = (int)s_font.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t c = s_font.codes[mid];
        if (c == cp) return mid;
        if (c < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

uint16_t render_font_size(void)
{
    return s_font.size;
}

uint8_t render_char_advance(uint16_t cp)
{
    return render_char_advance_sized(cp, s_font.size);
}

int render_text_width(const char *s)
{
    return render_text_width_sized(s, s_font.size);
}

bool render_text_ink_bounds(const char *s, render_bounds_t *out)
{
    if (!out) return false;
    *out = (render_bounds_t){0};
    if (!s_font.ok || !s) return false;
    int left = 0, top = 0, right = 0, bottom = 0, pen = 0;
    bool found = false;
    const int row_bytes = (s_font.size + 7) / 8;
    while (*s) {
        uint16_t cp;
        s += render_utf8_next(s, &cp);
        int gi = glyph_index(cp);
        if (gi >= 0) {
            const uint8_t *g = s_font.glyphs + (size_t)gi * s_font.per;
            int dx = pen - ((cp < 0x80) ? s_font.size / 4 : 0);
            for (int y = 0; y < s_font.size; y++) {
                for (int x = 0; x < s_font.size; x++) {
                    if (!(g[y * row_bytes + x / 8] & (0x80 >> (x % 8)))) continue;
                    int px = dx + x;
                    if (!found || px < left) left = px;
                    if (!found || px + 1 > right) right = px + 1;
                    if (!found || y < top) top = y;
                    if (!found || y + 1 > bottom) bottom = y + 1;
                    found = true;
                }
            }
        }
        pen += render_char_advance(cp);
    }
    if (found) *out = (render_bounds_t){left, top, right - left, bottom - top};
    return found;
}

bool render_sprite_ink_bounds(const uint8_t *data, int w, int h,
                              render_bounds_t *out)
{
    if (!out) return false;
    *out = (render_bounds_t){0};
    if (!data || w <= 0 || h <= 0) return false;
    int left = w, top = h, right = 0, bottom = 0;
    const int row_bytes = (w + 3) / 4;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (((data[y * row_bytes + x / 4] >> (6 - 2 * (x % 4))) & 3) == 3) continue;
            if (x < left) left = x;
            if (y < top) top = y;
            if (x + 1 > right) right = x + 1;
            if (y + 1 > bottom) bottom = y + 1;
        }
    }
    if (right == 0) return false;
    *out = (render_bounds_t){left, top, right - left, bottom - top};
    return true;
}

// 画一个字形到 canvas。1bpp → 前景色，0 位不画（透明）。
static void draw_glyph(int gi, int x, int y, uint16_t fg)
{
    const uint8_t *g = s_font.glyphs + (size_t)gi * s_font.per;
    uint8_t row_bytes = (uint8_t)((s_font.size + 7) / 8);

    for (uint16_t r = 0; r < s_font.size; r++) {
        for (uint16_t c = 0; c < s_font.size; c++) {
            if (g[r * row_bytes + c / 8] >> (7 - c % 8) & 1) {
                screen_px(x + c, y + r, fg);
            }
        }
    }
}

int render_text(int x, int y, const char *s, uint16_t fg)
{
    if (!s_font.ok || !s) return x;
    while (*s) {
        uint16_t cp;
        s += render_utf8_next(s, &cp);
        int gi = glyph_index(cp);
        if (gi >= 0) {
            // ASCII 半宽时把字形**左移**再画 —— 字形墨迹居中在
            // 16 格里（A 在 x=2~11），直接画会让相邻字符间距过大。
            // 左移 (16-8)/2 = 4 让墨迹落在 8px 步进的中间。
            int dx = (cp < 0x80) ? -(int)(s_font.size / 4) : 0;
            draw_glyph(gi, x + dx, y, fg);
        }
        x += render_char_advance(cp);
    }
    return x;
}

// ---------------------------------------------------------------------------
// 2bpp sprite
//
// 格式见 tools/pipeline/convert_sprites.py：每字节 4 像素，高位在左，
// 色号 0~3 查调色板。**3 号是透明**（与 sim/effects.py 的约定一致：
// 「最亮当透明，不画背景」）。
// ---------------------------------------------------------------------------

void render_sprite_2bpp_wh(int x, int y,
                           const uint8_t *data, int w, int h, int scale,
                           const uint16_t *palette)
{
    if (!data || w <= 0 || h <= 0 || scale <= 0) return;
    int row_bytes = (w * 2 + 7) / 8;

    for (int sy = 0; sy < h; sy++) {
        for (int sx = 0; sx < w; sx++) {
            uint8_t b = data[sy * row_bytes + (sx * 2) / 8];
            uint8_t shade = (b >> (6 - (sx * 2) % 8)) & 3;
            if (shade == 3) continue;         // 3 = 透明

            // 最近邻放大。GB 风格下这是对的 —— 双线性会把硬边缘糊掉，
            // 而硬边缘正是像素风的一部分（同 sim/effects.py 的 render）。
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    screen_px(x + sx * scale + dx, y + sy * scale + dy,
                              palette[shade]);
                }
            }
        }
    }
}

// 正方形是长方形的特例 —— 只留一份实现，避免两处改一处忘。
void render_sprite_2bpp(int x, int y,
                        const uint8_t *data, int size, int scale,
                        const uint16_t *palette)
{
    render_sprite_2bpp_wh(x, y, data, size, size, scale, palette);
}

void render_sprite_2bpp_thumbnail(int x, int y, const uint8_t *data,
                                  int size, int dest_size,
                                  const uint16_t *palette)
{
    if (!data || !palette || size <= 0 || dest_size <= 0 || dest_size > size) return;
    int row_bytes = (size + 3) / 4;
    for (int dy = 0; dy < dest_size; dy++) {
        int sy = ((2 * dy + 1) * size) / (2 * dest_size);
        for (int dx = 0; dx < dest_size; dx++) {
            int sx = ((2 * dx + 1) * size) / (2 * dest_size);
            uint8_t shade = (data[sy * row_bytes + sx / 4] >> (6 - 2 * (sx % 4))) & 3;
            if (shade != 3) screen_px(x + dx, y + dy, palette[shade]);
        }
    }
}

// ---------------------------------------------------------------------------

bool render_selftest(void)
{
    if (!s_font.ok) {
        ESP_LOGE(TAG, "字库未就绪");
        return false;
    }
    bool ok = true;

    // 码点索引必须升序 —— 二分查找依赖它
    for (uint32_t i = 1; i < s_font.count; i++) {
        if (s_font.codes[i] <= s_font.codes[i - 1]) {
            ESP_LOGE(TAG, "码点索引非升序 @%u", (unsigned)i);
            ok = false;
            break;
        }
    }

    // 抽查几个字必须查得到。挑的是各系统的关键字 ——
    // 缺任何一个都说明字库与代码不同步。
    static const struct { const char *s; const char *why; } PROBE[] = {
        {"皮", "物种名"}, {"鉴", "P6 标题（曾因 HK 字体全空）"},
        {"电", "招式名"}, {"徽", "S17 徽章"}, {"槽", "S18 存档"},
        {"A", "三键提示（曾整个缺失）"}, {"[", "同上"},
    };
    for (unsigned i = 0; i < sizeof(PROBE) / sizeof(PROBE[0]); i++) {
        uint16_t cp;
        render_utf8_next(PROBE[i].s, &cp);
        if (glyph_index(cp) < 0) {
            ESP_LOGE(TAG, "字库缺「%s」(%s)", PROBE[i].s, PROBE[i].why);
            ok = false;
        }
    }

    // 排版宽度 —— 与 sim/strings.py 的 text_px() 必须一致。
    // 「[A]照料」= 3 个 ASCII × 8 + 2 个汉字 × 16 = 56
    int w = render_text_width("[A]照料");
    if (w != 56) {
        ESP_LOGE(TAG, "排版宽度不符: 「[A]照料」得 %d 期望 56", w);
        ok = false;
    }
    // P1 完整提示行 —— 页面文档写的是 184px
    w = render_text_width("[A]照料 [B]图鉴 [C]遭遇");
    if (w != 184) {
        ESP_LOGE(TAG, "P1 提示行 %d px，页面文档写 184", w);
        ok = false;
    }

    ESP_LOGI(TAG, "自检 %s（%u 字形 · 升序 · 探针 7 · 排版 2）",
             ok ? "全部通过" : "**失败**", (unsigned)s_font.count);
    return ok;
}

// ---------------------------------------------------------------------------
// 动效
// ---------------------------------------------------------------------------

int render_shake_dx(int i, int frames, int amplitude)
{
    if (i < 0 || i >= frames) return 0;
    // 与 sim/effects.py 的 shake_sequence 同：偶数帧 +a，奇数帧 -a
    return (i % 2 == 0) ? amplitude : -amplitude;
}

bool render_flash_on(int i, int frames)
{
    if (i < 0 || i >= frames) return false;
    return (i % 2) == 0;
}
