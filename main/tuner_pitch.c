// main/tuner_pitch.c —— 见 tuner_pitch.h 的接口说明。
//
// 算法选用 YIN(差分函数 + 累积均值归一化),而不是朴素自相关:
//   1) 尤克里里拨弦谐波丰富,朴素自相关的"取全局最大"容易被倍周期(高八度)骗到;
//      YIN 的累积均值归一化会压制过早出现的浅谷,取"第一个足够深的谷"更接近基频周期。
//   2) ESP32-C3 没有硬件浮点,软浮点乘法代价高。差分累加全程用 int64 整数乘加,
//      每个滞后只在归一化时做一次浮点除法,实测一帧(1024 点)约 1~2ms,可稳定跑在
//      32ms 的刷新节奏里。
#include "tuner_pitch.h"

#include <math.h>
#include <string.h>

// A4 到各弦的半音距离:G4 在 A4 下方 2 个半音,C4 下方 9 个,E4 下方 5 个。
static const int STRING_SEMITONES_FROM_A4[TUNER_STRING_COUNT] = {
    [TUNER_STRING_G] = -2,
    [TUNER_STRING_C] = -9,
    [TUNER_STRING_E] = -5,
    [TUNER_STRING_A] = 0,
};

void tuner_pitch_scratch_reset(tuner_pitch_scratch_t *scratch) {
    if (scratch == NULL) return;   // 允许空指针,便于上层按需跳过清理
    memset(scratch, 0, sizeof(*scratch));
}

float tuner_string_frequency_hz(tuner_string_t string, float a4_hz) {
    if (string < 0 || string >= TUNER_STRING_COUNT) return 0.0f;
    if (!(a4_hz > 0.0f)) return 0.0f;
    return a4_hz * exp2f((float)STRING_SEMITONES_FROM_A4[string] / 12.0f);
}

float tuner_cents_between(float frequency_hz, float reference_hz) {
    if (!(frequency_hz > 0.0f) || !(reference_hz > 0.0f)) return 0.0f;
    return 1200.0f * log2f(frequency_hz / reference_hz);
}

tuner_string_t tuner_nearest_string(float frequency_hz, float a4_hz) {
    tuner_string_t best = TUNER_STRING_A;
    float best_distance = 0.0f;
    bool found = false;

    for (int i = 0; i < TUNER_STRING_COUNT; ++i) {
        float target = tuner_string_frequency_hz((tuner_string_t)i, a4_hz);
        if (!(target > 0.0f)) continue;
        float distance = fabsf(tuner_cents_between(frequency_hz, target));
        if (!found || distance < best_distance) {
            best_distance = distance;
            best = (tuner_string_t)i;
            found = true;
        }
    }
    return best;
}

float tuner_rms(const int16_t *samples, size_t count) {
    if (samples == NULL || count == 0) return 0.0f;

    // 1024 个满量程样本的平方和 ≈ 1.1e12,必须用 64bit 累加,否则溢出。
    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t value = samples[i];
        sum += (int64_t)value * value;
    }
    return sqrtf((float)sum / (float)count);
}

float tuner_smooth(float previous, float measured, float alpha_up, float alpha_down) {
    float alpha = (measured > previous) ? alpha_up : alpha_down;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    return previous + alpha * (measured - previous);
}

float tuner_meter_ratio(float cents) {
    float clamped = cents;
    if (!(clamped > -TUNER_METER_CENTS)) clamped = -TUNER_METER_CENTS;   // 同时兜住 NaN
    if (clamped > TUNER_METER_CENTS) clamped = TUNER_METER_CENTS;
    return (clamped + TUNER_METER_CENTS) / (2.0f * TUNER_METER_CENTS);
}

tuner_string_t tuner_string_step(tuner_string_t current, int delta) {
    int index = (int)current;
    if (index < 0 || index >= TUNER_STRING_COUNT) index = 0;

    // 取模前先加一圈,避免 C 的负数取模得到负结果。
    index = (index + delta % TUNER_STRING_COUNT + TUNER_STRING_COUNT) % TUNER_STRING_COUNT;
    return (tuner_string_t)index;
}

tuner_string_t tuner_resolve_target(bool auto_mode, tuner_string_t selected,
                                    tuner_string_t detected) {
    if (selected < 0 || selected >= TUNER_STRING_COUNT) selected = TUNER_STRING_A;
    if (auto_mode && detected >= 0 && detected < TUNER_STRING_COUNT) return detected;
    return selected;
}

// YIN 第 2 步差分函数 d(tau) = Σ (x[i] - x[i+tau])²,全程整数运算(见文件头说明)。
// 注意这里不需要先去直流:(x[i]-mean) - (x[i+tau]-mean) 恒等于 x[i]-x[i+tau],
// 常数直流项在差分里天然抵消,省掉一次遍历。
static void compute_difference(const int16_t *samples, size_t window, int max_lag,
                              tuner_pitch_scratch_t *scratch) {
    scratch->diff[0] = 0.0f;
    for (int tau = 1; tau <= max_lag; ++tau) {
        int64_t acc = 0;
        for (size_t i = 0; i < window; ++i) {
            int32_t delta = (int32_t)samples[i] - (int32_t)samples[i + tau];
            acc += (int64_t)delta * delta;
        }
        scratch->diff[tau] = (float)acc;
    }
}

// YIN 第 3 步:累积均值归一化 d'(tau) = d(tau) * tau / Σ_{k=1..tau} d(k)。
static void compute_cmnd(tuner_pitch_scratch_t *scratch, int max_lag) {
    float running = 0.0f;
    scratch->cmnd[0] = 1.0f;
    for (int tau = 1; tau <= max_lag; ++tau) {
        running += scratch->diff[tau];
        scratch->cmnd[tau] = (running > 0.0f)
                           ? (scratch->diff[tau] * (float)tau / running)
                           : 1.0f;
    }
}

// YIN 第 4 步:取第一个低于绝对阈值的谷;够不着阈值时退化为取区间最小谷并检查清晰度。
// 返回选中的滞后,-1 表示本帧不可信。
static int select_lag(const tuner_pitch_scratch_t *scratch, int min_lag, int max_lag) {
    int chosen = -1;
    for (int tau = min_lag; tau <= max_lag; ++tau) {
        if (scratch->cmnd[tau] < TUNER_YIN_THRESHOLD) {
            chosen = tau;
            break;
        }
    }

    if (chosen < 0) {
        // 拨弦起音瞬间或基频很弱时可能整段都不低于阈值,此时退回"最深的谷",
        // 但仍要求它足够深,避免把噪声当成音高。
        float best = 1.0f;
        for (int tau = min_lag; tau <= max_lag; ++tau) {
            if (scratch->cmnd[tau] < best) {
                best = scratch->cmnd[tau];
                chosen = tau;
            }
        }
        if (chosen < 0 || best > TUNER_YIN_FALLBACK) return -1;
        return chosen;
    }

    // 顺着谷底继续下降到局部极小,避免停在阈值边界上导致读数偏低。
    while (chosen + 1 <= max_lag && scratch->cmnd[chosen + 1] < scratch->cmnd[chosen]) {
        ++chosen;
    }
    return chosen;
}

// YIN 第 5 步:在选中滞后附近做抛物线插值,拿到亚采样精度的周期。
static float refine_lag(const tuner_pitch_scratch_t *scratch, int chosen, int max_lag) {
    if (chosen <= 1 || chosen >= max_lag) return (float)chosen;

    float y0 = scratch->diff[chosen - 1];
    float y1 = scratch->diff[chosen];
    float y2 = scratch->diff[chosen + 1];
    float denom = y0 - 2.0f * y1 + y2;
    if (fabsf(denom) < 1e-6f) return (float)chosen;

    float delta = 0.5f * (y0 - y2) / denom;
    if (delta <= -1.0f || delta >= 1.0f) return (float)chosen;   // 插值结果不可信就不动
    return (float)chosen + delta;
}

bool tuner_pitch_detect(const int16_t *samples, size_t count, uint32_t sample_rate_hz,
                        float min_hz, float max_hz, float a4_hz,
                        tuner_pitch_scratch_t *scratch, tuner_pitch_result_t *out) {
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    out->string = TUNER_STRING_A;

    if (samples == NULL || scratch == NULL || sample_rate_hz == 0) return false;
    if (!(min_hz > 0.0f) || !(max_hz > min_hz)) return false;

    out->rms = tuner_rms(samples, count);
    if (out->rms < TUNER_PITCH_MIN_RMS) return false;

    // 频率越高周期越短:滞后区间 [fs/max_hz, fs/min_hz]。
    int min_lag = (int)floorf((float)sample_rate_hz / max_hz);
    int max_lag = (int)ceilf((float)sample_rate_hz / min_hz);
    if (min_lag < 1) min_lag = 1;
    if (max_lag > TUNER_PITCH_MAX_LAG) max_lag = TUNER_PITCH_MAX_LAG;
    if (max_lag <= min_lag + 1) return false;

    // 所有滞后都用同一个比较长度,否则长滞后样本更少、d 天然更小,会偏向长周期。
    // 必须先比较再相减:count < max_lag 时 count - max_lag 会下溢成极大 size_t,
    // 后面的循环就会越界读。
    if (count < (size_t)max_lag + 64u) return false;
    size_t window = count - (size_t)max_lag;

    compute_difference(samples, window, max_lag, scratch);
    compute_cmnd(scratch, max_lag);

    int chosen = select_lag(scratch, min_lag, max_lag);
    if (chosen < 0) return false;

    float refined = refine_lag(scratch, chosen, max_lag);
    if (!(refined > 0.0f)) return false;

    float frequency = (float)sample_rate_hz / refined;
    if (frequency < min_hz || frequency > max_hz) return false;

    out->valid = true;
    out->frequency_hz = frequency;
    out->clarity = 1.0f - scratch->cmnd[chosen];
    out->string = tuner_nearest_string(frequency, a4_hz);
    out->cents = tuner_cents_between(frequency, tuner_string_frequency_hz(out->string, a4_hz));
    return true;
}
