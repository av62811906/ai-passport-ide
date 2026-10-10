<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Desktop IDE (Simulator)

A lightweight desktop companion for AI Passport development. It renders the
device's 240x320 UI on your computer and lets you drive the application with
virtual buttons and the keyboard, with the application's own log output shown
below. The computer's microphone can drive the tuner as if the device's audio
codec were connected, and the home page also opens the PokeWalk pixel game. On a
desktop the game's Wi-Fi sensing and saved data degrade (no radio; the save
lives in memory), but the game itself renders and plays.

The window is laid out like a small IDE: a menu bar and toolbar, an activity bar
with a side panel, an editor area with the live device mirror docked to its
right edge, a bottom log panel, and a status bar. The mirror re-fits to any
window size (including full screen), and two colour themes (dark and light)
switch at run time.

It is primarily a **desktop simulator**. Inside a full ai-passport firmware
checkout it can also find the serial ports on your computer and flash the
current application to a real AI Passport over USB.

## Requirements

- Python 3 with `tkinter` (bundled on macOS and most Linux distributions).
- CMake and a C/C++ compiler for the first build.
- LVGL 9.5.0 sources. The build prefers the copy already vendored by a device
  build at `managed_components/lvgl__lvgl`; otherwise
  [fetch_lvgl.sh](fetch_lvgl.sh) clones the pinned version.
- Optional: `sounddevice` (with `numpy`) for microphone input. Without it, the
  tuner simply runs its "no signal" screen.
- Optional: an ESP-IDF 5.5.3 checkout for the flashing buttons (`idf.py` and
  `esptool`). The simulator and the device mirror need none of it.

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
  rebuilds, `Cmd/Ctrl+L` clears the log, `Cmd/Ctrl+M` toggles the microphone,
  and `Cmd/Ctrl+Shift+T` switches the theme.
- The activity bar switches the side panel between the source explorer, device
  info, the flash page, the microphone state, and the shortcut list. The bottom
  panel streams the application's `ESP_LOGx` output with errors and warnings
  highlighted, and the status bar shows backend, resolution, zoom, backlight,
  audio, and theme.
- Every panel line is also mirrored to `build/simulator/ide.log` (truncated at
  each launch), so the session can be read from a terminal or an agent while the
  window is open; `Cmd/Ctrl+L` clears the panel and the file together.
- `--theme` selects the colour scheme: `dark` (default) or `light`.
- `--audio` selects microphone input: `auto` (default, use it when available),
  `on`, or `off`. Pluck a string or hum a note near the microphone and the
  tuner reacts; on first use macOS asks for microphone permission.
- The microphone can also be toggled while the IDE runs — the toolbar's
  microphone button, the audio menu, the audio page's button, or `Cmd/Ctrl+M`.
  Turning it off closes host capture, so the tuner only sees silence; turning it
  on opens it again. Because the device decides whether to run its capture task
  when it enters the tuner, a toggle restarts the simulated application, so the
  mirror returns to its home page.

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
- **Saving rebuilds the simulator library and swaps it in place**, so the change
  shows up in the device mirror right away. An incremental rebuild takes a few
  seconds; the log panel streams the compiler output and marks errors red. If
  the build fails, the previously built library is restored and the IDE keeps
  running with the old code.
- The application code runs inside the IDE process, so a runtime fault in it
  would normally take the whole IDE down. Before each swap the freshly built
  library is therefore booted in a throwaway process (home → tuner → home): a
  library that crashes on start is reported in the log and the swap is skipped,
  and the IDE keeps running the previous library.
- The swap keeps the window, the open editor tabs and the log; only the
  simulated application restarts, so the mirror returns to its home page.
- A process cannot reload a shared library from a path it has already mapped, so
  each swap loads a private copy of the freshly built library. The image it
  replaces stays mapped for the rest of the session — one per swap.
- `Cmd/Ctrl+R` runs the same flow without needing an edit.
- `Cmd/Ctrl+W` closes the active source tab, asking first when it is dirty; the
  window close button asks once for all unsaved files.

## External changes

Edits made outside the IDE — in another editor, or by an agent — are picked up
on their own. The IDE polls the sources the build reads (`main/`,
`components/bsp/include/`, `components/bsp/src/`, `assets/fonts/`, and
`tools/simulator/host/`) and, once they have stayed quiet for about a second,
refreshes the explorer, reloads the open editors and rebuilds the library.

- A burst of edits collapses into a single rebuild.
- An open file with unsaved IDE edits is never overwritten by the on-disk copy;
  the log says so instead.
- If the build fails, the previous library is kept and the IDE keeps running
  with the old code, exactly as it does for a save.
- When a source file is newer than the library, the IDE rebuilds once at
  startup, so edits made while it was closed are not missed.
- A newly added `.c` file is not compiled until it is listed in
  `host/CMakeLists.txt`.

## Flashing to the device

The IDE can also write the current application to a real AI Passport over USB.
The activity bar's **flash** entry (and the toolbar's **Flash** button for the
common case) lists the serial ports the computer currently exposes and rescans
them while the page is open.

- Devices whose USB identity looks like an ESP32 are marked and selected by
  default. pyserial is used when it is importable; otherwise the platform's
  device nodes (`/dev/cu.*` on macOS, `/dev/ttyUSB*` and `/dev/ttyACM*` on
  Linux) are scanned, keeping only names that look like a USB serial adapter so
  Bluetooth pseudo-ports are ignored.
- The page shows the ESP-IDF checkout the IDE will use. It is detected from
  `AI_PASSPORT_IDF_ROOT`, then `IDF_PATH`, then the usual install locations
  (`~/esp/esp-idf-v5.5.3`, `~/esp/esp-idf`, `/opt/esp-idf`). ESP-IDF 5.5.3 is
  the supported version; launch the IDE from a shell where `idf.py --version`
  reports it, or set `AI_PASSPORT_IDF_ROOT`. The IDE sources that checkout's
  `export.sh` itself, so nothing needs to be activated first.
- **Incremental flash** builds the current `main/` sources and flashes them with
  `idf.py -p PORT flash`. This is the segmented development flash, so it keeps
  the device's NVS (saved data).
- **Full image** runs `./tools/validate.sh --firmware` and then writes the
  verified merged image from `0x0` with `esptool`. This matches the delivery
  path, but it can reset the device's NVS, so it asks for confirmation first.
- Both commands run in the background; their output streams into the device log
  panel and the status bar shows the progress. A flash that finishes
  successfully is reported as such, and a failure points back at the log.

Flashing needs a POSIX shell (macOS/Linux) and the ESP-IDF checkout above; the
simulator itself needs neither. A build request, a connected device, or a
successful flash does not by itself prove the firmware behaves correctly on the
board — that still needs on-device testing.

## How it works

The IDE is a Tkinter window. All rendering and input go through a small shared
library, `libpassport_sim`, loaded with `ctypes`:

```text
Tkinter IDE  --ctypes-->  libpassport_sim  (host build)
   |  keyboard/buttons        |- real LVGL 9.5
   \- microphone ---------->  |- real application UI code (main/app_tuner.c, main/app_pokemon.c + main/pokewalk/, ...)
        (sounddevice)         |- simulator display driver -> 240x320 RGB565 framebuffer
                              |- audio ring buffer -> bsp_audio_read()
                              \- host stubs for ESP-IDF / BSP / FreeRTOS
```

- The application runs unmodified; only its hardware calls are replaced by
  stubs. The battery gauge is absent (the header shows `--`), and the
  microphone is fed from the host when audio is enabled. PokeWalk runs the same
  way: its Wi-Fi radio and USB console are absent (scanning just finds no access
  points), the save/display/audio settings live in an in-memory NVS, and there
  is no speaker output.
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
| [ide_flash.py](ide_flash.py) | Serial-port discovery, ESP-IDF detection, and the incremental/full-image flash commands |
| [ide_layout.py](ide_layout.py) | Auto-fit zoom math for the device mirror (pure, host-tested) |
| [backend.py](backend.py) | Backend abstraction plus the `ctypes` simulator backend |
| [audio_input.py](audio_input.py) | Microphone capture feeding the simulator's audio input |
| [run.sh](run.sh) | Build, then launch |
| [build.sh](build.sh) | Build the simulator shared library |
| [fetch_lvgl.sh](fetch_lvgl.sh) | Ensure LVGL 9.5.0 is available |
| [host/](host) | Host build: `sim.c`, `sim_display.c`, `sim_audio.c`, `sim_platform.c`, `sim_esp_idf.c`, `sim_pokewalk.c`, `lv_conf.h`, and the shim headers |

## Adding another application

The simulator boots the home page via `app_home_start()` in
[host/sim.c](host/sim.c) and routes the three keys the same way the firmware's
`main.c` does: `OK` on the home page enters the selected card (the ukulele tuner
or the PokeWalk game), `OK` long-press leaves the tuner, and `OK` double-click
(or, on the game's idle screen, `OK` long-press) leaves the game. To target a
different application, change that entry point and page router, and add the
application's source files to [host/CMakeLists.txt](host/CMakeLists.txt).

## Troubleshooting

- `Simulator library not found` — run `./tools/simulator/build.sh` first.
- `sim_init() failed` — read the LVGL error printed just before the failure.
- `Microphone unavailable` — `sounddevice` is missing or the microphone was
  refused. Install it with `pip install sounddevice` and allow microphone
  access for your terminal; the IDE still runs on the "no signal" screen.
- The tuner shows its "no signal" screen after you enter it — normal without
  microphone input, and expected for the battery gauge (`--`), which a desktop
  lacks. The window opens on the home page; press `OK` to enter the tuner, or
  `Down` then `OK` to open PokeWalk. Leave the game by long-pressing `OK` on its
  idle screen: the virtual buttons emit `CLICK`/`LONG` only, never a
  double-click.
- The flash page lists no devices — connect the board with a data-capable USB
  cable and power it on, then press **Refresh**. On Linux, serial access can
  require membership in the `dialout` group.
- The flash page reports no ESP-IDF — activate an ESP-IDF 5.5.3 `export.sh`, or
  set `AI_PASSPORT_IDF_ROOT` to that checkout, before launching the IDE.

## Status and next steps

Implemented: the desktop simulator and IDE described above, including
microphone input that drives the tuner through a real capture task, the
PokeWalk game (assets, pages and input; its Wi-Fi sensing and save degrade on
the desktop), and flashing the current application to a real device over USB
(device discovery plus incremental and full-image flashing). Planned next:
streaming the device's screen and injecting buttons over USB serial, following
the firmware repository's serial screenshot protocol.
