// main/play_battle.c —— P3 遭遇详情 / 战斗。
//
// 对应 docs/pages/P3-battle.md。
//
// ## 不做招式选择，张力从哪来
//
// 战斗是自动的（用户定的）。页面文档列了三处制造张力的地方：
//   1. **属性相克可见** —— 「效果绝佳」让玩家看到自己属性选择的因果
//   2. **HP 逐回合扣减**而非瞬间结算
//   3. **削弱机制** —— 战后野怪 HP 降低使捕获窗口加宽
//
// 第 3 点是关键：它让「先打再抓」成为**真策略**，而不是可跳过的动画。
// 战斗页因此不是通往捕获的走廊，而是一个决策点。
//
// Each attack commits one resumable battle step before its animation. Timers
// only pace presentation; capture and page navigation retain HP/RNG/turn order.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "assets.h"
#include "battle.h"
#include "battle_escape.h"
#include "battle_fx.h"
#include "esp_timer.h"
static uint32_t s_move_started_ms;
#include "battle_presentation.h"
#include "game_ui.h"
#include "exp.h"
#include "nav.h"
#include "play.h"
#include "pokemon_animation.h"
#include "shiny_entry.h"
#include "render.h"
#include "render_scene_screen.h"
#include "screen.h"
#include "sfx.h"
#include "music_director.h"
#include "world.h"

static const char *TAG = "p3";

// dbg.c 的战斗种子覆盖。0 = 不覆盖（用 enc.ts，与正常路径一致）。
// 见 dbg.c 顶部注释。
extern uint32_t dbg_battle_seed;

#define BAND_H SCREEN_BAND_H
#define SCR_W SCREEN_W
#define SCR_H SCREEN_H

// GSC composition adapted to 240x320: enemy status top-left, front sprite
// top-right, player back bottom-left and player status bottom-right.
// Original 8px HUD tiles use integer 2x scaling. Both sprite bands redraw
// together, while the complete 240x80 message box owns the bottom band.
#define WILD_NAME_Y 8
#define WILD_BAR_Y SCENE_P3_WILD_TOP
#define WILD_CAUGHT_X 8
#define WILD_BAR_X (WILD_CAUGHT_X + BATTLE_HUD_TILE_SIZE * BATTLE_HUD_SCALE)
#define WILD_FILL_TILES 3
#define WILD_RARITY_Y 50
#define WILD_SHINY_Y 68
#define PET_SPRITE_Y SCENE_P3_PET_BACK_Y
#define PET_NAME_Y SCENE_P3_PET_NAME_Y
#define PET_NAME_X SCENE_P3_PET_NAME_X
#define PET_BAR_Y SCENE_P3_PET_HP_Y
#define PET_BAR_X SCENE_P3_PET_HP_X
#define PET_BAR_W SCENE_P3_PET_HP_W
#define PET_HP_VALUE_Y 208
#define MSG_Y 256
#define MSG_DETAIL_Y 274
#define MSG_HINT_Y 292
#define MSG_X 16
#define MSG_RIGHT 224

SCREEN_ASSERT_WITHIN_BAND(battle_wild_name, WILD_NAME_Y, 16);
SCREEN_ASSERT_WITHIN_BAND(battle_wild_bar, WILD_BAR_Y, BATTLE_HUD_HP_HEIGHT);
_Static_assert(WILD_BAR_X + (WILD_FILL_TILES + 3) * BATTLE_HUD_TILE_SIZE *
               BATTLE_HUD_SCALE <= SCENE_P3_WILD_HUD_RIGHT,
               "Wild caught marker and HP must stay outside the front sprite");
SCREEN_ASSERT_WITHIN_BAND(battle_wild_rarity, WILD_RARITY_Y, 16);
SCREEN_ASSERT_ALLOW_CROSS_BAND(battle_wild_sprite_box,
                          SCENE_P3_WILD_TOP, SCENE_P3_WILD_MAX_SIZE);
SCREEN_ASSERT_WITHIN_BAND(battle_wild_shiny, WILD_SHINY_Y, 7);
// 主宠显示目标为 96px；共享配方从资产尺寸自动取 32x3 / 48x2。
// 96 > 80 带高，跨带 1/2 —— 用 SCREEN_ASSERT_ALLOW_CROSS_BAND 显式声明，
// 且 shake tick 必须重画带 1+2（见 tick 里的注释）。
// PET_SPRITE_Y=140 keeps its last pixel at 235, above the message box.
#define PET_SPRITE_DISPLAY SCENE_P3_PET_BACK_SIZE
SCREEN_ASSERT_ALLOW_CROSS_BAND(battle_pet_sprite, PET_SPRITE_Y, PET_SPRITE_DISPLAY);
SCREEN_ASSERT_WITHIN_BAND(battle_pet_name, PET_NAME_Y, 16);
SCREEN_ASSERT_WITHIN_BAND(battle_pet_bar, PET_BAR_Y, SCENE_P3_PET_HP_H);
SCREEN_ASSERT_WITHIN_BAND(battle_pet_hp_value, PET_HP_VALUE_Y, 16);
SCREEN_ASSERT_WITHIN_BAND(battle_round_message, MSG_Y, 16);
SCREEN_ASSERT_WITHIN_BAND(battle_effect, MSG_DETAIL_Y, 16);
SCREEN_ASSERT_WITHIN_BAND(battle_exp, MSG_DETAIL_Y, 16);
SCREEN_ASSERT_WITHIN_BAND(battle_weak, MSG_DETAIL_Y, 16);
SCREEN_ASSERT_WITHIN_BAND(battle_keys, MSG_HINT_Y, 16);

static lv_timer_t *s_tick;

static battle_result_t s_res;
static battle_session_t s_session;
static bool s_counter_anim;
static uint8_t s_play_i;         // 播到第几回合
static bool s_playing;
static bool s_done;
enum { ACTION_CAPTURE, ACTION_FIGHT, ACTION_ESCAPE };
static uint8_t s_choice, s_escape_choice;
static bool s_escape_prompt;

// Read-only presentation of the recorded move. Each entry resets the phase;
// no static hold counter can leak into the next battle after leaving a page.
static uint8_t s_fx_frame;
static uint8_t s_between_moves;
static bool s_entering, s_exp_anim, s_store_failed;
static bool s_loot_failed, s_settle_failed;
static bool s_wild_animating, s_escape_requested, s_counter_escape;
enum { ESCAPE_NONE, ESCAPE_SUCCESS, ESCAPE_FAILED };
static uint8_t s_escape_feedback, s_escape_hold;
static uint32_t s_wild_anim_ms;
static uint8_t s_wild_anim_frame;
static pokemon_anim_info_t s_wild_anim_info;
static uint8_t s_wild_frame_buffer[POKEMON_ANIM_BUFFER_BYTES];
static uint8_t s_entry_frame, s_exp_frame, s_exp_sound_level;
static uint16_t s_pet_hp_from, s_wild_hp_from;
static uint32_t s_exp_from;

static uint16_t s_pet_species;
static bool s_pet_shiny;
static shiny_entry_t s_shiny_entry;
static uint8_t s_pet_level;
static uint32_t s_pet_exp;
static const uint8_t *s_wild_sprite;
static uint8_t s_wild_size;
static scene_sprite_layout_t s_wild_layout;

static void prepare_wild_sprite(uint16_t species)
{
    memset(&s_wild_layout, 0, sizeof(s_wild_layout));
    s_wild_sprite = assets_front_sprite(species, &s_wild_size);
    if (!s_wild_sprite) {
        sprite_asset_t fallback;
        if (assets_back_sprite_info(species, &fallback) && fallback.w == fallback.h) {
            s_wild_sprite = fallback.data;
            s_wild_size = fallback.w;
        }
    }
    if (!scene_p3_wild_layout(s_wild_sprite, s_wild_size, &s_wild_layout)) {
        s_wild_sprite = NULL;
        s_wild_size = 0;
    }
    memset(&s_wild_anim_info, 0, sizeof(s_wild_anim_info));
    s_wild_anim_frame = 0;
    if (s_wild_sprite && pokemon_anim_info(species, &s_wild_anim_info) &&
        s_wild_anim_info.size == s_wild_size) {
        // One original-picture origin for every frame, including the resting
        // pose. The full animation's opaque union fits the enemy region.
        s_wild_layout.x = 232 - (s_wild_anim_info.x + s_wild_anim_info.w) * 2;
        s_wild_layout.y = SCENE_P3_WILD_TOP - s_wild_anim_info.y * 2;
        s_wild_layout.visible.x = 232 - s_wild_anim_info.w * 2;
        s_wild_layout.visible.y = SCENE_P3_WILD_TOP;
        s_wild_layout.visible.w = s_wild_anim_info.w * 2;
        s_wild_layout.visible.h = s_wild_anim_info.h * 2;
    }
}

static void sample_wild_motion(void)
{
    pokemon_anim_sample_t sample = pokemon_anim_sample(nav_ctx()->enc.species_id, s_wild_anim_ms);
    if (sample.frame != s_wild_anim_frame) {
        sprite_asset_t sprite;
        if (pokemon_anim_decode(nav_ctx()->enc.species_id, sample.frame,
            s_wild_frame_buffer, sizeof(s_wild_frame_buffer), &sprite)) {
            s_wild_sprite = sprite.data;
            s_wild_anim_frame = sample.frame;
        } else sample.finished = true;
    }
    if (sample.finished && s_wild_anim_ms>=900) s_wild_animating = false;
}

static battle_presentation_exp_t visible_exp(void)
{
    return battle_presentation_exp(s_exp_from, s_pet_exp, LEVEL_MAX,
        s_exp_anim ? s_exp_frame : BATTLE_PRESENTATION_EXP_FRAMES);
}

void play_battle_presentation_snapshot(play_battle_view_t *out)
{
    if (!out) return;
    *out = (play_battle_view_t){.pet_hp = s_session.pet_hp,
        .wild_hp = s_session.wild_hp, .phase = "choice"};
    if (s_play_i) {
        const battle_round_t *r = &s_res.rounds[0];
        out->move_id = r->move_id;
        out->by_pet = r->by_pet;
        uint16_t hit = battle_fx_hit_frame(r);
        uint16_t elapsed = s_fx_frame > hit ? s_fx_frame - hit : 0;
        out->pet_hp = battle_presentation_hp(s_pet_hp_from, s_session.pet_hp, false, elapsed);
        out->wild_hp = battle_presentation_hp(s_wild_hp_from, s_session.wild_hp, false, elapsed);
        out->phase = s_counter_anim ? "retaliation" : "attack";
    } else if (s_done) out->phase = "result";
    else if (s_playing) out->phase = "battle";
    battle_presentation_exp_t xp = visible_exp();
    out->exp = xp.total;
    out->level = (s_done || s_exp_anim) ? xp.level : s_pet_level;
    if (s_exp_anim) out->phase = "exp";
    if (s_wild_animating) out->phase = "entrance-motion";
    out->shiny_side = shiny_entry_side(&s_shiny_entry);
    if (out->shiny_side) out->phase = "shiny-entry";
    out->wild_frame = s_wild_anim_frame;
    if (s_escape_feedback) out->phase = s_escape_feedback == ESCAPE_SUCCESS ? "escaped" : "escape-failed";
    if (s_entering) {
        battle_presentation_entry_t entry = battle_presentation_entry(s_entry_frame,
            SCREEN_W - SCENE_P3_PET_BACK_X,
            -(s_wild_layout.visible.x + s_wild_layout.visible.w));
        out->pet_dx = entry.pet_dx;
        out->wild_dx = entry.wild_dx;
        out->phase = "entry";
    }
}

static void draw_band(int band_y)
{
    screen_band_clear(SCENE_P3_BG);
    #define Y(v) ((v) - band_y)

    const nav_ctx_t *c = nav_ctx();
    char buf[64];
    species_t wild_sp, pet_sp;
    bool has_wild = assets_species(c->enc.species_id, &wild_sp);
    bool has_pet = assets_species(s_pet_species, &pet_sp);

    uint8_t wlv = s_session.wild_level;
    const battle_round_t *round = !s_done && s_play_i > 0 &&
        s_play_i <= s_res.round_count ? &s_res.rounds[s_play_i - 1] : NULL;
    const uint8_t *wild_sprite=s_wild_sprite;uint8_t wild_size=s_wild_size;
    scene_sprite_layout_t wild_layout=s_wild_layout;
    species_t wild_art=wild_sp;
    if(s_session.fighters[1].transform_species){
        uint16_t id=s_session.fighters[1].transform_species;
        wild_sprite=assets_front_sprite(id,&wild_size);
        scene_p3_wild_layout(wild_sprite,wild_size,&wild_layout);
        assets_species(id,&wild_art);
    }
    battle_fx_rect_t pet = {SCENE_P3_PET_BACK_X, PET_SPRITE_Y,
                            PET_SPRITE_DISPLAY, PET_SPRITE_DISPLAY};
    battle_fx_rect_t wild = {wild_layout.visible.x, wild_layout.visible.y,
                             wild_layout.visible.w, wild_layout.visible.h};
    sprite_asset_t actor_sprite;render_bounds_t actor_ink;
    uint16_t pet_art_id=s_session.fighters[0].transform_species?s_session.fighters[0].transform_species:s_pet_species;
    if(assets_back_sprite_info(pet_art_id,&actor_sprite)&&render_sprite_ink_bounds(actor_sprite.data,actor_sprite.w,actor_sprite.h,&actor_ink)){
        int scale=PET_SPRITE_DISPLAY/actor_sprite.w;
        pet=(battle_fx_rect_t){SCENE_P3_PET_BACK_X+actor_ink.x*scale,PET_SPRITE_Y+actor_ink.y*scale,actor_ink.w*scale,actor_ink.h*scale};
    }
    if(wild_sprite&&render_sprite_ink_bounds(wild_sprite,wild_size,wild_size,&actor_ink))
        wild=(battle_fx_rect_t){wild_layout.x+actor_ink.x*2,wild_layout.y+actor_ink.y*2,actor_ink.w*2,actor_ink.h*2};
    battle_fx_pose_t pose = round ? battle_fx_pose_for_rects(round, s_fx_frame, pet, wild)
                                 : (battle_fx_pose_t){0, 0};

    play_battle_view_t view;
    play_battle_presentation_snapshot(&view);
    uint16_t p_hp = view.pet_hp, w_hp = view.wild_hp;
    if (s_entering) {
        pose.pet_dx = view.pet_dx;
        pose.wild_dx = view.wild_dx;
    }

    // HUD appears once the full sprites reach their battle positions.
    if (!s_entering) {
    // -- 野怪 ------------------------------------------------------------
    if (has_wild) {
        snprintf(buf, sizeof(buf), "%.*s Lv%u",
                 wild_sp.name_zh_len, wild_sp.name_zh, wlv);
    } else {
        snprintf(buf, sizeof(buf), "#%03u Lv%u", c->enc.species_id, wlv);
    }
    render_text(8, Y(WILD_NAME_Y), buf, C_INK);

    // Rarity remains below the enemy HP, separated from its name and sprite.
    {
        char st[32];
        int n = 0;
        for (int i = 0; i < 5; i++) {
            memcpy(st + n, (i < c->enc.rarity) ? "★" : "☆", 3);
            n += 3;
        }
        st[n] = '\0';
        render_text(8, Y(WILD_RARITY_Y), st, C_INK);
        if (dex_is_caught(world_dex(), c->enc.species_id)) {
            battle_hud_draw_caught(scene_screen_rect, &band_y,
                                  WILD_CAUGHT_X, WILD_BAR_Y,
                                  BATTLE_HUD_SCALE, SCENE_P3_BG);
        }
    }
    // Always reserve the caught slot; changing species never shifts the HP.
    // A three-tile fill keeps the right edge at 120, outside all front sprites.
    battle_hud_draw_hp(scene_screen_rect, &band_y, WILD_BAR_X, WILD_BAR_Y,
                       WILD_FILL_TILES, BATTLE_HUD_SCALE,
                       BATTLE_HUD_WILD, w_hp, s_res.wild_hp_max, SCENE_P3_BG);
    }

    // -- 野怪 front sprite -----------------------------------------------
    if (wild_sprite && has_wild && battle_fx_actor_visible(round,s_fx_frame,false)) {
        uint16_t pal[4];
        assets_palette_variant(wild_art.palette, c->enc.is_shiny, pal);
        render_sprite_2bpp(wild_layout.x + pose.wild_dx, Y(wild_layout.y),
                           wild_sprite, wild_size, SCENE_P3_WILD_SCALE, pal);
    }

    // Shiny marker sits left of the wild sprite, clear of names and HP.
    // The two assets and palette match the encounter list.
    if (c->enc.is_shiny && !s_entering) {
        static const uint16_t STAR_PAL[4] = {
            C_INK, RGB_HEX(0xfff0a0), RGB_HEX(0xffffff), 0,
        };
        ui_art_t s7, s5;
        if (assets_ui("star_7", &s7)) {
            render_sprite_2bpp_wh(96, Y(WILD_SHINY_Y),
                                  s7.data, s7.w, s7.h, 1, STAR_PAL);
            if (assets_ui("star_5", &s5)) {
                render_sprite_2bpp_wh(96 + s7.w + 2, Y(WILD_SHINY_Y),
                                      s5.data, s5.w, s5.h, 1, STAR_PAL);
            }
        }
    }

    // -- 主宠 ------------------------------------------------------------
    sprite_asset_t pet_spr;
    if (has_pet && battle_fx_actor_visible(round,s_fx_frame,true) && assets_back_sprite_info(pet_art_id, &pet_spr)) {
        uint16_t pal[4];
        species_t drawn=pet_sp;assets_species(pet_art_id,&drawn);
        assets_palette_variant(drawn.palette, s_pet_shiny, pal);
        scene_screen_p3_pet_back(band_y, pet_spr.data, pet_spr.w, pet_spr.h,
                                  pose.pet_dx+(s_wild_animating?pokemon_back_entrance_offset(s_wild_anim_ms):0), pal);
    }
    if (!s_entering) {
    if (has_pet) {
        scene_screen_p3_pet_name(band_y, pet_sp.name_zh,
                                  pet_sp.name_zh_len, view.level);
    }
    scene_screen_p3_pet_hp(band_y, p_hp, s_res.pet_hp_max);
    snprintf(buf, sizeof(buf), "%u/%u", p_hp, s_res.pet_hp_max);
    render_text(SCR_W - 8 - render_text_width(buf), Y(PET_HP_VALUE_Y), buf, C_INK);
    battle_presentation_exp_t xp = visible_exp();
    battle_hud_draw_exp(scene_screen_rect, &band_y, 120, 224, 7, BATTLE_HUD_SCALE,
                        xp.got, xp.need, SCENE_P3_BG);
    }

    if (round) {
        battle_fx_actor_t pa={0},wa={.data=wild_sprite,.w=wild_size,.h=wild_size};
        sprite_asset_t back_art;
        if(assets_back_sprite_info(pet_art_id,&back_art)){
            pa.data=back_art.data;pa.w=back_art.w;pa.h=back_art.h;
            species_t art_sp=pet_sp;assets_species(pet_art_id,&art_sp);
            assets_palette_variant(art_sp.palette,s_pet_shiny,pa.palette);
        }
        assets_palette_variant(wild_art.palette,c->enc.is_shiny,wa.palette);
        battle_fx_draw_scene_band(round,s_fx_frame,band_y,pet,wild,&pa,&wa);
    }

    shiny_entry_draw(&s_shiny_entry, band_y, pet, wild);

    battle_hud_draw_message_box(scene_screen_rect, &band_y,
                                0, 240, 15, 5, BATTLE_HUD_SCALE, SCENE_P3_BG);

    // -- 回合文字（两行：GSC 消息窗形态）--------------------------------
    // 第一行 MSG_Y：谁（野怪加「野生」前缀）+ 效果提示
    // 第二行 MSG_DETAIL_Y：做了什么 + 伤害数字
    // 单行最坏 256px 超 232px 上限（野生多刺菊石兽 + 尖刺加农炮），
    // 拆两行后最坏 144px（「使用了尖刺加农炮！」）。
    if (s_escape_prompt) {
        render_text(MSG_X, Y(MSG_Y), "要尝试逃跑吗？", C_INK);
        render_text(MSG_X, Y(MSG_DETAIL_Y), "失败后对方会反击", GAME_UI_MUTED);
    } else if(s_settle_failed){
        render_text(MSG_X,Y(MSG_Y),"结算保存失败",C_INK);
        render_text(MSG_X,Y(MSG_DETAIL_Y),"按C重试 奖励不会重复",GAME_UI_MUTED);
    } else if (s_escape_feedback) {
        render_text(MSG_X, Y(MSG_Y), s_escape_feedback == ESCAPE_SUCCESS
                    ? "成功逃跑了！" : "没能逃跑！", C_INK);
        if (s_escape_feedback == ESCAPE_FAILED)
            render_text(MSG_X, Y(MSG_DETAIL_Y), "对方准备反击", GAME_UI_MUTED);
    } else if (!s_done && s_play_i > 0 && s_play_i <= s_res.round_count) {
        const battle_round_t *r = &s_res.rounds[s_play_i - 1];
        char who[32];
        if (r->by_pet && has_pet) {
            snprintf(who, sizeof(who), "%.*s",
                     pet_sp.name_zh_len, pet_sp.name_zh);
        } else if (!r->by_pet && has_wild) {
            snprintf(who, sizeof(who), "野生%.*s",
                     wild_sp.name_zh_len, wild_sp.name_zh);
        } else {
            who[0] = '\0';
        }
        // 第一行：谁 + 效果提示（100 倍率不显示，
        // 每回合都弹「效果一般」会把「效果绝佳」的分量冲掉）
        snprintf(buf, sizeof(buf), "%s", who);
        game_ui_text_fitted(band_y, MSG_X, MSG_Y, MSG_RIGHT-MSG_X, buf, C_INK);

        // 第二行：做了什么 + 伤害
        if (r->skipped) {snprintf(buf,sizeof(buf),"%s",combat_feedback(r));
        } else if (r->missed) {
            snprintf(buf, sizeof(buf), "的攻击落空了！");
        } else {
            move_t display;
            bool named = combat_move(r->move_id, &display);
            snprintf(buf, sizeof(buf), "使用了%.*s！",
                     named ? display.name_zh_len : r->move_zh_len,
                     named ? display.name_zh : r->move_zh ? r->move_zh : "");
        }
        game_ui_text_fitted(band_y, MSG_X, MSG_DETAIL_Y, MSG_RIGHT-MSG_X, buf, C_INK);
    } else if (s_done) {
        unsigned gained = s_exp_anim
            ? (unsigned)s_res.exp * s_exp_frame / BATTLE_PRESENTATION_EXP_FRAMES : s_res.exp;
        if (s_res.won) {
            if(exploration_chain_encounter(&nav_ctx()->enc))snprintf(buf,sizeof(buf),"连胜%lu 经验+%u",(unsigned long)world_exploration_chain(),gained);
            else snprintf(buf, sizeof(buf), "胜利 经验 +%u", gained);
            game_ui_text_fitted(band_y,MSG_X,MSG_Y,MSG_RIGHT-MSG_X,buf,C_INK);
            const item_info_t *item = items_info(s_session.loot_item);
            if (s_loot_failed) snprintf(buf, sizeof(buf), "掉落保存失败，请重试");
            else if (item && s_session.loot_qty)
                snprintf(buf, sizeof(buf), "获得%s ×%u", item->name, s_session.loot_qty);
            else if (item && s_session.loot_full) snprintf(buf, sizeof(buf), "%s已满", item->name);
            else snprintf(buf, sizeof(buf), "这次没有掉落");
            render_text(MSG_X, Y(MSG_DETAIL_Y), buf, GAME_UI_MUTED);
        } else {
            snprintf(buf, sizeof(buf), "战败 经验 +%u", gained);
            render_text(MSG_X, Y(MSG_Y), buf, C_INK);
            game_ui_text_fitted(band_y,MSG_X,MSG_DETAIL_Y,MSG_RIGHT-MSG_X,exploration_chain_encounter(&nav_ctx()->enc)?"连胜中断 体能-20":"体能 -20 心情 -15",GAME_UI_MUTED);
        }
    } else {
        const char *message = s_store_failed ? "保存失败，请重试"
            : s_session.started ? "继续捕捉，还是战斗？" : "野生宝可梦出现了！";
        render_text(MSG_X, Y(MSG_Y), message, C_INK);
        if (!s_entering && !s_wild_animating && !shiny_entry_side(&s_shiny_entry) && has_pet && has_wild) {
            uint16_t chance = battle_escape_chance(
                battle_effective_stat(pet_sp.speed, s_session.pet_level),
                battle_effective_stat(wild_sp.speed, s_session.wild_level), s_session.escape_attempts);
            snprintf(buf, sizeof(buf), "逃跑成功率 %u%%", chance * 100 / 256);
            render_text(MSG_X, Y(MSG_DETAIL_Y), buf, GAME_UI_MUTED);
        }
    }

    // -- 三键 --------------------------------------------------------------
    const char *hint = s_escape_feedback ? ""
        : (s_entering || s_wild_animating || shiny_entry_side(&s_shiny_entry)) ? "宝可梦出场中"
        : s_exp_anim ? "正在获得经验"
        : (s_loot_failed||s_settle_failed) ? "[C]重试保存"
        : s_counter_anim ? (s_counter_escape ? "逃跑失败，对方反击" : "野生宝可梦正在反击")
        : s_done ? (s_session.won ? "选择投球或返回" : "选择照料或返回")
        : s_escape_requested ? "招式结束后尝试逃跑"
        : s_playing ? "长按B逃跑" : "A上 B下 C确认 长按B返回";
    char attack_hint[80];
    if (!s_done && s_playing && !s_escape_requested && s_play_i > 0 && s_play_i <= s_res.round_count) {
        const battle_round_t *r = &s_res.rounds[s_play_i-1];
        const char *effect = combat_feedback(r);
        if(r->charging||r->skipped||r->self_target||r->no_effect||!r->damage)snprintf(attack_hint,sizeof(attack_hint),"%s",effect?effect:"长按B逃跑");
        else if (r->missed) snprintf(attack_hint,sizeof(attack_hint),"长按B逃跑");
        else snprintf(attack_hint,sizeof(attack_hint),"%s -%uHP 长按B逃跑",effect?effect:"",r->damage);
        hint = attack_hint;
    }
    if (s_escape_prompt) {
        static const char *const labels[] = {"继续", "逃跑"};
        game_ui_action_row(band_y, MSG_X, MSG_HINT_Y, MSG_RIGHT - MSG_X, labels, 2, s_escape_choice);
    } else if (!s_entering && !s_wild_animating && !shiny_entry_side(&s_shiny_entry) && !s_exp_anim && !s_playing &&
               !s_escape_feedback && !s_settle_failed && !s_loot_failed) {
        const char *const labels[] = {s_done ? (s_session.won ? "投球" : "照料") : "捕获", s_done ? "返回" : "战斗", "逃跑"};
        game_ui_action_row(band_y, MSG_X, MSG_HINT_Y, MSG_RIGHT - MSG_X, labels, s_done ? 2 : 3, s_choice);
    } else game_ui_text_centered(band_y, MSG_X, MSG_HINT_Y, MSG_RIGHT - MSG_X, 16, hint, C_INK);

    #undef Y
    screen_push_band(band_y);
}

static void draw_all(void)
{
    for (int y = 0; y < SCR_H; y += BAND_H) draw_band(y);
}

static void redraw_for_dump(void) { draw_all(); }

static bool store_session(void)
{
    nav_ctx_t *c = nav_ctx();
    if (!world_battle_set_uid(c->uid, &s_session)) return false;
    c->battled = s_session.finished;
    c->battle_won = s_session.won;
    c->enc.hp_ratio = battle_session_hp_ratio(&s_session);
    world_update_hp_uid(c->uid, c->enc.hp_ratio);
    return true;
}

static void encounter_gone(void)
{
    nav_ctx()->valid = false;
    nav_end_encounter();
}

static void settle_loot(void)
{
    item_loot_t loot;
    s_loot_failed = !world_battle_loot_uid(nav_ctx()->uid, &loot);
    // Retain world's award guard when storing the session from this page.
    if (!s_loot_failed) world_battle_get_uid(nav_ctx()->uid, &s_session);
}

static bool settle_battle(void)
{
    s_playing = false;
    s_escape_prompt = false; s_choice = 0;
    s_done = true;
    s_counter_anim = false;
    s_play_i = 0;
    s_res.won = s_session.won;
    music_director_play(s_session.won ? MUSIC_WILD_WIN : MUSIC_HOME);
    s_res.exp = 0;
    s_exp_from = s_pet_exp;
    nav_ctx_t *c = nav_ctx();
    if (!s_session.won && !s_session.defeat_applied) {
        if (!world_apply_defeat_uid(c->uid)) return false;
        s_session.defeat_applied = true;
    }
    if (!s_session.reward_settled) {
        if(!world_battle_reward_uid(c->uid,&s_res.exp))return false;
        c->enc.exp_granted=true;
        s_session.reward_settled = true;
    }
    if (!store_session()) return false;
    if (s_session.won) settle_loot();
    world_t w;
    world_snapshot(&w);
    s_pet_exp = w.exp;
    s_exp_sound_level = exp_to_level(s_exp_from, LEVEL_MAX);
    s_exp_frame = 0;
    s_exp_anim = s_res.exp > 0 || s_pet_exp > s_exp_from;
    ESP_LOGI(TAG, "战斗结束：%s %u 次攻击，野怪剩 %u%%",
             s_session.won ? "胜" : "败", s_session.attack_count,
             battle_session_hp_ratio(&s_session));
    return true;
}

static bool begin_attack(void)
{
    battle_session_t previous = s_session;
    s_counter_anim = s_session.retaliation_pending;
    s_counter_escape = s_counter_anim && s_session.escape_retaliation;
    s_pet_hp_from = s_session.pet_hp;
    s_wild_hp_from = s_session.wild_hp;
    bool stepped = battle_session_step(&s_session, &s_res.rounds[0]);
    s_session.escape_retaliation = false;
    if (!stepped || !store_session()) {
        s_session = previous;
        s_counter_anim = false;
        return false;
    }
    s_res.round_count = 1;
    s_play_i = 1;
    s_fx_frame = 0;
    s_move_started_ms = (uint32_t)(esp_timer_get_time()/1000);
    return true;
}

static void attempt_escape(void)
{
    battle_session_t previous = s_session;
    battle_escape_result_t result;
    s_escape_requested = false;
    if (!s_session.started) {
        world_t w;
        world_snapshot(&w);
        s_session.ability_factor_q10 = nurture_ability_factor(&w.pet);
    }
    if (!battle_escape_try(&s_session, &result) || !store_session()) {
        s_session = previous;
        encounter_t current;
        if (!world_get_encounter_uid(nav_ctx()->uid, &current) ||
            current.ts != nav_ctx()->enc.ts) { encounter_gone(); return; }
        s_store_failed = true;
        draw_all();
        return;
    }
    s_store_failed = false;
    s_escape_feedback = result.escaped ? ESCAPE_SUCCESS : ESCAPE_FAILED;
    s_escape_hold = 0;
    draw_all();
}

static void tick(lv_timer_t *t)
{
    (void)t;
    static unsigned slow_phase;
    bool active_move=s_playing&&s_play_i&&!s_between_moves&&!s_entering&&!s_wild_animating&&!shiny_entry_side(&s_shiny_entry)&&!s_escape_feedback&&!s_exp_anim;
    if(!active_move&&!shiny_entry_side(&s_shiny_entry)&&++slow_phase%2)return;
    if(s_settle_failed)return;
    if (shiny_entry_side(&s_shiny_entry)) {
        if (shiny_entry_step(&s_shiny_entry, BATTLE_FX_TICK_MS)) sfx_play(SFX_SHINY);
        draw_all();
        return;
    }
    if (s_entering) {
        if (++s_entry_frame >= BATTLE_PRESENTATION_ENTRY_FRAMES) {
            s_entering = false;
            s_wild_animating = true;
            s_wild_anim_ms = 0;
            if (s_wild_animating) sample_wild_motion();
        }
        draw_all();
        return;
    }
    if (s_wild_animating) {
        s_wild_anim_ms += 90;
        sample_wild_motion();
        if (!s_wild_animating) {
            shiny_entry_begin(&s_shiny_entry, (s_pet_shiny ? 1u : 0u) | (nav_ctx()->enc.is_shiny ? 2u : 0u));
            if (shiny_entry_side(&s_shiny_entry)) sfx_play(SFX_SHINY);
        }
        draw_all();
        return;
    }
    if (s_escape_feedback) {
        if (++s_escape_hold < 12) return;
        if (s_escape_feedback == ESCAPE_SUCCESS) { encounter_gone(); return; }
        s_escape_feedback = ESCAPE_NONE;
        s_playing = true;
        if (!begin_attack()) { encounter_gone(); return; }
        draw_all();
        return;
    }
    if (s_exp_anim) {
        if (++s_exp_frame >= BATTLE_PRESENTATION_EXP_FRAMES) s_exp_anim = false;
        uint8_t level = visible_exp().level;
        if (level > s_exp_sound_level) { sfx_play(SFX_LEVEL_UP); s_exp_sound_level = level; }
        draw_all();
        return;
    }
    if (!s_playing) return;
    if (s_between_moves) { s_between_moves--; return; }
    if (s_play_i) {
        const battle_round_t *r = &s_res.rounds[0];
        uint16_t frames = battle_fx_frames(r);
        uint16_t hp_end = battle_fx_hit_frame(r) + BATTLE_PRESENTATION_HP_FRAMES + 1;
        if (hp_end > frames) frames = hp_end;
        if (s_fx_frame + 1 < frames) {
            unsigned elapsed_frame = ((uint32_t)(esp_timer_get_time()/1000)-s_move_started_ms)/BATTLE_FX_TICK_MS;
            if (elapsed_frame <= s_fx_frame) return;
            unsigned previous = s_fx_frame;
            s_fx_frame = elapsed_frame < frames ? elapsed_frame : frames-1;
            unsigned hit = battle_fx_hit_frame(r);
            if (previous < hit && s_fx_frame >= hit)
                if(r->move_id) sfx_move(r->move_id, r->move_type, r->missed);
            draw_band(0);
            draw_band(BAND_H);
            draw_band(BAND_H * 2);
            return;
        }
        s_counter_anim = false;
        s_play_i = 0;
        s_between_moves = 5;
        draw_all();
        return;
    }
    if (s_session.finished) {
        if (!settle_battle()) { s_settle_failed=true;draw_all();return; }
    } else if (s_escape_requested) {
        attempt_escape();
        return;
    } else if (!s_session.auto_battle) {
        // A failed capture costs exactly one enemy move, then restores choice.
        s_playing = false;
    } else if (!begin_attack()) {
        encounter_gone(); return;
    }
    draw_all();
}

void play_battle_enter(void)
{
    shiny_entry_begin(&s_shiny_entry, 0);
    s_choice = s_escape_choice = 0; s_escape_prompt = false;
    memset(&s_res, 0, sizeof(s_res));
    s_play_i = 0;
    s_fx_frame = 0;
    s_move_started_ms = (uint32_t)(esp_timer_get_time()/1000);
    s_between_moves = 0;
    s_counter_anim = false;
    s_counter_escape = s_escape_requested = s_wild_animating = false;
    s_escape_feedback = ESCAPE_NONE;
    s_escape_hold = 0;
    s_entering = s_exp_anim = s_store_failed = false;
    s_loot_failed = s_settle_failed = false;
    s_entry_frame = s_exp_frame = 0;
    world_t w;
    world_snapshot(&w);
    world_party_t party;
    world_party_snapshot(&party);
    s_pet_shiny = party.count && (party.members[0].flags & 1u);
    nav_ctx_t *c = nav_ctx();
    encounter_t current;
    if (!world_get_encounter_uid(c->uid, &current) || current.ts != c->enc.ts ||
        !world_battle_get_uid(c->uid, &s_session)) { encounter_gone(); return; }
    c->enc = current;
    if (!s_session.initialized || (!s_session.started &&
        (s_session.pet_species != w.species || s_session.pet_level != w.level))) {
        bool intro_seen = s_session.initialized && s_session.intro_seen;
        move_policy_t policy = s_session.move_policy;
        uint32_t seed = dbg_battle_seed ? dbg_battle_seed : (c->enc.ts ? c->enc.ts : 1u);
        if (!battle_session_init(&s_session, w.species, w.level,
                                  c->enc.species_id, (c->enc.level ? c->enc.level : battle_wild_level_for_pet(c->enc.rarity, w.level)),
                                  nurture_ability_factor(&w.pet), seed)) { encounter_gone(); return; }
        s_session.intro_seen = intro_seen;
        s_session.move_policy = policy;
        // Existing weakened encounters retain their saved wild HP percentage.
        if (c->enc.hp_ratio && c->enc.hp_ratio < 100) {
            s_session.wild_hp = (uint32_t)s_session.wild_hp_max * c->enc.hp_ratio / 100;
            if (!s_session.wild_hp) s_session.wild_hp = 1;
        }
        if (!store_session()) { encounter_gone(); return; }
    }
    c->valid = true;
    s_pet_species = s_session.pet_species;
    s_pet_level = s_session.pet_level;
    s_pet_exp = s_exp_from = w.exp;
    s_res.pet_hp_max = s_session.pet_hp_max;
    s_res.wild_hp_max = s_session.wild_hp_max;
    s_playing = (s_session.auto_battle || s_session.retaliation_pending) && !s_session.finished;
    s_done = s_session.finished;
    prepare_wild_sprite(c->enc.species_id);
    if (!s_session.intro_seen && !s_session.started) {
        s_entering = true;
        s_session.intro_seen = true;
        if (!store_session()) { encounter_gone(); return; }
    }
    if (s_done && !settle_battle()) { s_settle_failed=true;draw_all(); }
    if (s_session.retaliation_pending && !begin_attack()) { encounter_gone(); return; }
    screen_set_redraw(redraw_for_dump);
    draw_all();
    s_tick = lv_timer_create(tick, BATTLE_FX_TICK_MS, NULL);
}

void play_battle_exit(void)
{
    // 先停定时器再删屏 —— 反过来 tick 会访问野指针（上游 AGENTS.md）
    if (s_tick) { lv_timer_delete(s_tick); s_tick = NULL; }
}

static bool can_choose(void)
{
    return !s_entering && !s_wild_animating && !shiny_entry_side(&s_shiny_entry) && !s_escape_feedback && !s_exp_anim && !s_playing && !s_counter_anim &&
           !s_session.retaliation_pending && !(s_session.finished && !s_done);
}

bool play_battle_can_leave(void)
{
    // Hardware menu navigation must not bypass an unfinished escape roll.
    return s_done && !s_loot_failed && !s_settle_failed && can_choose();
}

bool play_battle_screen_busy(void)
{
    return !can_choose() || s_play_i != 0 || s_escape_requested;
}

static void perform_battle_action(unsigned action)
{
    if(s_settle_failed){
        if(action==ACTION_CAPTURE){s_settle_failed=!settle_battle();draw_all();}
        return;
    }
    if (action == ACTION_ESCAPE && s_playing && !s_session.finished &&
        !s_entering && !s_wild_animating && !shiny_entry_side(&s_shiny_entry) && !s_counter_anim &&
        !s_escape_feedback && !s_session.retaliation_pending) {
        s_escape_requested = true;
        draw_band(BAND_H * 3);
        return;
    }
    // The failed throw already committed the retaliation obligation. Neither
    // another throw nor returning to the list may skip its attack animation.
    if (!can_choose()) return;
    if (s_loot_failed) {
        if (action == ACTION_CAPTURE) { settle_loot(); draw_all(); }
        return;
    }
    switch (action) {
    case ACTION_CAPTURE:
        if (s_done && !s_session.won) { nav_go(PAGE_CARE); break; }
        if (battle_session_can_capture(&s_session)) {
            if (!store_session()) { encounter_gone(); return; }
            nav_go(PAGE_CAPTURE);
        }
        break;
    case ACTION_FIGHT:
        if (!s_playing && !s_done) {
            battle_session_t previous = s_session;
            if (!s_session.started) {
                world_t w;
                world_snapshot(&w);
                s_session.ability_factor_q10 = nurture_ability_factor(&w.pet);
            }
            s_session.started = true;
            s_session.auto_battle = true;
            if (!store_session()) {
                s_session = previous;
                encounter_t current;
                if (!world_get_encounter_uid(nav_ctx()->uid, &current) ||
                    current.ts != nav_ctx()->enc.ts) { encounter_gone(); return; }
                s_store_failed = true;
                draw_all();
                return;
            }
            s_store_failed = false;
            s_playing = true;
            draw_all();
        }
        break;
    case ACTION_ESCAPE:
        if (s_done) nav_end_encounter();
        else attempt_escape();
        break;
    default:
        break;
    }
}

void play_battle_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    bool back = nav_return(btn, ev), confirm = nav_confirm(btn, ev);
    int direction = nav_direction(btn, ev);
    if (!direction && !confirm && !back) return;
    if (s_settle_failed || s_loot_failed) {
        if (confirm) perform_battle_action(ACTION_CAPTURE);
        return;
    }
    if (s_escape_prompt) {
        if (direction) s_escape_choice = nav_list_selection(btn, ev, 2, s_escape_choice);
        else if (back) s_escape_prompt = false;
        else if (confirm) {
            s_escape_prompt = false;
            if (s_escape_choice) perform_battle_action(ACTION_ESCAPE);
        }
        draw_all(); return;
    }
    if (back && !s_done) {
        if (!s_entering && !s_wild_animating && !shiny_entry_side(&s_shiny_entry) && !s_counter_anim &&
            !s_escape_feedback && !s_session.retaliation_pending) {
            s_escape_prompt = true; s_escape_choice = 0; draw_all();
        }
        return;
    }
    if (!can_choose()) return;
    if (back) { perform_battle_action(ACTION_ESCAPE); return; }
    if (direction) { s_choice = nav_list_selection(btn, ev, s_done ? 2 : 3, s_choice); draw_all(); return; }
    if (s_done && s_choice == 1) { perform_battle_action(ACTION_ESCAPE); return; }
    if (!s_done && s_choice == ACTION_ESCAPE) {
        s_escape_prompt = true; s_escape_choice = 0; draw_all(); return;
    }
    perform_battle_action(s_choice);
}

#ifdef HOST_BUILD
unsigned play_battle_move_preview_frames(void) {
    return s_play_i > 0 && s_play_i <= s_res.round_count
        ? battle_fx_frames(&s_res.rounds[s_play_i - 1]) : 0;
}
// Isolated acceptance fixture: same combat calculation, sprites, HUD and FX.
// mode 0 is actual resolution; mode 1/2 are labelled visual hit/miss fixtures.
bool play_battle_move_preview(unsigned id,unsigned side,unsigned frame,unsigned mode){
 move_t m;if(!combat_move(id,&m)||side>1||mode>3)return false;
 if(s_tick){lv_timer_delete(s_tick);s_tick=NULL;}
 shiny_entry_begin(&s_shiny_entry, 0);
 s_entering=s_exp_anim=s_wild_animating=s_done=false;s_playing=true;
 s_escape_feedback=0;s_store_failed=s_loot_failed=s_settle_failed=false;
 s_session.pet_hp_max=s_session.wild_hp_max=500;
 combat_mon_t a,d;combat_init(&a,side?s_session.wild_species:s_pet_species,60,500);
 combat_init(&d,side?s_pet_species:s_session.wild_species,60,500);
 a.hp=250;if(mode==3){a.charge=1;a.charge_move=id;if(id==117){a.bide=1;a.bide_damage=30;}}d.status=4;d.sleep=2;a.last_damage=30;d.last_move=33;
 battle_round_t r={.by_pet=!side};uint32_t rng=123;
 combat_turn(&a,&d,1024,&rng,50,id,&r);
 if(mode==1||mode==2){r.move_id=id;r.move_type=m.type;r.move_zh=m.name_zh;r.move_zh_len=m.name_zh_len;
  r.missed=mode==2;r.no_effect=0;r.mult=100;r.damage=0;a.hp=250;d.hp=500;
  if(mode==2)r.healed=0;
  if(mode==1&&m.power&&!r.charging&&!r.self_target){r.damage=40;d.hp-=40;if(r.healed)r.healed=20;}
  if(mode==1&&r.healed)a.hp+=r.healed;
 }
 s_session.pet_hp=side?d.hp:a.hp;s_session.wild_hp=side?a.hp:d.hp;
 s_pet_hp_from=side?500:250;s_wild_hp_from=side?250:500;
 r.pet_hp=s_session.pet_hp;r.wild_hp=s_session.wild_hp;
 s_res.pet_hp_max=s_res.wild_hp_max=500;s_res.round_count=1;s_res.rounds[0]=r;
 s_play_i=1;s_fx_frame=frame<battle_fx_frames(&r)?frame:battle_fx_frames(&r)-1;
 draw_all();return true;
}
#endif
