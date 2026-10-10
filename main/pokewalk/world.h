// main/world.h —— 跨页面的游戏状态与后台扫描（F9-①）。
//
// ## 这个模块解决什么
//
// 在它之前，WiFi 扫描绑在 Collect 页的 lv_timer 上。两个后果：
//
//   · **离开那一页扫描就停** —— P1 的「今日行程」没有数据源，
//     只能写死 70；长跑采集必须把设备停在 Collect 页
//   · **开机得进 Collect 而不是 P1** —— 产品形态上主页应该是 P1，
//     但为了不丢数据只能让开机进采集页（main.c 里那段 BOOT_DEMO
//     的注释就是在解释这个妥协）
//
// world 把扫描 + sensing 收成一个独立 FreeRTOS 任务，
// 页面只读结果。谁在前台都不影响采集。
//
// ## 线程契约
//
// 扫描任务与 LVGL 任务是两个线程。上游 AGENTS.md 的硬规矩是
// **LVGL 非线程安全**，所以：
//
//   · world 任务里**绝不碰 LVGL** —— 只更新自己的状态
//   · 页面读状态走 world_snapshot()，它在**互斥锁内拷一份出来**，
//     调用方拿到的是一致的快照，不会读到改到一半的结构
//
// 为什么是快照而不是直接暴露指针：页面渲染一帧要读七八个字段
// （四条轴 + 遭遇数 + 地点），逐个读会撞上任务在中间改了其中几个 ——
// 屏幕上就是「饱食是新的、心情是旧的」。快照一次锁住全部。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "battle.h"
#include "encounter.h"
#include "nurture.h"
#include "items.h"
#include "party.h"
#include "trainer.h"
#include "sensing.h"

// 页面看到的世界。**这是个值拷贝**，拿到后随便读，不用加锁。
typedef struct {
    nurture_t pet;                 // 三条轴 + 亲密度
    uint16_t species;              // 主宠 = 队伍首位
    uint32_t exp;                  // 主宠累计经验
    uint8_t level;                 // 由累计经验换算出的等级
    uint16_t explore_value;        // 主宠探索值；移动状态的每次扫描 +1

    // 今日行程 0~100。S1 的移动量累积映射来的 —— 见 world.c。
    uint8_t progress;

    uint8_t pending;               // 待处理遭遇数（= queue.count）
    sens_state_t state;            // 移动 / 驻留
    uint16_t place_id;
    uint8_t biome;

    uint32_t scans;                // 已完成扫描次数（诊断用）
    uint8_t last_ap_count;
} world_t;

// 启动后台任务。**只调一次**（main.c 里，在页面之前）。
// WiFi 起不来时返回 false —— 那时页面照常能画，只是没有感知数据。
//
// **world 是 WiFi 的唯一所有者。** 页面不要再自己 esp_wifi_init ——
// 两个所有者会争同一个射频：Collect 页原本自己 bring_up，
// 与这里撞上就是 esp_wifi_init 返回 ESP_ERR_INVALID_STATE，
// 而它没判那个返回值，会当成初始化失败。
// True only when startup decoded and adopted a valid existing save.
bool world_save_loaded(void);
// Caller holds the UI lock. Borrow serialized world workspaces under the save
// lock to validate an isolated import; never adopt it or touch live NVS here.
typedef bool (*world_backup_reader_t)(void *context,void *blob,size_t *size,uint8_t *opening);
bool world_backup_validate(world_backup_reader_t reader,void *context,unsigned version);
bool world_start(void);

// WiFi 是否可用。Collect 页用它判断该不该自己起 —— 见 world_start。
bool world_wifi_ready(void);

// 取一份一致的快照。任何 FreeRTOS 任务都能调，不要求持 LVGL 锁；
// 本函数不碰 LVGL，并会在内部短暂等待 world 自己的状态锁。
// LVGL 锁只保护界面对象，不能替代这里对跨任务游戏状态的同步。
// 不可从 ISR 调用（互斥量可能阻塞）。
void world_snapshot(world_t *out);

// Six visible members, in current order; box_count counts occupied species
// slots, not total captures. No pointers into mutable world storage escape.
typedef struct {
    mon_t members[PARTY_MAX];
    uint8_t count, box_count;
    bool switch_locked; // Storage unavailable, no starter, or active P3/P4.
} world_party_t;
typedef enum {
    WORLD_SWITCH_OK = 0,
    WORLD_SWITCH_ALREADY_LEADER,
    WORLD_SWITCH_INVALID,
    WORLD_SWITCH_STALE,
    WORLD_SWITCH_BUSY,
    WORLD_SWITCH_SAVE_FAILED,
    WORLD_SWITCH_STORAGE_UNAVAILABLE,
    WORLD_SWITCH_LAST_MOVE,
} world_switch_result_t;

void world_party_snapshot(world_party_t *out);
// Slot 0..5 party, 6..156 warehouse. Read/set validates the displayed individual.
bool world_move_policy(unsigned slot,const mon_t *expected,move_policy_t *out);
world_switch_result_t world_move_set(unsigned slot,const mon_t *expected,unsigned move,bool enabled);
void world_move_policies(move_policy_t out[PARTY_MAX]);
// expected must be the complete member shown by world_party_snapshot(), so a
// reordered list or a different same-species member cannot be selected by a
// stale index. out may be NULL; OK/ALREADY_LEADER return a fresh snapshot.
// Save before publishing. Shared satiety/mood/stamina and time remain intact;
// intimacy/exploration/EXP follow the member. The outgoing Q10 intimacy is
// floored to mon_t's integer precision, so repeated switching cannot round up.
// Legacy EXP below exp_for_level(level) is repaired for all loaded party/box
// members and saved on startup; switch/capture candidates also enforce this
// floor without lowering the saved level or discarding higher existing EXP.
world_switch_result_t world_set_leader(uint8_t index, const mon_t *expected,
                                      world_party_t *out);

// Fresh saves have no leader until the chosen starter is durably saved. A
// valid existing V5 party already satisfies this requirement; it is preserved.
bool world_needs_starter(void);

// Inventory snapshots are locked value copies. A successful use means both
// the effect and decrement were saved; failure publishes neither. Active
// P3/P4 sessions must be left before using a care/evolution item.
void world_inventory_snapshot(inventory_t *out);
item_use_status_t world_item_use(uint16_t expected_species, uint8_t item_id,
                                  item_use_result_t *out);
// Once per won active session. Repeated calls return the same actual award
// without adding it again. A false return is retryable and publishes nothing.
bool world_battle_loot_uid(uint16_t uid, item_loot_t *out);
// Explicit capture-entry confirmation for a tagged exploration encounter.
// Atomically clears the exploration chain; a failed save preserves it and
// consumes no ball. Other encounter sources leave the chain untouched.
bool world_capture_break_chain_uid(uint16_t uid);
// Save one ball decrement and the throw's candidate session together. An
// unstarted pending encounter is detached in that same commit. The caller
// supplies the session immediately after choosing to throw, before cap_attempt.
// No stock/save failure leaves the previous session and final throw intact.
bool world_capture_ball_spend_uid(uint16_t uid, uint8_t ball,
                                  const battle_session_t *item_session);

typedef enum {
    WORLD_STARTER_OK = 0,
    WORLD_STARTER_ALREADY_CHOSEN,
    WORLD_STARTER_INVALID,
    WORLD_STARTER_SAVE_FAILED,
    WORLD_STARTER_STORAGE_UNAVAILABLE,
} world_starter_result_t;

// Accept only #1/#4/#7/#25. Build and save a complete party+dex snapshot before
// publishing the leader. Failure leaves the pending choice unchanged; retry is
// safe. ALREADY_CHOSEN never replaces a leader or adds another Pokemon.
world_starter_result_t world_choose_starter(uint16_t species);

// 照料。**由按键触发，走 world 而不是页面自己改** ——
// 状态的唯一所有者是 world，页面只读。
void world_feed(void);
bool world_play(void);
void world_rest(void);

// 发放主宠经验并立即存档。战斗页只调用一次，状态与持久化由 world 管。
void world_grant_exp(uint16_t amount);

#include "exp.h"
bool world_growth_pop(exp_growth_t *out);

// Once per defeated active battle: stamina -20, mood -15, no EXP reduction.
// Updates the runtime guard in the same lock, then saves existing nurture axes.
bool world_apply_defeat_uid(uint16_t uid);

// 原子完成队首进化并立即存档。会在锁内重新核对物种进化目标与实际
// 等级；expected_species 防止页面快照过期后把另一只误进化。
bool world_evolve_leader(uint16_t expected_species, uint16_t evolve_to);

// 一次提交捕获：收容、点亮图鉴、按 uid 出队在同一个临界区完成，
// 随后立即把包含三者的完整状态存档。无效/已淘汰 uid 返回 false。
bool world_capture_uid(uint16_t uid, const mon_t *mon);

// 遭遇队列与图鉴。**返回指针而不是拷贝** —— 队列保留 V5 的16槽、
// 图鉴 76 字节，每帧拷一遍不划算，而页面只读不写。
//
// 写操作走下面几个函数，它们内部加锁。
const enc_queue_t *world_queue(void);
const dex_t *world_dex(void);

// Consistent pending-list snapshot for rendering and selecting visible rows.
// A missing/unavailable world produces an empty snapshot.
void world_queue_snapshot(enc_queue_t *out);

// Locked snapshot of either a pending encounter or the one active P3/P4
// target. The active target is absent from world_queue() and the pending count.
bool world_get_encounter_uid(uint16_t uid, encounter_t *out);

// Leaving the P3/P4 chain ends an already started encounter permanently. It is
// never requeued. An unstarted target is still pending and remains untouched.
void world_end_active_encounter(void);

// 按下标取走（P2 的丢弃 —— 那一刻下标是准的）。加锁。
bool world_take_encounter(uint8_t index, encounter_t *out);

// **按 uid 取走** —— 跨页面（P4 捕获成功/逃跑）必须用这个。
// 返回 false 表示那条已被后台淘汰，正常情况，调用方不用报错。
bool world_take_uid(uint16_t uid, encounter_t *out);

// 更新待处理/活动遭遇的HP（P3/P4共享同一目标）。
void world_update_hp_uid(uint16_t uid, uint8_t hp_ratio);

// 首次结算时标记该遭遇已领取经验。返回 false 表示已领取或已被淘汰。
bool world_mark_exp_granted_uid(uint16_t uid);

// Runtime-only battle state, keyed by encounter uid+ts. First started=true
// commits its removal from the pending save before moving it to the single
// active slot. Save failure returns false with pending/session unchanged, so
// the caller must not publish the attack/throw and can retry. Further active updates
// do not write flash. Both active/session are excluded from V5 and disappear
// on reboot; the committed pending removal prevents a battled target returning.
bool world_battle_get_uid(uint16_t uid, battle_session_t *out);
bool world_battle_set_uid(uint16_t uid, const battle_session_t *session);

// 图鉴登记。加锁。
void world_mark_seen(uint16_t sid, bool shiny);

// 调试用：立刻造一条遭遇。
//
// 真实遭遇要么等基地排程（4 小时一次），要么带着设备走动
// （猎场路径要移动量）。验证玩法链路时两个都等不起 ——
// 而「等 4 小时才能测一次捕获」会让存档这类改动根本没法验。
bool world_debug_spawn(void);

// 调试用：立刻存档。正常路径是捕获时立刻存 + 每 5 分钟节流存，
// 而验证「拔电不丢」时不想等那 5 分钟。
void world_debug_save(void);
// Caller holds UI lock (dungeon/settings writers); checkpoint and copy under save lock.
bool world_backup_snapshot(bool (*reader)(void *), void *out);

// 调试用：把队首两条进化进度设到当前物种的门槛，不执行进化。
// 仅供 CONFIG_POKEWALK_DEBUG_KEYS 的串口验收入口调用。
#ifdef CONFIG_POKEWALK_DEBUG_KEYS
bool world_debug_evolution_ready(void);
#endif

// 与 PC 侧对账用：把移动量累积映射成 0~100 的今日行程。
// 单独暴露是为了能在宿主上测（见 tools/pipeline/verify_world.py）。
uint8_t world_progress_from_motion(uint32_t motion_units);

// Transactional trainer campaign, separate from the five pending wild encounters.
void world_challenge_snapshot(trainer_store_t *out);
typedef enum {
    WORLD_CHALLENGE_OK, WORLD_CHALLENGE_UNAVAILABLE, WORLD_CHALLENGE_BUSY,
    WORLD_CHALLENGE_LOCKED, WORLD_CHALLENGE_NO_STAMINA, WORLD_CHALLENGE_SAVE_FAILED
} world_challenge_result_t;
world_challenge_result_t world_challenge_start(uint8_t id);
bool world_challenge_begin(uint8_t id);
bool world_challenge_step(trainer_event_t *out);
bool world_challenge_move(uint8_t slot);
bool world_challenge_switch(uint8_t slot, bool forced);
bool world_challenge_retire(void);
bool world_challenge_settle(void);
bool world_challenge_recover(uint8_t slot); // One milk: heal max(50, half max HP), capped at max; clear status.

#include "achievements.h"
void world_achievements_snapshot(achievement_view_t *out);
achievement_claim_t world_achievement_claim(unsigned id);

#include "exploration.h"
void world_exploration_snapshot(exploration_view_t *out);
// Actual screen-on gameplay only; clock origin and offline time are excluded.
uint32_t world_playtime_seconds(void);
void world_playtime_set_paused(bool paused);

uint32_t world_exploration_chain(void);
exploration_kind_t world_exploration_select(uint8_t route);
exploration_kind_t world_exploration_depth(void);
bool world_region_collect(void);
bool world_region_discard(void);
exploration_event_t world_explore(void);
exploration_event_t world_explore_path(unsigned direction);

bool world_battle_reward_uid(uint16_t uid,uint16_t *amount);

void world_box_snapshot(mon_t out[BOX_SPECIES]);
// Physical warehouse slot + displayed individual; save before publishing removal.
world_switch_result_t world_box_release(unsigned slot, const mon_t *expected);
world_switch_result_t world_box_exchange(uint8_t slot,const mon_t *outgoing,const mon_t *incoming);

exploration_kind_t world_exploration_track(uint16_t species);

exploration_kind_t world_research_claim(uint8_t route,uint16_t *gain);

exploration_event_t world_exploration_activity(unsigned id,bool claim);

// Absolute UTC in microseconds, accepted only after network time synchronization.
bool world_sync_time(int64_t utc_us,uint8_t *recovered);
