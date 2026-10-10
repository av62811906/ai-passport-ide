#include "game_ui.h"
#include "assets.h"
#include "render.h"
#include <string.h>
#include <stdio.h>
#include "combat.h"

static void rect(void *ctx, int x, int y, int w, int h, uint16_t color)
{
    int by = *(const int *)ctx;
    int top = y > by ? y : by;
    int bottom = y + h < by + SCREEN_BAND_H ? y + h : by + SCREEN_BAND_H;
    int left = x > 0 ? x : 0;
    int right = x + w < SCREEN_W ? x + w : SCREEN_W;
    for (int py = top; py < bottom; py++)
        for (int px = left; px < right; px++) screen_px(px, py - by, color);
}

void game_ui_title(int band_y, const char *title, const char *right_text)
{
    render_text(12, 8 - band_y, title, GAME_UI_INK);
    if (right_text && *right_text)
        render_text(228 - render_text_width(right_text), 8 - band_y,
                      right_text, GAME_UI_MUTED);
    rect(&band_y, 12, 30, 216, 1, GAME_UI_INK);
}

void game_ui_box(int band_y, int x, int y, int w, int h)
{
    if (w < 24 || h < 24 || w % 8 || h % 8 || w > SCREEN_W || h > SCREEN_H) return;
    battle_hud_draw_message_box(rect, &band_y, x, y, (uint8_t)(w / 8),
                                (uint8_t)(h / 8), 1, GAME_UI_BG);
}

void game_ui_footer(int band_y, const char *hint)
{
    game_ui_box(band_y, 0, 280, 240, 40);
    // Frame-1's inner edges are x=4/234 and y=286/315. Its original
    // asymmetric tiles require centering in the visible interior, not y=296.
    game_ui_text_centered(band_y, 5, 287, 229, 28, hint, GAME_UI_INK);
}

void game_ui_action_row(int band_y, int x, int y, int width,
                        const char *const *labels, unsigned count, unsigned selected)
{
    if (!count) return;
    int cell = width / count;
    for (unsigned i = 0; i < count; i++) {
        int left = x + i * cell;
        game_ui_text_centered(band_y, left + 10, y, cell - 10, 16,
                              labels[i], i == selected ? GAME_UI_INK : GAME_UI_MUTED);
        if (i == selected) game_ui_cursor(band_y, left, y + 4);
    }
}
void game_ui_actions(int band_y, const char *const *labels, unsigned count, unsigned selected)
{
    game_ui_box(band_y, 0, 280, 240, 40);
    game_ui_action_row(band_y, 8, 294, 224, labels, count, selected);
}
const char *game_ui_list_hint(unsigned count) {
    return count ? GAME_UI_NAV_HINT : GAME_UI_BACK_HINT;
}
void game_ui_list_marker(int band_y, int x, int y, unsigned index, unsigned count, unsigned selected) {
    if (index < count && index == selected) game_ui_cursor(band_y, x, y + 4);
}

void game_ui_text_centered(int band_y, int x, int y, int w, int h,
                           const char *text, uint16_t color)
{
    render_bounds_t ink;
    if (!render_text_ink_bounds(text, &ink)) return;
    render_text(x + (w - ink.w) / 2 - ink.x,
                y + (h - ink.h) / 2 - ink.y - band_y, text, color);
}

void game_ui_sprite_centered(int band_y, int x, int y, int w, int h,
                             const uint8_t *data, int sw, int sh, int scale,
                             const uint16_t *palette)
{
    render_bounds_t ink;
    if (scale <= 0 || !render_sprite_ink_bounds(data, sw, sh, &ink)) return;
    render_sprite_2bpp_wh(x + (w - ink.w * scale) / 2 - ink.x * scale,
                          y + (h - ink.h * scale) / 2 - ink.y * scale - band_y,
                          data, sw, sh, scale, palette);
}

void game_ui_thumbnail_centered(int band_y, int x, int y, int w, int h,
                                const uint8_t *data, int size, int dest_size,
                                const uint16_t *palette)
{
    if (!data || size <= 0 || dest_size <= 0) return;
    // Measure the same pixel-center sampling used by the thumbnail renderer.
    int left = dest_size, top = dest_size, right = 0, bottom = 0;
    const int stride = (size + 3) / 4;
    for (int dy = 0; dy < dest_size; dy++) {
        int sy = ((2 * dy + 1) * size) / (2 * dest_size);
        for (int dx = 0; dx < dest_size; dx++) {
            int sx = ((2 * dx + 1) * size) / (2 * dest_size);
            if (((data[sy * stride + sx / 4] >> (6 - 2 * (sx % 4))) & 3) == 3) continue;
            if (dx < left) left = dx;
            if (dy < top) top = dy;
            if (dx + 1 > right) right = dx + 1;
            if (dy + 1 > bottom) bottom = dy + 1;
        }
    }
    if (!right) return;
    render_sprite_2bpp_thumbnail(x + (w - (right - left)) / 2 - left,
                                  y + (h - (bottom - top)) / 2 - top - band_y,
                                  data, size, dest_size, palette);
}

void game_ui_meter(int band_y, int x, int y, int w, uint8_t pct)
{
    if (w < 8) return;
    if (pct > 100) pct = 100;
    rect(&band_y, x, y, w, 16, GAME_UI_BG);
    rect(&band_y, x + 2, y + 4, w - 4, 10, GAME_UI_INK);
    rect(&band_y, x, y + 6, w, 6, GAME_UI_INK);
    rect(&band_y, x + 2, y + 6, w - 4, 6, GAME_UI_BG);
    rect(&band_y, x + 2, y + 6, (w - 4) * pct / 100, 6, GAME_UI_ACCENT);
}

void game_ui_cursor(int band_y, int x, int y)
{
    ui_art_t cursor;
    static const uint16_t palette[4] = {GAME_UI_INK, GAME_UI_INK, GAME_UI_INK, 0};
    if (assets_ui("cursor", &cursor))
        render_sprite_2bpp_wh(x, y - band_y, cursor.data, cursor.w, cursor.h, 1, palette);
}

void game_ui_text_fitted(int band_y, int x, int y, int width, const char *text, uint16_t color)
{
    char line[192]; size_t n = 0;
    while (text[n] && n < sizeof(line)-5) {
        unsigned char c = (unsigned char)text[n];
        size_t bytes = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (n + bytes >= sizeof(line)-4) break;
        memcpy(line+n, text+n, bytes); line[n+bytes] = 0;
        if (render_text_width(line) > width) break;
        n += bytes;
    }
    line[n] = 0;
    if (text[n]) {
        while (n && render_text_width(line) + render_text_width("...") > width) {
            do { n--; } while (n && ((unsigned char)line[n] & 0xc0) == 0x80);
            line[n] = 0;
        }
        if (width >= render_text_width("...")) strcat(line,"...");
    }
    render_text(x,y-band_y,line,color);
}

void game_ui_moves(int y,uint16_t species,uint8_t level,unsigned selected,bool in_battle){
 uint16_t ids[COMBAT_MOVE_CAP];int count=combat_known_moves(species,level,ids,COMBAT_MOVE_CAP);char text[80];
 snprintf(text,sizeof(text),"已学会 %d",count);game_ui_title(y,"技能",text);
 if(!count){game_ui_text_centered(y,16,112,208,16,"暂时没有已学招式",GAME_UI_MUTED);game_ui_footer(y,GAME_UI_BACK_HINT);return;}
 selected%=count;unsigned top=selected/5*5;
 for(unsigned row=0;row<5&&top+row<(unsigned)count;row++){
  unsigned index=top+row;move_t m;combat_move(ids[index],&m);int sy=44+row*34;
  snprintf(text,sizeof(text),"%.*s",m.name_zh_len,m.name_zh);render_text(32,sy-y,text,GAME_UI_INK);
  snprintf(text,sizeof(text),"Lv%u",combat_learn_level(species,m.id));render_text(228-render_text_width(text),sy-y,text,GAME_UI_MUTED);
  game_ui_list_marker(y,8,sy,index,count,selected);
 }
 game_ui_box(y,8,216,224,56);
 game_ui_text_fitted(y,16,228,208,combat_description(ids[selected]),GAME_UI_INK);
 game_ui_text_centered(y,16,250,208,16,"升级自动学会 永不遗忘",GAME_UI_MUTED);
 game_ui_footer(y,in_battle?"A上 B下 C继续 长按B返回":GAME_UI_NAV_HINT);
}

void game_ui_fade_background(int band_y,unsigned amount) {
 static const uint8_t rank[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
 if(amount>16)amount=16;
 for(int y=0;y<SCREEN_BAND_H;y++)for(int x=0;x<SCREEN_W;x++)
  if(rank[(band_y+y)&3][x&3]<amount)screen_px(x,y,GAME_UI_BG);
}

void game_ui_move_settings(int y,uint16_t species,uint8_t level,unsigned selected,const move_policy_t *policy,const char *feedback){
 uint16_t ids[COMBAT_MOVE_CAP];int n=combat_known_moves(species,level,ids,COMBAT_MOVE_CAP);char text[80];
 snprintf(text,sizeof(text),"启用 %d/%d",combat_enabled_moves(species,level,policy),n);game_ui_title(y,"招式设置",text);
 selected%=(unsigned)n+1;unsigned top=selected/5*5;
 for(unsigned row=0;row<5&&top+row<=(unsigned)n;row++){
  unsigned i=top+row;int sy=44+row*34;
  game_ui_list_marker(y,8,sy,i,n+1,selected);
  if(i==(unsigned)n){render_text(44,sy-y,"全部启用",GAME_UI_ACCENT);continue;}
  bool enabled=move_policy_allows(policy,ids[i]);
  // Pixel checkbox shares the monochrome Gold/Silver frame language.
  rect(&y,26,sy+2,12,12,GAME_UI_INK);rect(&y,28,sy+4,8,8,GAME_UI_BG);
  if(enabled){rect(&y,29,sy+7,3,3,GAME_UI_INK);rect(&y,32,sy+5,3,5,GAME_UI_INK);}
  move_t m;combat_move(ids[i],&m);snprintf(text,sizeof(text),"%.*s",m.name_zh_len,m.name_zh);
  game_ui_text_fitted(y,44,sy,140,text,enabled?GAME_UI_INK:GAME_UI_MUTED);
  render_text(192,sy-y,enabled?"启用":"禁用",enabled?GAME_UI_ACCENT:GAME_UI_MUTED);
 }
 game_ui_box(y,8,216,224,56);
 game_ui_text_fitted(y,16,226,208,selected<(unsigned)n?combat_description(ids[selected]):"恢复所有已学招式",GAME_UI_INK);
 game_ui_text_fitted(y,16,250,208,feedback&&*feedback?feedback:"新学招式默认启用",GAME_UI_MUTED);
 game_ui_footer(y,"A上 B下 C切换 长按B返回");
}
