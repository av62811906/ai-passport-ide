<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# AI Passport 桌面开发 IDE（模拟器）

一个为 AI Passport 开发准备的轻量桌面工具。它把设备 240x320 的界面渲染到电脑上，
用虚拟按键和键盘驱动应用，并在下方显示应用自身的日志输出。电脑的麦克风可以像设备的
音频 codec 一样驱动调音器。

窗口按小型 IDE 的样式排布：菜单栏与工具栏、带侧栏的活动栏、编辑器区域（设备画面固定在
其最右边缘）、底部日志面板与状态栏。画面会随窗口尺寸（含全屏）自动等比适配，深/浅两套
配色可在运行时切换。

这是**桌面模拟器**：它运行在电脑上，暂不连接真机；真机联调是后续阶段的工作。

## 环境要求

- Python 3 且带 `tkinter`（macOS 与多数 Linux 发行版自带）。
- 首次构建需要 CMake 与 C/C++ 编译器。
- LVGL 9.5.0 源码。构建优先使用设备构建已下载的
  `managed_components/lvgl__lvgl`；若不存在，则由 [fetch_lvgl.sh](fetch_lvgl.sh)
  克隆固定版本。
- 可选：麦克风输入需要 `sounddevice`（及其 `numpy`）。没有它时，调音器只显示
  "无信号"界面。

## 快速开始

```bash
./tools/simulator/run.sh
```

首次运行会编译 LVGL，需要几分钟；之后是增量编译。当
`managed_components/lvgl__lvgl` 已存在时，构建无需联网。

## 用法

```bash
./tools/simulator/run.sh                 # 构建后启动
./tools/simulator/run.sh --theme light   # 浅色配色
./tools/simulator/run.sh --scale 1       # 固定 1 倍缩放，替代自动适配
./tools/simulator/run.sh --fullscreen    # 以全屏启动
./tools/simulator/run.sh --audio off     # 关闭麦克风输入
./tools/simulator/build.sh               # 只构建，不启动
python3 tools/simulator/passport_ide.py --lib <path>   # 指定库路径
```

- 设备画面**固定在编辑器区域的最右边缘**，默认自动适配该区域，在任意窗口尺寸（含全屏）
  下保持 240x320 的宽高比。传 `--scale N` 则改为固定 N 倍缩放；工具栏与"视图"菜单可切回
  自动适配。
- 虚拟按键：设备画面下方的**上**、**确定**、**下**。单击发送 `CLICK`；按住超过 700ms
  发送 `LONG`。
- 键盘：`↑` / `↓` / `Enter`（或 `Space`）对应同样三个按键。
- 快捷键：`F11` 切换全屏（`Esc` 退出全屏）、`Cmd/Ctrl+0` 适应窗口、
  `Cmd/Ctrl+=` 与 `Cmd/Ctrl+-` 放大/缩小、`Cmd/Ctrl+S` 保存当前源码文件、
  `Cmd/Ctrl+W` 关闭当前标签、`Cmd/Ctrl+R` 保存全部并重新编译、
  `Cmd/Ctrl+L` 清空日志、`Cmd/Ctrl+Shift+T` 切换主题。
- 活动栏用于切换侧栏内容：资源管理器、设备信息、麦克风状态、快捷键列表。底部面板
  实时显示应用的 `ESP_LOGx` 日志（错误与警告着色）；状态栏显示后端、分辨率、缩放、背光、
  音频与主题。
- `--theme` 选择配色：`dark`（默认）或 `light`。
- `--audio` 选择麦克风输入：`auto`（默认，可用则启用）、`on`、`off`。对着麦克风拨弦
  或哼一个音，调音器会有反应；macOS 首次使用会请求麦克风权限。

## 资源管理器与代码编辑器

侧栏的 `文`（资源管理器）页列出要拷贝进 AI Passport 的应用源码：`main/` 下的全部
`*.c` 与 `*.h`，以及 `CMakeLists.txt`。单击文件即可在编辑器区域（设备画面左侧）打开
对应标签；源码标签可关闭，存在未保存改动时文件名前显示 `●`。

- 编辑器带行号栏、当前行高亮与 C 语法高亮（关键字、预处理行、字符串、数字、注释、函数
  调用）；`Tab` 插入 4 个空格。
- `Cmd/Ctrl+S` 把当前文件写回磁盘：保持原有的 LF/CRLF 换行风格，采用"临时文件 + 改名"
  的原子写入。写盘被限制在 `main/` 之内，解析后落在目录外（含通过符号链接绕出）一律拒绝。
- **保存会自动重新编译仿真库并重启本 IDE**，改动立刻反映到设备画面。增量编译通常几秒；
  日志面板实时输出编译信息并把错误行标红。编译失败时会恢复上一次的库，IDE 继续用旧代码运行。
- 由于运行中的进程无法重新加载已映射的同一个共享库，构建前会把库改名让开、构建后由新进程
  加载它：窗口会闪一下重新打开。
- `Cmd/Ctrl+R` 无需改动也可跑一遍同样的流程。
- `Cmd/Ctrl+W` 关闭当前源码标签（有未保存改动会先询问）；直接关窗口时会一次性汇总询问。

## 工作原理

IDE 是一个 Tkinter 窗口，所有渲染与输入都通过一个用 `ctypes` 加载的小型共享库
`libpassport_sim` 完成：

```text
Tkinter IDE  --ctypes-->  libpassport_sim  (主机构建)
   |  键盘 / 虚拟按键           |- 真实 LVGL 9.5
   \- 麦克风 ---------------->  |- 真实应用 UI 代码 (main/app_tuner.c, ...)
        (sounddevice)          |- 模拟显示驱动 -> 240x320 RGB565 帧缓冲
                               |- 音频环形缓冲 -> bsp_audio_read()
                               \- ESP-IDF / BSP / FreeRTOS 主机桩
```

- 应用代码不做改动，只是它的硬件调用被替换为桩。电量计不存在（顶栏显示 `--`），
  音频则在启用后由主机麦克风喂入。
- 启用音频时，调音器的采集任务运行在真正的宿主线程上：FreeRTOS 接口被映射到
  pthread，`bsp_audio_read()` 从麦克风回调填充的环形缓冲取数。该线程与 IDE 主线程
  通过应用所用的同一把递归锁共享 LVGL，因此 `sim_step()` 也会获取它。
- 显示驱动把每次 LVGL flush 写进静态整屏缓冲，并施加与固件相同的圆角遮罩，
  所以画面与真机一致。
- 时间由 IDE 掌控：`sim_step()` 推进 LVGL 的 tick，因此停止刷新循环即可冻结界面。

## 目录结构

| 路径 | 作用 |
| --- | --- |
| [passport_ide.py](passport_ide.py) | Tkinter IDE 外壳：右侧固定画面、编辑器标签、资源管理器接线、保存/构建动作、主题 |
| [ide_editor.py](ide_editor.py) | 可复用组件：自绘滚动条、资源管理器、C 代码编辑器、编辑器标签栏 |
| [ide_sources.py](ide_sources.py) | 源码模型：文件清单、写盘路径校验、换行风格、C 词法 |
| [ide_layout.py](ide_layout.py) | 画面自动适配的缩放数学（纯函数，含主机测试） |
| [backend.py](backend.py) | Backend 抽象与 `ctypes` 模拟器后端 |
| [audio_input.py](audio_input.py) | 麦克风采集，喂给模拟器的音频输入 |
| [run.sh](run.sh) | 构建后启动 |
| [build.sh](build.sh) | 构建模拟器共享库 |
| [fetch_lvgl.sh](fetch_lvgl.sh) | 确保 LVGL 9.5.0 可用 |
| [host/](host) | 主机构建：`sim.c`、`sim_display.c`、`sim_audio.c`、`sim_platform.c`、`lv_conf.h` 与桩头文件 |

## 对接其他应用

模拟器当前在 [host/sim.c](host/sim.c) 中调用 `app_tuner_start()` 启动调音器。
若要换一个应用，修改这一处调用，并把该应用的源文件加入
[host/CMakeLists.txt](host/CMakeLists.txt) 即可。

## 常见问题

- 提示 `Simulator library not found` —— 先运行 `./tools/simulator/build.sh`。
- 提示 `sim_init() failed` —— 查看报错前打印的 LVGL 错误信息。
- 提示 `Microphone unavailable` —— 缺少 `sounddevice` 或麦克风被拒绝。用
  `pip install sounddevice` 安装，并允许你的终端访问麦克风；此时 IDE 仍以
  "无信号"界面运行。
- 窗口显示调音器的"无信号"界面 —— 没有麦克风输入时正常；顶栏电量显示 `--` 也属
  预期，桌面没有电量计。

## 现状与后续

已实现：上述桌面模拟器与 IDE，包括通过真实采集任务驱动调音器的麦克风输入。后续计划：
通过 USB 串口连接真机（画面流式传输加按键注入，沿用仓库中的
[串口截图协议](../../docs/reference/y2lin/serial-screenshot-protocol.md)），以及在
IDE 内集成编译/烧录操作。
