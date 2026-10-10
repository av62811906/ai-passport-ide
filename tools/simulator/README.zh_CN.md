<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# AI Passport 桌面开发 IDE（模拟器）

一个为 AI Passport 开发准备的轻量桌面工具。它把设备 240x320 的界面渲染到电脑上，
用虚拟按键和键盘驱动应用，并在下方显示应用自身的日志输出。电脑的麦克风可以像设备的
音频 codec 一样驱动调音器，首页也能进入宝可梦像素游戏。在桌面上，游戏的 Wi-Fi 感知
与存档会降级（没有射频；存档只在内存里），但游戏本身可以正常渲染和游玩。

窗口按小型 IDE 的样式排布：菜单栏与工具栏、带侧栏的活动栏、编辑器区域（设备画面固定在
其最右边缘）、底部日志面板与状态栏。画面会随窗口尺寸（含全屏）自动等比适配，深/浅两套
配色可在运行时切换。

它主要是一个**桌面模拟器**；在完整的 ai-passport 固件仓库中运行时，它也能发现电脑上的
串口设备，并把当前应用通过 USB 烧录到真机。

## 环境要求

- Python 3 且带 `tkinter`（macOS 与多数 Linux 发行版自带）。
- 首次构建需要 CMake 与 C/C++ 编译器。
- LVGL 9.5.0 源码。构建优先使用设备构建已下载的
  `managed_components/lvgl__lvgl`；若不存在，则由 [fetch_lvgl.sh](fetch_lvgl.sh)
  克隆固定版本。
- 可选：麦克风输入需要 `sounddevice`（及其 `numpy`）。没有它时，调音器只显示
  "无信号"界面。
- 可选：烧录按钮需要一份 ESP-IDF 5.5.3（提供 `idf.py` 与 `esptool`）。模拟器与
  设备画面本身不需要它。

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
  `Cmd/Ctrl+L` 清空日志、`Cmd/Ctrl+M` 开关麦克风、`Cmd/Ctrl+Shift+T` 切换主题。
- 活动栏用于切换侧栏内容：资源管理器、设备信息、烧录页、麦克风状态、快捷键列表。底部面板
  实时显示应用的 `ESP_LOGx` 日志（错误与警告着色）；状态栏显示后端、分辨率、缩放、背光、
  音频与主题。
- 面板里的每一行也会同步写入 `build/simulator/ide.log`（每次启动时清空），因此窗口运行时
  也能从终端或 Agent 读取本次会话日志；`Cmd/Ctrl+L` 会同时清空面板与日志文件。
- `--theme` 选择配色：`dark`（默认）或 `light`。
- `--audio` 选择麦克风输入：`auto`（默认，可用则启用）、`on`、`off`。对着麦克风拨弦
  或哼一个音，调音器会有反应；macOS 首次使用会请求麦克风权限。
- 运行期间也可以开关麦克风——工具栏的**麦克风**按钮、**音频**菜单、音频页的按钮，或
  `Cmd/Ctrl+M`。关闭会停止主机采集，调音器只收到静音；开启则重新打开采集。由于设备是在
  进入调音器时才决定是否启动采集任务，开关会重启仿真实例，设备画面因此回到首页。

## 资源管理器与代码编辑器

侧栏的 `文`（资源管理器）页列出要拷贝进 AI Passport 的应用源码：`main/` 下的全部
`*.c` 与 `*.h`，以及 `CMakeLists.txt`。单击文件即可在编辑器区域（设备画面左侧）打开
对应标签；源码标签可关闭，存在未保存改动时文件名前显示 `●`。

- 编辑器带行号栏、当前行高亮与 C 语法高亮（关键字、预处理行、字符串、数字、注释、函数
  调用）；`Tab` 插入 4 个空格。
- `Cmd/Ctrl+S` 把当前文件写回磁盘：保持原有的 LF/CRLF 换行风格，采用"临时文件 + 改名"
  的原子写入。写盘被限制在 `main/` 之内，解析后落在目录外（含通过符号链接绕出）一律拒绝。
- **保存会自动重新编译仿真库并在原位热更新**，改动立刻反映到设备画面。增量编译通常几秒；
  日志面板实时输出编译信息并把错误行标红。编译失败时会恢复上一次的库，IDE 继续用旧代码运行。
- 应用代码跑在 IDE 进程内，一旦运行期崩溃会直接带崩整个 IDE。因此每次热更新前会先在一个
  一次性进程里试跑新库（首页 → 调音器 → 回到首页）：启动即崩溃的库会在日志里报出并跳过本次
  热更新，IDE 继续运行原来的库。
- 热更新会保留窗口、已打开的编辑器标签与日志；只有被仿真的应用重新启动，因此画面回到首页。
- 运行中的进程无法重新加载同一路径上已映射的共享库，所以每次热更新会加载一份新编译库的私有副本；
  被替换掉的库映像会在本次会话内一直保留（每次更新一个）。
- `Cmd/Ctrl+R` 无需改动也可跑一遍同样的流程。
- `Cmd/Ctrl+W` 关闭当前源码标签（有未保存改动会先询问）；直接关窗口时会一次性汇总询问。

## 外部改动

在 IDE 之外做的改动——其他编辑器里改的，或 AI 改的——会被自动感知。IDE 会轮询构建实际读取的源码
（`main/`、`components/bsp/include/`、`components/bsp/src/`、`assets/fonts/`、
`tools/simulator/host/`），在改动停止约一秒后刷新资源管理器、重新加载已打开的编辑器，并重新编译仿真库。

- 连续多次改动会合并为一次重编。
- 在 IDE 内有未保存改动的文件不会被磁盘上的内容覆盖，只会在日志里提示。
- 编译失败时保留上一次的库，IDE 继续用旧代码运行，与保存时的表现一致。
- 启动时若发现源码比仿真库新，会先重编一次，避免漏掉 IDE 关闭期间的改动。
- 新增的 `.c` 文件需要先加进 `host/CMakeLists.txt` 才会被编译。

## 烧录到设备

IDE 也能把当前应用通过 USB 写入真机。活动栏的"烧"（烧录）页会列出电脑当前可用的串口设备；
打开该页时会持续刷新；工具栏的"烧录"按钮则直接走最常用的增量烧录。

- USB 身份看起来是 ESP32 的设备会被标记并默认选中。有 `pyserial` 时优先用它枚举（能拿到
  VID/PID）；没有时退回扫描系统设备节点（macOS 为 `/dev/cu.*`，Linux 为 `/dev/ttyUSB*` 与
  `/dev/ttyACM*`），只保留命名像 USB 串口适配器的项，因此蓝牙伪串口会被过滤掉。
- 页面会显示将使用的 ESP-IDF 目录：依次探测 `AI_PASSPORT_IDF_ROOT`、`IDF_PATH`，再到常见
  安装位置（`~/esp/esp-idf-v5.5.3`、`~/esp/esp-idf`、`/opt/esp-idf`）。支持版本为 ESP-IDF
  5.5.3；从 `idf.py --version` 能报出该版本的 shell 启动 IDE，或设置
  `AI_PASSPORT_IDF_ROOT` 即可。IDE 会自行 source 该目录的 `export.sh`，无需提前激活。
- **增量烧录**：编译当前 `main/` 源码并用 `idf.py -p PORT flash` 写入。这是分段式开发烧录，
  会保留设备上的 NVS（存档等）。
- **完整镜像**：先运行 `./tools/validate.sh --firmware`，再用 `esptool` 从 `0x0` 写入已验证的
  合并镜像。这与交付路径一致，但可能重置设备 NVS，因此会先弹出确认。
- 两条命令都在后台执行，输出实时流入底部日志面板，状态栏显示进度；成功会提示"烧录完成"，
  失败会指向日志。

烧录需要 POSIX shell（macOS/Linux）与上述 ESP-IDF 目录；模拟器本身两者都不需要。构建请求、
设备已连接或烧录成功，都不代表固件在真机上的行为已被验证——那仍需要真机验收。

## 工作原理

IDE 是一个 Tkinter 窗口，所有渲染与输入都通过一个用 `ctypes` 加载的小型共享库
`libpassport_sim` 完成：

```text
Tkinter IDE  --ctypes-->  libpassport_sim  (主机构建)
   |  键盘 / 虚拟按键           |- 真实 LVGL 9.5
   \- 麦克风 ---------------->  |- 真实应用 UI 代码 (main/app_tuner.c, main/app_pokemon.c + main/pokewalk/, ...)
        (sounddevice)          |- 模拟显示驱动 -> 240x320 RGB565 帧缓冲
                               |- 音频环形缓冲 -> bsp_audio_read()
                               \- ESP-IDF / BSP / FreeRTOS 主机桩
```

- 应用代码不做改动，只是它的硬件调用被替换为桩。电量计不存在（顶栏显示 `--`），
  音频则在启用后由主机麦克风喂入。宝可梦游戏同理：它的 Wi-Fi 射频与 USB 控制台不存在
  （扫描就是扫不到任何 AP），存档/显示/音频设置存放在内存版 NVS 中，也没有扬声器输出。
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
| [ide_flash.py](ide_flash.py) | 串口发现、ESP-IDF 探测，以及增量/完整镜像烧录命令 |
| [ide_layout.py](ide_layout.py) | 画面自动适配的缩放数学（纯函数，含主机测试） |
| [backend.py](backend.py) | Backend 抽象与 `ctypes` 模拟器后端 |
| [audio_input.py](audio_input.py) | 麦克风采集，喂给模拟器的音频输入 |
| [run.sh](run.sh) | 构建后启动 |
| [build.sh](build.sh) | 构建模拟器共享库 |
| [fetch_lvgl.sh](fetch_lvgl.sh) | 确保 LVGL 9.5.0 可用 |
| [host/](host) | 主机构建：`sim.c`、`sim_display.c`、`sim_audio.c`、`sim_platform.c`、`sim_esp_idf.c`、`sim_pokewalk.c`、`lv_conf.h` 与桩头文件 |

## 对接其他应用

模拟器当前在 [host/sim.c](host/sim.c) 中调用 `app_home_start()` 启动首页，并按固件
`main.c` 的同一套路由转发按键：首页按"确定"进入选中的卡片（尤克里里调音器或宝可梦
游戏），调音器长按"确定"返回首页，宝可梦双击"确定"（或在待机页长按"确定"）返回首页。
若要换一个应用，修改这一处入口与页面路由，并把该应用的源文件加入
[host/CMakeLists.txt](host/CMakeLists.txt) 即可。

## 常见问题

- 提示 `Simulator library not found` —— 先运行 `./tools/simulator/build.sh`。
- 提示 `sim_init() failed` —— 查看报错前打印的 LVGL 错误信息。
- 提示 `Microphone unavailable` —— 缺少 `sounddevice` 或麦克风被拒绝。用
  `pip install sounddevice` 安装，并允许你的终端访问麦克风；此时 IDE 仍以
  "无信号"界面运行。
- 进入调音器后显示"无信号"界面 —— 没有麦克风输入时正常；顶栏电量显示 `--` 也属
  预期，桌面没有电量计。窗口启动时显示的是首页（"确定"进入调音器，"下"再"确定"
  进入宝可梦游戏）。退出游戏：在宝可梦的待机页长按"确定"——模拟器的虚拟按键只会发
  `CLICK`/`LONG`，发不出双击。
- 烧录页列不出设备 —— 用可传数据的 USB 线连接设备并打开电源，再点"刷新"。Linux 下
  串口访问可能需要加入 `dialout` 组。
- 烧录页提示未找到 ESP-IDF —— 启动 IDE 前先激活 ESP-IDF 5.5.3 的 `export.sh`，或把
  `AI_PASSPORT_IDF_ROOT` 指向该目录。

## 现状与后续

已实现：上述桌面模拟器与 IDE，包括通过真实采集任务驱动调音器的麦克风输入、宝可梦
游戏（资产、页面与按键；其 Wi-Fi 感知与存档在桌面上降级），以及把当前应用通过 USB 烧录
到真机（设备发现，加上增量与完整镜像两种烧录）。后续计划：通过 USB 串口连接真机时流式
传输画面并注入按键，沿用固件仓库的串口截图协议。
