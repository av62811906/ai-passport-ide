<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport IDE

A desktop IDE for AI Passport development. It renders the device's 240x320 UI
on your computer and lets you drive the application with virtual buttons and the
keyboard, alongside a small C source editor and the application's log output.

The device UI is produced by a host build of the **real** LVGL application code:
the application runs unmodified and only its hardware calls are replaced by
host stubs. This is a **desktop simulator** — it runs on your computer and does
not talk to the device over USB yet.

See [tools/simulator/README.md](tools/simulator/README.md) for the full
IDE/simulator documentation (window layout, shortcuts, source explorer,
themes, and how the host build works).

## Requirements

- Python 3 with `tkinter` (bundled on macOS and most Linux distributions).
- CMake and a C/C++ compiler for the first build.
- LVGL 9.5.0 sources. The build prefers a copy already vendored under
  `managed_components/lvgl__lvgl`; otherwise
  [fetch_lvgl.sh](tools/simulator/fetch_lvgl.sh) clones the pinned version.
- Optional: `sounddevice` (with `numpy`) for microphone input. Without it, the
  tuner simply runs its "no signal" screen.

## Quick start

```bash
./tools/simulator/run.sh
```

The first run compiles LVGL and takes a few minutes; later runs are incremental.

## Usage

```bash
./tools/simulator/run.sh                 # build + launch
./tools/simulator/run.sh --theme light   # light colour scheme
./tools/simulator/run.sh --scale 1       # fixed 1x zoom instead of auto-fit
./tools/simulator/run.sh --fullscreen    # start in full screen
./tools/simulator/run.sh --audio off     # disable microphone input
./tools/simulator/build.sh               # build only, do not launch
```

## Repository layout

| Path | Purpose |
| --- | --- |
| [tools/simulator/](tools/simulator) | The IDE and the host simulator build (see its README) |
| [main/](main) | The application under test (the ukulele tuner) and its pure logic |
| [components/bsp/](components/bsp) | The BSP headers and sources the simulator build needs |
| [assets/fonts/](assets/fonts) | The tuner's LVGL font subset |
| [tests/](tests) | Host tests for the IDE's pure Python modules |

The `main/`, `components/`, and `assets/` trees are the subset of the upstream
[ai-passport](https://github.com/folotoy/ai-passport) firmware that the
simulator compiles on the host. They are kept here so this repository builds
standalone without a full ESP-IDF checkout.

## Tests

```bash
python3 -m pytest tests/ -q
```

## Status

Implemented: the desktop IDE and simulator, including microphone input that
drives the tuner through a real capture task. Planned next: connecting to real
hardware over USB serial (screen streaming plus button injection) and
build/flash actions inside the IDE.
