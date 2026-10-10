#include "display_settings.h"
#include "battery_ui.h"
// P11: GSC's right-side Start menu, adapted to the 240x320 display.
// Frame/cursor are the original shared tiles.
#include <stdio.h>
#include <string.h>

#include "assets.h"
#include "game_ui.h"
#include "nav.h"
#include "play.h"
#include "pokemon_names.h"
#include "render.h"
#include "screen.h"
#include "screen_idle.h"
#include "world.h"
#include "audio_settings.h"
#include "usb_backup.h"
#include "wifi_time.h"
#include "lvgl.h"

#define MENU_COUNT 10
#define MENU_X 120
#define MENU_Y 64
#define MENU_W 112
#define MENU_H 184
#define ROW_Y 72
#define ROW_STEP 17
#define OPTION_Y 48
#define OPTION_STEP 18
enum { OPTION_NAMES, OPTION_SCREEN_OFF, OPTION_MUTE, OPTION_VOLUME, OPTION_BACKUP, OPTION_IMPORT, OPTION_WIFI, OPTION_BRIGHTNESS, OPTION_BATTERY, OPTION_RETURN, OPTION_COUNT };

SCREEN_ASSERT_WITHIN_BAND(menu_title, 8, 16);
SCREEN_ASSERT_ALLOW_CROSS_BAND(menu_portrait, 88, 96);
SCREEN_ASSERT_WITHIN_BAND(menu_description, 252, 16);
SCREEN_ASSERT_ALLOW_CROSS_BAND(menu_option_names, OPTION_Y, 16);
SCREEN_ASSERT_ALLOW_CROSS_BAND(menu_option_screen, OPTION_Y + OPTION_STEP, 16);
SCREEN_ASSERT_ALLOW_CROSS_BAND(menu_option_return, OPTION_Y + OPTION_STEP * 3, 16);

static const char *LABELS[MENU_COUNT] = {"图鉴", "队伍", "道具", "照料", "挑战", "选项", "成就", "探索", "秘境", "关闭"};
static const char *DESCRIPTIONS[MENU_COUNT] = {
    "查看见过和捕获的伙伴", "查看伙伴 选择出战队首", "查看和使用随身道具",
    "喂食 玩耍 等待体力恢复", "道馆 徽章 联盟 赤红", "译名 声音与屏幕设置", "查看成长 领取成就奖励", "选择路线 追踪野生伙伴", "租借伙伴闯关 获取奖励", "回到冒险中",
};
static uint8_t s_selected;
static bool s_options;
static bool s_backup_view;
static bool s_wifi_view;
static wifi_time_view_t s_wifi_shown;
static bool s_import_accept;
static lv_timer_t *s_backup_timer;
static usb_backup_state_t s_backup_shown;
static bool s_volume_edit;
static bool s_brightness_edit;
static bool s_display_save_failed;
static bool s_audio_save_failed;
static uint8_t s_option_selected;
static world_t s_world;
static world_party_t s_party;
static unsigned s_claimable;

static void draw_options(int band_y)
{
    char value[64];
    game_ui_title(band_y, "选项", "");
    game_ui_box(band_y, 8, 40, 224, 200);
    render_text(40, OPTION_Y - band_y, "译名", GAME_UI_INK);
    snprintf(value, sizeof(value), "%s", pokemon_names_style_label());
    render_text(212 - render_text_width(value), OPTION_Y - band_y, value, GAME_UI_ACCENT);
    render_text(40, OPTION_Y + OPTION_STEP - band_y, "立即熄屏", GAME_UI_INK);
    render_text(40, OPTION_Y + OPTION_STEP * 2 - band_y, "静音", GAME_UI_INK);
    render_text(196, OPTION_Y + OPTION_STEP * 2 - band_y,
                audio_settings_muted() ? "开" : "关", GAME_UI_ACCENT);
    render_text(40, OPTION_Y + OPTION_STEP * 3 - band_y, "音量", GAME_UI_INK);
    snprintf(value, sizeof(value), "%u%%", audio_settings_volume());
    render_text(212-render_text_width(value), OPTION_Y + OPTION_STEP * 3-band_y, value, GAME_UI_ACCENT);
    render_text(40, OPTION_Y + OPTION_STEP * 4 - band_y, "备份存档", GAME_UI_INK);
    render_text(40, OPTION_Y + OPTION_STEP * 5 - band_y, "导入存档", GAME_UI_INK);
    render_text(40, OPTION_Y + OPTION_STEP * 6 - band_y, "Wi-Fi校时", GAME_UI_INK);
    render_text(40, OPTION_Y + OPTION_STEP * OPTION_BRIGHTNESS - band_y, "亮度", GAME_UI_INK);
    snprintf(value, sizeof(value), "%u%%", display_settings_brightness());
    render_text(212-render_text_width(value), OPTION_Y + OPTION_STEP * OPTION_BRIGHTNESS-band_y, value, GAME_UI_ACCENT);
    render_text(40, OPTION_Y + OPTION_STEP * OPTION_BATTERY-band_y, "电量显示", GAME_UI_INK);
    render_text(196, OPTION_Y + OPTION_STEP * OPTION_BATTERY-band_y,
                display_settings_battery_visible() ? "开" : "关", GAME_UI_ACCENT);
    render_text(40, OPTION_Y + OPTION_STEP * OPTION_RETURN - band_y, "返回菜单", GAME_UI_INK);
    game_ui_cursor(band_y, 20, OPTION_Y + s_option_selected * OPTION_STEP + 4);
    const char *hint;
    if (s_option_selected == OPTION_MUTE) {
        snprintf(value, sizeof(value), "%s", audio_settings_muted() ? "音乐和音效已关闭" : "音乐和音效已开启");
        hint = s_audio_save_failed ? "保存失败 按确认重试" : "按确认切换 重启后保留";
    } else if (s_option_selected == OPTION_VOLUME) {
        snprintf(value, sizeof(value), "%s", audio_settings_muted() ? "静音开启 调整后仍静音" : "音乐 音效 遇敌提示");
        hint = s_audio_save_failed ? "保存失败 请重试" : "音量设置重启后保留";
    } else if (s_option_selected == OPTION_BRIGHTNESS) {
        snprintf(value, sizeof(value), "屏幕亮度 即时生效");
        hint = s_display_save_failed ? "保存失败 请重试" : "重启与唤醒后保留";
    } else if (s_option_selected == OPTION_BATTERY) {
        snprintf(value, sizeof(value), "菜单与照料页显示电量");
        hint = s_display_save_failed ? "保存失败 按确认重试" : "按确认切换 重启后保留";
    } else if (s_option_selected == OPTION_BACKUP || s_option_selected == OPTION_IMPORT) {
        snprintf(value,sizeof(value),"USB连接电脑管理存档");
        hint=s_option_selected==OPTION_IMPORT?"电脑选文件 设备确认覆盖":"先打开电脑存档管理页";
    } else if (s_option_selected == OPTION_WIFI) {
        snprintf(value,sizeof(value),"联网补回关机期间的体能");
        hint="首次请设置家庭Wi-Fi";
    } else if (s_option_selected == OPTION_NAMES) {
        snprintf(value, sizeof(value), "设置只影响显示名称");
        hint = "译名设置本次运行有效";
    } else {
        snprintf(value, sizeof(value), "%lu秒无操作自动熄屏", (unsigned long)(screen_idle_timeout_ms() / 1000));
        hint = s_option_selected == OPTION_SCREEN_OFF ? "按确认熄屏 任意键亮屏" : "返回上一级菜单";
    }
    game_ui_text_centered(band_y, 12, 240, 216, 16, value, GAME_UI_MUTED);
    game_ui_text_centered(band_y, 12, 260, 216, 16, hint, GAME_UI_INK);
    game_ui_footer(band_y, (s_volume_edit || s_brightness_edit) ? "[A]加 [B]减 [C]完成" : GAME_UI_NAV_HINT);
}

static void draw_backup(int band_y)
{
    usb_backup_state_t st=usb_backup_state();
    bool importing=usb_backup_import_mode_active();
    game_ui_title(band_y,importing?"导入存档":"备份存档", "USB");
    if(st==USB_RESTORE_OFFER) {
        game_ui_text_centered(band_y,8,64,224,16,"覆盖当前游戏存档？",GAME_UI_INK);
        game_ui_text_centered(band_y,8,96,224,16,"确认后先备份当前进度",GAME_UI_MUTED);
        game_ui_text_centered(band_y,8,120,224,16,"完成后设备会重新启动",GAME_UI_MUTED);
        game_ui_box(band_y,16,164,208,80);
        render_text(56,180-band_y,"取消",GAME_UI_INK);
        render_text(56,214-band_y,"确认覆盖",GAME_UI_INK);
        game_ui_cursor(band_y,32,(s_import_accept?214:180)+4);
        game_ui_footer(band_y,GAME_UI_NAV_HINT);return;
    }
    game_ui_box(band_y,8,64,224,160);
    const char *a="先打开电脑存档管理页", *b="连接设备并选择目录", *c="按C重试";
    if(importing){a="在电脑选择备份文件";b="选择后在这里确认覆盖";c="长按B取消并返回";}
    if(st==USB_BACKUP_SENDING) {a="正在传送存档";b="请保持USB连接";c="等待传送完成";}
    else if(st==USB_BACKUP_WAIT_ACK) {a="电脑正在保存";b="等待文件写入完成";c="请保持网页打开";}
    else if(st==USB_RESTORE_RECEIVING) {a="正在接收导入存档";b="当前进度已备份到电脑";c="请保持USB连接";}
    else if(st==USB_RESTORE_STAGED) {a="存档已校验";b="正在重启并完成导入";c="请保持设备供电";}
    else if(st==USB_BACKUP_DONE) {a="备份完成";b="文件已保存到电脑";c="原存档保持不变";}
    else if(st==USB_BACKUP_FAILED) {a=importing?"导入未完成":"备份未完成";b="请检查连接和保存目录";c=importing?"原存档仍保留 请重新连接":"按C重试 原存档仍保留";}
    game_ui_text_centered(band_y,12,88,216,16,a,GAME_UI_INK);
    game_ui_text_centered(band_y,12,128,216,16,b,GAME_UI_MUTED);
    game_ui_text_centered(band_y,12,184,216,16,c,GAME_UI_INK);
    bool busy=st==USB_BACKUP_SENDING||st==USB_BACKUP_WAIT_ACK||st==USB_RESTORE_RECEIVING||st==USB_RESTORE_STAGED;
    game_ui_footer(band_y,busy?"请等待操作完成":importing?"长按B返回":"[C]备份 长按B返回");
}

static void draw_wifi(int band_y)
{
    wifi_time_view_t v;wifi_time_view(&v);char text[64];
    game_ui_title(band_y,"Wi-Fi校时","长按B返回");
    if(v.setup){
        game_ui_text_centered(band_y,8,48,224,16,"手机连接以下临时热点",GAME_UI_INK);
        game_ui_box(band_y,8,76,224,112);
        game_ui_text_centered(band_y,12,92,216,16,v.ssid,GAME_UI_INK);
        snprintf(text,sizeof(text),"密码 %s",v.password);
        game_ui_text_centered(band_y,12,120,216,16,text,GAME_UI_INK);
        game_ui_text_centered(band_y,12,156,216,16,"192.168.4.1",GAME_UI_ACCENT);
        game_ui_text_centered(band_y,8,204,224,16,"浏览器打开上方地址",GAME_UI_INK);
        game_ui_text_centered(band_y,8,232,224,16,"填写家庭Wi-Fi和密码",GAME_UI_MUTED);
        game_ui_footer(band_y,"[C]关闭 长按B返回");return;
    }
    const char *status="尚未设置网络";
    if(v.state==WIFI_TIME_CONNECTING)status="正在连接Wi-Fi";
    else if(v.state==WIFI_TIME_WAITING)status="已联网 等待校时";
    else if(v.state==WIFI_TIME_READY)status="时间已同步";
    else if(v.state==WIFI_TIME_OFFLINE)status="未连上网络 将自动重试";
    else if(v.state==WIFI_TIME_ERROR)status="校时或保存失败 请重试";
    game_ui_box(band_y,8,56,224,88);
    game_ui_text_centered(band_y,12,72,216,16,status,GAME_UI_INK);
    snprintf(text,sizeof(text),"本次补回体能 %u",v.recovered);
    game_ui_text_centered(band_y,12,108,216,16,text,GAME_UI_ACCENT);
    game_ui_text_centered(band_y,8,168,224,16,"开机联网后补回离线体能",GAME_UI_INK);
    game_ui_text_centered(band_y,8,196,224,16,"约一小时恢复满",GAME_UI_MUTED);
    game_ui_text_centered(band_y,8,224,224,16,"首次校时后开始计算",GAME_UI_MUTED);
    game_ui_footer(band_y,"[A]重试 [C]配网");
}

static void draw_main(int band_y)
{
    char name[48], text[48];
    battery_ui_playtime_text(text, sizeof(text));
    game_ui_title(band_y, "菜单", text);
    species_t species;
    bool found = assets_species(s_world.species, &species);
    if (found) snprintf(name, sizeof(name), "%.*s", species.name_zh_len, species.name_zh);
    else snprintf(name, sizeof(name), "#%03u", s_world.species);
    game_ui_text_fitted(band_y, 12, 40, 144, name, GAME_UI_INK);
    battery_ui_draw(band_y, 228, 40);
    snprintf(text, sizeof(text), "Lv%u", s_world.level);
    game_ui_text_centered(band_y, 8, 72, 104, 16, text, GAME_UI_MUTED);
    uint8_t size;
    const uint8_t *front = assets_front_sprite(s_world.species, &size);
    if (found && front) {
        uint16_t palette[4];
        bool shiny = s_party.count && (s_party.members[0].flags & 1);
        assets_palette_variant(species.palette, shiny, palette);
        game_ui_sprite_centered(band_y, 8, 88, 104, 96, front, size, size, 1, palette);
    }
    snprintf(text, sizeof(text), "队伍 %u/%u", s_party.count, PARTY_MAX);
    game_ui_text_centered(band_y, 8, 196, 104, 16, text, GAME_UI_INK);
    snprintf(text, sizeof(text), "捕获 %u", dex_count_caught(world_dex()));
    game_ui_text_centered(band_y, 8, 220, 104, 16, text, GAME_UI_MUTED);

    game_ui_box(band_y, MENU_X, MENU_Y, MENU_W, MENU_H);
    for (unsigned row = 0; row < MENU_COUNT; row++) {
        int y = ROW_Y + row * ROW_STEP;
        render_text(156, y - band_y, LABELS[row], GAME_UI_INK);
        if(row==6&&s_claimable)render_text(212,y-band_y,"!",GAME_UI_ACCENT);
        if (row == s_selected) game_ui_cursor(band_y, 136, y + 4);
    }
    if(s_selected==6&&s_claimable){snprintf(text,sizeof(text),"可领取 %u 项奖励",s_claimable);game_ui_text_centered(band_y,8,252,224,16,text,GAME_UI_INK);}
    else game_ui_text_centered(band_y, 8, 252, 224, 16, DESCRIPTIONS[s_selected], GAME_UI_MUTED);
    game_ui_footer(band_y, GAME_UI_NAV_HINT);
}

static void draw_all(void)
{
    for (int band_y = 0; band_y < SCREEN_H; band_y += SCREEN_BAND_H) {
        screen_band_clear(GAME_UI_BG);
        if (s_wifi_view) draw_wifi(band_y);
        else if (s_backup_view) draw_backup(band_y);
        else if (s_options) draw_options(band_y);
        else draw_main(band_y);
        screen_push_band(band_y);
    }
}

static void backup_refresh(lv_timer_t *timer)
{
    (void)timer;
    if(s_wifi_view){wifi_time_view_t v;wifi_time_view(&v);if(memcmp(&v,&s_wifi_shown,sizeof(v))){s_wifi_shown=v;draw_all();}}
    if(s_backup_view&&s_backup_shown!=usb_backup_state()) {
        s_backup_shown=usb_backup_state();
        if(s_backup_shown==USB_RESTORE_OFFER)s_import_accept=false;
        draw_all();
    }
}

void play_menu_enter(void)
{
    if (!nav_is_returning()) {
        s_selected = 0;
        s_options = false;
        s_backup_view = false;
        s_wifi_view = false;
        s_volume_edit = false;
        s_brightness_edit = false;
        s_display_save_failed = false;
        s_option_selected = 0;
        s_audio_save_failed = false;
    }
    world_snapshot(&s_world);
    world_party_snapshot(&s_party);
    achievement_view_t achievements;world_achievements_snapshot(&achievements);s_claimable=0;
    for(unsigned i=0;i<ACHIEVEMENT_COUNT;i++)
        if(!(achievements.store.claimed&(1u<<i))&&achievement_progress(&achievements,i)==achievement_info(i)->target)s_claimable++;
    s_backup_timer=lv_timer_create(backup_refresh,250,NULL);
    screen_set_redraw(draw_all);
    draw_all();
}

void play_menu_exit(void) { if(s_wifi_view){wifi_time_setup_request(false);s_wifi_view=false;} if(s_backup_timer) {lv_timer_delete(s_backup_timer);s_backup_timer=NULL;} }

void play_menu_presentation_snapshot(play_menu_view_t *out)
{
    if (out) *out = (play_menu_view_t){.selected = s_selected, .options = s_options,
                                     .option_selected = s_option_selected};
}

void play_menu_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if(s_wifi_view){
        wifi_time_view_t v;wifi_time_view(&v);
        if(nav_return(btn,ev)){wifi_time_setup_request(false);s_wifi_view=false;}
        else if(nav_confirm(btn,ev))wifi_time_setup_request(!v.setup);
        else if(btn==BSP_BTN_UP&&ev==BSP_BTN_CLICK&&!v.setup)wifi_time_retry();
        draw_all();return;
    }
    if(s_backup_view) {
        usb_backup_state_t st=usb_backup_state();
        if(st==USB_BACKUP_SENDING||st==USB_BACKUP_WAIT_ACK||st==USB_RESTORE_RECEIVING||st==USB_RESTORE_STAGED)return;
        if(st==USB_RESTORE_OFFER) {
            // Reset selection even if a key arrives before the redraw timer.
            if(s_backup_shown!=st){s_import_accept=false;s_backup_shown=st;}
            if(nav_return(btn,ev))usb_backup_restore_confirm(false);
            else if(nav_direction(btn,ev))s_import_accept=!s_import_accept;
            else if(nav_confirm(btn,ev))usb_backup_restore_confirm(s_import_accept);
        } else if(nav_return(btn,ev)){s_backup_view=false;usb_backup_import_mode(false);}
        else if(nav_confirm(btn,ev)&&!usb_backup_import_mode_active())usb_backup_request();
        draw_all();return;
    }
    if (s_brightness_edit) {
        if (nav_return(btn, ev)) { s_brightness_edit = false; draw_all(); return; }
        if (ev != BSP_BTN_CLICK) return;
        if (btn == BSP_BTN_OK) s_brightness_edit = false;
        else if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            int next = display_settings_brightness() + (btn == BSP_BTN_UP ? 10 : -10);
            if (next < DISPLAY_BRIGHTNESS_MIN) next = DISPLAY_BRIGHTNESS_MIN;
            if (next > 100) next = 100;
            s_display_save_failed = !display_settings_set_brightness(next);
        }
        draw_all(); return;
    }
    if (s_volume_edit) {
        if (nav_return(btn, ev)) { s_volume_edit = false; draw_all(); return; }
        if (ev != BSP_BTN_CLICK) return;
        if (btn == BSP_BTN_OK) s_volume_edit = false;
        else {
            int next = audio_settings_volume() + (btn == BSP_BTN_UP ? 5 : -5);
            if (next < 0) next = 0;
            if (next > 100) next = 100;
            s_audio_save_failed = !audio_settings_set_volume(next);
        }
        draw_all(); return;
    }
    if (nav_direction(btn, ev) != 0) {
        if (s_options)
            s_option_selected = (s_option_selected + OPTION_COUNT + nav_direction(btn, ev)) % OPTION_COUNT;
        else s_selected = (s_selected + MENU_COUNT + nav_direction(btn, ev)) % MENU_COUNT;
        draw_all();
        return;
    }
    if (ev != BSP_BTN_CLICK && !nav_return(btn, ev)) return;
    if (nav_return(btn, ev)) {
        if (s_options) { s_options = false; draw_all(); }
        else nav_back(PAGE_IDLE);
    } else if (nav_confirm(btn, ev)) {
        if (s_options) {
            if (s_option_selected == OPTION_SCREEN_OFF) {
                screen_idle_request_off();
                return;
            }
            if (s_option_selected == OPTION_MUTE) {
                s_audio_save_failed = !audio_settings_set_muted(!audio_settings_muted());
            } else if (s_option_selected == OPTION_VOLUME) s_volume_edit = true;
            else if (s_option_selected == OPTION_BRIGHTNESS) s_brightness_edit = true;
            else if (s_option_selected == OPTION_BATTERY) {
                s_display_save_failed = !display_settings_set_battery_visible(!display_settings_battery_visible());
                if (!s_display_save_failed) battery_ui_start();
            }
            else if (s_option_selected == OPTION_BACKUP) {usb_backup_import_mode(false);s_backup_view=true;usb_backup_request();}
            else if (s_option_selected == OPTION_IMPORT) {usb_backup_import_mode(true);s_import_accept=false;s_backup_view=true;}
            else if (s_option_selected == OPTION_WIFI) {s_wifi_view=true;wifi_time_view(&s_wifi_shown);}
            else if (s_option_selected == OPTION_RETURN) s_options = false;
            else pokemon_names_set_style(pokemon_names_get_style() == POKEMON_NAMES_OFFICIAL
                                             ? POKEMON_NAMES_GS_LEGACY : POKEMON_NAMES_OFFICIAL);
            draw_all();
        } else {
            switch (s_selected) {
            case 0: nav_open(PAGE_DEX); break;
            case 1: nav_open(PAGE_PARTY); break;
            case 2: nav_open(PAGE_BAG); break;
            case 3: nav_open(PAGE_CARE); break;
            case 4: nav_open(PAGE_TRAINER); break;
            case 5: s_options = true; s_option_selected = 0; draw_all(); break;
            case 6: nav_open(PAGE_ACHIEVEMENTS); break;
            case 7: nav_open(PAGE_EXPLORATION); break;
            case 8: nav_open(PAGE_DUNGEON); break;
            case 9: nav_back(PAGE_IDLE); break;
            default: break;
            }
        }
    }
}
