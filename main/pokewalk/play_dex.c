// main/play_dex.c —— P6 图鉴。
//
// 对应 docs/pages/P6-dex.md。
//
// ## 未捕获显示为剪影
//
// `shade_map = (0,0,0,3)` —— 三档前景色全指向最深色，背景保持透明。
// **零额外素材**：同一张 sprite，换个调色板就是剪影。
//
// 已知局限（页面文档记了）：主色偏浅的物种（猫老大、大针蜂）剪影里
// 会漏出白色区域，因为色号 3 既是背景也是「最浅色」，
// sprite 内部的浅色区域会跟着变透明。这是 2bpp 的固有性质不是 bug；
// 彻底解决要加一位掩码（+19 字节/151 只），留待真机看观感再定。
//
// ## 「见过」与「未捕获」要分开
//
// 遇到但没抓到的标「见过」。这一条承载 S8 那份「闪光遇到了但跑了」的
// 遗憾 —— 四个位图而非两个就是为了存下它。

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "assets.h"
#include "encounter.h"
#include "game_ui.h"
#include "nav.h"
#include "play.h"
#include "render.h"
#include "screen.h"
#include "world.h"

static const char *TAG = "p6";

#define BAND_H SCREEN_BAND_H
#define SCR_W SCREEN_W
#define SCR_H SCREEN_H

// 网格：5 列 × 4 行 = 每页 20 只，151 只共 8 页。
// 每格 46×56，正面图最近邻缩成 32px，编号与图片居中。
#define COLS 5
#define ROWS 4
#define PER_PAGE (COLS * ROWS)
#define CELL_W 46
#define CELL_H 56
#define GRID_X 5
#define GRID_Y 40
#define THUMB_SIZE 32
#define PAGE_Y 260

SCREEN_ASSERT_WITHIN_BAND(dex_page_number, PAGE_Y, 16);

static uint8_t s_page;
static uint16_t s_species;
static bool s_detail;
static const char *s_feedback;

static void draw_band(int band_y)
{
    screen_band_clear(GAME_UI_BG);
    #define Y(v) ((v) - band_y)

    const dex_t *d = world_dex();
    char buf[48];

    // 标题 + 计数
    snprintf(buf, sizeof(buf), "%u/%u", dex_count_caught(d), DEX_SPECIES);
    game_ui_title(band_y, "图鉴", buf);

    if(s_detail){
        exploration_view_t view;world_exploration_snapshot(&view);
        species_t sp;unsigned rarity=0;int route=exploration_habitat(s_species,&rarity);
        if(assets_species(s_species,&sp)){
            snprintf(buf,sizeof(buf),"%03u %.*s",s_species,sp.name_zh_len,sp.name_zh);
            game_ui_text_centered(band_y,8,40,224,16,buf,GAME_UI_INK);
            uint8_t size;const uint8_t *art=assets_front_sprite(s_species,&size);uint16_t pal[4];assets_palette_variant(sp.palette,false,pal);
            if(art)game_ui_sprite_centered(band_y,8,64,224,112,art,size,size,2,pal);
        }
        snprintf(buf,sizeof(buf),"%s  %u星",route>=0?exploration_route(route)->name:"未知",rarity);
        game_ui_text_centered(band_y,8,184,224,16,buf,GAME_UI_INK);
        unsigned chapter=exploration_unlock_chapter(s_species);bool open=exploration_species_open(s_species,view.defeated);
        game_ui_text_centered(band_y,8,208,224,16,open?"已开放：可追踪获取":exploration_chapter(chapter)->condition,GAME_UI_MUTED);
        game_ui_text_centered(band_y,8,232,224,16,s_feedback?s_feedback:dex_is_caught(d,s_species)?"已捕获 仍可追踪闪光":"未捕获 追踪可确保遇到",GAME_UI_MUTED);
        game_ui_text_centered(band_y,8,256,224,16,"上下切换 长按B返回",GAME_UI_MUTED);
        game_ui_footer(band_y,view.state.tracked_species==s_species?"上下切换 确认取消追踪":"上下切换 确认追踪");
        screen_push_band(band_y);return;
    }
    // Static grid: every page/selection update calls draw_all(). A thumbnail
    // or number may cross an 80px band; both parts are redrawn together.
    for (int i = 0; i < PER_PAGE; i++) {
        uint16_t sid = (uint16_t)(s_page * PER_PAGE + i + 1);
        if (sid > DEX_SPECIES) break;
        int cx = GRID_X + (i % COLS) * CELL_W;
        int cy = GRID_Y + (i / COLS) * CELL_H;

        bool caught = dex_is_caught(d, sid);
        bool seen = dex_is_seen(d, sid);

        uint8_t sprite_size = 0;
        const uint8_t *spr = assets_front_sprite(sid, &sprite_size);
        species_t sp;
        if (spr && assets_species(sid, &sp)) {
            uint16_t pal[4];
            if (caught) {
                assets_palette_variant(sp.palette, dex_is_shiny_caught(d, sid), pal);
            } else if (seen) {
                // 剪影：三档前景全指最深色，背景仍透明（色号 3）
                pal[0] = pal[1] = pal[2] = GAME_UI_MUTED;
                pal[3] = 0;
            } else {
                // 没见过 —— 连剪影都不画，只留编号
                spr = NULL;
            }
            if (spr) game_ui_thumbnail_centered(band_y, cx, cy, CELL_W, THUMB_SIZE,
                                                 spr, sprite_size, THUMB_SIZE, pal);
        }

        // 编号。见过但没抓到的标一下 —— 「见过」是 P6 的专有状态
        snprintf(buf, sizeof(buf), "%03u", sid);
        game_ui_text_centered(band_y, cx, cy + 34, CELL_W, 16, buf,
                               caught ? GAME_UI_INK : GAME_UI_MUTED);
    }

    // 页码
    snprintf(buf, sizeof(buf), "%u/%u", s_page + 1,
             (DEX_SPECIES + PER_PAGE - 1) / PER_PAGE);
    render_text(228 - render_text_width(buf), Y(PAGE_Y), buf, GAME_UI_MUTED);

    game_ui_footer(band_y, "A上 B下 C详情 长按B返回");

    #undef Y
    screen_push_band(band_y);
}

static void draw_all(void)
{
    for (int y = 0; y < SCR_H; y += BAND_H) draw_band(y);
}

static void redraw_for_dump(void) { draw_all(); }

void play_dex_enter(void)
{

    if(!nav_is_returning()){s_page = 0;s_detail=false;s_species=1;s_feedback=NULL;}
    screen_set_redraw(redraw_for_dump);
    draw_all();

    // **不做自动截图** —— P1 那个是在只有一页时加的，
    // 现在有了 dbg.c 的按键注入，截图由 walk.py 显式发 's' 触发。
    // 页面自己再截一张只会与之交错，让 PC 侧收到半张（踩过一次）。

    const dex_t *d = world_dex();
    ESP_LOGI(TAG, "P6：已捕获 %u 见过 %u", dex_count_caught(d),
             dex_count_seen(d));
}

void play_dex_exit(void)
{
}

void play_dex_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if(s_detail){
        if(nav_direction(btn,ev)!=0){
            s_species=(s_species+150+nav_direction(btn,ev))%151+1;s_feedback=NULL;
        }else if(nav_return(btn,ev)){s_page=(s_species-1)/PER_PAGE;s_detail=false;}
        else if(nav_confirm(btn,ev)){
            exploration_view_t view;world_exploration_snapshot(&view);bool cancel=view.state.tracked_species==s_species;
            exploration_kind_t result=world_exploration_track(cancel?0:s_species);
            if(result==EXPLORE_NONE&&!cancel){nav_open(PAGE_EXPLORATION);return;}
            s_feedback=result==EXPLORE_NONE?"已恢复章节目标":result==EXPLORE_BUSY?"请先结束当前对战":result==EXPLORE_BLOCKED?"尚未解锁该栖息地":"保存失败 请重试";
        }else return;
        draw_all();return;
    }
    if (nav_return(btn, ev)) { nav_back(PAGE_IDLE); return; }
    int direction = nav_direction(btn, ev);
    if (direction) {
        unsigned pages = (DEX_SPECIES + PER_PAGE - 1) / PER_PAGE;
        s_page = (s_page + pages + direction) % pages;
        draw_all(); return;
    }
    if (nav_confirm(btn, ev)) {
        s_species = s_page * PER_PAGE + 1; s_detail = true; s_feedback = NULL;
        draw_all();
    }
}
