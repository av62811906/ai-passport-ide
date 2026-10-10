---
name: ai-passport-ide
description: Start the AI Passport desktop IDE and read its session log file. Use when asked to open or launch the AI Passport IDE, or to read its IDE, build, or flash logs. Do not use to flash a device.
---

<p align="right"><a href="SKILL.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# AI Passport IDE: Launch and Logs

This skill covers exactly two things: starting the desktop IDE and reading its
log. It does not cover flashing, editing policy, or device acceptance.

## Launch the IDE

Run the launcher from the repository root:

```bash
./tools/simulator/run.sh
```

- The launcher builds the simulator library when needed and then starts
  `tools/simulator/passport_ide.py`. The first run needs CMake and a C/C++
  compiler; later runs are quick.
- The IDE is a long-lived Tkinter window and needs a graphical session. Start it
  in a terminal that has a display and leave it running; do not wait for it to
  exit. If you only need a build check, do not use this skill.
- Flags pass through the launcher, for example `--theme light`, `--scale 2`,
  `--audio off`, or `--lib PATH`.
- Launch it outside a restricted sandbox. A sandboxed terminal can block writes
  to the ESP-IDF git tree, so builds and flash jobs started from the IDE fail
  even though the window loads normally.
- Run one IDE per checkout at a time. Each launch truncates the shared log file.

## Read the logs

Every line of the bottom log panel is mirrored to one file:

```text
build/simulator/ide.log
```

- The file is created at launch and written line by line, so it can be followed
  while the IDE runs: `tail -f build/simulator/ide.log`.
- Read the whole session with your file-reading tool; each line is
  `<local time> [<level>] <message>`, where the level is `info`, `build`,
  `warn`, `err`, or `host`.
- The file and the panel hold the same content: application `ESP_LOGx` output,
  editor save/build/hot-swap messages, external-change rebuilds, and flash job
  output.
- Clearing the panel (the **Clear** button or `Cmd/Ctrl+L`) truncates the file
  too. The file is truncated on every launch, so read it before restarting the
  IDE if you need the previous session.
- If the file does not exist, the IDE could not create it (for example a
  read-only checkout); fall back to reading the on-screen panel. `LOG_PATH` in
  `tools/simulator/passport_ide.py` is the exact path.
