// Gold source-derived display tracks shared by firmware and native preview.
// Legacy adapted effects below remain for failed/immune/skipped presentations;
// successful and charging moves dispatch to gold_fx before those fallbacks.
#include "battle_fx.h"
#include "gold_fx.h"
#include "battle_presentation.h"
#include "battle_fx_assets.h"
#include "render_scene.h"
#include "screen.h"

typedef struct { uint16_t move_id; uint8_t style; } move_style_t;
static const move_style_t MOVE_STYLES[] = {
    {172, BATTLE_FX_FIRE},
    {181, BATTLE_FX_ICE},
    {183, BATTLE_FX_PALM},
    {185, BATTLE_FX_LUNGE},
    {186, BATTLE_FX_PSYCHIC},
    {188, BATTLE_FX_POISON},
    {189, BATTLE_FX_GROUND},
    {192, BATTLE_FX_THUNDER},
    {196, BATTLE_FX_ICE},
    {198, BATTLE_FX_MULTICUT},
    {200, BATTLE_FX_DRAGON_RAGE},
    {202, BATTLE_FX_DRAIN},
    {204, BATTLE_FX_PSYCHIC},
    {206, BATTLE_FX_CUT},
    {211, BATTLE_FX_WIND},
    {223, BATTLE_FX_PALM},
    {225, BATTLE_FX_DRAGON},
    {231, BATTLE_FX_WHIP},
    {238, BATTLE_FX_CUT},
    {239, BATTLE_FX_WIND},
    {242, BATTLE_FX_BITE},
    {245, BATTLE_FX_LUNGE},
    {246, BATTLE_FX_ROCK},
    {247, BATTLE_FX_ORB},
    {249, BATTLE_FX_PALM},

    {1, BATTLE_FX_PALM}, {2, BATTLE_FX_PALM}, {3, BATTLE_FX_PALM},
    {7, BATTLE_FX_FIRE}, {8, BATTLE_FX_ICE}, {9, BATTLE_FX_ELECTRIC},
    {10, BATTLE_FX_CUT}, {16, BATTLE_FX_WIND}, {17, BATTLE_FX_WIND},
    {20, BATTLE_FX_BIND}, {22, BATTLE_FX_WHIP}, {23, BATTLE_FX_KICK},
    {24, BATTLE_FX_KICK}, {29, BATTLE_FX_LUNGE}, {30, BATTLE_FX_HORN},
    {31, BATTLE_FX_HORN}, {33, BATTLE_FX_LUNGE}, {34, BATTLE_FX_LUNGE},
    {35, BATTLE_FX_BIND}, {40, BATTLE_FX_NEEDLE}, {44, BATTLE_FX_BITE},
    {51, BATTLE_FX_POISON}, {52, BATTLE_FX_FIRE}, {55, BATTLE_FX_WATER},
    {64, BATTLE_FX_HORN}, {65, BATTLE_FX_HORN}, {67, BATTLE_FX_KICK},
    {71, BATTLE_FX_DRAIN}, {72, BATTLE_FX_DRAIN}, {75, BATTLE_FX_LEAF},
    {84, BATTLE_FX_ELECTRIC}, {85, BATTLE_FX_ELECTRIC}, {87, BATTLE_FX_THUNDER},
    {88, BATTLE_FX_ROCK}, {93, BATTLE_FX_PSYCHIC}, {94, BATTLE_FX_PSYCHIC},
    {98, BATTLE_FX_LUNGE}, {122, BATTLE_FX_LICK}, {129, BATTLE_FX_STARS},
    {140, BATTLE_FX_ORB}, {141, BATTLE_FX_DRAIN}, {145, BATTLE_FX_BUBBLE},
    {154, BATTLE_FX_MULTICUT}, {158, BATTLE_FX_BITE}, {163, BATTLE_FX_CUT},
    {165, BATTLE_FX_IMPACT}, {250, BATTLE_FX_BIND},
    {49, BATTLE_FX_SONIC_BOOM}, {69, BATTLE_FX_SEISMIC_TOSS},
    {82, BATTLE_FX_DRAGON_RAGE}, {101, BATTLE_FX_NIGHT_SHADE},
    {162, BATTLE_FX_SUPER_FANG}, {56, BATTLE_FX_HYDRO_PUMP},
    {63, BATTLE_FX_HYPER_BEAM}, {120, BATTLE_FX_EXPLOSION},
    {153, BATTLE_FX_EXPLOSION},
};

// Order is the shared TY_* order. No floating point, transparency or RGB888
// alpha blending enters the pixel path; each visible pixel is logical RGB565.
static const uint16_t TYPE_PAL[BATTLE_TYPE_COUNT][3] = {
    {RGB_HEX(0x302818), RGB_HEX(0x806848), RGB_HEX(0xffffff)}, // normal
    {RGB_HEX(0x803000), RGB_HEX(0xf04800), RGB_HEX(0xffcc38)}, // fire
    {RGB_HEX(0x083888), RGB_HEX(0x1888f0), RGB_HEX(0xb8f0ff)}, // water
    {RGB_HEX(0x805800), RGB_HEX(0xf0b800), RGB_HEX(0xffffff)}, // electric
    {RGB_HEX(0x185018), RGB_HEX(0x38a820), RGB_HEX(0xc0f068)}, // grass
    {RGB_HEX(0x186880), RGB_HEX(0x50c8e0), RGB_HEX(0xffffff)}, // ice
    {RGB_HEX(0x681810), RGB_HEX(0xc84828), RGB_HEX(0xffc880)}, // fighting
    {RGB_HEX(0x501070), RGB_HEX(0xa048c0), RGB_HEX(0xe8b8f8)}, // poison
    {RGB_HEX(0x604018), RGB_HEX(0xb07828), RGB_HEX(0xe8c068)}, // ground
    {RGB_HEX(0x284868), RGB_HEX(0x7098c0), RGB_HEX(0xe0f0ff)}, // flying
    {RGB_HEX(0x781858), RGB_HEX(0xd85898), RGB_HEX(0xffc8e0)}, // psychic
    {RGB_HEX(0x385010), RGB_HEX(0x80a020), RGB_HEX(0xe0f080)}, // bug
    {RGB_HEX(0x383028), RGB_HEX(0x887858), RGB_HEX(0xd0c8a0)}, // rock
    {RGB_HEX(0x201838), RGB_HEX(0x605080), RGB_HEX(0xb8a8d8)}, // ghost
    {RGB_HEX(0x381878), RGB_HEX(0x8858d8), RGB_HEX(0xe8c0ff)}, // dragon
    {RGB_HEX(0x201820), RGB_HEX(0x605068), RGB_HEX(0xc8a8c8)}, // dark
    {RGB_HEX(0x283848), RGB_HEX(0x708898), RGB_HEX(0xe0f0ff)}, // steel
};

battle_fx_style_t battle_fx_style(const battle_round_t *round)
{
    if (!round) return BATTLE_FX_IMPACT;
    for (unsigned i = 0; i < sizeof(MOVE_STYLES) / sizeof(MOVE_STYLES[0]); i++) {
        if (MOVE_STYLES[i].move_id == round->move_id)
            return (battle_fx_style_t)MOVE_STYLES[i].style;
    }
    if(round->move_type==TY_DARK)return BATTLE_FX_GHOST;
    if(round->move_type==TY_STEEL)return BATTLE_FX_CUT;
    return round->move_type < 15
        ? (battle_fx_style_t)round->move_type : BATTLE_FX_IMPACT;
}

bool battle_fx_has_dedicated(uint16_t move_id)
{
    for (unsigned i = 0; i < sizeof(MOVE_STYLES) / sizeof(MOVE_STYLES[0]); i++)
        if (MOVE_STYLES[i].move_id == move_id) return true;
    return false;
}

uint8_t battle_fx_frames(const battle_round_t *round)
{
    if (gold_fx_enabled(round)) return gold_fx_frame_count(round);
    if (!round || round->missed) return 20;
    if(round->charging||round->self_target||round->healed||round->skipped)return 20;
    if(round->hits>1)return 24+round->hits*2;
    switch (battle_fx_style(round)) {
    case BATTLE_FX_IMPACT: case BATTLE_FX_LUNGE: case BATTLE_FX_CUT:
    case BATTLE_FX_PALM: case BATTLE_FX_KICK: case BATTLE_FX_HORN:
    case BATTLE_FX_NEEDLE: case BATTLE_FX_WHIP:
        return 20;
    case BATTLE_FX_ELECTRIC: case BATTLE_FX_ICE: case BATTLE_FX_PSYCHIC:
    case BATTLE_FX_DRAIN: case BATTLE_FX_BUBBLE: case BATTLE_FX_GHOST:
    case BATTLE_FX_SONIC_BOOM: case BATTLE_FX_NIGHT_SHADE: case BATTLE_FX_SUPER_FANG:
        return 24;
    case BATTLE_FX_ROCK: case BATTLE_FX_GROUND: case BATTLE_FX_THUNDER:
    case BATTLE_FX_DRAGON:
    case BATTLE_FX_SEISMIC_TOSS: case BATTLE_FX_DRAGON_RAGE:
    case BATTLE_FX_HYDRO_PUMP: case BATTLE_FX_HYPER_BEAM: case BATTLE_FX_EXPLOSION:
        return 28;
    default:
        return 22;
    }
}

bool battle_fx_actor_visible(const battle_round_t *round,uint8_t frame,bool pet){
    if(!round||!round->damage||round->missed||round->no_effect||round->self_target||round->charging||round->skipped||pet==(bool)round->by_pet)return true;
    unsigned active=battle_fx_frames(round)-2,span=gold_fx_enabled(round)?12:6,start=active>span?active-span:0;
    return frame<start||frame>=active||(((frame-start)/(span/6))&1u);
}

// Keep impact phases visible for at least 540 ms on the 90 ms device timer.
// 12 authoring phases followed by two empty hardware frames. No mutable clock
// state lives here: a screenshot/replay always derives the same frame.
static int phase_of(const battle_round_t *round, uint8_t frame)
{
    int active = battle_fx_frames(round) - 2;
    return frame < active ? (int)frame * 12 / active : 12;
}

battle_fx_pose_t battle_fx_pose(const battle_round_t *round, uint8_t frame)
{
    battle_fx_pose_t pose = {0, 0};
    if (gold_fx_enabled(round)) return pose;
    if (!round || round->self_target || round->charging || round->skipped) return pose;
    int phase = phase_of(round, frame);
    if (phase >= 12) return pose;
    static const int8_t LUNGE[12] = {0, -1, -2, 2, 6, 10, 12, 6, 2, 0, 0, 0};
    static const int8_t HIT[12] = {0, 0, 0, 0, 0, 0, -3, 3, -2, 2, 0, 0};
    static const int8_t EVADE[12] = {0, 0, 0, 0, 2, 5, 8, 8, 5, 2, 0, 0};
    battle_fx_style_t style = battle_fx_style(round);
    int attack = LUNGE[phase];
    if (style != BATTLE_FX_LUNGE && style != BATTLE_FX_HORN &&
        style != BATTLE_FX_FIGHTING && style != BATTLE_FX_IMPACT)
        attack /= 3;
    int defense = round->missed ? EVADE[phase] : (round->damage ? HIT[phase] : 0);
    if (round->by_pet) {
        pose.pet_dx = (int16_t)attack;
        pose.wild_dx = (int16_t)defense;
    } else {
        pose.wild_dx = (int16_t)-attack;
        pose.pet_dx = (int16_t)-defense;
    }
    // Keep the actor and the overlay on the same bounded pose. The pet's
    // right-hand motion budget comes from the shared layout, not another 8.
    const int pet_right = SCENE_P3_PET_NAME_X -
        (SCENE_P3_PET_BACK_X + SCENE_P3_PET_BACK_SIZE);
    if (pose.pet_dx > pet_right) pose.pet_dx = (int16_t)pet_right;
    if (pose.pet_dx < -SCENE_P3_PET_BACK_X) pose.pet_dx = -SCENE_P3_PET_BACK_X;
    if (pose.wild_dx > 8) pose.wild_dx = 8; // wild rectangle has an 8 px right margin
    return pose;
}

static int16_t bounded_dx(int16_t dx, battle_fx_rect_t bounds, int left, int right)
{
    if (bounds.w <= 0 || bounds.h <= 0 || bounds.x < left ||
        bounds.x + bounds.w > right) return 0;
    int low = left - bounds.x, high = right - bounds.x - bounds.w;
    if (dx < low) return (int16_t)low;
    if (dx > high) return (int16_t)high;
    return dx;
}

battle_fx_pose_t battle_fx_pose_for_rects(const battle_round_t *round, uint8_t frame,
                                         battle_fx_rect_t pet, battle_fx_rect_t wild)
{
    battle_fx_pose_t pose = battle_fx_pose(round, frame);
    pose.pet_dx = bounded_dx(pose.pet_dx, pet, 0, SCENE_P3_PET_NAME_X);
    pose.wild_dx = bounded_dx(pose.wild_dx, wild, SCENE_P3_WILD_HUD_RIGHT, SCREEN_W);
    return pose;
}

typedef struct {
    int band_y;
    const uint16_t *palette;
} draw_ctx_t;

// Effects overlay the entire battlefield, including both HUDs. The message
// window starts at y=240 and remains readable. Each band is rebuilt from the
// clean scene before this pass, so recovery restores sprites and HUD together.
static void effect_px(const draw_ctx_t *ctx, int px, int py, unsigned shade)
{
    if (px < 0 || px >= SCREEN_W || py < 0 || py >= 240 ||
        py < ctx->band_y || py >= ctx->band_y + SCREEN_BAND_H) return;
    screen_px(px, py - ctx->band_y, ctx->palette[shade]);
}

// There is no clipping of the original sprite asset: this guard protects screen
// regions only. Skipping source rows outside the active band keeps work bounded.
static void stamp(const draw_ctx_t *ctx, unsigned art_id, int cx, int cy,
                   int scale, unsigned flips)
{
    if (art_id >= FX_ART_COUNT || scale < 1 || scale > 3) return;
    const battle_fx_art_t *art = &FX_ART[art_id];
    int left = cx - art->w * scale / 2, top = cy - art->h * scale / 2;
    int stride = (art->w + 3) / 4;
    for (int y = 0; y < art->h; y++) {
        int dy = top + y * scale;
        if (dy + scale <= ctx->band_y || dy >= ctx->band_y + SCREEN_BAND_H) continue;
        int sy = (flips & 2u) ? art->h - 1 - y : y;
        for (int x = 0; x < art->w; x++) {
            int sx = (flips & 1u) ? art->w - 1 - x : x;
            unsigned shade = (art->data[sy * stride + sx / 4] >> (6 - 2 * (sx % 4))) & 3u;
            if (shade == 3u) continue;
            for (int yy = 0; yy < scale; yy++) {
                int py = dy + yy;
                for (int xx = 0; xx < scale; xx++) {
                    int px = left + x * scale + xx;
                    effect_px(ctx, px, py, shade);
                }
            }
        }
    }
}

static int lerp(int from, int to, int t)
{
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    return from + (to - from) * t / 256;
}

static void projectile(const draw_ctx_t *ctx, unsigned art, int ax, int ay,
                        int dx, int dy, int phase, int delay, int arc,
                        int scale, unsigned flip)
{
    int age = phase - 2 - delay;
    if (age < 0 || age > 4) return;
    static const int8_t ARC[5] = {0, 3, 4, 3, 0};
    int t = age * 64;
    stamp(ctx, art, lerp(ax, dx, t), lerp(ay, dy, t) - ARC[age] * arc, scale, flip);
}

void battle_fx_draw_band(const battle_round_t *round, uint8_t frame, int band_y,
                         battle_fx_rect_t pet, battle_fx_rect_t wild)
{
    if (gold_fx_enabled(round)) {gold_fx_draw(round,frame,band_y,0,0);return;}
    if (!round || band_y < 0 || band_y >= 240 ||
        pet.w <= 0 || pet.h <= 0 || wild.w <= 0 || wild.h <= 0) return;
    int phase = phase_of(round, frame);
    if (phase>=10) return;
    // An immune target can see the attempted projectile, never a damage burst.
    // Zero damage also includes recovery, buffs, barriers and status moves.
    battle_fx_style_t style = battle_fx_style(round);
    unsigned type = round->move_type < BATTLE_TYPE_COUNT ? round->move_type : TY_NORMAL;
    if (style == BATTLE_FX_STARS) type = TY_ELECTRIC;
    if (style == BATTLE_FX_SONIC_BOOM) type = TY_FLYING;
    draw_ctx_t ctx = {band_y, TYPE_PAL[type]};
    battle_fx_pose_t pose = battle_fx_pose_for_rects(round, frame, pet, wild);
    pet.x += pose.pet_dx;
    wild.x += pose.wild_dx;
    battle_fx_rect_t atk = round->by_pet ? pet : wild;
    battle_fx_rect_t def = round->by_pet ? wild : pet;
    int ax = atk.x + atk.w / 2, ay = atk.y + atk.h / 2;
    int dx = def.x + def.w / 2, dy = def.y + def.h / 2;
    unsigned flip = round->by_pet ? 0u : 1u;
    bool impact = phase >= 6 && phase <= 9 && round->damage > 0;
    move_t move;bool status_move=combat_move(round->move_id,&move)&&!move.power;
    if(round->self_target||round->charging||round->skipped||status_move){
        int cx=round->self_target||round->charging||round->skipped?ax:dx;
        int cy=round->self_target||round->charging||round->skipped?ay:dy;
        if(phase<1||phase>9)return;
        if(round->no_effect||round->missed){
            for(int k=-5;k<=5;k++){effect_px(&ctx,cx+k,cy+k,0);effect_px(&ctx,cx+k,cy-k,0);}return;
        }
        static const int8_t ring[8][2]={{0,-8},{6,-6},{8,0},{6,6},{0,8},{-6,6},{-8,0},{-6,-6}};
        static const uint16_t green[3]={0x0200,0x3ea7,0xaff5},blue[3]={0x001f,0x6dff,0xffff};
        bool recovery=round->healed||round->move_id==105||round->move_id==135||round->move_id==156;
        bool barrier=round->move_id==113||round->move_id==115||round->move_id==112||round->move_id==54||round->move_id==164;
        if(recovery)ctx.palette=green;else if(barrier||round->charging)ctx.palette=blue;
        int radius=1+(phase%4);
        for(unsigned n=0;n<8;n++){
            unsigned at=(n+phase)%8;int x=cx+ring[at][0]*radius,y=cy+ring[at][1]*radius;
            if(recovery){y-=phase*2;for(int k=-3;k<=3;k++){effect_px(&ctx,x+k,y,1);effect_px(&ctx,x,y+k,1);}}
            else for(int yy=0;yy<3;yy++)for(int xx=0;xx<3;xx++)effect_px(&ctx,x+xx,y+yy,n%2);
        }
        if(barrier){int w=12+phase*2,h=20+phase;for(int k=-w;k<=w;k++){effect_px(&ctx,cx+k,cy-h,1);effect_px(&ctx,cx+k,cy+h,1);}for(int k=-h;k<=h;k++){effect_px(&ctx,cx-w,cy+k,1);effect_px(&ctx,cx+w,cy+k,1);}}
        else if(round->self_target&&!recovery&&!round->charging){for(int k=0;k<9;k++){int y=cy+12-phase*3+k;effect_px(&ctx,cx,y,1);if(k<5){effect_px(&ctx,cx-k,y,1);effect_px(&ctx,cx+k,y,1);}}}
        return;
    }
    if((round->no_effect||!round->damage||round->missed)&&phase>=6)return;
    if(impact&&round->hits>1){int offset=((phase-6)%round->hits)*5-7;stamp(&ctx,FX_ART_HIT,dx+offset,dy-offset,1,flip);}


    switch (style) {
    case BATTLE_FX_IMPACT: case BATTLE_FX_LUNGE:
        if (impact) stamp(&ctx, phase < 8 ? FX_ART_HIT_BIG : FX_ART_HIT, dx, dy, 2, flip);
        break;
    case BATTLE_FX_FIGHTING: case BATTLE_FX_PALM: case BATTLE_FX_KICK:
        if (impact) {
            unsigned art = style == BATTLE_FX_PALM ? FX_ART_PALM
                : (style == BATTLE_FX_KICK ? FX_ART_KICK : FX_ART_PUNCH);
            if (phase & 1) art = FX_ART_HIT;
            stamp(&ctx, art, dx + (phase - 7) * 4, dy - (phase - 7) * 5, 2, flip);
        }
        break;
    case BATTLE_FX_FIRE: case BATTLE_FX_DRAGON:
        for (int i = 0; i < 3; i++) {
            projectile(&ctx, (phase + i) & 1 ? FX_ART_FIRE0 : FX_ART_FIRE1,
                       ax, ay + i * 5, dx, dy - i * 4, phase, i, style == BATTLE_FX_DRAGON ? 5 : 2, 2, flip);
            if (impact) stamp(&ctx, (phase + i) & 1 ? FX_ART_FIRE0 : FX_ART_FIRE1,
                              dx - 12 + i * 12, dy + 12 - (phase - 6) * 8 - i * 3, 2, flip);
        }
        break;
    case BATTLE_FX_WATER:
        for (int i = 0; i < 3; i++) projectile(&ctx, FX_ART_WATER0, ax, ay, dx, dy + i * 4, phase, i, 0, 2, flip);
        if (impact) stamp(&ctx, FX_ART_WATER1, dx, dy - (phase - 6) * 3, 2, flip);
        break;
    case BATTLE_FX_ELECTRIC:
        if (phase <= 2) stamp(&ctx, FX_ART_CHARGE, ax, ay - 8, 2, flip);
        if (phase >= 4 && round->damage) {
            stamp(&ctx, FX_ART_CORE, dx + (phase & 1 ? 3 : -3), dy, 1, flip);
            if (phase >= 6) stamp(&ctx, phase & 1 ? FX_ART_SPARK1 : FX_ART_SPARK0, dx, dy, 2, flip);
        }
        break;
    case BATTLE_FX_LEAF: case BATTLE_FX_STARS:
        for (int i = 0; i < 3; i++) projectile(&ctx,
            style == BATTLE_FX_STARS ? FX_ART_STAR : ((phase + i) & 1 ? FX_ART_LEAF1 : FX_ART_LEAF0),
            ax, ay, dx, dy + (i - 1) * 10, phase, i, (i - 1) * 3, 2, flip ^ (unsigned)(phase & 1));
        break;
    case BATTLE_FX_ICE:
        if (phase >= 2) for (int i = 0; i < 5; i++) {
            int fall = phase - 2 - i / 2;
            if (fall >= 0) stamp(&ctx, FX_ART_ICE, dx + (i - 2) * 9,
                                  dy - 34 + fall * 6 + (i & 1) * 5, 2, (unsigned)(i & 1));
        }
        break;
    case BATTLE_FX_POISON:
        for (int i = 0; i < 3; i++) projectile(&ctx, FX_ART_POISON, ax, ay, dx + (i - 1) * 8, dy, phase, i, 5, 2, flip);
        if (impact) for (int i = 0; i < 3; i++) stamp(&ctx, FX_ART_POISON, dx + (i - 1) * 12, dy + 10 - (phase & 1) * 4, 2, flip);
        break;
    case BATTLE_FX_GROUND: case BATTLE_FX_ROCK:
        if (phase >= 2) for (int i = 0; i < 3; i++) {
            int t = (phase - 2 - i) * 64;
            if (t < 0) continue;
            int y = style == BATTLE_FX_ROCK ? lerp(40, dy, t) : lerp(dy + 26, dy - 6, t);
            stamp(&ctx, i == 1 ? FX_ART_BOULDER : FX_ART_ROCK,
                  dx + (i - 1) * 14, y + (i & 1) * 5, i == 1 ? 1 : 2, (unsigned)(i & 1));
        }
        break;
    case BATTLE_FX_WIND:
        for (int i = 0; i < 4; i++) projectile(&ctx, (phase + i) & 1 ? FX_ART_WIND1 : FX_ART_WIND0,
            ax, ay + (i - 2) * 5, dx, dy + (i - 2) * 6, phase, i / 2, 0, 2, flip ^ (unsigned)(i & 1));
        break;
    case BATTLE_FX_PSYCHIC:
        for (int i = 0; i < 3; i++) projectile(&ctx, phase & 1 ? FX_ART_WAVE0 : FX_ART_WAVE1,
            ax, ay, dx, dy, phase, i, 0, 2, flip);
        break;
    case BATTLE_FX_BUG: case BATTLE_FX_NEEDLE: case BATTLE_FX_HORN:
        for (int i = 0; i < (style == BATTLE_FX_HORN && round->move_id != 31 ? 1 : 3); i++)
            projectile(&ctx, style == BATTLE_FX_HORN ? FX_ART_HORN : FX_ART_NEEDLE,
                       ax, ay, dx, dy + (i - 1) * 8, phase, i, style == BATTLE_FX_BUG ? -2 : 0, 2, flip);
        if (impact) stamp(&ctx, FX_ART_HIT, dx, dy, 1, flip);
        break;
    case BATTLE_FX_GHOST:
        if (phase >= 3) {
            int spread = (9 - phase) * 4;
            stamp(&ctx, FX_ART_WAVE0, dx - spread, dy - 10, 1, 1);
            stamp(&ctx, FX_ART_WAVE0, dx + spread, dy + 10, 1, 0);
            if (phase >= 6) stamp(&ctx, FX_ART_CORE, dx, dy, 1, 0);
        }
        break;
    case BATTLE_FX_CUT: case BATTLE_FX_MULTICUT:
        if (phase >= 4) {
            int count = style == BATTLE_FX_MULTICUT ? 3 : 2;
            for (int i = 0; i < count; i++) stamp(&ctx,
                round->move_id == 163 ? FX_ART_CUT_LONG : FX_ART_CUT,
                dx - 9 + i * 9, dy - 16 + (phase - 4) * 6 + i * 4, 2, flip ^ (unsigned)(phase & 1));
        }
        break;
    case BATTLE_FX_BIND:
        if (phase >= 3) for (int i = 0; i < 3; i++)
            stamp(&ctx, FX_ART_BIND, dx, dy + (i - 1) * (phase < 7 ? 12 : 7), 2, flip);
        break;
    case BATTLE_FX_BITE:
        if (phase >= 3) {
            int gap = phase < 6 ? (6 - phase) * 5 : 2;
            stamp(&ctx, FX_ART_FANG, dx, dy - gap, 2, flip);
            stamp(&ctx, FX_ART_FANG, dx, dy + gap, 2, flip | 2u);
        }
        break;
    case BATTLE_FX_DRAIN:
        for (int i = 0; i < 4; i++) projectile(&ctx, round->move_id == 141 ? FX_ART_BUBBLE : FX_ART_CHARGE,
            dx, dy + (i - 2) * 6, ax, ay, phase, i, (i & 1) ? 3 : -3, 2, flip);
        break;
    case BATTLE_FX_BUBBLE:
        for (int i = 0; i < 3; i++) projectile(&ctx, FX_ART_BUBBLE,
            ax, ay, dx + (i - 1) * 10, dy, phase, i, 4, 2, flip);
        break;
    case BATTLE_FX_THUNDER:
        if (phase >= 3) {
            int side = phase < 5 ? -10 : (phase < 7 ? 10 : 0);
            stamp(&ctx, FX_ART_THUNDER, dx + side, dy - 12, 1, (unsigned)(phase < 7));
        }
        break;
    case BATTLE_FX_WHIP:
        if(round->move_id==231&&phase>=4&&phase<=9){stamp(&ctx,FX_ART_CUT_LONG,dx+(phase-6)*5,dy,2,flip);if(impact)stamp(&ctx,FX_ART_HIT_BIG,dx,dy,2,flip);}
        if (phase >= 3) {
            stamp(&ctx, FX_ART_WHIP, lerp(ax, dx, (phase - 2) * 64), lerp(ay, dy, (phase - 2) * 64), 2, flip);
            if (impact) stamp(&ctx, FX_ART_HIT, dx, dy, 1, flip);
        }
        break;
    case BATTLE_FX_ORB:
        if(round->move_id==247){
            if(phase<4)stamp(&ctx,FX_ART_CORE,ax,ay,1,flip);
            if(phase>=3&&phase<=8){int t=(phase-3)*256/5;stamp(&ctx,FX_ART_CORE,lerp(ax,dx,t),lerp(ay,dy,t),2,flip);}
            if(impact){stamp(&ctx,FX_ART_HIT_BIG,dx,dy,2,flip);stamp(&ctx,FX_ART_ORB,dx-18,dy-12,3,flip);stamp(&ctx,FX_ART_ORB,dx+18,dy+12,3,flip);}
            break;
        }
        for (int i = 0; i < 2; i++) projectile(&ctx, FX_ART_ORB, ax, ay, dx, dy + i * 6, phase, i * 2, 3, 1, flip);
        break;
    case BATTLE_FX_LICK:
        if (phase >= 3 && phase < 8) stamp(&ctx, FX_ART_LICK, dx, dy + 8 - (phase - 3) * 3, 1, flip);
        break;
    case BATTLE_FX_SONIC_BOOM:
        // Crystal uses Gust objects at the target. Three expanding pulses are
        // distinct from the ordinary wind projectile's cross-stage travel.
        if (phase >= 2) {
            int radius = 6 + ((phase - 2) % 3) * 9;
            for (int i = -1; i <= 1; i += 2) {
                stamp(&ctx, phase & 1 ? FX_ART_WIND0 : FX_ART_WIND1,
                      dx + i * radius, dy - radius / 2, 2, i < 0 ? 1u : 0u);
                stamp(&ctx, phase & 1 ? FX_ART_WIND1 : FX_ART_WIND0,
                      dx + i * radius, dy + radius / 2, 2, i < 0 ? 1u : 0u);
            }
            if (impact && (phase & 1)) stamp(&ctx, FX_ART_HIT, dx, dy, 1, flip);
        }
        break;
    case BATTLE_FX_SEISMIC_TOSS:
        // Original Globe OAM: a short lift/arc/slam, followed by an impact.
        if (phase >= 1 && phase <= 6) {
            static const int8_t lift[] = {0, 12, 28, 42, 32, 14};
            int t = (phase - 1) * 256 / 5;
            stamp(&ctx, FX_ART_GLOBE, lerp(ax, dx, t),
                  lerp(ay, dy, t) - lift[phase - 1], 1, flip);
        }
        if (impact) stamp(&ctx, FX_ART_HIT_BIG, dx, dy + (phase & 1 ? 3 : -3), 2, flip);
        break;
    case BATTLE_FX_DRAGON_RAGE:
        for (int i = 0; i < 4; i++)
            projectile(&ctx, FX_ART_DRAGON_RAGE, ax, ay, dx, dy + (i - 1) * 6,
                       phase, i / 2, (i & 1) ? 7 : -5, 2, flip ^ (unsigned)(i & 1));
        if (impact) stamp(&ctx, FX_ART_DRAGON_RAGE, dx, dy - (phase - 6) * 5,
                          phase & 1 ? 2 : 3, flip);
        break;
    case BATTLE_FX_NIGHT_SHADE:
        // The original is a background/palette oscillation, not a unique
        // sprite. Adapt it as moving shade bands inside the target rectangle.
        if (phase >= 2) {
            int top = def.y > band_y ? def.y : band_y;
            int bottom = def.y + def.h < band_y + SCREEN_BAND_H
                ? def.y + def.h : band_y + SCREEN_BAND_H;
            for (int y = top; y < bottom; y++) {
                int stripe = (y - def.y + phase * 5) % 16;
                if (stripe >= 3) continue;
                int inset = (phase & 1) ? 3 : 0;
                for (int x = def.x + inset; x < def.x + def.w - inset; x++)
                    effect_px(&ctx, x, y, (unsigned)(stripe & 1));
            }
            if (impact && (phase & 1)) stamp(&ctx, FX_ART_HIT, dx, dy, 1, flip);
        }
        break;
    case BATTLE_FX_SUPER_FANG:
        if (phase >= 2) {
            int pulse = (phase - 2) % 3;
            int gap = 14 - pulse * 6;
            stamp(&ctx, FX_ART_FANG, dx, dy - gap, 2, flip);
            stamp(&ctx, FX_ART_FANG, dx, dy + gap, 2, flip | 2u);
            if (impact && pulse == 2) stamp(&ctx, FX_ART_HIT_BIG, dx, dy, 1, flip);
        }
        break;
    case BATTLE_FX_HYDRO_PUMP:
        // Original pump column grows through three OAM frames at the target;
        // the incoming water marks its direction on this taller battle stage.
        for (int i = 0; i < 3; i++)
            projectile(&ctx, FX_ART_WATER0, ax, ay, dx, dy + def.h / 4,
                       phase, i, 0, 2, flip);
        if (phase >= 4) {
            unsigned art = phase < 5 ? FX_ART_HYDRO0 : phase < 6 ? FX_ART_HYDRO1 : FX_ART_HYDRO2;
            stamp(&ctx, art, dx + (phase & 1 ? 3 : -3), dy, 1, flip);
            if (impact) stamp(&ctx, FX_ART_WATER1, dx, dy - def.h / 4, 2, flip);
        }
        break;
    case BATTLE_FX_HYPER_BEAM:
        if (phase < 3) stamp(&ctx, FX_ART_CHARGE, ax, ay, phase + 1, flip);
        if (phase >= 3) {
            int end = phase < 6 ? (phase - 2) * 2 : 8;
            for (int i = 1; i <= end; i++)
                stamp(&ctx, i == end ? FX_ART_BEAM_TIP : FX_ART_BEAM,
                      lerp(ax, dx, i * 32), lerp(ay, dy, i * 32), 2, flip);
            if (impact) stamp(&ctx, FX_ART_HIT_BIG, dx, dy, phase & 1 ? 1 : 2, flip);
        }
        break;
    case BATTLE_FX_EXPLOSION:
        if (phase >= 1) {
            unsigned art = phase < 3 ? FX_ART_EXPLOSION0
                : phase < 5 ? FX_ART_EXPLOSION1 : FX_ART_EXPLOSION2;
            stamp(&ctx, art, ax, ay, phase < 5 ? 1 : 2, flip);
            if (impact) {
                stamp(&ctx, art, dx, dy, 2, flip);
                int spread = (phase - 5) * 9;
                stamp(&ctx, FX_ART_EXPLOSION0, dx - spread, dy - spread / 2, 2, flip);
                stamp(&ctx, FX_ART_EXPLOSION0, dx + spread, dy + spread / 2, 2, flip);
            }
        }
        break;
    default:
        break;
    }
}

void battle_fx_draw_scene_band(const battle_round_t *round,uint8_t frame,int band_y,
                               battle_fx_rect_t pet,battle_fx_rect_t wild,
                               const battle_fx_actor_t *pet_art,const battle_fx_actor_t *wild_art) {
    if(gold_fx_enabled(round))gold_fx_draw(round,frame,band_y,pet_art,wild_art);
    else battle_fx_draw_band(round,frame,band_y,pet,wild);
}

uint8_t battle_fx_hit_frame(const battle_round_t *round) {
    if(gold_fx_enabled(round))return gold_fx_frame_count(round)-(round->damage?14:2);
    return battle_presentation_hit_frame(battle_fx_frames(round));
}
