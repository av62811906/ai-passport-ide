// main/encounter.h —— S1 遭遇累积 + S5 图鉴 + S8 闪光。
//
// PC 侧是 sim/systems.py 的 EncounterQueue / EncounterAccumulator
// 与 sim/state.py 的 Dex。
//
// **这一整块是确定性的** —— 同一个 AP 在同一小时永远刷出同一只，
// 靠 crc32 而不是随机数。三个好处（docs/03-spawning.md#36）：
// 跨设备一致（两人站一起看到同一批怪，不需要服务器）、
// 可预测（「这个 AP 下午三点出火系」能做攻略）、
// 可重算（不用存刷新表）。
//
// 因此它能与 PC 侧**逐值对账**，不像战斗只能对分布。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Keep the physical V5 save layout unchanged; gameplay only exposes five.
#define ENC_QUEUE_CAP 16
#define ENC_QUEUE_LIMIT 5
#define DEX_SPECIES 151
#define DEX_BYTES ((DEX_SPECIES + 7) / 8)    // 19

// V19 provenance: 1..8 are badge exploration activities; 9 is map exploration.
#define ENC_ACTIVITY_EXPLORATION 9

// 一条待处理遭遇。定长 —— 固件不做动态分配。
typedef struct {
    // 稳定标识。**不能用队列下标认这一条** ——
    // 玩家在 P3/P4 期间后台还在往队列里塞，而队列满了会淘汰
    // 第一条，那会把后面的条目整体左移。
    //
    // 实测症状：打的是 #64，抓到的却是 #23；连打三轮只有第一轮
    // 真的捕获，另外两轮的 take 落在别的条目上，队列还越攒越多。
    uint16_t uid;
    uint8_t level;            // V16: fixed at discovery; zero only for legacy callers.
    uint8_t activity;         // V16: badges 1..8; V19: 9 map exploration; 0 passive/dungeon.

    uint32_t ts;
    uint16_t species_id;
    uint8_t rarity;           // 1=常见 5=极稀有
    uint8_t biome;
    uint8_t hp_ratio;         // 100 = 满血；打过之后降低，S2 用它算窗口
    bool is_shiny;
    bool is_transient;        // 猎场遭遇（瞬现 AP）还是基地遭遇
    bool exp_granted;         // 这条遭遇的战斗经验是否已经领取
} encounter_t;

typedef struct {
    encounter_t items[ENC_QUEUE_CAP];
    uint8_t count;
    uint16_t dropped;         // 统计：被挤掉了多少条
    uint16_t next_uid;        // 单调递增，0 保留作「无效」
} enc_queue_t;

// 图鉴四张位图。**四张而不是两张**：
// 「闪光遇到了但跑了」那份遗憾要能存下来（S5/S8）。
typedef struct {
    uint8_t seen[DEX_BYTES];
    uint8_t caught[DEX_BYTES];
    uint8_t shiny_seen[DEX_BYTES];
    uint8_t shiny_caught[DEX_BYTES];
} dex_t;

void enc_queue_init(enc_queue_t *q);

// 入队。最多五条，满后无条件淘汰第一条，再把新遭遇放到末尾。
// 返回是否发生了淘汰。
bool enc_queue_push(enc_queue_t *q, const encounter_t *e);

// Validated legacy V5 queues may contain up to 16 entries. Keep the newest five
// in their existing order; return how many entries were removed.
uint8_t enc_queue_trim(enc_queue_t *q);

// 按下标取走（P2 的丢弃用 —— 那一刻下标是准的）。
bool enc_queue_take(enc_queue_t *q, uint8_t index, encounter_t *out);

// **按 uid 取走** —— 跨页面的那条路径必须用这个。
// 找不到返回 false（那条已经被后台淘汰了，属正常，不是错误）。
bool enc_queue_take_uid(enc_queue_t *q, uint16_t uid, encounter_t *out);

// 按 uid 找。返回 NULL 表示已经不在队列里。
encounter_t *enc_queue_find(enc_queue_t *q, uint16_t uid);

// 闪光判定 —— crc32，确定性，可与 PC 侧逐值对账。
// 极稀有（rarity ≥ 5）翻倍概率。
// Acquisition effort controls shiny odds; species rarity never changes them.
typedef enum { SHINY_PASSIVE, SHINY_EXPLORATION, SHINY_BADGE, SHINY_DUNGEON, SHINY_SOURCE_COUNT } shiny_source_t;
uint16_t enc_shiny_denominator(shiny_source_t source);
bool enc_shiny_from_roll(uint32_t roll, shiny_source_t source);
bool enc_roll_shiny(const uint8_t bssid[6], uint32_t ts);

// 确定性刷新种子：同一 AP 同一小时永远同一个值。
uint32_t enc_spawn_seed(const uint8_t bssid[6], uint32_t ts);

// AP 属性 → 稀有度 1~5。信号弱/隐藏 SSID/企业级/瞬现都加分 ——
// 玩家追着奇怪的路由器跑这件事本身就很对味。
uint8_t enc_rarity_from_ap(int8_t rssi, uint8_t auth, bool has_ssid,
                           bool is_transient);

// 按稀有度从对应强度档挑一只。**不是均匀采样 151 只** ——
// 均匀采样会让 Lv12 的初期主宠遇到超梦，实测打 46 回合都赢不了。
uint16_t enc_pick_species(const uint8_t bssid[6], uint32_t ts, uint8_t rarity);

// 图鉴
void dex_init(dex_t *d);
void dex_mark_seen(dex_t *d, uint16_t sid, bool shiny);
void dex_mark_caught(dex_t *d, uint16_t sid, bool shiny);
bool dex_is_seen(const dex_t *d, uint16_t sid);
bool dex_is_caught(const dex_t *d, uint16_t sid);
bool dex_is_shiny_caught(const dex_t *d, uint16_t sid);
uint16_t dex_count_caught(const dex_t *d);
uint16_t dex_count_seen(const dex_t *d);

bool enc_selftest(void);
