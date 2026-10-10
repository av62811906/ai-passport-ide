// main/app_texts.h —— 首页界面出现的全部文字(单一事实来源)。
//
// 与 tuner_texts.h 一样,这里的中文/符号会随 assets/fonts/tuner_font_20.c 一起被约束:
//   * tools/gen_tuner_font.sh 扫描 main/*_texts.h,从字面量生成字体子集;
//   * tests/test_tuner_font.py 反向校验字体确实覆盖了这些码点。
// 因此【改动这里的任何字符串,都要重新生成字体】,否则字形会变成空白/方框。
// ASCII 部分由字体的 0x20-0x7E 区间覆盖,不用单独列举。
#pragma once

// 顶栏:产品标签 + 用户名(ASCII,由可打印 ASCII 区间覆盖)。
#define APP_TEXT_BRAND      "AI 护照"
#define APP_TEXT_USER_NAME  "riiki"

// 功能卡片:调音器。
#define APP_TEXT_HOME_TUNER_TITLE "调音器"
#define APP_TEXT_HOME_TUNER_SUB   "尤克里里 · 标准调弦"

// 功能卡片:宝可梦像素游戏(移植自 PokeWalk)。
#define APP_TEXT_HOME_POKEMON_TITLE "宝可梦"
#define APP_TEXT_HOME_POKEMON_SUB   "像素冒险"

// 底部按键提示。
#define APP_TEXT_HOME_HINT  "上下选择 · 确定进入"
