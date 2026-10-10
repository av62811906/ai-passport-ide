<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# AI Passport IDE

面向 AI Passport 开发的桌面 IDE。它在电脑上渲染设备的 240x320 界面，让你用虚拟
按键和键盘驱动应用，并附带一个轻量的 C 源码编辑器和应用日志输出。电脑的麦克风可以
像连接了设备音频编解码器一样驱动调音器，首页还可以进入宝可梦（PokeWalk）像素游戏
（其 Wi-Fi 感知与存档在桌面上降级）。

设备界面由**真实的** LVGL 应用代码在主机上构建而成：应用不做任何修改，只是把它的
硬件调用替换成主机桩。它主要是一个**桌面模拟器**；在完整的 ai-passport 固件仓库中
运行时，其烧录页也能把当前应用通过 USB 写入真机。

完整的 IDE / 模拟器文档（窗口布局、快捷键、源码浏览器、主题、外部变更重编译、烧录，
以及主机构建的原理）见 [tools/simulator/README.zh_CN.md](tools/simulator/README.zh_CN.md)。

## 环境要求

- Python 3 及 `tkinter`（macOS 和多数 Linux 发行版自带）。
- 首次构建需要 CMake 和 C/C++ 编译器。
- LVGL 9.5.0 源码。构建会优先使用已存放在 `managed_components/lvgl__lvgl` 的副本；
  否则由 [fetch_lvgl.sh](tools/simulator/fetch_lvgl.sh) 克隆锁定版本。
- 可选：`sounddevice`（配合 `numpy`）用于麦克风输入。没有它时，调音器直接进入
  “无信号”界面。
- 可选：烧录功能需要一份 ESP-IDF 5.5.3（提供 `idf.py` 与 `esptool`），并且面向完整
  的 ai-passport 固件仓库。

## 快速开始

```bash
./tools/simulator/run.sh
```

首次运行会编译 LVGL，需要几分钟；之后的运行是增量编译。

## 用法

```bash
./tools/simulator/run.sh                 # 构建并启动
./tools/simulator/run.sh --theme light   # 浅色配色
./tools/simulator/run.sh --scale 1       # 固定 1 倍缩放，而非自动适配
./tools/simulator/run.sh --fullscreen    # 以全屏启动
./tools/simulator/run.sh --audio off     # 关闭麦克风输入
./tools/simulator/build.sh               # 只构建，不启动
```

## 仓库结构

| 路径 | 用途 |
| --- | --- |
| [tools/simulator/](tools/simulator) | IDE 与主机模拟器构建（详见其 README） |
| [main/](main) | 被测试的应用（首页、尤克里里调音器、宝可梦游戏）及其纯逻辑 |
| [components/bsp/](components/bsp) | 模拟器构建所需的 BSP 头文件与源码 |
| [assets/](assets) | 应用界面的 LVGL 字体子集与宝可梦的嵌入式资源 |
| [skills/ai-passport-ide/](skills/ai-passport-ide) | 配套的智能体 Skill：启动 IDE 并读取其会话日志 |
| [tests/](tests) | IDE 纯 Python 模块的主机测试 |

`main/`、`components/`、`assets/` 是上游
[ai-passport](https://github.com/folotoy/ai-passport) 固件中被模拟器在主机上编译的
子集。把它们放在这里，是为了让本仓库无需完整的 ESP-IDF 环境即可独立构建。

## 配套 Skill

[skills/ai-passport-ide](skills/ai-passport-ide) 让编码智能体启动本 IDE 并读取会话日志
（`build/simulator/ide.log`）。把该目录复制或软链到你的客户端 skills 目录即可使用——
例如 Trae 的 `~/.trae-cn/skills/`，或项目的 `.trae/skills/`。

## 测试

```bash
python3 -m pytest tests/ -q
```

## 状态

已实现：上述桌面 IDE 与模拟器——麦克风驱动的调音器、宝可梦游戏、保存与外部变更触发的
原地重编译，以及 USB 烧录（设备发现，加上增量与完整镜像两种模式；需在完整固件仓库中
运行）。后续计划：通过设备的 USB 串口协议流式传输画面并注入按键。