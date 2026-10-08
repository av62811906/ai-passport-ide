// main/tuner_pitch.h —— 尤克里里调音器的纯逻辑部分:音高检测、音名映射与读数平滑。
//
// 本模块刻意【不依赖 ESP-IDF 与 LVGL】,只处理调用方已经采集好的 16bit 单声道 PCM 块,
// 这样音高算法、音分换算与平滑都能在主机上做确定性测试(tests/test_tuner_pitch.c)。
// 麦克风采样与音频读写属于硬件侧,由 main/app_tuner.c 通过 bsp_audio_read() 完成。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 标准尤克里里四根弦。枚举顺序 = 界面上从左到右的弦块顺序。
// 注意 G4 是【高音弦】:标准 re-entrant 定弦下 4 弦 G4 比 3 弦 C4 高,不是从低到高排列。
typedef enum {
    TUNER_STRING_G = 0,      // 4 弦,G4
    TUNER_STRING_C = 1,      // 3 弦,C4
    TUNER_STRING_E = 2,      // 2 弦,E4
    TUNER_STRING_A = 3,      // 1 弦,A4
    TUNER_STRING_COUNT = 4,
} tuner_string_t;

// A4 = 440Hz 十二平均律基准。应用当前为固定值,不做校准(仅调音器核心页)。
#define TUNER_A4_HZ 440.0f

// |偏差| 小于该音分即判定"音准"。
#define TUNER_IN_TUNE_CENTS 5.0f

// 指针表量程:横轴为 -TUNER_METER_CENTS .. +TUNER_METER_CENTS。
#define TUNER_METER_CENTS 50.0f

// 音高搜索频带。下限低于 C4 且留出降弦余量;上限 520Hz 刻意卡在 C4 的
// 二次谐波(523Hz)之下,让"基频很弱、谐波很强"的拨弦不会被判成高八度。
#define TUNER_SEARCH_MIN_HZ 220.0f
#define TUNER_SEARCH_MAX_HZ 520.0f

// YIN 差分函数的最大滞后。16000 / 96 ≈ 167Hz,足以覆盖 C4(261.6Hz)并留出降弦余量。
// 也决定了 scratch 内两个数组的长度。
#define TUNER_PITCH_MAX_LAG 96

// 判定"有人在拨弦"的最小 RMS(满量程 32767)。低于该值视为静音/环境底噪,不输出读数。
#define TUNER_PITCH_MIN_RMS 120.0f

// YIN 的绝对阈值与兜底阈值(见 tuner_pitch_detect 内注释)。
#define TUNER_YIN_THRESHOLD 0.15f
#define TUNER_YIN_FALLBACK 0.45f

// 音高检测的工作缓冲。由调用方分配(可放在任务栈或静态区),使本模块零堆分配。
// 约 776 字节,内部只读/写自身,不做跨实例共享。
typedef struct {
    float diff[TUNER_PITCH_MAX_LAG + 1];   // YIN 差分函数 d(tau)
    float cmnd[TUNER_PITCH_MAX_LAG + 1];   // 累积均值归一化后的 d'(tau)
} tuner_pitch_scratch_t;

// 单帧检测结果。
typedef struct {
    bool valid;             // 本帧是否得到可信音高
    float frequency_hz;     // 检测到的基频;invalid 时为 0
    float cents;            // 相对【最近弦】的偏差音分(+ 偏高 / - 偏低);invalid 时无意义
    float clarity;          // 0..1,越大越可信(1 - d'(tau))
    float rms;              // 本帧电平,可用于判断是否在拨弦
    tuner_string_t string;  // 最接近的弦(自动模式下即识别结果)
} tuner_pitch_result_t;

// 清零工作缓冲。首次使用前调用一次即可;检测函数本身不要求缓冲是干净的。
void tuner_pitch_scratch_reset(tuner_pitch_scratch_t *scratch);

// 在 samples[0..count) 内检测基频。
//
// sample_rate_hz: 采样率;min_hz/max_hz: 搜索频带(应包含全部目标弦且排除高八度伪峰);
// a4_hz: 用于音名映射的 A4 基准;输出写入 out。
// 返回 true 表示 out->valid 为真。函数不分配内存、不加锁,可在音频任务里直接调用。
// 频率精度受窗口长度限制:16kHz/1024 点下 C4 约 0.3 音分。
bool tuner_pitch_detect(const int16_t *samples, size_t count, uint32_t sample_rate_hz,
                        float min_hz, float max_hz, float a4_hz,
                        tuner_pitch_scratch_t *scratch, tuner_pitch_result_t *out);

// 某根弦在给定 A4 基准下的目标频率。
float tuner_string_frequency_hz(tuner_string_t string, float a4_hz);

// 与 frequency_hz 音高最接近的弦(按音分距离取最近)。
tuner_string_t tuner_nearest_string(float frequency_hz, float a4_hz);

// 从 reference_hz 到 frequency_hz 的音分偏差,1200 * log2(f / ref)。
float tuner_cents_between(float frequency_hz, float reference_hz);

// 一段 PCM 的 RMS 电平。
float tuner_rms(const int16_t *samples, size_t count);

// 非对称指数平滑:读数增大用 alpha_up,减小用 alpha_down(0..1,越小越平滑)。
// 指针表需要"快速跟上、缓慢回落",故两个方向取不同系数。
float tuner_smooth(float previous, float measured, float alpha_up, float alpha_down);

// 把音分偏差映射成指针在表上的横向比例(0.0 = 最左 -50 音分,1.0 = 最右 +50 音分)。
// 超出量程时钳位到 0/1,不会把指针推出轨道。属于布局计算,故放在纯逻辑里一起测。
float tuner_meter_ratio(float cents);

// 在四根弦之间循环选弦,+1 向下一个,-1 向上一个,自动回绕。
tuner_string_t tuner_string_step(tuner_string_t current, int delta);

// 决定本次读数展示的目标弦:自动模式用识别结果,手动模式用用户选中的弦。
// detected 越界时回退到 selected,保证返回值一定合法。
tuner_string_t tuner_resolve_target(bool auto_mode, tuner_string_t selected,
                                    tuner_string_t detected);
