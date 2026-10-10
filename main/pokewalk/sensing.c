// main/sensing.c —— WiFi 指纹感知层，sim/sensing.py 的定点移植。
//
// ## 移植的第一原则：与 PC 侧算出同一个数
//
// F5 一致性检验（docs/07-roadmap.md）要求「把 data/raw/*.ndjson 灌进固件，
// 算出与 sim/replay.py 完全相同的状态序列」。
// `tools/pipeline/verify_sensing.py` 在主机上编译本文件跑同一份数据，
// **逐扫描比中间量**（状态、距离、瞬现 AP、新地点），几秒出结果。
//
// 第一版只移了一半就去跑，1061 次扫描里 2223 处不符 —— 地点数 155 vs 8。
// 那次暴露的四个漏掉的机制，现在都在下面：
//
//   ① **平滑指纹**（SlidingSignature）。距离要用「最近 4 次扫描的
//      出现率加权指纹」算，不是对单帧距离做平均。我第一版平滑的是
//      距离值 —— 完全不同的东西
//   ② **AP 新鲜度**（fresh_ratio）。移动判定的第二条判据：
//      驻留时 AP 集合封闭，移动时持续见到全新 BSSID
//   ③ **无新鲜 AP 时否决距离判据**（VETO_MOVE_WHEN_NO_FRESH）
//   ④ **地点只在驻留时记**，且要同时看 state 与原始距离 ——
//      迟滞下移动的第一帧 state 仍是 staying
//
// ## 两处不得不偏离 PC 侧
//
//   · `seen_hashes` 在 PC 上是无上限集合，固件用 8192 位布隆过滤器
//     （1KB）。实测三份数据 149 个 BSSID 只占 3.6% 的位，
//     与真集合**零差异**。假阳性方向是保守的：把新 AP 当成见过的
//     → fresh_ratio 偏低 → 少判几次移动，而非误判
//   · 浮点全部换 Q10 定点。1060 对相邻扫描判定零翻转

#include <stdio.h>
#include <string.h>

#include "esp_rom_crc.h"

#include "sensing.h"

// ---------------------------------------------------------------------------
// 基础运算
// ---------------------------------------------------------------------------

uint32_t sens_hash_bssid(const uint8_t b[6])
{
    // 必须与 PC 侧的 zlib.crc32(bssid_string) 一致。
    //
    // 算的是**格式化后的字符串**，不是 6 字节原始数组 ——
    // 而且是小写冒号分隔（tools/collector 与 play_collect.c 都这么打印）。
    // 大写会算出完全不同的哈希，地点表整个失配。
    char s[18];
    snprintf(s, sizeof(s), "%02x:%02x:%02x:%02x:%02x:%02x",
             b[0], b[1], b[2], b[3], b[4], b[5]);

    // **直接调用即可 —— ESP32-C3 的 ROM crc32_le 就是标准 CRC-32。**
    //
    // esp_rom_crc.h 的注释写着「要在函数前后各加一个 ~」，那句话
    // 有歧义，我照它猜了两次都错。真机打表才给出答案：
    //
    //     rom(0)  = 0xD9CF27A9   ← 正是 zlib.crc32("aa:bb:...")
    //     rom(~0) = 0xEFDF29EB
    //
    // 教训不是「文档不可信」，是**host 模拟不能用来反推硬件语义**：
    // 我用 zlib 写了个 shim 去模拟 ROM，然后拿 shim 的行为推 ROM 该怎么调 ——
    // 循环论证。两次改动都让 host 全绿而真机全错。
    // 自检里的 ROM 探针留着，换芯片/换 IDF 版本时重跑一次就知道。
    return esp_rom_crc32_le(0U, (const uint8_t *)s, 17);
}

uint16_t sens_rssi_weight(int8_t rssi)
{
    if (rssi <= SENS_RSSI_FLOOR) return 0;
    if (rssi >= SENS_RSSI_CEIL) return SENS_Q;
    return (uint16_t)(((int32_t)rssi - SENS_RSSI_FLOOR) * SENS_Q /
                      (SENS_RSSI_CEIL - SENS_RSSI_FLOOR));
}

// 一次扫描内先按 BSSID 去重取最强 —— 多 SSID 共用射频时同一 BSSID
// 会出现多次，不去重会重复计数抬高出现率。
static uint8_t dedup(const sens_ap_t *aps, uint8_t n,
                     uint32_t *h, int8_t *r, uint8_t cap)
{
    uint8_t m = 0;
    for (uint8_t i = 0; i < n && m < cap; i++) {
        uint32_t hh = sens_hash_bssid(aps[i].bssid);
        uint8_t j = 0;
        for (; j < m; j++) {
            if (h[j] == hh) {
                if (aps[i].rssi > r[j]) r[j] = aps[i].rssi;
                break;
            }
        }
        if (j == m) { h[m] = hh; r[m] = aps[i].rssi; m++; }
    }
    return m;
}

// 按权重降序取 top-N 填进 sig。选择排序 —— N=8，比快排常数小。
//
// ⚠️ 权重相同时的取舍会影响哪些入选。Python 的 sorted 是稳定排序
// 保留原顺序；这里用严格大于 `>` 比较，同样保留先出现的。
// 两边入选一致还要求输入顺序和权重并列关系一致；平滑窗口须按旧→新聚合。
static void take_top(const uint32_t *h, const uint32_t *w, uint8_t m,
                     sens_sig_t *out)
{
    memset(out, 0, sizeof(*out));
    // static + 手动清零：栈预算紧（见 smooth_current 的说明）
    static bool used[64];
    memset(used, 0, sizeof(used));
    for (uint8_t k = 0; k < SENS_TOP_N; k++) {
        int best = -1;
        for (uint8_t j = 0; j < m; j++) {
            if (used[j]) continue;
            if (best < 0 || w[j] > w[best]) best = j;
        }
        if (best < 0 || w[best] == 0) break;
        used[best] = true;
        out->hash[out->n] = h[best];
        out->weight[out->n] = (uint16_t)w[best];
        out->n++;
    }
}

void sens_sig_from_aps(const sens_ap_t *aps, uint8_t n, sens_sig_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!aps || !n) return;

    // 同样 static —— 64×(4+1+4) = 576 B，加上调用链里的其他帧
    // 仍然吃紧。单线程前提见 smooth_current 的说明。
    static uint32_t h[64];
    static int8_t r[64];
    static uint32_t w[64];
    uint8_t m = dedup(aps, n, h, r, 64);
    for (uint8_t i = 0; i < m; i++) w[i] = sens_rssi_weight(r[i]);
    take_top(h, w, m, out);
}

uint16_t sens_similarity(const sens_sig_t *a, const sens_sig_t *b)
{
    // 两个空指纹的相似度定义为 0 而非 1 —— 「什么都没扫到」
    // 不应该被当作「回到了某个熟悉的地方」。
    if (!a->n || !b->n) return 0;

    uint32_t inter = 0, uni = 0;
    for (uint8_t i = 0; i < a->n; i++) {
        uint16_t wa = a->weight[i], wb = 0;
        for (uint8_t j = 0; j < b->n; j++) {
            if (b->hash[j] == a->hash[i]) { wb = b->weight[j]; break; }
        }
        inter += (wa < wb) ? wa : wb;
        uni += (wa > wb) ? wa : wb;
    }
    for (uint8_t j = 0; j < b->n; j++) {
        bool seen = false;
        for (uint8_t i = 0; i < a->n; i++) {
            if (a->hash[i] == b->hash[j]) { seen = true; break; }
        }
        if (!seen) uni += b->weight[j];
    }
    if (!uni) return 0;
    return (uint16_t)(inter * SENS_Q / uni);
}

// ---------------------------------------------------------------------------
// 平滑指纹 —— 用最近 N 次扫描的**出现率**加权
//
// 这是抗噪的关键，也是我第一版漏掉的东西。公式（与 sim 一致）：
//
//     weight(h) = (窗口内出现次数 / 窗口大小) × (该 AP 的平均 rssi_weight)
//
// 出现率那一项是重点：稳定 AP 每次都在，出现率接近 1；
// 噪声 AP 时有时无，出现率被压到 0.2~0.4，于是挤不进 top-N。
//
// 单帧指纹仍然要留着算 transient_aps —— 平滑会把一次性出现的 AP 抹掉，
// 而那正是猎场遭遇的原料（docs/04-gameplay.md#411）。
// ---------------------------------------------------------------------------

static void smooth_push(sens_core_t *c, const sens_ap_t *aps, uint8_t n)
{
    uint8_t slot = c->win_head;
    sens_frame_t *f = &c->win[slot];
    memset(f, 0, sizeof(*f));

    static uint32_t h[64];
    static int8_t r[64];
    uint8_t m = dedup(aps, n, h, r, 64);
    if (m > SENS_FRAME_APS) m = SENS_FRAME_APS;
    for (uint8_t i = 0; i < m; i++) {
        f->hash[i] = h[i];
        f->weight[i] = sens_rssi_weight(r[i]);
    }
    f->n = m;

    c->win_head = (uint8_t)((c->win_head + 1) % SENS_SMOOTH_WINDOW);
    if (c->win_n < SENS_SMOOTH_WINDOW) c->win_n++;
}

static void smooth_current(const sens_core_t *c, sens_sig_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!c->win_n) return;

    // 聚合窗口内所有 AP 的出现次数与权重和。
    //
    // ⚠️ 这三个数组共 1.9KB，**必须 static** —— 放栈上会溢出。
    // main 任务栈默认只有 3584 B，而 sens_core_t 本身就 2.6KB
    // （含 1KB 布隆）。实测栈上分配直接 Guru Meditation。
    //
    // static 的前提是感知层单线程调用：sens_feed 只在采集任务里跑，
    // 不会重入。若将来要多任务，这里得改成调用方传缓冲。
    static uint32_t hs[SENS_FRAME_APS * SENS_SMOOTH_WINDOW];
    static uint32_t cnt[SENS_FRAME_APS * SENS_SMOOTH_WINDOW];
    static uint32_t wsum[SENS_FRAME_APS * SENS_SMOOTH_WINDOW];
    uint8_t m = 0;
    const uint8_t cap = SENS_FRAME_APS * SENS_SMOOTH_WINDOW;

    // win_head 指向下次写入位置；未满时最旧帧在 0，满后在 win_head。
    // 按旧→新遍历，使并列权重的 AP 保留与 Python deque 相同的首次出现顺序。
    const uint8_t oldest = (c->win_head + SENS_SMOOTH_WINDOW - c->win_n) %
                           SENS_SMOOTH_WINDOW;
    for (uint8_t k = 0; k < c->win_n; k++) {
        const sens_frame_t *f = &c->win[(oldest + k) % SENS_SMOOTH_WINDOW];
        for (uint8_t i = 0; i < f->n; i++) {
            uint8_t j = 0;
            for (; j < m; j++) if (hs[j] == f->hash[i]) break;
            if (j == m) {
                if (m >= cap) continue;
                hs[m] = f->hash[i]; cnt[m] = 0; wsum[m] = 0; m++;
            }
            cnt[j]++;
            wsum[j] += f->weight[i];
        }
    }

    // weight = (cnt / win_n) × (wsum / cnt) = wsum / win_n
    //
    // 代数上两式等价，但浮点与定点的舍入均可能改变权重的并列关系。
    // sim 侧 (cnt/n) * (wsum/cnt) 是两步浮点运算，并非两步整数除；
    // 浮点结果不保证与 wsum/n 逐位相等，细微差异也会影响 top-N 入选。
    // 这里保留一次定点除法，避免额外截断；不能据此声称两端排序完全一致。
    uint32_t w[SENS_FRAME_APS * SENS_SMOOTH_WINDOW];
    for (uint8_t i = 0; i < m; i++) w[i] = wsum[i] / c->win_n;

    take_top(hs, w, m, out);
}

// ---------------------------------------------------------------------------
// 见过的 AP —— 布隆过滤器
//
// PC 侧是无上限集合，固件用 8192 位（1KB）。实测三份数据的 149 个
// BSSID 只占 3.6% 的位，与真集合**零差异**。
//
// 两个独立哈希取自 crc32 的高低半部 —— crc32 各位独立性好，
// 不需要再算第二个哈希函数。
// ---------------------------------------------------------------------------

static inline void bloom_idx(uint32_t h, uint16_t *a, uint16_t *b)
{
    *a = (uint16_t)((h & 0xFFFF) % SENS_BLOOM_BITS);
    *b = (uint16_t)(((h >> 16) & 0xFFFF) % SENS_BLOOM_BITS);
}

static void bloom_add(sens_core_t *c, uint32_t h)
{
    uint16_t i, j;
    bloom_idx(h, &i, &j);
    c->seen[i >> 3] |= (uint8_t)(1u << (i & 7));
    c->seen[j >> 3] |= (uint8_t)(1u << (j & 7));
}

static bool bloom_has(const sens_core_t *c, uint32_t h)
{
    uint16_t i, j;
    bloom_idx(h, &i, &j);
    return (c->seen[i >> 3] >> (i & 7) & 1) &&
           (c->seen[j >> 3] >> (j & 7) & 1);
}

// ---------------------------------------------------------------------------
// 地点记忆
// ---------------------------------------------------------------------------

static int place_match(const sens_core_t *c, const sens_sig_t *sig,
                       uint16_t *score)
{
    int best = -1;
    uint16_t best_sim = 0;
    for (uint8_t i = 0; i < c->place_count; i++) {
        uint16_t s = sens_similarity(&c->places[i].sig, sig);
        if (s > best_sim) { best_sim = s; best = i; }
    }
    if (score) *score = best_sim;
    return (best_sim >= SENS_MATCH_THRESHOLD) ? best : -1;
}

static int place_insert(sens_core_t *c, const sens_sig_t *sig, uint32_t ts)
{
    uint8_t idx;
    if (c->place_count < SENS_PLACE_SLOTS) {
        idx = c->place_count++;
    } else {
        // LRU：挤掉最久没见到的
        idx = 0;
        for (uint8_t i = 1; i < SENS_PLACE_SLOTS; i++) {
            if (c->places[i].last_seen < c->places[idx].last_seen) idx = i;
        }
    }
    memset(&c->places[idx], 0, sizeof(c->places[idx]));
    c->places[idx].pid = c->next_pid++;
    c->places[idx].sig = *sig;
    c->places[idx].first_seen = ts;
    c->places[idx].last_seen = ts;
    c->places[idx].biome = SENS_BIOME_NONE;
    return idx;
}

// 指纹的指数移动平均更新 —— 让地点随环境缓慢演化。
// alpha 小 = 记忆顽固，这是有意的（对应「首次分类后冻结」的软化版）。
static void place_merge(sens_sig_t *dst, const sens_sig_t *src)
{
    // alpha = 1/5（sim 侧 0.2）。用移位而非乘除：Q10 下 v/5 精度够。
    for (uint8_t i = 0; i < src->n; i++) {
        bool found = false;
        for (uint8_t j = 0; j < dst->n; j++) {
            if (dst->hash[j] == src->hash[i]) {
                int32_t cur = dst->weight[j];
                dst->weight[j] = (uint16_t)(cur + (src->weight[i] - cur) / 5);
                found = true;
                break;
            }
        }
        if (!found && dst->n < SENS_TOP_N) {
            dst->hash[dst->n] = src->hash[i];
            dst->weight[dst->n] = (uint16_t)(src->weight[i] / 5);
            dst->n++;
        }
    }
    // 衰减本次未见到的
    for (uint8_t j = 0; j < dst->n; ) {
        bool seen = false;
        for (uint8_t i = 0; i < src->n; i++) {
            if (src->hash[i] == dst->hash[j]) { seen = true; break; }
        }
        if (!seen) {
            dst->weight[j] = (uint16_t)(dst->weight[j] * 4 / 5);
            if (dst->weight[j] < SENS_Q / 20) {      // 低于 0.05 丢弃
                dst->hash[j] = dst->hash[dst->n - 1];
                dst->weight[j] = dst->weight[dst->n - 1];
                dst->n--;
                continue;
            }
        }
        j++;
    }
}

// ---------------------------------------------------------------------------
// 主循环
// ---------------------------------------------------------------------------

void sens_init(sens_core_t *c)
{
    memset(c, 0, sizeof(*c));
    c->next_pid = 1;
    c->state = SENS_UNKNOWN;
    c->cand = SENS_UNKNOWN;
}

void sens_feed(sens_core_t *c, uint32_t ts,
               const sens_ap_t *aps, uint8_t n, sens_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->ts = ts;
    out->ap_count = n;

    // 单帧指纹 —— 只用于 transient_aps（平滑会抹掉瞬现 AP）
    sens_sig_t frame;
    sens_sig_from_aps(aps, n, &frame);

    // 平滑指纹 —— 用于移动判定与地点匹配
    smooth_push(c, aps, n);
    sens_sig_t sig;
    smooth_current(c, &sig);

    // -- 距离 ------------------------------------------------------------
    uint16_t dist = 0;
    if (c->has_prev) dist = (uint16_t)(SENS_Q - sens_similarity(&sig, &c->prev));
    out->distance = dist;

    // -- AP 新鲜度：移动判定的第二条判据 ---------------------------------
    //
    // 单靠距离不够 —— 平滑会把「连续变化」也压低。实测通勤距离只有
    // 0.2~0.39（阈值 0.4），于是通勤被误判为驻留并沿路建了 5 个地点。
    //
    // 但通勤有个驻留没有的特征：**持续见到全新 AP**。驻留时 AP 集合
    // 封闭，偶有闪烁但都是老面孔。这条不受平滑影响。
    //
    // ⚠️ 分母是**去重后的全部 AP**，不是 top-8 截断后的 frame.n。
    // 我第一版用了 frame.n，分母偏小让 fresh_ratio 虚高，
    // 结果 821 次扫描被误判成 moving（PC 侧全是 staying）。
    static uint32_t dh[64];
    static int8_t dr[64];
    uint8_t dn = dedup(aps, n, dh, dr, 64);
    uint8_t fresh_n = 0;
    for (uint8_t i = 0; i < dn; i++) {
        if (!bloom_has(c, dh[i])) fresh_n++;
    }
    uint16_t fresh_ratio = dn ? (uint16_t)(fresh_n * SENS_Q / dn) : 0;

    bool window_full = (c->win_n >= SENS_SMOOTH_WINDOW);
    bool is_fresh = window_full && fresh_ratio >= SENS_FRESH_RATIO_THRESHOLD;

    // AP 太少时距离判据不可信（sim 侧栽过：稀疏扫描下一个 AP 掉线
    // 就让相似度暴跌，静坐被判成移动）。用 -1 表示「不可信」。
    int32_t reliable = (dn >= SENS_MIN_APS_FOR_DIST) ? (int32_t)dist : -1;

    // 无新鲜 AP → 否决距离判据。要等窗口填满才启用，否则开机头几次
    // 扫描布隆还空着，fresh_ratio 恒为满，这条永远不触发。
    if (window_full && fresh_ratio == 0) reliable = -1;

    // -- 状态机（迟滞）---------------------------------------------------
    //
    // 判定规则与 sim/sensing.py 的 MotionState.update 逐字对应：
    //
    //     raw = MOVING if ((distance >= 0 and distance > threshold)
    //                      or fresh) else STAYING
    //
    // 注意**没有「保持原状」这一档**：距离不可信时也要给出 raw，
    // 只不过那时只看 fresh。我第一版加了 `want = c->state` 的分支，
    // 让状态黏在 moving 上下不来。
    {
        sens_state_t raw = ((reliable >= 0 && reliable > SENS_MOVE_THRESHOLD)
                            || is_fresh) ? SENS_MOVING : SENS_STAYING;
        if (raw == c->cand) {
            if (c->cand_count < 255) c->cand_count++;
        } else {
            c->cand = raw;
            c->cand_count = 1;
        }
        if (c->cand_count >= SENS_HYSTERESIS) c->state = raw;
    }
    out->state = c->state;

    // -- 瞬现 AP：用**单帧**哈希，不用平滑 -------------------------------
    if (c->has_prev_frame) {
        for (uint8_t i = 0; i < frame.n; i++) {
            bool seen = false;
            for (uint8_t j = 0; j < c->prev_frame.n; j++) {
                if (c->prev_frame.hash[j] == frame.hash[i]) { seen = true; break; }
            }
            if (!seen) out->transient_aps++;
        }
    }

    // -- 地点识别：只在**驻留**时做 --------------------------------------
    //
    // 移动中指纹一直在变，记下来毫无意义，只会把 8 个槽位迅速塞满
    // 并把真正的地点挤掉。
    //
    // 同时看 state 与原始距离：迟滞下移动的第一帧 state 仍是 staying，
    // 只看 state 会把通勤第一帧误建成新地点（sim 侧实测过的 bug）。
    bool settled = (c->state != SENS_MOVING) && (dist <= SENS_MOVE_THRESHOLD);
    if (settled && sig.n) {
        uint16_t score = 0;
        int idx = place_match(c, &sig, &score);
        bool is_new = false;
        if (idx < 0) {
            idx = place_insert(c, &sig, ts);
            is_new = true;
        } else {
            place_merge(&c->places[idx].sig, &sig);
        }
        sens_place_t *p = &c->places[idx];
        out->is_new_place = is_new;
        out->place_id = p->pid;
        out->match_score = score;

        if (c->last_ts && !is_new && ts > c->last_ts) {
            uint32_t d = ts - c->last_ts;
            // 单次间隔超过 1 小时不计驻留 —— 那多半是设备睡过去了
            if (d <= 3600) {
                p->dwell += d;
                if (p->biome < 5) c->dwell_by_biome[p->biome] += d;
            }
        }
        p->last_seen = ts;
    }

    // 更新历史。
    // 记的是**去重后的全部** AP，不是 top-8 —— PC 侧的 seen_hashes
    // 收的是 cur_hashes（单帧全集），少记会让下次 fresh_ratio 虚高。
    for (uint8_t i = 0; i < dn; i++) bloom_add(c, dh[i]);
    c->prev = sig;
    c->has_prev = sig.n > 0;
    c->prev_frame = frame;
    c->has_prev_frame = frame.n > 0;
    c->last_ts = ts;
}

// ---------------------------------------------------------------------------
// 自检 —— 与 PC 侧逐值对账
//
// 感知层错一位地点表就整个失配，而失配**不会报错**：
// 设备只会安静地把家认成新地点，一天认出几十个。
// 所以每次启动都跑一遍，数字对不上立刻在串口喊。
// ---------------------------------------------------------------------------

#include "esp_log.h"

static const char *TAG_ST = "sensing";

bool sens_selftest(void)
{
    bool ok = true;

    // ROM crc32 语义打表 —— 文档的 "init = ~init" 有歧义，
    // 两种解读在 host 上（用 zlib 模拟）都能自圆其说，
    // 只有真机能给出答案。留着这几行，换芯片/换 IDF 时重跑。
    {
        const char *probe = "aa:bb:cc:dd:ee:ff";   // zlib 期望 0xD9CF27A9
        ESP_LOGI(TAG_ST, "ROM 探针: rom(0)=0x%08X ~=0x%08X | "
                         "rom(~0)=0x%08X ~=0x%08X",
                 (unsigned)esp_rom_crc32_le(0U, (const uint8_t *)probe, 17),
                 (unsigned)~esp_rom_crc32_le(0U, (const uint8_t *)probe, 17),
                 (unsigned)esp_rom_crc32_le(~0U, (const uint8_t *)probe, 17),
                 (unsigned)~esp_rom_crc32_le(~0U, (const uint8_t *)probe, 17));
    }

    // ① 哈希 —— 值来自 PC 侧 zlib.crc32(bssid.encode())
    static const struct { uint8_t b[6]; uint32_t want; } HV[] = {
        {{0xaa,0xbb,0xcc,0xdd,0xee,0xff}, 0xD9CF27A9},
        {{0x26,0x18,0xc6,0x19,0x01,0x97}, 0x0877CFCA},
        {{0x00,0x00,0x00,0x00,0x00,0x00}, 0x0ABCC352},
        {{0x60,0x01,0xb1,0xd2,0xb4,0xd4}, 0xEDF619A4},
    };
    for (unsigned i = 0; i < sizeof(HV) / sizeof(HV[0]); i++) {
        uint32_t got = sens_hash_bssid(HV[i].b);
        if (got != HV[i].want) {
            ESP_LOGE(TAG_ST, "哈希不符 #%u: 得 0x%08X 期望 0x%08X"
                             " —— 地点表会与 PC 侧完全失配",
                     i, (unsigned)got, (unsigned)HV[i].want);
            ok = false;
        }
    }

    // ② 权重曲线
    static const struct { int8_t rssi; uint16_t want; } WV[] = {
        {-40, 1024}, {-50, 853}, {-60, 682}, {-70, 512},
        {-80, 341}, {-90, 170}, {-100, 0}, {-30, 1024}, {-110, 0},
    };
    for (unsigned i = 0; i < sizeof(WV) / sizeof(WV[0]); i++) {
        uint16_t got = sens_rssi_weight(WV[i].rssi);
        if (got != WV[i].want) {
            ESP_LOGE(TAG_ST, "权重不符 rssi=%d: 得 %u 期望 %u",
                     WV[i].rssi, got, WV[i].want);
            ok = false;
        }
    }

    // ③ 相似度 —— 权重 853/512/170，交集 1365 并集 1705 → 819
    sens_ap_t a[3] = {
        {{0x11,0,0,0,0,1}, -50, 6, 0},
        {{0x11,0,0,0,0,2}, -70, 6, 0},
        {{0x11,0,0,0,0,3}, -90, 6, 0},
    };
    sens_ap_t b[3] = {
        {{0x11,0,0,0,0,1}, -50, 6, 0},
        {{0x11,0,0,0,0,2}, -70, 6, 0},
        {{0x11,0,0,0,0,4}, -90, 6, 0},
    };
    sens_sig_t sa, sb;
    sens_sig_from_aps(a, 3, &sa);
    sens_sig_from_aps(b, 3, &sb);
    if (sens_similarity(&sa, &sb) != 819) {
        ESP_LOGE(TAG_ST, "相似度不符: 得 %u 期望 819",
                 sens_similarity(&sa, &sb));
        ok = false;
    }
    if (sens_similarity(&sa, &sa) != SENS_Q) {
        ESP_LOGE(TAG_ST, "自相似度应为 %d", SENS_Q);
        ok = false;
    }
    sens_sig_t empty = {0};
    if (sens_similarity(&sa, &empty) != 0) {
        ESP_LOGE(TAG_ST, "空指纹相似度应为 0");
        ok = false;
    }

    // ④ 布隆过滤器 —— 加进去要查得到，没加的多数查不到。
    // static：sens_core_t 有 2.6KB（含 1KB 布隆），栈上放不下。
    static sens_core_t c;
    sens_init(&c);
    bloom_add(&c, 0x12345678);
    if (!bloom_has(&c, 0x12345678)) {
        ESP_LOGE(TAG_ST, "布隆：加了却查不到");
        ok = false;
    }
    unsigned fp = 0;
    for (uint32_t i = 1; i <= 100; i++) {
        if (bloom_has(&c, 0xA0000000u + i * 7919u)) fp++;
    }
    if (fp > 5) {
        ESP_LOGE(TAG_ST, "布隆假阳性过高: %u/100", fp);
        ok = false;
    }

    ESP_LOGI(TAG_ST, "自检 %s（哈希 4 · 权重 9 · 相似度 3 · 布隆 2）",
             ok ? "全部通过" : "**失败**");
    return ok;
}
