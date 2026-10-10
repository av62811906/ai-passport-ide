---
name: ai-passport-ide
description: 启动 AI Passport 桌面 IDE 并读取其会话日志文件。用于要求打开／启动 AI Passport IDE，或读取其 IDE、编译、烧录日志的场景；不用于刷写设备。
---

<p align="right"><strong>简体中文</strong> · <a href="SKILL.md">English</a></p>

# AI Passport IDE：启动与日志

本技能只覆盖两件事：启动桌面 IDE，以及读取它的日志。不涉及烧录、编辑策略或
真机验收。

## 启动 IDE

在仓库根目录运行启动器：

```bash
./tools/simulator/run.sh
```

- 启动器在需要时先编译仿真库，然后启动 `tools/simulator/passport_ide.py`。首次
  运行需要 CMake 和 C/C++ 编译器，之后启动很快。
- IDE 是一个长期运行的 Tkinter 窗口，需要图形会话。请在有显示环境的终端里启动
  并让它常驻，不要等待它退出。若只想做一次编译检查，请勿使用本技能。
- 启动器支持透传参数，例如 `--theme light`、`--scale 2`、`--audio off`、
  `--lib PATH`。
- 请在不受限的沙箱环境外启动。受限沙箱可能拦截对 ESP-IDF git 仓库的写入，导致
  从 IDE 发起的编译与烧录任务失败，即使窗口本身能正常打开。
- 同一个检出目录同一时间只运行一个 IDE。每次启动都会清空共享的日志文件。

## 读取日志

底部日志面板的每一行都会镜像写入同一个文件：

```text
build/simulator/ide.log
```

- 该文件在启动时创建并逐行写入，因此可在 IDE 运行时持续跟踪：
  `tail -f build/simulator/ide.log`。
- 用文件读取工具读取整段会话；每行格式为 `<本地时间> [<级别>] <消息>`，级别为
  `info`、`build`、`warn`、`err` 或 `host`。
- 文件与面板内容一致：应用 `ESP_LOGx` 输出、编辑器的保存／编译／热更新消息、
  外部改动触发的重编，以及烧录任务的输出。
- 清空面板（**清空** 按钮或 `Cmd/Ctrl+L`）会同时截断该文件；文件在每次启动时都会
  清空，如需上一次会话的日志，请在重启 IDE 前读取。
- 如果文件不存在，说明 IDE 无法创建它（例如检出目录只读），此时退回读取界面上的
  面板。`tools/simulator/passport_ide.py` 中的 `LOG_PATH` 是确切路径。
