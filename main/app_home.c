// main/app_home.c —— 首页(应用入口页)界面实现(见 app_home.h)。
//
// 240x320 竖屏,暗色极简风(设计稿 V2 方案):
//   * 近黑背景,不用网格/描边/底部光晕,靠留白与单一强调色建立秩序;
//   * 全局只有一个强调色:荧光青柠。顶栏标签、电量、选中卡片描边与圆点都用它;
//   * 顶栏:左"AI 护照"(青柠),右电池图标 + 电量;下面是灰阶用户名小字;
//   * 屏幕中部是唯一一张大聚焦卡片:图标 + 主标题 + 副标题纵向居中,
//     深灰底 + 1 px 青柠描边 + 柔和青柠外发光;
//   * 卡片下方两个圆点指示器(当前项实心青柠,其余空心灰);
//   * 底部居中按键提示。
// 首页属于派生应用自定义界面,不复用基线 demo 的菜单/测试页/像素风外壳。
//
// 线程模型(遵守仓库运行期约束):
//   * 按键回调只入队,真正的状态更新在按键分发任务里完成(app_home_handle_key);
//   * 这里所有 lv_* 调用都在 bsp_lvgl_lock() 保护下进行;
//   * lv_timer 回调由 LVGL 任务调用(该任务已持有 port 锁),故不再重复加锁。
#include "app_home.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "lvgl.h"

#include "app_texts.h"

static const char *TAG = "home";

// ---------------------------------------------------------------------------
// 布局(240x320 竖屏,数值按 V2 设计稿量取)
// ---------------------------------------------------------------------------
#define SCREEN_W        240
#define SCREEN_H        320

#define BRAND_X         12
#define BRAND_Y         10
#define TOPBAR_MID_Y    12      /* 顶栏(电量行)垂直位置 */
#define TOPBAR_RIGHT    228     /* 顶栏右边缘 */

#define NAME_X          12
#define NAME_Y          38      /* 用户名小字,灰阶弱化 */

// 唯一的大聚焦卡片:水平居中(x=36,宽 168),纵向占据屏幕中部。
#define CARD_X          36
#define CARD_Y          68
#define CARD_W          168
#define CARD_H          189
#define CARD_RADIUS     12
#define CARD_ICON_DY    (-38)   /* 图标相对卡片中心的上移量 */
#define CARD_TITLE_Y    126     /* 主标题顶端(相对卡片顶部) */
#define CARD_SUB_Y      151     /* 副标题顶端(相对卡片顶部) */
#define CARD_SLIDE_MS   240     /* 切换菜单时卡片横向滑动的时长 */
// 滑动行程取整屏宽:这样滑出/滑入的卡片能完全离开屏幕,中途不会半张停在边上。
#define CARD_SLIDE_DIST SCREEN_W

// 圆点指示器:两个点以屏幕中线对称分布,当前项实心、其余空心。
#define DOT_SIZE        6
#define DOT_RADIUS      (DOT_SIZE / 2)
#define DOT_GAP         14      /* 相邻圆点圆心间距 */
#define DOT_ROW_Y       270

#define FOOTER_Y        300

// 背景美化:极浅的纵向渐变给出纵深,底部再加一层很淡的青柠环境光托住构图。
// 两者都是渐变填充(不分配阴影缓冲),是这块 24KB 显存池上最省的做法。
#define AMBIENT_GLOW_Y    248
#define AMBIENT_GLOW_OPA  LV_OPA_10

#define BATT_BODY_W     15
#define BATT_BODY_H     11
#define BATT_NUB_W      2
#define BATT_NUB_H      4
#define BATT_FILL_PAD   2
#define TOPBAR_ROW_H    18
#define TOPBAR_GAP      5

// ---------------------------------------------------------------------------
// 配色(取自 V2 设计稿采样):近黑底 + 单一荧光青柠强调色 + 灰阶层级。
// ---------------------------------------------------------------------------
#define COLOR_BG_TOP    0x15151B
#define COLOR_BG_BOTTOM 0x08080B
#define COLOR_ACCENT    0xC6F03C
#define COLOR_CARD      0x2F2F33
#define COLOR_INK       0xFFFFFF
#define COLOR_SUB       0x9EA3A9
#define COLOR_MUTED     0x6E6E6E
#define COLOR_DOT_OFF   0x8A8A8A

// 卡片霓虹光晕:宽度按设计稿的扩散范围,透明度取到既亮又不糊的程度。
#define CARD_GLOW_W     34
#define CARD_GLOW_OPA   LV_OPA_30

// ---------------------------------------------------------------------------
// 功能表。首页把选中项渲染进居中的卡片;顺序即下标,按键路由据此进入对应页面。
// 加功能:在这里追加一项,并在 main.c 的按键路由里把对应下标映射到目标页面。
// ---------------------------------------------------------------------------
typedef struct {
    const char *title;
    const char *subtitle;
    const char *icon;      /* LV_SYMBOL_*(由 Montserrat 字体提供) */
} home_item_t;

static const home_item_t HOME_ITEMS[] = {
    { APP_TEXT_HOME_TUNER_TITLE,   APP_TEXT_HOME_TUNER_SUB,   LV_SYMBOL_AUDIO },
    { APP_TEXT_HOME_POKEMON_TITLE, APP_TEXT_HOME_POKEMON_SUB, LV_SYMBOL_PLAY  },
};
#define HOME_ITEM_COUNT ((int)(sizeof(HOME_ITEMS) / sizeof(HOME_ITEMS[0])))

// 对外约定的功能下标(与 main.c 的路由一致)。
#define HOME_ITEM_TUNER    0
#define HOME_ITEM_POKEMON  1

// 背光节流:长时间无操作压暗,避免亮屏干耗电池。tuner 也有同样的策略。
#define HOME_BRIGHT_PERCENT 100
#define HOME_DIM_PERCENT    8
#define HOME_DIM_TICKS      45
#define HOME_BATT_TICKS     5    /* 每 5 个 tick(约 5s)刷新一次电量 */
#define HOME_TIMER_MS       1000

// ---------------------------------------------------------------------------
// 界面对象与状态。仅在持 bsp_lvgl_lock() 时读写。
// ---------------------------------------------------------------------------
static lv_obj_t *s_scr;
static lv_obj_t *s_batt_row;
static lv_obj_t *s_batt_fill;
static lv_obj_t *s_batt_text;
static lv_obj_t *s_dots[HOME_ITEM_COUNT];
static lv_timer_t *s_timer;

static int s_selected;        /* 选中项下标 */
static int s_idle_ticks;
static bool s_dimmed;

// 应用自有字体:只有 ASCII + 本项目用到的汉字,缺的字形(如 LV_SYMBOL_*)回落到 Montserrat。
LV_FONT_DECLARE(tuner_font_20);
LV_FONT_DECLARE(app_font_12);
static lv_font_t s_font_20;
static lv_font_t s_font_12;

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
    style_plain(label);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

// 背光:有操作就点亮并重置计时(持锁调用)。
static void backlight_activity_locked(void) {
    s_idle_ticks = 0;
    if (s_dimmed) {
        s_dimmed = false;
        bsp_display_backlight(HOME_BRIGHT_PERCENT);
    }
}

static void refresh_battery_locked(void) {
    int soc = bsp_battery_soc();
    int inner = BATT_BODY_W - 2 * BATT_FILL_PAD;
    if (soc < 0) {
        lv_label_set_text(s_batt_text, "--");
        lv_obj_set_width(s_batt_fill, 0);
    } else {
        if (soc > 100) soc = 100;
        lv_label_set_text_fmt(s_batt_text, "%d%%", soc);
        lv_obj_set_width(s_batt_fill, (inner * soc) / 100);
    }
}

// 圆点指示器:当前项实心青柠,其余空心灰(持锁调用)。
static void update_dots_locked(void) {
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        bool on = (i == s_selected);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(on ? COLOR_ACCENT : COLOR_DOT_OFF), 0);
        lv_obj_set_style_bg_opa(s_dots[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_dots[i], on ? 0 : 1, 0);
        lv_obj_set_style_border_color(s_dots[i], lv_color_hex(COLOR_DOT_OFF), 0);
    }
}

// LVGL 定时器:刷新电量 + 空闲压暗。运行在 LVGL 任务里,已持锁,不再加锁。
static void home_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_scr) return;

    if (++s_idle_ticks >= HOME_DIM_TICKS && !s_dimmed) {
        s_dimmed = true;
        bsp_display_backlight(HOME_DIM_PERCENT);
    } else if (s_idle_ticks % HOME_BATT_TICKS == 0) {
        refresh_battery_locked();
    }
}

// ---------------------------------------------------------------------------
// 界面构建(持锁调用)
// ---------------------------------------------------------------------------

// 背景:极浅的纵向渐变(上略亮、下略沉)给出纵深,再叠一层贴底的青柠环境光。
// 不做纹理/描边:V2 的美化靠"层次"而不是"元素",渐变本身不分配额外显存。
static void build_background(void) {
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG_TOP), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(COLOR_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(s_scr, 0, 0);
    lv_obj_set_style_bg_grad_stop(s_scr, 255, 0);

    // 底部环境光:整幅全宽、由透明渐变到青柠,避免出现硬边。
    lv_obj_t *glow = lv_obj_create(s_scr);
    style_plain(glow);
    lv_obj_set_pos(glow, 0, AMBIENT_GLOW_Y);
    lv_obj_set_size(glow, SCREEN_W, SCREEN_H - AMBIENT_GLOW_Y);
    lv_obj_set_style_bg_color(glow, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(glow, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_grad_dir(glow, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(glow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(glow, AMBIENT_GLOW_OPA, 0);
    lv_obj_set_style_bg_main_stop(glow, 0, 0);
    lv_obj_set_style_bg_grad_stop(glow, 255, 0);
}

// 顶栏:左产品标签(青柠),右电量图标 + 数字,均为强调色。
static void build_topbar(void) {
    lv_obj_t *brand = make_label(s_scr, &s_font_12, COLOR_ACCENT, APP_TEXT_BRAND);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, BRAND_X, BRAND_Y);

    // 电量行用 flex 容器,电池图标在数字左侧,整体右对齐。
    s_batt_row = lv_obj_create(s_scr);
    style_plain(s_batt_row);
    lv_obj_set_size(s_batt_row, LV_SIZE_CONTENT, TOPBAR_ROW_H);
    lv_obj_set_flex_flow(s_batt_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_batt_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_batt_row, TOPBAR_GAP, 0);
    lv_obj_align(s_batt_row, LV_ALIGN_TOP_RIGHT, -(SCREEN_W - TOPBAR_RIGHT), TOPBAR_MID_Y);

    lv_obj_t *batt = lv_obj_create(s_batt_row);
    style_plain(batt);
    lv_obj_set_size(batt, BATT_BODY_W + BATT_NUB_W, BATT_BODY_H);
    lv_obj_set_style_border_width(batt, 1, 0);
    lv_obj_set_style_border_color(batt, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_opa(batt, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(batt, 3, 0);

    s_batt_fill = lv_obj_create(batt);
    style_plain(s_batt_fill);
    lv_obj_set_pos(s_batt_fill, BATT_FILL_PAD, BATT_FILL_PAD);
    lv_obj_set_size(s_batt_fill, 0, BATT_BODY_H - 2 * BATT_FILL_PAD);
    lv_obj_set_style_radius(s_batt_fill, 1, 0);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_batt_fill, LV_OPA_COVER, 0);

    lv_obj_t *nub = lv_obj_create(batt);
    style_plain(nub);
    lv_obj_set_pos(nub, BATT_BODY_W, (BATT_BODY_H - BATT_NUB_H) / 2);
    lv_obj_set_size(nub, BATT_NUB_W, BATT_NUB_H);
    lv_obj_set_style_radius(nub, 1, 0);
    lv_obj_set_style_bg_color(nub, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);

    s_batt_text = make_label(s_batt_row, &lv_font_montserrat_14, COLOR_ACCENT, "--");
}

// 圆点指示器:以屏幕中线为对称轴,平铺 HOME_ITEM_COUNT 个圆点。
static void build_dots(void) {
    int base_cx = SCREEN_W / 2 - (HOME_ITEM_COUNT - 1) * DOT_GAP / 2;
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(s_scr);
        style_plain(dot);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(dot, DOT_RADIUS, 0);
        lv_obj_set_pos(dot, base_cx + i * DOT_GAP - DOT_RADIUS, DOT_ROW_Y);
        s_dots[i] = dot;
    }
}

// 卡片做成两份(整卡双缓冲):切换菜单时整张卡片一起横向滑动,
// 一份滑出、另一份滑入,而不是只让卡片里的文字换掉。
typedef struct {
    lv_obj_t *card;         /* 带描边与外发光的卡片外框 */
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *sub;
} card_view_t;

static card_view_t s_views[2];
static int s_view_cur;          /* 当前停在屏幕上那一张 */

static void anim_x_cb(void *obj, int32_t value) {
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void set_view_item(card_view_t *v, int index) {
    lv_label_set_text(v->icon, HOME_ITEMS[index].icon);
    lv_label_set_text(v->title, HOME_ITEMS[index].title);
    lv_label_set_text(v->sub, HOME_ITEMS[index].subtitle);
}

// 一张完整卡片:深灰底 + 1 px 青柠描边 + 柔和外发光,内部图标/主标题/副标题纵向居中。
static void build_card_view(card_view_t *v) {
    v->card = lv_obj_create(s_scr);
    style_plain(v->card);
    lv_obj_set_pos(v->card, CARD_X, CARD_Y);
    lv_obj_set_size(v->card, CARD_W, CARD_H);
    lv_obj_set_style_radius(v->card, CARD_RADIUS, 0);
    lv_obj_set_style_bg_color(v->card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(v->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(v->card, 1, 0);
    lv_obj_set_style_border_color(v->card, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_opa(v->card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(v->card, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_shadow_width(v->card, CARD_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(v->card, CARD_GLOW_OPA, 0);

    // 图标:Montserrat 符号字形,亮青柠色画在深色卡片上就是霓虹灯管的效果。
    // 注意:LVGL 的文字描边(outline stroke)只在 FreeType 字体下才有效,
    // 内置位图字体不支持,所以这里不能用"描边勾轮廓"的做法。
    v->icon = make_label(v->card, &lv_font_montserrat_48, COLOR_ACCENT, HOME_ITEMS[0].icon);
    v->title = make_label(v->card, &s_font_20, COLOR_INK, HOME_ITEMS[0].title);
    v->sub = make_label(v->card, &s_font_12, COLOR_SUB, HOME_ITEMS[0].subtitle);
    set_view_item(v, 0);
    lv_obj_align(v->icon, LV_ALIGN_CENTER, 0, CARD_ICON_DY);
    lv_obj_align(v->title, LV_ALIGN_TOP_MID, 0, CARD_TITLE_Y);
    lv_obj_align(v->sub, LV_ALIGN_TOP_MID, 0, CARD_SUB_Y);
}

static void build_card(void) {
    s_view_cur = 0;
    build_card_view(&s_views[0]);
    build_card_view(&s_views[1]);
    lv_obj_set_x(s_views[1].card, CARD_X + CARD_SLIDE_DIST);    /* 备用卡片先停在屏幕外 */
}

// 切换选中项。dir = +1 表示下一项(卡片自右滑入),-1 表示上一项(自左滑入)。
// 持锁调用;动画本身由 LVGL 任务推进。
static void slide_to_item_locked(int dir) {
    card_view_t *in = &s_views[1 - s_view_cur];
    card_view_t *out = &s_views[s_view_cur];
    if (!in->card) return;

    set_view_item(in, s_selected);
    // 上一段动画还没结束就顺着当前位置接着走,避免位置跳变。
    if (lv_anim_get(in->card, anim_x_cb) == NULL) {
        lv_obj_set_x(in->card, CARD_X + dir * CARD_SLIDE_DIST);
    }

    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, in->card);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(in->card), CARD_X);
    lv_anim_set_duration(&a, CARD_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, out->card);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(out->card), CARD_X - dir * CARD_SLIDE_DIST);
    lv_anim_set_duration(&a, CARD_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    s_view_cur = 1 - s_view_cur;
    update_dots_locked();
}

static void build_screen(void) {
    s_font_20 = tuner_font_20;
    s_font_20.fallback = &lv_font_montserrat_20;
    s_font_12 = app_font_12;
    s_font_12.fallback = &lv_font_montserrat_14;

    s_scr = lv_obj_create(NULL);
    style_plain(s_scr);

    build_background();

    // 用户名小字(ASCII,直接用 Montserrat);V2 里它是弱化的灰阶信息。
    lv_obj_t *name = make_label(s_scr, &lv_font_montserrat_14, COLOR_MUTED, APP_TEXT_USER_NAME);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, NAME_X, NAME_Y);

    build_card();
    build_dots();

    lv_obj_t *foot = make_label(s_scr, &s_font_12, COLOR_MUTED, APP_TEXT_HOME_HINT);
    lv_obj_align(foot, LV_ALIGN_TOP_MID, 0, FOOTER_Y);

    build_topbar();

    update_dots_locked();
    lv_screen_load(s_scr);
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
void app_home_start(void) {
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败,首页界面未创建");
        return;
    }
    s_selected = 0;
    s_idle_ticks = 0;
    s_dimmed = false;

    build_screen();
    refresh_battery_locked();

    if (!s_timer) {
        s_timer = lv_timer_create(home_timer_cb, HOME_TIMER_MS, NULL);
    }
    bsp_lvgl_unlock();

    bsp_display_backlight(HOME_BRIGHT_PERCENT);
}

void app_home_exit(void) {
    if (!bsp_lvgl_lock(500)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败,首页界面未删除");
        return;
    }
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        // 删除首页屏。前置条件:调用方必须在【持 LVGL 锁】的换页序列里调用本函数,
        // 并在释放锁之前载入新屏(main.c 的 page_show_* 是唯一入口)。
        // 少了这把锁,刷新任务就会看到 act_scr == NULL,在 lv_obj_update_layout(NULL)
        // 上触发 LV_ASSERT_NULL → while(1) 永久停摆;持锁时刷新插不进来,看不见中间态。
        // 而这里必须真的删掉:LVGL 池只有 24KB 且不可扩,首页与下一页的控件同时在世
        // 会耗光池子,同样停在断言里(这次停在持锁的调用方)。
        lv_obj_del(s_scr);
        s_scr = NULL;
        s_batt_row = NULL;
        s_batt_fill = NULL;
        s_batt_text = NULL;
        for (int i = 0; i < HOME_ITEM_COUNT; i++) {
            s_dots[i] = NULL;
        }
        for (int i = 0; i < 2; i++) {
            s_views[i].card = NULL;
            s_views[i].icon = NULL;
            s_views[i].title = NULL;
            s_views[i].sub = NULL;
        }
        s_view_cur = 0;
    }
    bsp_lvgl_unlock();
}

int app_home_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return -1;   // 首页只用单击

    int enter = -1;
    int dir = 0;                  /* 0 = 不换项;±1 = 换项并给出滑动方向 */
    switch (btn) {
    case BSP_BTN_OK:
        if (s_selected >= 0 && s_selected < HOME_ITEM_COUNT) enter = s_selected;
        break;
    case BSP_BTN_UP:
        if (s_selected > 0) { --s_selected; dir = -1; }
        break;
    case BSP_BTN_DOWN:
        if (s_selected + 1 < HOME_ITEM_COUNT) { ++s_selected; dir = +1; }
        break;
    default:
        break;
    }

    if (!bsp_lvgl_lock(200)) return enter;
    backlight_activity_locked();
    if (dir != 0) slide_to_item_locked(dir);
    bsp_lvgl_unlock();
    return enter;
}
