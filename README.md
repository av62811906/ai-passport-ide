<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport IDE

A desktop IDE for AI Passport development. It renders the device's 240x320 UI
on your computer and lets you drive the application with virtual buttons and the
keyboard, alongside a small C source editor and the application's log output.
The computer's microphone can drive the tuner as if the device's audio codec
were connected, and the home page also opens the PokeWalk pixel game (its Wi-Fi
sensing and saved data degrade on the desktop).

The device UI is produced by a host build of the **real** LVGL application code:
the application runs unmodified and only its hardware calls are replaced by
host stubs. It is primarily a **desktop simulator**. Inside a full ai-passport
firmware checkout, its flash page can also write the current application to a
real AI Passport over USB.

See [tools/simulator/README.md](tools/simulator/README.md) for the full
IDE/simulator documentation (window layout, shortcuts, source explorer, themes,
external-change rebuilds, flashing, and how the host build works).

## Requirements

- Python 3 with `tkinter` (bundled on macOS and most Linux distributions).
- CMake and a C/C++ compiler for the first build.
- LVGL 9.5.0 sources. The build prefers a copy already vendored under
  `managed_components/lvgl__lvgl`; otherwise
  [fetch_lvgl.sh](tools/simulator/fetch_lvgl.sh) clones the pinned version.
- Optional: `sounddevice` (with `numpy`) for microphone input. Without it, the
  tuner simply runs its "no signal" screen.
- Optional: an ESP-IDF 5.5.3 checkout for the flash actions (`idf.py` and
  `esptool`), which target a full ai-passport firmware checkout.

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
| [main/](main) | The application under test (home page, ukulele tuner, PokeWalk game) and its pure logic |
| [components/bsp/](components/bsp) | The BSP headers and sources the simulator build needs |
| [assets/](assets) | The application's LVGL font subsets and PokeWalk's embedded assets |
| [skills/ai-passport-ide/](skills/ai-passport-ide) | Companion agent skill: launch the IDE and read its session log |
| [tests/](tests) | Host tests for the IDE's pure Python modules |

The `main/`, `components/`, and `assets/` trees are the subset of the upstream
[ai-passport](https://github.com/folotoy/ai-passport) firmware that the
simulator compiles on the host. They are kept here so this repository builds
standalone without a full ESP-IDF checkout.

## Companion skill

[skills/ai-passport-ide](skills/ai-passport-ide) teaches a coding agent to open
the IDE and read its session log (`build/simulator/ide.log`). Copy or symlink
the folder into your client's skills directory — for Trae, `~/.trae-cn/skills/`
or a project's `.trae/skills/` — to use it.

## Tests

```bash
python3 -m pytest tests/ -q
```

## Status

Implemented: the desktop IDE and simulator — the microphone-driven tuner, the
PokeWalk game, in-place rebuilds from saves and external edits, and USB
flashing (device discovery plus incremental and full-image modes, inside a full
firmware checkout). Planned next: streaming the device's screen and injecting
buttons over the device's USB serial protocol.