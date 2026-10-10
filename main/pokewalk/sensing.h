// main/sensing.h —— WiFi 指纹感知层（F5 移植）。
//
// 对应 sim/sensing.py。**这是整个项目的技术核心** ——
// 没有 GPS，地点识别全靠 WiFi 指纹。
//
// ## 定点而非浮点
//
// C3 无 FPU，软浮点每次运算几十个周期，而感知层每次扫描要算
// 几十次相似度。全部改成 Q10 定点（权重 0~1024）。
//
// 定点会不会改变判定？**实测不会**：拿 data/raw 的 1061 次扫描
// 跑 1060 对相邻比较，Q10 与浮点的判定**零翻转**，相似度最大
// 误差 0.00115。验证脚本见 tools/pipeline/verify_fixedpoint.py。
//
// 为什么是 Q10 而不是 Q8 或 Q16：
//   Q8  误差 0.002，仍可用但没必要省那 2 字节
//   Q10 误差 0.0007，且 TOP_N=8 时相似度分母最大 16384，u16 装得下
//   Q16 分母会到 1048560，要 u32 —— 白白多一倍内存与运算宽度
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 这些常量必须与 sim/sensing.py 逐个对应。改一边必须改另一边 ——
// 不一致的后果是固件与 PC 算出不同的地点/移动判定，而 F5 检验
// 正是为了抓这个。
#define SENS_TOP_N              8       // 指纹保留最强的 N 个 AP
#define SENS_Q                  1024    // 定点标度（Q10）
#define SENS_MATCH_THRESHOLD    358     // 0.35 × 1024，判为同一地点
#define SENS_MOVE_THRESHOLD     410     // 0.40 × 1024，判为「在移动」
#define SENS_HYSTERESIS         2       // 状态切换需连续 N 次一致
#define SENS_PLACE_SLOTS        8       // LRU 槽位（固件 512 字节预算）
#define SENS_SMOOTH_WINDOW      4       // 滑动窗口（修移动误报，见 .c）
#define SENS_MIN_APS_FOR_DIST   5       // 少于这么多 AP 不判距离
#define SENS_FRESH_RATIO_THRESHOLD 256  // 0.25 × 1024，新 AP 占比达此判移动
// 平滑窗口每帧记多少个 AP。
//
// **不能设小**：出现率统计要看窗口里的全部 AP，截断会让弱信号 AP
// 被丢掉，平滑指纹就与 PC 侧不同。第一版设 12，实测 1061 次扫描里
// 有 345 次距离对不上 —— 因为真实扫描常有 13~31 个 2.4G AP。
//
// 40 的依据：实测 data/raw 单次最多 31 个，办公室能到 65 但那是
// 含 5G 的总数。窗口占用 4×40×6 = 960 字节，聚合时栈上再用 1.9KB ——
// 在 231KB 可用堆里可以接受。
#define SENS_FRAME_APS          40
#define SENS_BLOOM_BITS         8192    // 「见过的 AP」布隆位数（1KB）
#define SENS_BIOME_NONE         0xFF    // biome 未分类
#define SENS_RSSI_FLOOR         (-100)
#define SENS_RSSI_CEIL          (-40)
#define SENS_CHANNEL_24G_MAX    14

typedef enum {
    SENS_UNKNOWN = 0,
    SENS_STAYING,
    SENS_MOVING,
} sens_state_t;

// 一次扫描里的单个 AP。字段与 tools/collector 的 NDJSON 对应。
typedef struct {
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth;                // wifi_auth_mode_t，classify_biome 要用
} sens_ap_t;

// 加权指纹：定长 8 × (u32 哈希 + u16 权重) = 48 字节。
// sim 侧是 dict，这里是定长数组 —— 固件不做动态分配。
typedef struct {
    uint32_t hash[SENS_TOP_N];
    uint16_t weight[SENS_TOP_N];   // Q10
    uint8_t n;
} sens_sig_t;

typedef struct {
    uint16_t pid;
    sens_sig_t sig;
    uint32_t first_seen;
    uint32_t last_seen;
    uint32_t dwell;                // 累计驻留秒数
    uint8_t biome;                 // 首次分类后冻结
} sens_place_t;

typedef struct {
    uint32_t ts;
    uint8_t ap_count;
    sens_state_t state;
    uint16_t distance;             // Q10，与上次扫描的**平滑**指纹距离
    uint8_t transient_aps;         // 本次新出现的 AP 数（用单帧指纹算）
    bool is_new_place;
    uint16_t place_id;
    uint16_t match_score;          // Q10，与最匹配地点的相似度
} sens_result_t;

// 平滑窗口的一帧。存去重后的 (哈希, 权重) —— 出现率要靠它算。
typedef struct {
    uint32_t hash[SENS_FRAME_APS];
    uint16_t weight[SENS_FRAME_APS];
    uint8_t n;
} sens_frame_t;

// 感知层的全部状态。**刻意放一个结构体里**而非模块级静态变量 ——
// 那样「哪些状态要存档」是明确的（同 sim/orchestrate.py 的 Session）。
typedef struct {
    sens_place_t places[SENS_PLACE_SLOTS];
    uint8_t place_count;
    uint16_t next_pid;

    sens_sig_t prev;               // 上次的**平滑**指纹（算距离用）
    bool has_prev;
    sens_sig_t prev_frame;         // 上次的**单帧**指纹（算瞬现 AP 用）
    bool has_prev_frame;

    // 平滑窗口 —— 存原始帧而非距离值。
    // 第一版存的是距离值然后取平均，那是完全不同的东西：
    // 正确做法是用窗口内的 AP **出现率**重建指纹，再拿它算距离。
    sens_frame_t win[SENS_SMOOTH_WINDOW];
    uint8_t win_n;
    uint8_t win_head;

    // 见过的 AP —— PC 侧是无上限集合，固件用布隆过滤器（1KB）。
    // 实测三份数据 149 个 BSSID 只占 3.6% 的位，与真集合零差异。
    uint8_t seen[SENS_BLOOM_BITS / 8];

    sens_state_t state;
    sens_state_t cand;             // 候选状态（迟滞用）
    uint8_t cand_count;

    uint32_t last_ts;
    uint32_t dwell_by_biome[5];    // 野外/住宅/办公/商业/交通
} sens_core_t;

void sens_init(sens_core_t *c);

// 喂一次扫描。aps 只需含 2.4G 的（调用方过滤，或用 sens_is_24g）。
void sens_feed(sens_core_t *c, uint32_t ts,
               const sens_ap_t *aps, uint8_t n, sens_result_t *out);

// -- 下面几个导出是为了 host test 能逐函数对账 --

uint32_t sens_hash_bssid(const uint8_t bssid[6]);
uint16_t sens_rssi_weight(int8_t rssi);          // → Q10
void sens_sig_from_aps(const sens_ap_t *aps, uint8_t n, sens_sig_t *out);
uint16_t sens_similarity(const sens_sig_t *a, const sens_sig_t *b);  // → Q10

// 与 PC 侧逐值对账。感知层错一位地点表就整个失配，而失配不报错 ——
// 设备只会安静地把家认成新地点。所以每次启动都跑。
bool sens_selftest(void);
static inline bool sens_is_24g(uint8_t ch) { return ch <= SENS_CHANNEL_24G_MAX; }
