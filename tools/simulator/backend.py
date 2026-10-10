#!/usr/bin/env python3
"""Display/input backends for the AI Passport desktop IDE.

A backend hides *how* the framebuffer is obtained and how key events are
delivered, so the same Tkinter window can drive either the desktop simulator
(ctypes, this file) or, later, a real device over a serial link.

Framebuffers are always returned as raw RGB565 little-endian bytes plus
dimensions; the IDE owns the pixel decoding.
"""

from __future__ import annotations

import ctypes
import platform
import shutil
import signal
import subprocess
import sys
from pathlib import Path

# Key encoding shared with components/bsp/include/bsp_button.h.
BTN_UP, BTN_DOWN, BTN_OK = 0, 1, 2
EV_PRESS, EV_CLICK, EV_DOUBLE, EV_LONG = 0, 1, 2, 3

BTN_LABELS = {BTN_UP: "UP", BTN_DOWN: "DOWN", BTN_OK: "OK"}
EV_LABELS = {EV_PRESS: "PRESS", EV_CLICK: "CLICK", EV_DOUBLE: "DOUBLE", EV_LONG: "LONG"}


class Backend:
    """Interface implemented by concrete backends."""

    def init(self, audio_ready: bool = False) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def step(self, elapsed_ms: int) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def push_button(self, btn: int, ev: int) -> None:  # pragma: no cover - interface
        raise NotImplementedError

    def push_audio(self, data: bytes) -> None:
        """Deliver 16 kHz / 16-bit / mono PCM. Backends that get audio from the
        device ignore this."""
        return None

    def framebuffer(self):  # pragma: no cover - interface
        """Return (raw_rgb565_bytes, width, height)."""
        raise NotImplementedError

    def drain_logs(self) -> list[str]:
        return []

    def backlight_percent(self) -> int:
        return -1

    def shutdown(self) -> None:
        pass


def default_library_path() -> Path:
    repo_root = Path(__file__).resolve().parents[2]
    name = "libpassport_sim.dylib" if platform.system() == "Darwin" else "libpassport_sim.so"
    return repo_root / "build" / "simulator" / "build" / "lib" / name


def stage_library(lib_path: Path, token: str) -> Path:
    """Copy a freshly built library to a private path the process can load.

    Loading the same path twice in one process hands back the already-mapped
    image, so every in-place swap needs a name the process has not loaded yet.
    The caller removes the previous copy once the new one is running.
    """
    lib_path = Path(lib_path)
    staged = lib_path.with_name(f"{lib_path.stem}.{token}{lib_path.suffix}")
    shutil.copy2(lib_path, staged)
    return staged


def _signal_name(number: int) -> str:
    try:
        return signal.Signals(number).name
    except ValueError:
        return f"signal {number}"


def describe_exit(returncode: int, output: str) -> str:
    """Summarise a finished validation run for the IDE console.

    ``returncode`` is negative when the child was killed by a signal, which is
    what a fault in the application code looks like from the parent process.
    """
    if returncode < 0:
        summary = f"新仿真库启动即崩溃（{_signal_name(-returncode)}）"
    else:
        summary = f"新仿真库启动失败（退出码 {returncode}）"
    tail = [line for line in output.splitlines() if line.strip()]
    if tail:
        summary += "\n" + "\n".join(tail[-8:])
    return summary


def validate_library(lib_path: Path, timeout: float = 30.0) -> str | None:
    """Boot ``lib_path`` in a throwaway process to prove it does not crash.

    The application runs in this process through ctypes, so a fault in freshly
    written code (a bad pointer, a failed LVGL assertion) takes the IDE down
    with it, and no exception can catch that. Booting the candidate library in
    a short-lived child first turns such a fault into an ordinary non-zero exit
    the IDE can report and recover from. Returns ``None`` when the library
    boots, otherwise a reason to show in the console.
    """
    lib_path = Path(lib_path)
    if not lib_path.is_file():
        return f"未找到仿真库：{lib_path}"
    command = [sys.executable, str(Path(__file__).resolve()), "--validate", str(lib_path)]
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return f"新仿真库启动超时（超过 {timeout:.0f} 秒无响应）"
    except OSError as exc:
        return f"无法运行仿真库校验：{exc}"
    if result.returncode == 0:
        return None
    return describe_exit(result.returncode, result.stdout or "")


def _validate_child(lib_path: Path) -> int:
    """Load the library, walk it through its screens, then exit.

    Runs in a separate process (see validate_library) so a crash in the
    application cannot hurt the IDE. Walking home -> tuner -> home covers the
    screen-building code on both pages, which is where a fault shows up first.
    """
    be = SimBackend(lib_path)
    be.init(False)
    for _ in range(3):
        be.step(33)
    be.push_button(BTN_OK, EV_CLICK)   # enter the tuner
    for _ in range(3):
        be.step(33)
    be.push_button(BTN_OK, EV_LONG)    # long-press back to home
    for _ in range(3):
        be.step(33)
    be.framebuffer()
    be.shutdown()
    return 0


class SimBackend(Backend):
    """Loads libpassport_sim and drives the in-process LVGL application."""

    _LOG_CB = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_void_p)

    def __init__(self, lib_path: Path | None = None):
        self._lib_path = Path(lib_path) if lib_path else default_library_path()
        self._lib = None
        # Library images retired by swap(): ctypes never unloads a mapped image,
        # so they are kept referenced instead of being dropped mid-call.
        self._retired: list = []
        self._logs: list[str] = []
        self._log_cb = self._LOG_CB(self._on_log)  # keep a strong reference
        self._width = 0
        self._height = 0

    def _on_log(self, line, _user) -> None:
        # Runs on the same thread as step()/init(); just buffer the line.
        if line:
            self._logs.append(line.decode("utf-8", "replace"))

    def _load(self, path: Path, audio_ready: bool):
        """Bind the ABI of ``path`` and start the application inside it."""
        if not path.is_file():
            raise FileNotFoundError(
                f"Simulator library not found: {path}\n"
                "Build it with tools/simulator/build.sh"
            )
        lib = ctypes.CDLL(str(path))
        lib.sim_init.restype = ctypes.c_int
        lib.sim_init.argtypes = [ctypes.c_int]
        lib.sim_shutdown.restype = None
        lib.sim_shutdown.argtypes = []
        lib.sim_step.restype = None
        lib.sim_step.argtypes = [ctypes.c_uint32]
        lib.sim_push_button.restype = None
        lib.sim_push_button.argtypes = [ctypes.c_int, ctypes.c_int]
        lib.sim_audio_push.restype = None
        lib.sim_audio_push.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t]
        lib.sim_framebuffer.restype = ctypes.POINTER(ctypes.c_uint16)
        lib.sim_framebuffer.argtypes = [ctypes.POINTER(ctypes.c_int)] * 3
        lib.sim_backlight_percent.restype = ctypes.c_int
        lib.sim_backlight_percent.argtypes = []
        lib.sim_set_log_callback.restype = None
        lib.sim_set_log_callback.argtypes = [self._LOG_CB, ctypes.c_void_p]
        # Used by swap() to stop the previous image's capture task.
        lib.app_tuner_exit.restype = None
        lib.app_tuner_exit.argtypes = []

        lib.sim_set_log_callback(self._log_cb, None)
        if lib.sim_init(1 if audio_ready else 0) != 0:
            raise RuntimeError("sim_init() failed (LVGL display creation error)")
        return lib

    def init(self, audio_ready: bool = False) -> None:
        self._lib = self._load(self._lib_path, audio_ready)

    def swap(self, new_lib_path: Path, audio_ready: bool) -> None:
        """Replace the running library with a freshly built one, in place.

        The new library is loaded and started first, so one that fails to
        initialise leaves the running instance untouched; only then is the old
        one stopped and replaced. The caller must have stopped feeding audio
        first, because the old image cannot be stepped once it is retired.
        """
        new_lib_path = Path(new_lib_path)
        lib = self._load(new_lib_path, audio_ready)
        self._quiesce()
        self._retired.append(self._lib)
        self._lib = lib
        self._lib_path = new_lib_path

    def _quiesce(self) -> None:
        """Stop the application running in the currently loaded library.

        The tuner's capture task runs on a host thread; leaving it alive would
        keep it touching an image nothing steps or shuts down any more.
        """
        if self._lib is None:
            return
        try:
            self._lib.app_tuner_exit()
        except (AttributeError, OSError):
            pass
        try:
            self._lib.sim_shutdown()
        except (AttributeError, OSError):
            pass

    def step(self, elapsed_ms: int) -> None:
        self._lib.sim_step(elapsed_ms)

    def push_button(self, btn: int, ev: int) -> None:
        self._lib.sim_push_button(btn, ev)

    def push_audio(self, data: bytes) -> None:
        # The microphone callback can fire before init() binds the library.
        if self._lib is None:
            return
        count = len(data) // 2
        if count <= 0:
            return
        samples = (ctypes.c_int16 * count).from_buffer_copy(data)
        self._lib.sim_audio_push(samples, count)

    def framebuffer(self):
        w = ctypes.c_int()
        h = ctypes.c_int()
        stride = ctypes.c_int()
        ptr = self._lib.sim_framebuffer(ctypes.byref(w), ctypes.byref(h), ctypes.byref(stride))
        self._width, self._height = w.value, h.value
        if not ptr or self._width <= 0 or self._height <= 0:
            return b"", 0, 0
        raw = ctypes.string_at(ctypes.cast(ptr, ctypes.c_void_p), self._width * self._height * 2)
        return raw, self._width, self._height

    def drain_logs(self) -> list[str]:
        logs, self._logs = self._logs, []
        return logs

    def backlight_percent(self) -> int:
        return int(self._lib.sim_backlight_percent())

    def shutdown(self) -> None:
        if self._lib is not None:
            self._lib.sim_shutdown()


def create_backend(name: str, lib_path: Path | None = None) -> Backend:
    if name == "sim":
        return SimBackend(lib_path)
    raise ValueError(f"Unsupported backend: {name!r} (only 'sim' is implemented)")


if __name__ == "__main__":
    # The IDE runs this file as a child process to try a freshly built library
    # before loading it in-process; see validate_library().
    if "--validate" in sys.argv:
        raise SystemExit(_validate_child(Path(sys.argv[sys.argv.index("--validate") + 1])))

    be = create_backend("sim")  # tiny smoke check: python3 backend.py
    be.init()
    try:
        be.step(33)
        raw, w, h = be.framebuffer()
        print(f"backend ok: {w}x{h}, {len(raw)} bytes, backlight={be.backlight_percent()}")
        for line in be.drain_logs():
            print(line)
    finally:
        be.shutdown()
    sys.exit(0)
