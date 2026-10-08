<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Desktop IDE (Simulator)

A lightweight desktop companion for AI Passport development. It renders the
device's 240x320 UI on your computer and lets you drive the application with
virtual buttons and the keyboard, with the application's own log output shown
below. The computer's microphone can drive the tuner as if the device's audio
codec were connected.

The window is laid out like a small IDE: a menu bar and toolbar, an activity bar
with a side panel, an editor area with the live device mirror docked to its
right edge, a bottom log panel, and a status bar. The mirror re-fits to any
window size (including full screen), and two colour themes (dark and light)
switch at run time.

This is a **desktop simulator**: it runs on your computer and does not talk to
the device yet. Connecting to real hardware is planned as a later stage.

## Requirements

- Python 3 with `tkinter` (bundled on macOS and most Linux distributions).
- CMake and a C/C++ compiler for the first build.
- LVGL 9.5.0 sources. The build prefers the copy already vendored by a device
  build at `managed_components/lvgl__lvgl`; otherwise
  [fetch_lvgl.sh](fetch_lvgl.sh) clones the pinned version.
- Optional: `sounddevice` (with `numpy`) for microphone input. Without it, the
  tuner simply runs its "no signal" screen.

## Quick start

```bash
./tools/simulator/run.sh
```

The first run compiles LVGL and takes a few minutes; later runs are incremental.
No network access is needed when `managed_components/lvgl__lvgl` already exists.

## Usage

```bash
./tools/simulator/run.sh                 # build + launch
./tools/simulator/run.sh --theme light   # light colour scheme
./tools/simulator/run.sh --scale 1       # fixed 1x zoom instead of auto-fit
./tools/simulator/run.sh --fullscreen    # start in full screen
./tools/simulator/run.sh --audio off     # disable microphone input
./tools/simulator/build.sh               # build only, do not launch
python3 tools/simulator/passport_ide.py --lib <path>   # explicit library path
```

- The device mirror is **docked to the right edge of the editor area** and
  auto-fits it by default, keeping the 240x320 aspect ratio at any window size,
  including full screen. Passing `--scale N` selects a fixed N-x zoom instead;
  the toolbar and the View menu switch back to auto-fit.
- Virtual buttons: **Up**, **OK**, **Down**, directly below the device mirror. A
  click sends `CLICK`; holding a button for more than 700 ms sends `LONG`.
- Keyboard: `Up` / `Down` / `Enter` (or `Space`) map to the same keys.
- Shortcuts: `F11` toggles full screen (`Esc` leaves it), `Cmd/Ctrl+0` fits the
  window, `Cmd/Ctrl+=` and `Cmd/Ctrl+-` zoom in and out, `Cmd/Ctrl+S` saves the
  active source file, `Cmd/Ctrl+W` closes it, `Cmd/Ctrl+R` saves everything and
  rebuilds, `Cmd/Ctrl+L` clears the log, and `Cmd/Ctrl+Shift+T` switches the
  theme.
- The activity bar switches the side panel between the source explorer, device
  info, the microphone state, and the shortcut list. The bottom panel streams
  the application's `ESP_LOGx` output with errors and warnings highlighted, and
  the status bar shows backend, resolution, zoom, backlight, audio, and theme.
- `--theme` selects the colour scheme: `dark` (default) or `light`.
- `--audio` selects microphone input: `auto` (default, use it when available),
  `on`, or `off`. Pluck a string or hum a note near the microphone and the
  tuner reacts; on first use macOS asks for microphone permission.

## Source explorer and editor

The side panel's **sources** entry lists the code that makes up the application
copied into the firmware: every `*.c` and `*.h` in `main/`, plus
`CMakeLists.txt`. Clicking a file opens it in the editor area next to the
**Device screen** tab; each source tab is closable and shows a `●` while it has
unsaved changes.

- The editor has a line-number gutter, current-line highlight, and C syntax
  highlighting (keywords, preprocessor lines, strings, numbers, comments,
  calls); `Tab` inserts four spaces.
- `Cmd/Ctrl+S` writes the active file back to disk, preserving its LF/CRLF
  style and writing atomically. Writes are restricted to `main/`: a path that
  resolves outside (including through a symlink) is refused.
- **Saving rebuilds the simulator library and restarts this IDE**, so the change
  shows up in the device mirror right away. An incremental rebuild takes a few
  seconds; the log panel streams the compiler output and marks errors red. If
  the build fails, the previously built library is restored and the IDE keeps
  running with the old code.
- Because a running process cannot reload a shared library it has already
  mapped, the library is renamed aside before the build and the process replaces
  itself afterwards: expect the window to blink and reopen.
- `Cmd/Ctrl+R` runs the same flow without needing an edit.
- `Cmd/Ctrl+W` closes the active source tab, asking first when it is dirty; the
  window close button asks once for all unsaved files.

## How it works

The IDE is a Tkinter window. All rendering and input go through a small shared
library, `libpassport_sim`, loaded with `ctypes`:

```text
Tkinter IDE  --ctypes-->  libpassport_sim  (host build)
   |  keyboard/buttons        |- real LVGL 9.5
   \- microphone ---------->  |- real application UI code (main/app_tuner.c, ...)
        (sounddevice)         |- simulator display driver -> 240x320 RGB565 framebuffer
                              |- audio ring buffer -> bsp_audio_read()
                              \- host stubs for ESP-IDF / BSP / FreeRTOS
```

- The application runs unmodified; only its hardware calls are replaced by
  stubs. The battery gauge is absent (the header shows `--`), and the
  microphone is fed from the host when audio is enabled.
- When audio is enabled the tuner's capture task runs on a real host thread:
  the FreeRTOS API is mapped onto pthreads, and `bsp_audio_read()` drains a ring
  buffer that the microphone callback fills. Both that thread and the IDE main
  thread share LVGL through the same recursive lock the application uses, so
  `sim_step()` takes it too.
- The display driver writes each LVGL flush into a static full-screen buffer and
  applies the same rounded-corner mask the firmware uses, so the mirror matches
  the device.
- Time is owned by the IDE: `sim_step()` advances LVGL's tick, so stopping the
  refresh loop freezes the UI.

## Layout

| Path | Purpose |
| --- | --- |
| [passport_ide.py](passport_ide.py) | Tkinter IDE shell: docked mirror, editor tabs, explorer wiring, save/build actions, themes |
| [ide_editor.py](ide_editor.py) | Reusable widgets: themed scrollbar, source explorer, C code editor, editor tabs |
| [ide_sources.py](ide_sources.py) | Source model: file listing, save-path containment, newline style, C tokenizer |
| [ide_layout.py](ide_layout.py) | Auto-fit zoom math for the device mirror (pure, host-tested) |
| [backend.py](backend.py) | Backend abstraction plus the `ctypes` simulator backend |
| [audio_input.py](audio_input.py) | Microphone capture feeding the simulator's audio input |
| [run.sh](run.sh) | Build, then launch |
| [build.sh](build.sh) | Build the simulator shared library |
| [fetch_lvgl.sh](fetch_lvgl.sh) | Ensure LVGL 9.5.0 is available |
| [host/](host) | Host build: `sim.c`, `sim_display.c`, `sim_audio.c`, `sim_platform.c`, `lv_conf.h`, and the shim headers |

## Adding another application

The simulator starts the tuner via `app_tuner_start()` in
[host/sim.c](host/sim.c). To target a different application, change that call
and add the application's source files to [host/CMakeLists.txt](host/CMakeLists.txt).

## Troubleshooting

- `Simulator library not found` — run `./tools/simulator/build.sh` first.
- `sim_init() failed` — read the LVGL error printed just before the failure.
- `Microphone unavailable` — `sounddevice` is missing or the microphone was
  refused. Install it with `pip install sounddevice` and allow microphone
  access for your terminal; the IDE still runs on the "no signal" screen.
- The window shows the tuner's "no signal" screen — normal without microphone
  input, and expected for the battery gauge (`--`), which a desktop lacks.

## Status and next steps

Implemented: the desktop simulator and IDE described above, including
microphone input that drives the tuner through a real capture task. Planned
next: connecting to real hardware over USB serial (screen streaming plus button
injection, following the repository's
[serial screenshot protocol](../../docs/reference/y2lin/serial-screenshot-protocol.md)),
and build/flash actions inside the IDE.
