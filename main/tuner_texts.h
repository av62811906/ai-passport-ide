// main/tuner_texts.h —— 调音器界面出现的全部文字(单一事实来源)。
//
// 这里的中文会随 assets/fonts/tuner_font_20.c 一起被约束:
//   * tools/gen_tuner_font.sh 从 main/*_texts.h 提取字面量生成字体子集;
//   * tests/test_tuner_font.py 反向校验字体确实覆盖了这些头文件的每个码点。
// 因此【改动这里的任何字符串,都要重新生成字体】,否则字形会变成空白/方框。
// ASCII 部分由字体的 0x20-0x7E 区间覆盖,不用单独列举。
#pragma once

// 界面标题与乐器名。
#define TUNER_TEXT_TITLE       "调音器"
#define TUNER_TEXT_INSTRUMENT  "尤克里里"

// 中央提示与判定结果。
#define TUNER_TEXT_PLUCK       "请拨动琴弦"
#define TUNER_TEXT_FLAT        "偏低"
#define TUNER_TEXT_SHARP       "偏高"
#define TUNER_TEXT_IN_TUNE     "音准"
#define TUNER_TEXT_NO_AUDIO    "无信号"

// 音分读数单位。
#define TUNER_TEXT_CENTS_UNIT  "音分"

// 模式与按键提示。
#define TUNER_TEXT_MODE_AUTO   "自动"
#define TUNER_TEXT_MODE_MANUAL "手动"
#define TUNER_TEXT_KEY_STRING  "上下选弦"
#define TUNER_TEXT_KEY_MODE    "确定切换"
