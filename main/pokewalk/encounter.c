// main/encounter.c —— S1 遭遇 + S5 图鉴 + S8 闪光。
//
// PC 侧 sim/systems.py + sim/state.py。整块确定性，逐值对账
// （tools/pipeline/verify_encounter.py）。
//
// ## 为什么种子是 crc32(bssid|小时) 而不是随机数
//
// 确定性刷新是设计要求（docs/03-spawning.md#36），不是实现偷懒：
// 两人站一起看到同一批怪、社区能做「这个 AP 下午三点出火系」的攻略、
// 不用存刷新表随时能重算。用随机数这三条全没了。
//
// ## 字符串拼接必须与 PC 侧逐字节一致
//
// PC 侧算的是 `f"{bssid}|{ts//3600}"` 的 UTF-8 字节，
// 其中 bssid 是 "aa:bb:cc:dd:ee:ff" 这种小写冒号分隔的**字符串**，
// 不是 6 字节原始数组。这里必须先格式化再算 crc ——
// 直接对 6 字节算会得到完全不同的值，而且两边都「能跑」，
// 只是刷出来的怪不一样（sensing.c 的 hash_bssid 踩过同一个坑）。

#include <stdio.h>
#include <string.h>

#include "encounter.h"
#include "assets.h"

#ifdef HOST_BUILD
#include <zlib.h>
#define CRC32(buf, len) ((uint32_t)crc32(0UL, (const unsigned char *)(buf), (len)))
#else
#include "esp_rom_crc.h"
#define CRC32(buf, len) esp_rom_crc32_le(0U, (const uint8_t *)(buf), (len))
#endif

#define TIME_BUCKET 3600      // 一小时一换

// 强度档：种族值总和的区间。实测 151 只的分布是
// 最低 175（绿毛虫）中位 345 最高 590（超梦），四分位 275/345/420。
// 各档物种数 18/46/37/42/8，都不为空。
static const uint16_t TIER_LO[5] = {0, 250, 320, 400, 480};
static const uint16_t TIER_HI[5] = {250, 320, 400, 480, 9999};

// 把 6 字节 BSSID 写成 PC 侧那种小写冒号分隔的 17 字符串。
static int fmt_bssid(const uint8_t b[6], char *out)
{
    return snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
                    b[0], b[1], b[2], b[3], b[4], b[5]);
}

uint32_t enc_spawn_seed(const uint8_t bssid[6], uint32_t ts)
{
    char key[48];
    char mac[18];
    fmt_bssid(bssid, mac);
    int n = snprintf(key, sizeof(key), "%s|%u", mac,
                     (unsigned)(ts / TIME_BUCKET));
    return CRC32(key, (size_t)n);
}

uint16_t enc_shiny_denominator(shiny_source_t source)
{
    static const uint16_t denominators[SHINY_SOURCE_COUNT] = {64, 48, 32, 16};
    return (unsigned)source < SHINY_SOURCE_COUNT ? denominators[source] : 0;
}

bool enc_shiny_from_roll(uint32_t roll, shiny_source_t source)
{
    unsigned denominator = enc_shiny_denominator(source);
    return denominator && roll % denominator == 0;
}

bool enc_roll_shiny(const uint8_t bssid[6], uint32_t ts)
{
    // 独立 salt 而非复用 spawn_seed 的低位 —— 否则闪光判定与种类判定
    // 相关，某些种类会**永远不闪光**（那种 bug 要玩几个月才发现）。
    //
    // 独立哈希流，增加闪光机会但不改变已有遭遇/捕获个体的标记。
    char key[64];
    char mac[18];
    fmt_bssid(bssid, mac);
    int n = snprintf(key, sizeof(key), "%s|%u|shiny", mac,
                     (unsigned)(ts / TIME_BUCKET));
    return enc_shiny_from_roll(CRC32(key, (size_t)n), SHINY_PASSIVE);
}

uint8_t enc_rarity_from_ap(int8_t rssi, uint8_t auth, bool has_ssid,
                           bool is_transient)
{
    // 稀有度直接挂在 AP 属性上（docs/03-spawning.md#31）：
    // 信号弱、隐藏 SSID、企业级加密、转瞬即逝 —— 天然就是稀有刷新点。
    uint8_t r = 1;
    if (rssi < -80) r++;
    if (!has_ssid) r++;
    // ESP-IDF: 8 is WAPI, not enterprise. Include WPA/WPA2/WPA3 EAP modes.
    if (auth == 5 || auth == 10 || auth == 14 || auth == 15 || auth == 16) r++;
    if (is_transient) r++;
    return r > 5 ? 5 : r;
}

uint16_t enc_pick_species(const uint8_t bssid[6], uint32_t ts, uint8_t rarity)
{
    // 按稀有度取物种池，**不是均匀采样 151 只**。
    //
    // 均匀采样的后果是实测出来的：Lv12 的初期主宠会遇到鸭嘴火兽
    // （种族值 395）甚至超梦，打 46 回合都赢不了。
    // 原版靠「不同区域不同等级带」解决，本项目没有地图，
    // 改用 AP 稀有度决定物种池 —— 稀有 AP 才出强种。
    uint8_t idx = (rarity >= 1 && rarity <= 5) ? (uint8_t)(rarity - 1) : 0;
    uint16_t lo = TIER_LO[idx], hi = TIER_HI[idx];

    // 先数池子大小，再取第 k 只。**两趟扫描而不是先收集到数组** ——
    // 数组要 151×2 字节，而这个函数每次遭遇才调一次，两趟不值一提。
    uint16_t n = 0;
    uint32_t total = assets_species_count();
    species_t sp;
    for (uint16_t id = 1; id <= total; id++) {
        if (!assets_species(id, &sp)) continue;
        uint16_t sum = (uint16_t)sp.hp + sp.attack + sp.defense
                     + sp.special + sp.speed;
        if (sum >= lo && sum < hi) n++;
    }
    if (n == 0) {
        // 池空则退回全表（与 sim 的 `pool or list(...)` 同）
        uint32_t seed = enc_spawn_seed(bssid, ts);
        return (uint16_t)((seed >> 8) % (total ? total : 1) + 1);
    }

    uint32_t seed = enc_spawn_seed(bssid, ts);
    uint16_t k = (uint16_t)((seed >> 8) % n);
    for (uint16_t id = 1; id <= total; id++) {
        if (!assets_species(id, &sp)) continue;
        uint16_t sum = (uint16_t)sp.hp + sp.attack + sp.defense
                     + sp.special + sp.speed;
        if (sum >= lo && sum < hi) {
            if (k == 0) return id;
            k--;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 队列
// ---------------------------------------------------------------------------

void enc_queue_init(enc_queue_t *q)
{
    memset(q, 0, sizeof(*q));
    q->next_uid = 1;          // 0 留作「无效/未选中」
}

encounter_t *enc_queue_find(enc_queue_t *q, uint16_t uid)
{
    if (!uid) return NULL;
    for (uint8_t i = 0; i < q->count; i++) {
        if (q->items[i].uid == uid) return &q->items[i];
    }
    return NULL;
}

bool enc_queue_take_uid(enc_queue_t *q, uint16_t uid, encounter_t *out)
{
    if (!uid) return false;
    for (uint8_t i = 0; i < q->count; i++) {
        if (q->items[i].uid != uid) continue;
        return enc_queue_take(q, i, out);
    }
    return false;             // 已被后台淘汰 —— 正常，不是错误
}

uint8_t enc_queue_trim(enc_queue_t *q)
{
    if (q->count <= ENC_QUEUE_LIMIT) return 0;
    uint8_t removed = q->count - ENC_QUEUE_LIMIT;
    memmove(q->items, q->items + removed, ENC_QUEUE_LIMIT * sizeof(q->items[0]));
    q->count = ENC_QUEUE_LIMIT;
    q->dropped += removed;
    memset(q->items + q->count, 0, (ENC_QUEUE_CAP - q->count) * sizeof(q->items[0]));
    return removed;
}

bool enc_queue_push(enc_queue_t *q, const encounter_t *e)
{
    // UIDs identify live entries even after FIFO shifts their indexes. Skip a
    // queued ID on wrap rather than making two entries share an identity.
    do {
        if (q->next_uid == 0) q->next_uid = 1;
        if (!enc_queue_find(q, q->next_uid)) break;
        q->next_uid++;
    } while (true);
    uint16_t uid = q->next_uid++;
    bool dropped = enc_queue_trim(q) != 0;
    if (q->count == ENC_QUEUE_LIMIT) {
        enc_queue_take(q, 0, NULL);
        q->dropped++;
        dropped = true;
    }
    q->items[q->count] = *e;
    q->items[q->count].uid = uid;
    q->items[q->count].exp_granted = false;
    q->count++;
    return dropped;
}

bool enc_queue_take(enc_queue_t *q, uint8_t index, encounter_t *out)
{
    if (index >= q->count) return false;
    if (out) *out = q->items[index];
    for (uint8_t k = index; k + 1 < q->count; k++) q->items[k] = q->items[k + 1];
    q->count--;
    return true;
}

// ---------------------------------------------------------------------------
// 图鉴
// ---------------------------------------------------------------------------

static void bit_set(uint8_t *bm, uint16_t sid)
{
    if (sid < 1 || sid > DEX_SPECIES) return;
    uint16_t i = (uint16_t)(sid - 1);
    bm[i >> 3] |= (uint8_t)(1u << (i & 7));
}

static bool bit_get(const uint8_t *bm, uint16_t sid)
{
    if (sid < 1 || sid > DEX_SPECIES) return false;
    uint16_t i = (uint16_t)(sid - 1);
    return (bm[i >> 3] >> (i & 7)) & 1;
}

static uint16_t bit_count(const uint8_t *bm)
{
    uint16_t n = 0;
    for (int i = 0; i < DEX_BYTES; i++) {
        uint8_t b = bm[i];
        while (b) { n += b & 1; b >>= 1; }
    }
    return n;
}

void dex_init(dex_t *d) { memset(d, 0, sizeof(*d)); }

void dex_mark_seen(dex_t *d, uint16_t sid, bool shiny)
{
    bit_set(d->seen, sid);
    if (shiny) bit_set(d->shiny_seen, sid);
}

void dex_mark_caught(dex_t *d, uint16_t sid, bool shiny)
{
    // 抓到必然见过 —— 两个位都置，与 sim 的 mark_caught 一致
    bit_set(d->seen, sid);
    bit_set(d->caught, sid);
    if (shiny) {
        bit_set(d->shiny_seen, sid);
        bit_set(d->shiny_caught, sid);
    }
}

bool dex_is_seen(const dex_t *d, uint16_t sid) { return bit_get(d->seen, sid); }
bool dex_is_caught(const dex_t *d, uint16_t sid) { return bit_get(d->caught, sid); }
bool dex_is_shiny_caught(const dex_t *d, uint16_t sid)
{
    return bit_get(d->shiny_caught, sid);
}
uint16_t dex_count_caught(const dex_t *d) { return bit_count(d->caught); }
uint16_t dex_count_seen(const dex_t *d) { return bit_count(d->seen); }

// ---------------------------------------------------------------------------

bool enc_selftest(void)
{
    bool ok = true;

    // ① FIFO always keeps the newest five, including a low-rarity arrival.
    enc_queue_t q;
    enc_queue_init(&q);
    encounter_t e = {.rarity = 5, .species_id = 150, .ts = 1};
    enc_queue_push(&q, &e);
    uint16_t expired = q.items[0].uid;
    for (int i = 0; i < ENC_QUEUE_LIMIT; i++) {
        e.rarity = 1; e.species_id = (uint16_t)(10 + i); e.ts = (uint32_t)(2 + i);
        bool dropped = enc_queue_push(&q, &e);
        if (dropped != (i == ENC_QUEUE_LIMIT - 1)) ok = false;
    }
    if (q.count != ENC_QUEUE_LIMIT || q.dropped != 1 ||
        q.items[0].species_id != 10 || q.items[q.count - 1].species_id != 14 ||
        enc_queue_find(&q, expired)) {
        printf("encounter: FIFO did not keep the newest five\n");
        ok = false;
    }

    // ①b A retained uid follows its encounter as earlier entries are removed.
    uint16_t watched = q.items[2].uid;
    enc_queue_take(&q, 0, NULL);
    encounter_t *found = enc_queue_find(&q, watched);
    encounter_t taken;
    if (!found || found != &q.items[1] || found->species_id != 12 ||
        !enc_queue_take_uid(&q, watched, &taken) || taken.species_id != 12) {
        printf("encounter: shifted uid no longer identifies its encounter\n");
        ok = false;
    }

    // Wrap must never collide with a live uid or issue reserved uid zero.
    q.next_uid = q.items[0].uid;
    enc_queue_push(&q, &e);
    for (uint8_t i = 0; i < q.count; i++) {
        if (!q.items[i].uid) ok = false;
        for (uint8_t j = 0; j < i; j++)
            if (q.items[i].uid == q.items[j].uid) ok = false;
    }

    // 取一个不存在的 uid 应当安静地失败，不能误伤别人
    uint8_t before_n = q.count;
    if (enc_queue_take_uid(&q, 60000, NULL)) {
        printf("encounter: 不存在的 uid 竟然取成功了\n");
        ok = false;
    }
    if (q.count != before_n) {
        printf("encounter: 取失败却改了队列长度\n");
        ok = false;
    }

    // ② 取走
    enc_queue_init(&q);
    e.rarity = 5; e.species_id = 150; e.ts = 1;
    enc_queue_push(&q, &e);
    for (int i = 0; i < 3; i++) {
        e.rarity = 1; e.species_id = (uint16_t)(10 + i); e.ts = (uint32_t)(2 + i);
        enc_queue_push(&q, &e);
    }
    encounter_t got;
    uint8_t before = q.count;
    if (!enc_queue_take(&q, 0, &got) || q.count != before - 1) {
        printf("encounter: take 之后长度不对\n");
        ok = false;
    }

    // ③ 图鉴位图
    dex_t d;
    dex_init(&d);
    dex_mark_seen(&d, 25, false);
    dex_mark_caught(&d, 1, true);
    if (!dex_is_seen(&d, 25) || dex_is_caught(&d, 25)) {
        printf("encounter: seen/caught 分不开\n");
        ok = false;
    }
    if (!dex_is_caught(&d, 1) || !dex_is_seen(&d, 1) ||
        !dex_is_shiny_caught(&d, 1)) {
        printf("encounter: 抓到应当同时置 seen 与 shiny\n");
        ok = false;
    }
    if (dex_count_seen(&d) != 2 || dex_count_caught(&d) != 1) {
        printf("encounter: 计数 seen=%u caught=%u，期望 2/1\n",
               dex_count_seen(&d), dex_count_caught(&d));
        ok = false;
    }
    // 边界：1 与 151 都要能存（位图 19 字节 = 152 位，末位不能越界）
    dex_mark_caught(&d, 151, false);
    if (!dex_is_caught(&d, 151)) {
        printf("encounter: #151 存不进去 —— 位图边界错了\n");
        ok = false;
    }
    dex_mark_caught(&d, 152, false);      // 越界应当被忽略而不是踩内存
    if (dex_count_caught(&d) != 2) {
        printf("encounter: #152 越界没被挡住\n");
        ok = false;
    }

    if (ok) {
        printf("encounter: 自检 全部通过"
               "（淘汰规则 · uid 稳定 · 取走 · 图鉴位图 · 边界）\n");
    }
    return ok;
}

// Durable, device-local refresh planning. Unlike the legacy pure AP/hour
// functions, serial and exploration history deliberately diversify each visit.
#include "encounter_refresh.h"

static uint32_t refresh_key(const uint8_t bssid[6])
{
    uint32_t key = CRC32(bssid, 6);
    return key ? key : 1;
}
static int refresh_history(const enc_refresh_state_t *s, uint32_t key)
{
    for (unsigned i=0;i<s->history_count;i++) if(s->history[i].key==key)return (int)i;
    return -1;
}
static bool refresh_species_present(const enc_queue_t *q, uint16_t species)
{
    for(unsigned i=0;i<q->count;i++) if(q->items[i].species_id==species)return true;
    return false;
}
typedef struct {int selected,history;unsigned replacement;} refresh_choice_t;
static refresh_choice_t refresh_select(const enc_refresh_state_t *s,const enc_refresh_ap_t *aps,unsigned n)
{
    int selected=-1, history=-1;
    unsigned replacement=s->history_count;
    if(replacement==ENC_REFRESH_HISTORY) {
        replacement=0;
        for(unsigned i=1;i<ENC_REFRESH_HISTORY;i++)
            if(s->history[i].last_s<s->history[replacement].last_s)replacement=i;
    }
    bool room=s->history_count<ENC_REFRESH_HISTORY ||
        s->online_s-s->history[replacement].last_s>=ENC_AP_COOLDOWN_S;
    uint32_t oldest=UINT32_MAX;
    // Prefer unvisited APs. Stable rotation avoids always selecting strongest RSSI.
    for(unsigned k=0;k<n;k++) {
        unsigned i=(k+s->serial%n)%n;
        int h=refresh_history(s,refresh_key(aps[i].bssid));
        if(h<0) {if(room){selected=(int)i;history=-1;break;}else continue;}
        uint32_t last=s->history[h].last_s;
        if(s->online_s-last>=ENC_AP_COOLDOWN_S && last<oldest) {
            selected=(int)i;history=h;oldest=last;
        }
    }
    return (refresh_choice_t){selected,history,replacement};
}
static bool refresh_one(enc_refresh_state_t *s, const enc_refresh_ap_t *aps,
    unsigned n, bool hunt, enc_queue_t *q, dex_t *dex, uint16_t active_uid)
{
    refresh_choice_t choice=refresh_select(s,aps,n);
    int selected=choice.selected,history=choice.history;
    unsigned replacement=choice.replacement;
    if(selected<0)return false;
    const enc_refresh_ap_t *ap=&aps[selected];
    uint32_t previous_discoveries=s->discoveries;
    if(hunt && history<0 && s->discoveries<UINT32_MAX)s->discoveries++;
    unsigned bonus=s->discoveries/10;
    if(bonus>10)bonus=10;
    uint32_t seed=enc_spawn_seed(ap->bssid,s->serial*3600u);
    unsigned roll=(seed>>12)%1000;
    uint8_t rarity=enc_rarity_from_ap(ap->rssi,ap->auth,ap->has_ssid,hunt);
    uint8_t extra=roll<10+2*bonus?5:roll<60+4*bonus?4:roll<250+10*bonus?3:1;
    if(extra>rarity)rarity=extra;
    if(hunt && s->since_elite>=29)rarity=5;
    else if(hunt && s->since_rare>=7 && rarity<4)rarity=4;
    encounter_t e={.ts=s->online_s,.rarity=rarity,.hp_ratio=100,.is_transient=hunt};
    // Reject all pending species, including entries about to be FIFO-evicted.
    // A tier has >=8 species, so the five-entry queue cannot exhaust its pool.
    bool found=false;
    for(unsigned attempt=0;attempt<256;attempt++) {
        uint32_t salt=(s->serial+attempt)*3600u;
        e.species_id=enc_pick_species(ap->bssid,salt,rarity);
        if(!refresh_species_present(q,e.species_id)) {
            e.is_shiny=enc_roll_shiny(ap->bssid,salt);found=true;break;
        }
    }
    if(!found) {s->discoveries=previous_discoveries;return false;}
    if(history<0) {
        history=(int)replacement;
        s->history_next=(s->history_next+1)%ENC_REFRESH_HISTORY;
        if(s->history_count<ENC_REFRESH_HISTORY)s->history_count++;
    }
    s->history[history]=(enc_ap_history_t){refresh_key(ap->bssid),s->online_s};
    if(hunt) {
        s->since_rare=rarity>=4?0:s->since_rare+1;
        s->since_elite=rarity>=5?0:s->since_elite+1;
    }
    s->serial++;
    while(!q->next_uid || q->next_uid==active_uid || enc_queue_find(q,q->next_uid))q->next_uid++;
    enc_queue_push(q,&e);dex_mark_seen(dex,e.species_id,e.is_shiny);
    return true;
}
uint8_t enc_refresh_scan(enc_refresh_state_t *s,const enc_refresh_ap_t *aps,
    unsigned n,bool exploring,uint16_t distance_q10,enc_queue_t *q,dex_t *dex,uint16_t active_uid)
{
    if(!s||!aps||!n||n>64||!q||!dex||!enc_refresh_valid(s))return 0;
    if(exploring) {
        unsigned credit=s->hunt_q10+distance_q10;
        s->hunt_q10=credit>ENC_HUNT_CREDIT_MAX?ENC_HUNT_CREDIT_MAX:credit;
    }
    uint8_t made=0;
    while(exploring && s->hunt_q10>=1024 && made<4) {
        if(!refresh_one(s,aps,n,true,q,dex,active_uid))break;
        s->hunt_q10-=1024;made++;
    }
    if((!s->base_started || s->online_s>=s->next_base_s) &&
        refresh_one(s,aps,n,false,q,dex,active_uid)) {
        s->base_started=1;
        s->next_base_s=s->online_s>UINT32_MAX-ENC_BASE_INTERVAL_S?UINT32_MAX:s->online_s+ENC_BASE_INTERVAL_S;
        made++;
    }
    return made;
}

// V11: radio earns banked opportunities. No Pokémon, dex bits or rarity pity
// are resolved until the player explores a selected route.
static bool refresh_collect_one(enc_refresh_state_t *s,const enc_refresh_ap_t *aps,unsigned n,bool hunt)
{
    refresh_choice_t c=refresh_select(s,aps,n);
    if(c.selected<0)return false;
    if(c.history<0) {
        if(hunt && s->discoveries<UINT32_MAX)s->discoveries++;
        c.history=(int)c.replacement;
        if(s->history_count<ENC_REFRESH_HISTORY)s->history_count++;
        s->history_next=(s->history_next+1)%ENC_REFRESH_HISTORY;
    }
    s->history[c.history]=(enc_ap_history_t){refresh_key(aps[c.selected].bssid),s->online_s};
    s->serial++;
    return true;
}
uint8_t enc_refresh_collect(enc_refresh_state_t *s,const enc_refresh_ap_t *aps,unsigned n,
    bool exploring,uint16_t distance_q10,uint8_t room)
{
    if(!s||!aps||!n||n>64||!enc_refresh_valid(s))return 0;
    if(exploring) {
        unsigned credit=s->hunt_q10+distance_q10;
        s->hunt_q10=credit>ENC_HUNT_CREDIT_MAX?ENC_HUNT_CREDIT_MAX:credit;
    }
    uint8_t made=0;
    // Movement has already been filtered by sensing. A completed supply meter
    // pays once even if the current AP is still cooling down; that cooldown
    // only limits new-place credit and the separate passive supply below.
    // Banked movement can also pay on the next stationary scan after spending.
    while(s->hunt_q10>=ENC_HUNT_CREDIT_STEP && made<4 && made<room) {
        if(!refresh_collect_one(s,aps,n,true))s->serial++;
        s->hunt_q10-=ENC_HUNT_CREDIT_STEP;made++;
    }
    if(made<room && (!s->base_started||s->online_s>=s->next_base_s) &&
        refresh_collect_one(s,aps,n,false)) {
        s->base_started=1;
        s->next_base_s=s->online_s>UINT32_MAX-ENC_BASE_INTERVAL_S?UINT32_MAX:s->online_s+ENC_BASE_INTERVAL_S;
        made++;
    }
    return made;
}

// Current runtime: observe new places without earning or banking a currency.
bool enc_refresh_observe(enc_refresh_state_t *s,const enc_refresh_ap_t *aps,unsigned n,
    bool moving,uint16_t distance_q10)
{
    if(!s||!aps||!n||n>64||!enc_refresh_valid(s))return false;
    s->hunt_q10=0; // Discard obsolete banked movement; never redeem it later.
    return moving&&distance_q10>0&&refresh_collect_one(s,aps,n,true);
}
