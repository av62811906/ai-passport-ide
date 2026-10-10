// main/save.h —— S18 存档。
//
// ## 为什么用 NVS 而不是 S6 设计的双 buffer
//
// S6 文档设计了「A/B 两槽 + CRC32 + 单调递增序号」，理由是
// **任何时刻掉电都至少有一份完好**（写 A 时掉电就回退到 B）。
// 那个理由完全成立 —— 但 NVS 已经提供同样的保证：
// 每条记录带 CRC32、页级状态机、写入未完成的条目在下次挂载时被丢弃。
//
// 区别在于双 buffer 要我自己实现「先写数据后写 CRC」的顺序、
// 自己处理擦除块边界、自己做磨损均衡；而这三件事写错的表现都是
// 「拔电偶尔丢档」—— 概率性的、极难复现的那类缺陷。
//
// 用 NVS 是把这三件事交给已经被大量产品验证过的实现。
// 代价是存档格式不再与 sim/state.py 的字节布局一致 ——
// 但那份布局本来就只是**给固件参考的**，PC 侧不读设备存档。
//
// 分区：用现成的 nvs（0x6000 = 24 KB），不新增分区。
// 上游 AGENTS.md 那三条契约（app ≤3MB、cardid@0x356000、
// recovery@0x700000）都不受影响。
//
// ## 存什么
//
// 约 292 字节：遭遇队列 160 + 图鉴 76 + 养成 24 + 主宠 16 + 累积量 16。
// **不存的东西**：sensing 的地点记忆（2.6 KB，重启后重新学更省事，
// 且它本来就是滚动窗口）、招式列表（现算，见 assets_known_moves）。
#pragma once
#include <string.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "encounter.h"
#include "nurture.h"
#include "party.h"
#include "items.h"
#include "trainer.h"
#include "trainer_legacy.h"
#include "achievements.h"
#include "encounter_refresh.h"
#include "exploration.h"
#include "dungeon_rewards.h"
#include "rest_clock.h"

// 存档版本。**加字段时必须 +1** —— load 会拒绝不认识的版本，
// 那比读到错位的字段好（错位不报错，只是数值离谱）。
#define SAVE_VERSION 21 // Individual move exclusions; V5-V20 default to all enabled.
#define SAVE_LEGACY_VERSION 5

typedef struct {
    uint16_t version;

    // 养成（S4）
    nurture_t pet;

    // 主宠身份的兼容镜像；权威值来自下面队伍区的队首。
    uint16_t species;
    uint8_t level;
    uint32_t exp;

    // 队伍 6 格 + 仓库 151 格（V12起按实际格子保留同类），布局由 party.c 统一编解码。
    uint8_t party[PARTY_BYTES];

    // 遭遇队列（S1）与图鉴（S5/S8）
    enc_queue_t queue;
    dex_t dex;

    // 累积量：今日行程的来源、诊断用的扫描计数
    uint32_t motion_q10;
    uint32_t scans;

    // 上次存盘时的开机微秒数。**不是墙钟** —— 设备没有 RTC 对时。
    // 用途是算「关机了多久」：下次开机的 esp_timer 从 0 开始，
    // 所以只能知道「上次跑了多久」，不知道中间隔了多久。
    // 这正是 S4 的 on_reunion（「好久不见」）现在接不上的原因。
    int64_t last_uptime_us;

    // S16 开场只播一次。save.c 同时用独立的单调 NVS 键保存它，避免
    // world 生成整块快照时把这个页面侧字段清零。
    bool opening_seen;
} save_v5_t;

// The first 2296 bytes retain V5's exact layout, including tail padding. This
// makes migration a bounded prefix read; all new inventory fields are explicit.
typedef struct {
    uint16_t version;
    nurture_t pet;
    uint16_t species;
    uint8_t level;
    uint32_t exp;
    uint8_t party[PARTY_BYTES];
    enc_queue_t queue;
    dex_t dex;
    uint32_t motion_q10, scans;
    int64_t last_uptime_us;
    bool opening_seen;
    uint8_t legacy_padding[sizeof(save_v5_t) - offsetof(save_v5_t, opening_seen) - sizeof(bool)];
    inventory_t inventory;
} save_v6_t;

typedef struct {
    union {
        save_v6_t v6;
        struct {
    uint16_t version;
    nurture_t pet;
    uint16_t species;
    uint8_t level;
    uint32_t exp;
    uint8_t party[PARTY_BYTES];
    enc_queue_t queue;
    dex_t dex;
    uint32_t motion_q10, scans;
    int64_t last_uptime_us;
    bool opening_seen;
    uint8_t legacy_padding[sizeof(save_v5_t) - offsetof(save_v5_t, opening_seen) - sizeof(bool)];
    inventory_t inventory;

        };
    };
    trainer_store_v8_t challenge;
} save_v7_t;

typedef struct {
    union {
        save_v7_t v7;
        struct {
    union {
        save_v6_t v6;
        struct {
    uint16_t version;
    nurture_t pet;
    uint16_t species;
    uint8_t level;
    uint32_t exp;
    uint8_t party[PARTY_BYTES];
    enc_queue_t queue;
    dex_t dex;
    uint32_t motion_q10, scans;
    int64_t last_uptime_us;
    bool opening_seen;
    uint8_t legacy_padding[sizeof(save_v5_t) - offsetof(save_v5_t, opening_seen) - sizeof(bool)];
    inventory_t inventory;

        };
    };
    trainer_store_v8_t challenge;
        };
    };
    achievement_store_t achievements;
} save_v8_t;

// V9 keeps the established V6 party/inventory prefix, then the expanded combat state.
typedef struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;
} save_v9_t;
typedef struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;
} save_v10_t;
typedef struct {
 union {
  save_v10_t v10;
  struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;

  };
 };
 exploration_state_t exploration;
} save_v14_t;
typedef struct {
 union {
  save_v14_t v14;
  struct {
 union {
  save_v10_t v10;
  struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;

  };
 };
 exploration_state_t exploration;

  };
 };
 dungeon_progress_t dungeon;
} save_v15_t;
typedef struct {
 union {
  save_v14_t v14;
  struct {
 union {
  save_v10_t v10;
  struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;

  };
 };
 exploration_state_t exploration;

  };
 };
 dungeon_progress_t dungeon;
 uint8_t v15_padding[sizeof(save_v15_t)-offsetof(save_v15_t,dungeon)-sizeof(dungeon_progress_t)];
 exploration_updates_t exploration_updates;
} save_v16_t;
typedef struct {
 union {
  save_v14_t v14;
  struct {
 union {
  save_v10_t v10;
  struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;

  };
 };
 exploration_state_t exploration;

  };
 };
 dungeon_progress_t dungeon;
 uint8_t v15_padding[sizeof(save_v15_t)-offsetof(save_v15_t,dungeon)-sizeof(dungeon_progress_t)];
 exploration_updates_t exploration_updates;
 rest_clock_t rest_clock;
} save_v17_t;
typedef struct {
 union {
  save_v14_t v14;
  struct {
 union {
  save_v10_t v10;
  struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;

  };
 };
 exploration_state_t exploration;

  };
 };
 dungeon_progress_t dungeon;
 uint8_t v15_padding[sizeof(save_v15_t)-offsetof(save_v15_t,dungeon)-sizeof(dungeon_progress_t)];
 exploration_updates_t exploration_updates;
 rest_clock_t rest_clock;
 exploration_regions_t regions;
} save_v18_t;
typedef struct {
 union {
  save_v18_t v18;
  struct {
 union {
  save_v14_t v14;
  struct {
 union {
  save_v10_t v10;
  struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;

  };
 };
 exploration_state_t exploration;

  };
 };
 dungeon_progress_t dungeon;
 uint8_t v15_padding[sizeof(save_v15_t)-offsetof(save_v15_t,dungeon)-sizeof(dungeon_progress_t)];
 exploration_updates_t exploration_updates;
 rest_clock_t rest_clock;
 exploration_regions_t regions;
  };
 };
 uint32_t exploration_wins; // Only exploration wild victories; defeat resets.
} save_v19_t;
typedef struct {
 union {
  save_v19_t v19;
  struct {
 union {
  save_v18_t v18;
  struct {
 union {
  save_v14_t v14;
  struct {
 union {
  save_v10_t v10;
  struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;

  };
 };
 exploration_state_t exploration;

  };
 };
 dungeon_progress_t dungeon;
 uint8_t v15_padding[sizeof(save_v15_t)-offsetof(save_v15_t,dungeon)-sizeof(dungeon_progress_t)];
 exploration_updates_t exploration_updates;
 rest_clock_t rest_clock;
 exploration_regions_t regions;
  };
 };
 uint32_t exploration_wins; // Only exploration wild victories; defeat resets.
  };
 };
 uint32_t playtime_s; // Cumulative visible game time; no offline/screen-off time.
} save_v20_t;
typedef struct {
 union { save_v20_t v20; struct {
 union {
  save_v19_t v19;
  struct {
 union {
  save_v18_t v18;
  struct {
 union {
  save_v14_t v14;
  struct {
 union {
  save_v10_t v10;
  struct {
 union {
  save_v9_t v9;
  struct {
 union {
  save_v6_t v6;
  struct {
   uint16_t version; nurture_t pet; uint16_t species; uint8_t level; uint32_t exp;
   uint8_t party[PARTY_BYTES]; enc_queue_t queue; dex_t dex; uint32_t motion_q10,scans;
   int64_t last_uptime_us; bool opening_seen;
   uint8_t legacy_padding[sizeof(save_v5_t)-offsetof(save_v5_t,opening_seen)-sizeof(bool)];
   inventory_t inventory;
  };
 };
 trainer_store_t challenge;
 achievement_store_t achievements;

  };
 };
 enc_refresh_state_t refresh;

  };
 };
 exploration_state_t exploration;

  };
 };
 dungeon_progress_t dungeon;
 uint8_t v15_padding[sizeof(save_v15_t)-offsetof(save_v15_t,dungeon)-sizeof(dungeon_progress_t)];
 exploration_updates_t exploration_updates;
 rest_clock_t rest_clock;
 exploration_regions_t regions;
  };
 };
 uint32_t exploration_wins; // Only exploration wild victories; defeat resets.
  };
 };
 uint32_t playtime_s; // Cumulative visible game time; no offline/screen-off time.
 }; };
 move_policy_t move_policies[PARTY_MAX+BOX_SPECIES];
} save_t;
_Static_assert(sizeof(save_v20_t)==4024,"Freeze V20 world layout");
_Static_assert(offsetof(save_t,move_policies)==sizeof(save_v20_t),"V21 preserves V20 including tail padding");
static inline void save_store_party(save_t *s,const party_t *p){party_serialize(p,s->party);memcpy(s->move_policies,p->policies,sizeof(s->move_policies));}

_Static_assert(sizeof(save_v19_t)==4016,"Freeze V19 world layout");
_Static_assert(offsetof(save_t,playtime_s)==sizeof(save_v19_t),"V20 preserves all V19 bytes including tail padding");

_Static_assert(sizeof(save_v18_t)==4008,"Freeze V18 world layout");
_Static_assert(offsetof(save_t,exploration_wins)==sizeof(save_v18_t),"V19 preserves all V18 bytes including tail padding");
_Static_assert(sizeof(save_v17_t)==3816,"Freeze V17 world layout");
_Static_assert(offsetof(save_t,regions)==sizeof(save_v17_t),"V18 preserves V17 prefix");
_Static_assert(offsetof(save_t,rest_clock)==sizeof(save_v16_t),"V17 preserves V16 prefix");
_Static_assert(offsetof(save_t,exploration_updates)==sizeof(save_v15_t),"V16 preserves complete V15 prefix");
_Static_assert(sizeof(encounter_t)==16 && offsetof(encounter_t,ts)==4,"V16 uses legacy encounter padding");
_Static_assert(offsetof(save_t,dungeon)==sizeof(save_v14_t),"V15 preserves complete V14 prefix");
_Static_assert(sizeof(save_v10_t)==3648,"V10 save layout changed");
_Static_assert(offsetof(save_t,exploration)==sizeof(save_v10_t),"V11 preserves V10 prefix");
_Static_assert(sizeof(save_v9_t)==3104,"V9 save layout changed");
_Static_assert(offsetof(save_t,refresh)==sizeof(save_v9_t),"V10 preserves V9 prefix");
_Static_assert(sizeof(save_v8_t)==2696,"V8 save layout changed");
_Static_assert(sizeof(save_v7_t) == 2688, "V7 save layout changed");
_Static_assert(offsetof(save_v8_t, achievements) == sizeof(save_v7_t), "V8 preserves V7 prefix");
_Static_assert(offsetof(save_t, challenge) == sizeof(save_v6_t), "V7 preserves V6 prefix");
_Static_assert(sizeof(save_v5_t) == 2296, "legacy save layout changed");
_Static_assert(offsetof(save_t, inventory) == sizeof(save_v5_t), "V6 must preserve the complete V5 prefix");
_Static_assert(offsetof(save_t, opening_seen) == offsetof(save_v5_t, opening_seen), "V5 field offset changed");

// 初始化 NVS。**在任何 save_read/write 之前调**。
// 幂等，且不依赖 WiFi —— 见 save.c 里那段（这条依赖搞反过一次：
// NVS 初始化藏在 wifi_bring_up 里，而读档在起 WiFi 之前，
// 于是每次开机都读不到存档，而写入是成功的）。
bool save_init(void);

// 存。**会阻塞几毫秒**（flash 写），别在渲染循环里调。
bool save_write(const save_t *s);

typedef enum {
    SAVE_READ_OK = 0,
    SAVE_READ_EMPTY,
    SAVE_READ_ERROR,
    SAVE_READ_MIGRATED, // Valid V5 decoded with its one-time initial inventory.
} save_read_result_t;

// Distinguish a genuinely absent save from unreadable/unsupported data. Only
// EMPTY permits new-game writes; ERROR must preserve the existing NVS bytes.
save_read_result_t save_read_status(save_t *out);
// Same bounded decoding/migration and world validation for startup and USB import.
// blob may alias out; unused bytes are zeroed before legacy migration.
save_read_result_t save_decode(save_t *out, const void *blob, size_t len, uint8_t opening_seen);
bool save_validate_world(const save_t *saved, party_t *party);

// Compatibility wrapper. false does not imply a new game; world uses status.
bool save_read(save_t *out);

// 清档（调试用）。
bool save_erase(void);

// 有没有存档 —— 开机时用它决定走「继续」还是「新游戏」。
bool save_exists(void);

// 开场标记只从 false 变 true；独立键让页面无需写整块世界存档。
bool save_opening_seen(void);
bool save_mark_opening_seen(void);
