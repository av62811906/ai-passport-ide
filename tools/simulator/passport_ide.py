#!/usr/bin/env python3
"""AI Passport desktop IDE (lightweight, Tkinter only).

Presents the device in an IDE-style shell: menu bar, toolbar, an activity bar
with a side panel, an editor area with a live device mirror docked to its right
edge and a tabbed C editor for the application sources, a bottom log panel, and
a status bar.
Two colour themes (a PyCharm-like dark and an IntelliJ-like light) switch at run
time.

Saving a source file rebuilds the simulator library and swaps it in place, so the
change is visible in the device mirror right away without restarting this IDE.

Usage:
    tools/simulator/run.sh                  # build + launch
    python3 passport_ide.py --theme light    # light colour scheme
    python3 passport_ide.py --scale 1        # fixed zoom instead of auto-fit
    python3 passport_ide.py --lib PATH       # explicit simulator library

Only the Python standard library is required (ctypes + tkinter).
"""

from __future__ import annotations

import argparse
import base64
import io
import os
import platform
import queue
import re
import struct
import subprocess
import sys
import threading
import time
import tkinter as tk
import tkinter.font as tkfont
import zlib
from dataclasses import dataclass
from pathlib import Path
from tkinter import messagebox

sys.path.insert(0, str(Path(__file__).resolve().parent))
from backend import (  # noqa: E402
    BTN_DOWN,
    BTN_LABELS,
    BTN_OK,
    BTN_UP,
    EV_CLICK,
    EV_LONG,
    create_backend,
    default_library_path,
    stage_library,
    validate_library,
)
from audio_input import SAMPLE_RATE, MicCapture  # noqa: E402
from ide_editor import CodeEditor, EditorTabs, SourceExplorer, ThemedScrollbar  # noqa: E402
from ide_flash import (  # noqa: E402
    MODE_FULL,
    MODE_INCREMENTAL,
    build_flash_job,
    find_idf_root,
    list_serial_ports,
)
from ide_layout import SRC_HEIGHT, SRC_WIDTH, fit_scale, zoom_factor  # noqa: E402
from ide_sources import is_within, list_sources, newline_style, normalize_newlines  # noqa: E402
from ide_watch import changed_paths, is_stale, snapshot, watch_files  # noqa: E402

# Long-press threshold for a virtual button to emit a LONG event instead of CLICK.
LONG_PRESS_MS = 700
# Image refresh cadence. The framebuffer is only re-decoded when it changes,
# so a static screen costs almost nothing at 30 fps.
STEP_MS = 33

# Shell metrics (pixels).
TOOLBAR_HEIGHT = 34
STATUS_HEIGHT = 24
ACTIVITY_WIDTH = 44
SIDEBAR_WIDTH = 240
LOG_HEIGHT = 180
SASH_WIDTH = 6
EDITOR_PADDING = 16
# Fixed width of the device mirror docked to the right edge of the editor area.
SCREEN_PANE_WIDTH = 340
MIN_ZOOM = 1
MAX_ZOOM = 8

MAX_LOG_LINES = 2000

# External-change watch. Polling a few dozen small files is cheap; a change is
# only acted on once the tree has stayed quiet for WATCH_DEBOUNCE_MS, so an
# editor or an agent writing several files in a row causes one rebuild.
WATCH_MS = 700
WATCH_DEBOUNCE_MS = 900

# How often the flash page rescans the serial ports while it is visible. The
# scan only reads device metadata, so a couple of seconds costs nothing.
PORT_SCAN_MS = 2000

REPO_ROOT = Path(__file__).resolve().parents[2]
MAIN_DIR = REPO_ROOT / "main"
BUILD_SCRIPT = Path(__file__).resolve().parent / "build.sh"

# The bottom log panel is mirrored to this file so a terminal or an AI agent can
# read the same session log without the GUI. It is truncated at each launch; one
# line per panel entry, prefixed with the local time and the entry's level tag.
LOG_PATH = REPO_ROOT / "build" / "simulator" / "ide.log"


DARK = {
    "window_bg": "#1E1F22",
    "panel_bg": "#2B2D30",
    "toolbar_bg": "#2B2D30",
    "editor_bg": "#1E1F22",
    "tab_active_bg": "#1E1F22",
    "tab_inactive_bg": "#2B2D30",
    "border": "#393B40",
    "border_soft": "#2F3136",
    "fg": "#DFE1E5",
    "fg_muted": "#9DA0A8",
    "accent": "#3574F0",
    "accent_fg": "#FFFFFF",
    "hover": "#35373B",
    "selection": "#214283",
    "log_fg": "#A9B7C6",
    "log_err": "#F76D6D",
    "log_warn": "#D6A84F",
    "status_bg": "#2B2D30",
    "gutter_fg": "#6E7079",
    "cur_line_bg": "#26282E",
    "syn_cmt": "#808080",
    "syn_str": "#6A8759",
    "syn_pp": "#BBB529",
    "syn_kw": "#CF8E6D",
    "syn_num": "#6897BB",
    "syn_fn": "#FFC66D",
}

LIGHT = {
    "window_bg": "#F7F8FA",
    "panel_bg": "#FFFFFF",
    "toolbar_bg": "#F7F8FA",
    "editor_bg": "#FFFFFF",
    "tab_active_bg": "#FFFFFF",
    "tab_inactive_bg": "#F0F1F3",
    "border": "#D1D5DB",
    "border_soft": "#E5E7EB",
    "fg": "#1F2328",
    "fg_muted": "#6B7280",
    "accent": "#3574F0",
    "accent_fg": "#FFFFFF",
    "hover": "#E9EBEE",
    "selection": "#CCE0FF",
    "log_fg": "#24292F",
    "log_err": "#C4262E",
    "log_warn": "#9A6700",
    "status_bg": "#F0F1F3",
    "gutter_fg": "#9AA0A6",
    "cur_line_bg": "#F2F4F7",
    "syn_cmt": "#8C8C8C",
    "syn_str": "#067D17",
    "syn_pp": "#9E880D",
    "syn_kw": "#0033B3",
    "syn_num": "#1750EB",
    "syn_fn": "#00627A",
}

THEMES = {"dark": DARK, "light": LIGHT}
THEME_LABELS = {"dark": "深色", "light": "浅色"}

PAGES = (
    ("sources", "文", "资源管理器"),
    ("device", "设", "设备"),
    ("flash", "烧", "烧录"),
    ("audio", "音", "音频"),
    ("keys", "键", "快捷键"),
)


@dataclass
class _Document:
    """An open source file: where it lives and how it is currently known."""

    path: Path
    newline: str
    dirty: bool = False


def build_rgb565_table() -> list[bytes]:
    """Precompute RGB565 -> 3-byte RGB mapping (built once)."""
    table = []
    for value in range(0x10000):
        r = (value >> 11) & 0x1F
        g = (value >> 5) & 0x3F
        b = value & 0x1F
        r8 = (r << 3) | (r >> 2)
        g8 = (g << 2) | (g >> 4)
        b8 = (b << 3) | (b >> 2)
        table.append(bytes((r8, g8, b8)))
    return table


RGB565_TABLE = build_rgb565_table()


def decode_frame(raw: bytes, width: int, height: int) -> bytes:
    """Convert a raw little-endian RGB565 frame to packed RGB888."""
    from array import array

    pixels = array("H")
    pixels.frombytes(raw)
    if sys.byteorder == "big":
        pixels.byteswap()
    return b"".join(map(RGB565_TABLE.__getitem__, pixels))


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def rgb_to_png(rgb: bytes, width: int, height: int) -> bytes:
    """Encode packed RGB888 into a PNG (stdlib zlib only) for Tk to display."""
    stride = width * 3
    filtered = bytearray()
    for y in range(height):
        filtered.append(0)  # filter type 0 (None) per scanline
        filtered += rgb[y * stride:(y + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", zlib.compress(bytes(filtered), 6))
        + _png_chunk(b"IEND", b"")
    )


def _pick_family(families: set[str], *candidates: str) -> str:
    for name in candidates:
        if name in families:
            return name
    return candidates[-1]


class IdeApp:
    def __init__(
        self,
        root: tk.Tk,
        be,
        theme: str = "dark",
        fixed_scale: int | None = None,
        mic=None,
        audio_ready: bool = False,
        audio_note: str = "",
        fullscreen: bool = False,
        lib_path: Path | None = None,
    ):
        self.root = root
        self.backend = be
        self.mic = mic
        self.audio_ready = audio_ready
        self.audio_note = audio_note
        # The audio state the running application was started with. The device
        # only creates its capture task when it enters with audio available, so
        # a change here means the application must be restarted to take effect.
        self._app_audio = audio_ready
        self.theme = theme
        self.palette = THEMES[theme]
        self.fixed_scale = fixed_scale
        self.fullscreen = fullscreen
        self.lib_path = Path(lib_path) if lib_path else default_library_path()

        self._last_raw: bytes | None = None
        self._base_photo: tk.PhotoImage | None = None
        self._render_photo: tk.PhotoImage | None = None
        self._frame_serial = 0
        self._render_key: tuple | None = None
        self._last_zoom = 1.0
        self._press_start: dict[int, float] = {}
        self._running = True
        self._themed: list[tuple[tk.Widget, dict[str, str]]] = []
        self._device_size = (SRC_WIDTH, SRC_HEIGHT)
        self._backlight = -1

        # Open source documents and their editors, keyed by document path.
        self._docs: dict[str, _Document] = {}
        self._editors: dict[str, CodeEditor] = {}
        # Key of the active source document, or None while no file is open. The
        # device mirror is a permanent pane, not a tab, so it is never active.
        self.active_key: str | None = None
        # Build/relaunch state; the worker thread only talks through the queue.
        self._build_queue: queue.Queue[tuple[str, str]] = queue.Queue()
        self._building = False
        self._build_pending = False
        self._build_note = ""
        # External-change watch: the source files the build reads, the last state
        # seen for them, and the files awaiting the debounce window.
        self._watch_files = watch_files(REPO_ROOT)
        self._watch_state = snapshot(self._watch_files)
        self._watch_changed: set[Path] = set()
        self._watch_pending_at: float | None = None
        # Hot swap state: the private library copy currently loaded, and the
        # counter that keeps every swap's path unique within this process.
        self._swap_serial = 0
        self._staged: Path | None = None

        # Flashing state: the detected ESP-IDF checkout, the serial ports seen
        # on the host, the chosen target, and the worker thread's queue. Port
        # rows are recoloured directly (not through the theme registry) because
        # they are rebuilt whenever the device list changes.
        self._idf_root = find_idf_root()
        self._ports: list = []
        self._port_signature: list[tuple] | None = None
        self._port_row_parts: list[dict] = []
        self._selected_port: str | None = None
        self._flash_scan_at = 0.0
        self._flash_queue: queue.Queue[tuple[str, str]] = queue.Queue()
        self._flashing = False
        self._flash_note = ""

        # Mirrored log file; None when it cannot be opened. The GUI panel stays
        # the primary view, so a missing file must never block startup.
        self._log_handle = self._open_log_file()
        self._cleanup_stale_library()
        self._build_ui()
        if self._log_handle is not None:
            print(f"IDE log: {LOG_PATH}", flush=True)
            self._log_line(f"[ide] 日志同步到 {LOG_PATH}", "build")
        self._register_keys()
        self.apply_theme(theme)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._rebuild_if_sources_are_newer()

    def _cleanup_stale_library(self) -> None:
        """Remove library leftovers from a previous session.

        ``*.old`` is the rename-aside copy an interrupted build leaves behind;
        ``libpassport_sim.<token>.*`` are the private copies earlier hot swaps
        loaded, which a new process does not need (and must not reuse).
        """
        stale = self.lib_path.with_name(self.lib_path.name + ".old")
        try:
            stale.unlink(missing_ok=True)
        except OSError:
            pass
        for path in self.lib_path.parent.glob(f"{self.lib_path.stem}.*{self.lib_path.suffix}"):
            try:
                path.unlink()
            except OSError:
                pass

    def _open_log_file(self) -> io.TextIOWrapper | None:
        """Open the mirrored log file, or return None when it is unavailable."""
        try:
            LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
            return LOG_PATH.open("w", encoding="utf-8", buffering=1)
        except OSError:
            return None

    def _write_log_file(self, line: str, tag: str | None) -> None:
        """Append one panel entry to the mirrored file; never raise."""
        handle = self._log_handle
        if handle is None:
            return
        try:
            handle.write(f"{time.strftime('%H:%M:%S')} [{tag or 'info'}] {line}\n")
        except (OSError, ValueError):
            pass

    # -- theming ------------------------------------------------------------
    def _theme(self, widget: tk.Widget, **options: str) -> tk.Widget:
        """Register a widget option -> palette role mapping and apply it."""
        self._themed.append((widget, options))
        widget.configure(**{opt: self.palette[key] for opt, key in options.items()})
        return widget

    def apply_theme(self, name: str) -> None:
        self.theme = name
        self.palette = THEMES[name]
        for widget, options in self._themed:
            widget.configure(**{opt: self.palette[key] for opt, key in options.items()})

        self.log_text.tag_configure("err", foreground=self.palette["log_err"])
        self.log_text.tag_configure("warn", foreground=self.palette["log_warn"])
        self.log_text.tag_configure("host", foreground=self.palette["accent"])
        self.log_text.tag_configure("build", foreground=self.palette["fg_muted"])
        self.log_text.tag_configure("info", foreground=self.palette["log_fg"])

        if platform.system() != "Darwin":
            for menu in self._menus:
                menu.configure(
                    background=self.palette["panel_bg"],
                    foreground=self.palette["fg"],
                    activebackground=self.palette["accent"],
                    activeforeground=self.palette["accent_fg"],
                )

        # Text-tag colours and the active tab highlight live outside the registry.
        self.explorer.apply_palette(self.palette)
        self.tabs.apply_palette(self.palette)
        for editor in self._editors.values():
            editor.apply_palette(self.palette)

        self.theme_var.set(name)
        self.toolbar_theme_button.configure(text=f"主题 · {THEME_LABELS[name]}")
        self._style_port_rows()
        self._select_page(self.page)
        self._render_preview(force=True)

    # -- shell construction --------------------------------------------------
    def _build_ui(self) -> None:
        families = set(tkfont.families(self.root))
        family = _pick_family(families, "Helvetica", "DejaVu Sans", "TkDefaultFont")
        mono = _pick_family(families, "Menlo", "DejaVu Sans Mono", "Courier")
        self.font_ui = (family, 12)
        self.font_ui_bold = (family, 12, "bold")
        self.font_small = (family, 11)
        self.font_mono = (mono, 12)
        self.fonts = {"ui": self.font_ui, "small": self.font_small, "mono": self.font_mono}

        self.root.title("AI Passport Dev IDE")
        self.root.minsize(760, 560)
        # Open centered and never larger than the screen, so the shell fits small
        # displays as well as large ones.
        screen_w = self.root.winfo_screenwidth()
        screen_h = self.root.winfo_screenheight()
        width = min(1180, max(760, screen_w - 80))
        height = min(760, max(560, screen_h - 120))
        self.root.geometry(
            f"{width}x{height}+{max(0, (screen_w - width) // 2)}+{max(0, (screen_h - height) // 3)}"
        )
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)

        self._build_menu()

        toolbar = tk.Frame(self.root, height=TOOLBAR_HEIGHT)
        toolbar.grid(row=0, column=0, sticky="ew")
        toolbar.pack_propagate(False)
        self._theme(toolbar, bg="toolbar_bg")
        self._build_toolbar(toolbar)

        self.main_pane = tk.PanedWindow(
            self.root,
            orient="vertical",
            sashwidth=SASH_WIDTH,
            sashrelief="flat",
            bd=0,
            opaqueresize=True,
            showhandle=False,
        )
        self.main_pane.grid(row=1, column=0, sticky="nsew")
        self._theme(self.main_pane, bg="border")

        top_pane = tk.PanedWindow(
            self.main_pane,
            orient="horizontal",
            sashwidth=SASH_WIDTH,
            sashrelief="flat",
            bd=0,
            opaqueresize=True,
            showhandle=False,
        )
        self._theme(top_pane, bg="border")

        left = self._build_left_column(top_pane)
        editor = self._build_editor(top_pane)
        top_pane.add(left, minsize=ACTIVITY_WIDTH + 160, width=ACTIVITY_WIDTH + SIDEBAR_WIDTH, stretch="never")
        # Keep the editor pane wide enough for the code area plus the docked
        # device mirror, so the mirror is never clipped.
        top_pane.add(editor, minsize=SCREEN_PANE_WIDTH + 160, stretch="always")

        log_panel = self._build_log_panel(self.main_pane)
        self.main_pane.add(top_pane, minsize=320, stretch="always")
        self.main_pane.add(log_panel, minsize=110, height=LOG_HEIGHT, stretch="never")

        status = tk.Frame(self.root, height=STATUS_HEIGHT)
        status.grid(row=2, column=0, sticky="ew")
        status.pack_propagate(False)
        self._theme(status, bg="status_bg")
        self.status_left_var = tk.StringVar(value="")
        self.status_right_var = tk.StringVar(value="")
        self._theme(
            tk.Label(status, textvariable=self.status_left_var, font=self.font_small, anchor="w"),
            bg="status_bg", fg="fg_muted",
        ).pack(side="left", padx=12)
        self._theme(
            tk.Label(status, textvariable=self.status_right_var, font=self.font_small, anchor="e"),
            bg="status_bg", fg="fg_muted",
        ).pack(side="right", padx=12)

        if self.fullscreen:
            # macOS only honours -fullscreen once the window is mapped.
            self.root.after(200, lambda: self.root.attributes("-fullscreen", True))

    def _build_menu(self) -> None:
        accel = "⌘" if platform.system() == "Darwin" else "Ctrl+"
        menubar = tk.Menu(self.root)
        self._menus = [menubar]

        file_menu = tk.Menu(menubar, tearoff=0)
        self._menus.append(file_menu)
        file_menu.add_command(label="保存", accelerator=f"{accel}S", command=self._save_active)
        file_menu.add_command(label="关闭标签", accelerator=f"{accel}W", command=self._close_active_tab)
        file_menu.add_separator()
        file_menu.add_command(label="清空日志", accelerator=f"{accel}L", command=self._clear_log)
        file_menu.add_separator()
        file_menu.add_command(label="退出", command=self._on_close)
        menubar.add_cascade(label="文件", menu=file_menu)

        view_menu = tk.Menu(menubar, tearoff=0)
        self._menus.append(view_menu)
        view_menu.add_command(label="适应窗口", accelerator=f"{accel}0", command=self._fit)
        view_menu.add_command(label="放大", accelerator=f"{accel}=", command=lambda: self._zoom_by(1))
        view_menu.add_command(label="缩小", accelerator=f"{accel}-", command=lambda: self._zoom_by(-1))
        view_menu.add_separator()
        self.theme_var = tk.StringVar(value=self.theme)
        theme_menu = tk.Menu(view_menu, tearoff=0)
        self._menus.append(theme_menu)
        theme_menu.add_radiobutton(
            label="深色", variable=self.theme_var, value="dark",
            command=lambda: self.apply_theme("dark"),
        )
        theme_menu.add_radiobutton(
            label="浅色", variable=self.theme_var, value="light",
            command=lambda: self.apply_theme("light"),
        )
        view_menu.add_cascade(label="主题", menu=theme_menu)
        view_menu.add_separator()
        self.fullscreen_var = tk.BooleanVar(value=self.fullscreen)
        view_menu.add_checkbutton(
            label="全屏", accelerator="F11", variable=self.fullscreen_var,
            command=self._toggle_fullscreen,
        )
        menubar.add_cascade(label="视图", menu=view_menu)

        run_menu = tk.Menu(menubar, tearoff=0)
        self._menus.append(run_menu)
        run_menu.add_command(
            label="重新编译并热更新", accelerator=f"{accel}R", command=self._run_build
        )
        run_menu.add_separator()
        run_menu.add_command(
            label="烧录到设备（增量）",
            command=lambda: self._start_flash(MODE_INCREMENTAL),
        )
        run_menu.add_command(
            label="烧录完整镜像（重置 NVS）",
            command=lambda: self._start_flash(MODE_FULL),
        )
        menubar.add_cascade(label="运行", menu=run_menu)

        device_menu = tk.Menu(menubar, tearoff=0)
        self._menus.append(device_menu)
        device_menu.add_command(label="上", command=lambda: self._send(BTN_UP, EV_CLICK))
        device_menu.add_command(label="确定", command=lambda: self._send(BTN_OK, EV_CLICK))
        device_menu.add_command(label="下", command=lambda: self._send(BTN_DOWN, EV_CLICK))
        menubar.add_cascade(label="设备", menu=device_menu)

        audio_menu = tk.Menu(menubar, tearoff=0)
        self._menus.append(audio_menu)
        audio_menu.add_command(
            label="开关麦克风", accelerator=f"{accel}M", command=self._toggle_microphone
        )
        menubar.add_cascade(label="音频", menu=audio_menu)

        help_menu = tk.Menu(menubar, tearoff=0)
        self._menus.append(help_menu)
        help_menu.add_command(label="快捷键", command=lambda: self._select_page("keys"))
        help_menu.add_command(label="关于", command=self._show_about)
        menubar.add_cascade(label="帮助", menu=help_menu)

        self.root.configure(menu=menubar)
        self._accel = accel

    def _build_toolbar(self, toolbar: tk.Frame) -> None:
        self._theme(
            tk.Label(toolbar, text="AI PASSPORT DEV IDE", font=self.font_ui_bold),
            bg="toolbar_bg", fg="fg",
        ).pack(side="left", padx=(12, 16))

        right = tk.Frame(toolbar)
        right.pack(side="right", padx=(0, 8))
        self._theme(right, bg="toolbar_bg")

        self.toolbar_theme_button = self._flat_button(right, "主题 · 深色", self._toggle_theme)
        self.toolbar_theme_button.pack(side="right", padx=2)
        self._flat_button(right, "清空日志", self._clear_log).pack(side="right", padx=2)
        self._flat_button(right, "全屏", self._toggle_fullscreen).pack(side="right", padx=2)
        self._flat_button(right, "放大", lambda: self._zoom_by(1)).pack(side="right", padx=2)
        self._flat_button(right, "缩小", lambda: self._zoom_by(-1)).pack(side="right", padx=2)
        self._flat_button(right, "适应窗口", self._fit).pack(side="right", padx=2)
        self._flat_button(right, "保存", self._save_active).pack(side="right", padx=2)
        self._flat_button(
            right, "烧录", lambda: self._start_flash(MODE_INCREMENTAL)
        ).pack(side="right", padx=2)
        self.mic_button = self._flat_button(
            right, f"麦克风 · {'开' if self.audio_ready else '关'}", self._toggle_microphone
        )
        self.mic_button.pack(side="right", padx=2)

    # Buttons are Labels, not tk.Buttons: on macOS tk.Button is drawn by the
    # native (light) appearance and ignores -background/-foreground, which would
    # break both themes. A bound Label is fully colour-controlled everywhere.
    def _flat_button(
        self,
        parent: tk.Widget,
        text: str,
        command=None,
        *,
        bg: str = "toolbar_bg",
        fg: str = "fg",
        font: tuple | None = None,
        px: int = 10,
        py: int = 4,
    ) -> tk.Label:
        label = tk.Label(
            parent,
            text=text,
            font=font or self.font_small,
            padx=px,
            pady=py,
            cursor="hand2",
            takefocus=0,
        )
        self._theme(label, bg=bg, fg=fg)
        label.bind("<Enter>", lambda _e: label.configure(bg=self.palette["hover"]))
        label.bind("<Leave>", lambda _e: label.configure(bg=self.palette[bg]))
        if command is not None:
            label.bind("<Button-1>", lambda _e: label.configure(bg=self.palette["selection"]))
            label.bind(
                "<ButtonRelease-1>",
                lambda _e: (label.configure(bg=self.palette["hover"]), command()),
            )
        return label

    def _device_key_button(self, parent: tk.Widget, text: str, btn: int) -> None:
        wrapper = tk.Frame(parent, bd=0, highlightthickness=0)
        self._theme(wrapper, bg="border")
        wrapper.pack(side="left", fill="both", expand=True, padx=3, pady=2)
        label = tk.Label(
            wrapper,
            text=text,
            font=self.font_ui,
            padx=10,
            pady=7,
            cursor="hand2",
            takefocus=0,
        )
        self._theme(label, bg="panel_bg", fg="fg")
        label.pack(fill="both", expand=True, padx=1, pady=1)
        label.bind("<Enter>", lambda _e: label.configure(bg=self.palette["hover"]))
        label.bind("<Leave>", lambda _e: label.configure(bg=self.palette["panel_bg"]))
        # Press/release so a click emits exactly one CLICK or LONG.
        label.bind(
            "<ButtonPress-1>",
            lambda _e: (
                label.configure(bg=self.palette["selection"]),
                self._press_start.__setitem__(btn, time.monotonic()),
            ),
        )
        label.bind("<ButtonRelease-1>", lambda _e: self._on_key_release(btn, label))

    # -- left column ---------------------------------------------------------
    def _build_left_column(self, parent: tk.PanedWindow) -> tk.Frame:
        column = tk.Frame(parent)
        self._theme(column, bg="panel_bg")

        bar = tk.Frame(column, width=ACTIVITY_WIDTH)
        bar.pack(side="left", fill="y")
        bar.pack_propagate(False)
        self._theme(bar, bg="panel_bg")

        self.activity_items: dict[str, tuple[tk.Frame, tk.Label]] = {}
        for key, glyph, _title in PAGES:
            item = tk.Frame(bar, height=42)
            item.pack(fill="x")
            item.pack_propagate(False)
            self._theme(item, bg="panel_bg")
            indicator = tk.Frame(item, width=2)
            indicator.pack(side="left", fill="y")
            self._theme(indicator, bg="panel_bg")
            button = tk.Label(
                item,
                text=glyph,
                cursor="hand2",
                takefocus=0,
                font=(self.font_ui[0], 15),
            )
            self._theme(button, bg="panel_bg", fg="fg_muted")
            button.pack(side="left", fill="both", expand=True)
            button.bind("<Button-1>", lambda _e, k=key: self._select_page(k))
            button.bind("<Enter>", lambda _e, k=key: self._activity_hover(k, True))
            button.bind("<Leave>", lambda _e, k=key: self._activity_hover(k, False))
            self.activity_items[key] = (indicator, button)

        self.side_panel = tk.Frame(column, width=SIDEBAR_WIDTH)
        self.side_panel.pack(side="left", fill="both", expand=True)
        self.side_panel.pack_propagate(False)
        self._theme(self.side_panel, bg="panel_bg")
        self.side_panel.rowconfigure(0, weight=1)
        self.side_panel.columnconfigure(0, weight=1)

        self.pages: dict[str, tk.Frame] = {}
        for key, _glyph, title in PAGES:
            page = tk.Frame(self.side_panel)
            self._theme(page, bg="panel_bg")
            page.grid(row=0, column=0, sticky="nsew")
            self._section_header(page, title)
            self.pages[key] = page

        self._build_sources_page()
        self._build_device_page()
        self._build_flash_page()
        self._build_audio_page()
        self._build_keys_page()
        self.page = "sources"
        self._select_page("sources")
        return column

    def _build_sources_page(self) -> None:
        page = self.pages["sources"]
        self.explorer = SourceExplorer(
            page,
            self._theme,
            self.palette,
            on_open=self._open_source,
            root_label=f"{MAIN_DIR.name}/",
        )
        self.explorer.pack(fill="both", expand=True, pady=(6, 0))
        self.explorer.refresh(list_sources(MAIN_DIR))

    def _section_header(self, page: tk.Frame, text: str) -> tk.Frame:
        header = tk.Frame(page)
        header.pack(fill="x")
        self._theme(header, bg="panel_bg")
        self._theme(
            tk.Label(header, text=text, font=self.font_ui_bold, anchor="w"),
            bg="panel_bg", fg="fg",
        ).pack(side="left", padx=12, pady=(10, 6))
        line = tk.Frame(page, height=1)
        line.pack(fill="x")
        self._theme(line, bg="border_soft")
        return header

    def _key_value_row(self, parent: tk.Frame, label: str, variable: tk.StringVar) -> None:
        row = tk.Frame(parent)
        row.pack(fill="x", padx=12, pady=2)
        self._theme(row, bg="panel_bg")
        self._theme(
            tk.Label(row, text=label, font=self.font_small, anchor="w", width=8),
            bg="panel_bg", fg="fg_muted",
        ).pack(side="left")
        self._theme(
            tk.Label(row, textvariable=variable, font=self.font_small, anchor="w"),
            bg="panel_bg", fg="fg",
        ).pack(side="left")

    def _build_device_page(self) -> None:
        page = self.pages["device"]
        body = tk.Frame(page)
        body.pack(fill="x", pady=(6, 4))
        self._theme(body, bg="panel_bg")

        self.resolution_var = tk.StringVar(value=f"{SRC_WIDTH}×{SRC_HEIGHT}")
        self.zoom_var = tk.StringVar(value="100%")
        self.backlight_var = tk.StringVar(value="--")
        self.audio_var = tk.StringVar(value="--")
        self.backend_var = tk.StringVar(value="simulator (ctypes)")
        self._key_value_row(body, "分辨率", self.resolution_var)
        self._key_value_row(body, "缩放", self.zoom_var)
        self._key_value_row(body, "背光", self.backlight_var)
        self._key_value_row(body, "音频", self.audio_var)
        self._key_value_row(body, "后端", self.backend_var)

        self._section_header(page, "按键说明")
        self._theme(
            tk.Label(
                page,
                text="按键位于设备屏幕下方；长按超过 700 ms 触发 LONG；\n"
                     "键盘 ↑ / ↓ / Enter / Space 等效。",
                font=self.font_small,
                justify="left",
                anchor="w",
                wraplength=SIDEBAR_WIDTH - 30,
            ),
            bg="panel_bg", fg="fg_muted",
        ).pack(fill="x", padx=12, pady=(6, 0))

    def _build_flash_page(self) -> None:
        page = self.pages["flash"]
        body = tk.Frame(page)
        body.pack(fill="x", pady=(6, 4))
        self._theme(body, bg="panel_bg")

        self.idf_var = tk.StringVar(value="")
        idf_row = tk.Frame(body)
        idf_row.pack(fill="x", padx=12, pady=2)
        self._theme(idf_row, bg="panel_bg")
        self._theme(
            tk.Label(idf_row, text="ESP-IDF", font=self.font_small, anchor="w"),
            bg="panel_bg", fg="fg_muted",
        ).pack(side="top", fill="x")
        self._theme(
            tk.Label(
                idf_row, textvariable=self.idf_var, font=self.font_small, anchor="w",
                justify="left", wraplength=SIDEBAR_WIDTH - 30,
            ),
            bg="panel_bg", fg="fg",
        ).pack(side="top", fill="x")

        self._section_header(page, "串口设备")
        controls = tk.Frame(page)
        controls.pack(fill="x", padx=12, pady=(6, 2))
        self._theme(controls, bg="panel_bg")
        self._flat_button(controls, "刷新", self._refresh_ports, bg="panel_bg").pack(side="left")
        self.port_hint_var = tk.StringVar(value="")
        self._theme(
            tk.Label(controls, textvariable=self.port_hint_var, font=self.font_small, anchor="w"),
            bg="panel_bg", fg="fg_muted",
        ).pack(side="left", padx=(8, 0))

        self.ports_frame = tk.Frame(page)
        self.ports_frame.pack(fill="x", padx=12, pady=(4, 0))
        self._theme(self.ports_frame, bg="panel_bg")

        self._section_header(page, "烧录")
        actions = tk.Frame(page)
        actions.pack(fill="x", padx=12, pady=(6, 0))
        self._theme(actions, bg="panel_bg")
        self._flat_button(
            actions, "增量烧录", lambda: self._start_flash(MODE_INCREMENTAL), bg="panel_bg"
        ).pack(side="left")
        self._flat_button(
            actions, "完整镜像", lambda: self._start_flash(MODE_FULL), bg="panel_bg"
        ).pack(side="left", padx=6)

        self.flash_note_var = tk.StringVar(value="")
        self._theme(
            tk.Label(
                page, textvariable=self.flash_note_var, font=self.font_small,
                anchor="w", justify="left", wraplength=SIDEBAR_WIDTH - 30,
            ),
            bg="panel_bg", fg="fg",
        ).pack(fill="x", padx=12, pady=(8, 0))
        self._theme(
            tk.Label(
                page,
                text="增量烧录保留设备 NVS；完整镜像从 0x0 写入，会重置 NVS。",
                font=self.font_small, anchor="w", justify="left",
                wraplength=SIDEBAR_WIDTH - 30,
            ),
            bg="panel_bg", fg="fg_muted",
        ).pack(fill="x", padx=12, pady=(6, 0))

        self._refresh_ports()

    # -- device discovery ----------------------------------------------------
    def _refresh_ports(self) -> None:
        """Rescan ESP-IDF and the serial ports; rebuild rows only on change."""
        self._idf_root = find_idf_root()
        self.idf_var.set(
            str(self._idf_root) if self._idf_root else "未找到（激活 export.sh 或设置 AI_PASSPORT_IDF_ROOT）"
        )
        ports = list_serial_ports()
        signature = [(port.device, port.description, port.likely_esp) for port in ports]
        if signature == self._port_signature:
            return
        self._port_signature = signature
        self._ports = ports
        devices = {port.device for port in ports}
        if self._selected_port not in devices:
            preferred = next((port.device for port in ports if port.likely_esp), None)
            self._selected_port = preferred or (ports[0].device if ports else None)
        self._rebuild_port_rows()

    def _rebuild_port_rows(self) -> None:
        for parts in self._port_row_parts:
            parts["row"].destroy()
        self._port_row_parts = []
        if not self._ports:
            self.port_hint_var.set("未发现设备，请检查 USB 连接")
            return
        self.port_hint_var.set(f"发现 {len(self._ports)} 个设备")
        for port in self._ports:
            self._port_row_parts.append(self._port_row(port))
        self._style_port_rows()

    def _port_row(self, port) -> dict:
        row = tk.Frame(self.ports_frame, cursor="hand2")
        row.pack(fill="x", pady=1)
        parts = {"row": row, "device": port.device}
        parts["marker"] = tk.Label(row, text="", width=2, takefocus=0, font=self.font_small)
        parts["marker"].pack(side="left")
        info = tk.Frame(row)
        info.pack(side="left", fill="x", expand=True)
        parts["info"] = info
        title = port.device + ("   · ESP32 可能" if port.likely_esp else "")
        parts["name"] = tk.Label(
            info, text=title, anchor="w", takefocus=0, font=self.font_small,
            justify="left", wraplength=SIDEBAR_WIDTH - 60,
        )
        parts["name"].pack(fill="x")
        parts["desc"] = tk.Label(
            info, text=port.description or "未知设备", anchor="w", takefocus=0,
            font=self.font_small, justify="left", wraplength=SIDEBAR_WIDTH - 60,
        )
        parts["desc"].pack(fill="x")
        for widget in (row, parts["marker"], info, parts["name"], parts["desc"]):
            widget.bind("<Button-1>", lambda _e, device=port.device: self._select_port(device))
        return parts

    def _style_port_rows(self) -> None:
        """Recolour the port rows; they are rebuilt often, so they skip the
        theme registry and are repainted from the current palette instead."""
        palette = self.palette
        for parts in self._port_row_parts:
            selected = parts["device"] == self._selected_port
            bg = palette["selection"] if selected else palette["panel_bg"]
            parts["row"].configure(bg=bg)
            parts["info"].configure(bg=bg)
            parts["marker"].configure(
                text="●" if selected else "○",
                bg=bg, fg=palette["accent"] if selected else palette["fg_muted"],
            )
            parts["name"].configure(bg=bg, fg=palette["accent"] if selected else palette["fg"])
            parts["desc"].configure(bg=bg, fg=palette["fg_muted"])

    def _select_port(self, device: str) -> None:
        self._selected_port = device
        self._style_port_rows()
        self._log_line(f"[flash] 目标设备 {device}", "build")

    # -- flashing ------------------------------------------------------------
    def _start_flash(self, mode: str) -> None:
        self._select_page("flash")
        if self._flashing:
            self._log_line("[flash] 正在烧录，请等待当前任务结束", "warn")
            return
        idf_root = self._idf_root or find_idf_root()
        self._idf_root = idf_root
        if idf_root is None:
            self._log_line(
                "[flash] 未找到 ESP-IDF 5.5.3；请先激活 export.sh 或设置 AI_PASSPORT_IDF_ROOT", "err"
            )
            return
        if self._selected_port is None:
            self._refresh_ports()
        if self._selected_port is None:
            self._log_line("[flash] 未选择串口设备；请连接设备后点击“刷新”", "err")
            return
        if mode == MODE_FULL and not messagebox.askyesno(
            "完整镜像烧录",
            f"将从 0x0 写入完整镜像到：\n{self._selected_port}\n\n"
            "这会重置设备上的 NVS 设置（存档等）。是否继续？",
        ):
            return
        try:
            job = build_flash_job(mode, idf_root, self._selected_port, REPO_ROOT)
        except (OSError, ValueError) as exc:
            self._log_line(f"[flash] 无法构建烧录命令: {exc}", "err")
            return
        self._flashing = True
        self._flash_note = f"{job.description}…"
        self.flash_note_var.set(self._flash_note)
        self._log_line(f"[flash] {job.description} → {self._selected_port}", "host")
        threading.Thread(target=self._flash_worker, args=(job,), daemon=True).start()

    def _flash_worker(self, job) -> None:
        """Run the flashing command, streaming its output to the queue."""
        try:
            process = subprocess.Popen(
                job.argv,
                cwd=str(REPO_ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            assert process.stdout is not None
            for line in process.stdout:
                self._flash_queue.put(("line", line.rstrip("\n")))
            code = process.wait()
        except Exception as exc:  # noqa: BLE001 - surface any launch failure
            self._flash_queue.put(("error", f"[flash] 无法启动烧录: {exc}"))
            self._flash_queue.put(("done", "fail"))
            return
        self._flash_queue.put(("done", "ok" if code == 0 else "fail"))

    def _drain_flash(self) -> None:
        while True:
            try:
                kind, payload = self._flash_queue.get_nowait()
            except queue.Empty:
                return
            if kind == "line":
                lowered = payload.lower()
                tag = "err" if ("error" in lowered or "失败" in payload) else "build"
                self._log_line(payload, tag)
            elif kind == "error":
                self._log_line(payload, "err")
            else:
                self._on_flash_done(payload)

    def _on_flash_done(self, result: str) -> None:
        self._flashing = False
        if result == "ok":
            self._flash_note = "烧录完成"
            self._log_line("[flash] 烧录完成", "host")
        else:
            self._flash_note = "烧录失败"
            self._log_line("[flash] 烧录失败，请查看上方日志", "err")
        self.flash_note_var.set(self._flash_note)

    def _build_audio_page(self) -> None:
        page = self.pages["audio"]
        body = tk.Frame(page)
        body.pack(fill="x", pady=(6, 4))
        self._theme(body, bg="panel_bg")

        self.audio_state_var = tk.StringVar(value="--")
        self.audio_rate_var = tk.StringVar(value=f"{SAMPLE_RATE} Hz")
        self.audio_format_var = tk.StringVar(value="16-bit / mono")
        self._key_value_row(body, "状态", self.audio_state_var)
        self._key_value_row(body, "采样率", self.audio_rate_var)
        self._key_value_row(body, "格式", self.audio_format_var)

        if not self.audio_note:
            self.audio_note = (
                "麦克风已连接，可驱动调音器。" if self.audio_ready else "麦克风不可用（无信号模式）。"
            )
        controls = tk.Frame(page)
        controls.pack(fill="x", padx=12, pady=(8, 0))
        self._theme(controls, bg="panel_bg")
        self.audio_mic_button = self._flat_button(
            controls,
            "关闭麦克风" if self.audio_ready else "开启麦克风",
            self._toggle_microphone,
            bg="panel_bg",
        )
        self.audio_mic_button.pack(side="left")

        self.audio_note_var = tk.StringVar(value=self.audio_note)
        self._theme(
            tk.Label(page, textvariable=self.audio_note_var, font=self.font_small, justify="left", anchor="w", wraplength=SIDEBAR_WIDTH - 30),
            bg="panel_bg", fg="fg_muted",
        ).pack(fill="x", padx=12, pady=(10, 0))
        self._theme(
            tk.Label(
                page,
                text="在麦克风旁拨弦或哼唱即可看到调音器响应。",
                font=self.font_small,
                justify="left",
                anchor="w",
                wraplength=SIDEBAR_WIDTH - 30,
            ),
            bg="panel_bg", fg="fg_muted",
        ).pack(fill="x", padx=12, pady=(6, 0))

    def _build_keys_page(self) -> None:
        page = self.pages["keys"]
        rows = (
            ("↑ / ↓ / Enter / Space", "设备按键 CLICK"),
            ("长按按键 > 700 ms", "设备按键 LONG"),
            ("F11 / Esc", "进入 / 退出全屏"),
            (f"{self._accel}0", "适应窗口"),
            (f"{self._accel}= / {self._accel}-", "放大 / 缩小"),
            (f"{self._accel}S", "保存（并自动重编热更新）"),
            (f"{self._accel}W", "关闭当前标签"),
            (f"{self._accel}R", "保存全部并重新编译"),
            (f"{self._accel}L", "清空日志"),
            (f"{self._accel}M", "开关麦克风"),
            (f"{self._accel}⇧T", "切换深/浅主题"),
        )
        body = tk.Frame(page)
        body.pack(fill="x", pady=(6, 4))
        self._theme(body, bg="panel_bg")
        for keys_text, action in rows:
            row = tk.Frame(body)
            row.pack(fill="x", padx=12, pady=(4, 0))
            self._theme(row, bg="panel_bg")
            self._theme(
                tk.Label(row, text=keys_text, font=self.font_small, anchor="w"),
                bg="panel_bg", fg="accent",
            ).pack(side="top", fill="x")
            self._theme(
                tk.Label(row, text=action, font=self.font_small, anchor="w"),
                bg="panel_bg", fg="fg",
            ).pack(side="top", fill="x")

    def _select_page(self, key: str) -> None:
        self.page = key
        for page_key, page in self.pages.items():
            if page_key == key:
                page.grid()
            else:
                page.grid_remove()
        for item_key, (indicator, button) in self.activity_items.items():
            selected = item_key == key
            indicator.configure(bg=self.palette["accent"] if selected else self.palette["panel_bg"])
            button.configure(
                bg=self.palette["hover"] if selected else self.palette["panel_bg"],
                fg=self.palette["accent"] if selected else self.palette["fg_muted"],
            )

    def _activity_hover(self, key: str, entered: bool) -> None:
        if key == self.page:
            return
        _indicator, button = self.activity_items[key]
        button.configure(bg=self.palette["hover"] if entered else self.palette["panel_bg"])

    # -- editor --------------------------------------------------------------
    def _build_editor(self, parent: tk.PanedWindow) -> tk.Frame:
        editor = tk.Frame(parent)
        self._theme(editor, bg="editor_bg")
        # Column 0 is the code area (stretches); the device mirror is docked to
        # the right edge at a fixed width, behind a one-pixel separator.
        editor.rowconfigure(0, weight=1)
        editor.columnconfigure(0, weight=1)
        editor.columnconfigure(2, minsize=SCREEN_PANE_WIDTH)

        left = tk.Frame(editor)
        self._theme(left, bg="editor_bg")
        left.grid(row=0, column=0, sticky="nsew")
        left.rowconfigure(1, weight=1)
        left.columnconfigure(0, weight=1)

        self.tabs = EditorTabs(
            left,
            self._theme,
            self.palette,
            on_activate=self._activate_tab,
            on_close=self._close_tab,
        )
        self.tabs.grid(row=0, column=0, sticky="ew")

        # Open code editors share this cell; only the active tab's widget stays
        # gridded, so nothing is destroyed when switching tabs.
        self.content = tk.Frame(left)
        self.content.grid(row=1, column=0, sticky="nsew")
        self.content.rowconfigure(0, weight=1)
        self.content.columnconfigure(0, weight=1)
        self._theme(self.content, bg="editor_bg")

        divider = tk.Frame(editor, width=1)
        divider.grid(row=0, column=1, sticky="ns")
        self._theme(divider, bg="border")

        self._build_screen_pane(editor)
        return editor

    def _build_screen_pane(self, editor: tk.Frame) -> None:
        """The live device mirror, permanently docked to the right of the editor."""
        pane = tk.Frame(editor, width=SCREEN_PANE_WIDTH)
        pane.grid(row=0, column=2, sticky="ns")
        pane.grid_propagate(False)
        pane.rowconfigure(2, weight=1)
        pane.columnconfigure(0, weight=1)
        self._theme(pane, bg="editor_bg")

        self.zoom_row = tk.Frame(pane)
        self.zoom_row.grid(row=1, column=0, sticky="ew")
        self._theme(self.zoom_row, bg="editor_bg")
        self.zoom_info_var = tk.StringVar(value="")
        self._theme(
            tk.Label(self.zoom_row, textvariable=self.zoom_info_var, font=self.font_small, anchor="w"),
            bg="editor_bg", fg="fg_muted",
        ).pack(side="left", padx=12, pady=(6, 0))
        self._flat_button(self.zoom_row, "适应窗口", self._fit, bg="editor_bg").pack(
            side="right", padx=8, pady=(4, 0)
        )

        self.canvas = tk.Canvas(pane, highlightthickness=0, bd=0)
        self.canvas.grid(row=2, column=0, sticky="nsew")
        self._theme(self.canvas, bg="editor_bg")

        # Virtual device keys live directly below the mirror they drive.
        keys = tk.Frame(pane)
        keys.grid(row=3, column=0, sticky="ew", padx=6, pady=(2, 8))
        self._theme(keys, bg="editor_bg")
        for label, btn in (("上", BTN_UP), ("确定", BTN_OK), ("下", BTN_DOWN)):
            self._device_key_button(keys, label, btn)

    # -- log panel -----------------------------------------------------------
    def _build_log_panel(self, parent: tk.PanedWindow) -> tk.Frame:
        panel = tk.Frame(parent)
        self._theme(panel, bg="panel_bg")
        panel.rowconfigure(1, weight=1)
        panel.columnconfigure(0, weight=1)

        tab_bar = tk.Frame(panel)
        tab_bar.grid(row=0, column=0, sticky="ew")
        self._theme(tab_bar, bg="tab_inactive_bg")
        tab = tk.Frame(tab_bar)
        tab.pack(side="left", fill="y")
        self._theme(tab, bg="tab_active_bg")
        accent_line = tk.Frame(tab, height=2)
        accent_line.pack(side="bottom", fill="x")
        self._theme(accent_line, bg="accent")
        self._theme(
            tk.Label(tab, text="设备日志", font=self.font_small, padx=14, pady=7),
            bg="tab_active_bg", fg="fg",
        ).pack(side="top", fill="both", expand=True)
        self._flat_button(tab_bar, "清空", self._clear_log, bg="tab_inactive_bg").pack(
            side="right", padx=8, pady=4
        )

        body = tk.Frame(panel)
        body.grid(row=1, column=0, sticky="nsew")
        self._theme(body, bg="panel_bg")
        self.log_text = tk.Text(
            body,
            wrap="none",
            state="disabled",
            takefocus=0,
            relief="flat",
            bd=0,
            highlightthickness=0,
            padx=10,
            pady=6,
            font=self.font_mono,
        )
        self._theme(
            self.log_text,
            bg="editor_bg",
            fg="log_fg",
            insertbackground="fg",
            selectbackground="selection",
            selectforeground="fg",
        )
        # ThemedScrollbar, because tk.Scrollbar keeps its native look on macOS.
        self.log_scroll = ThemedScrollbar(body, self.log_text.yview, self._theme)
        self.log_scroll.pack(side="right", fill="y")
        self.log_text.configure(yscrollcommand=self.log_scroll.set)
        self.log_text.pack(side="left", fill="both", expand=True)
        return panel

    # -- actions -------------------------------------------------------------
    def _toggle_theme(self) -> None:
        self.apply_theme("light" if self.theme == "dark" else "dark")

    def _fit(self) -> None:
        self.fixed_scale = None
        self._render_preview(force=True)

    def _zoom_by(self, delta: int) -> None:
        current = self.fixed_scale if self.fixed_scale is not None else int(round(self._last_zoom))
        self.fixed_scale = max(MIN_ZOOM, min(MAX_ZOOM, current + delta))
        self._render_preview(force=True)

    def _toggle_fullscreen(self) -> None:
        self.fullscreen = not self.fullscreen
        self.root.attributes("-fullscreen", self.fullscreen)
        self.fullscreen_var.set(self.fullscreen)

    def _clear_log(self) -> None:
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")
        handle = self._log_handle
        if handle is not None:
            try:
                handle.seek(0)
                handle.truncate()
            except (OSError, ValueError):
                pass

    def _show_about(self) -> None:
        messagebox.showinfo(
            "关于",
            "AI Passport Dev IDE\n\n"
            "桌面仿真器：在电脑上镜像 240×320 设备界面，\n"
            "用虚拟按键或键盘驱动应用，并实时查看设备日志。\n\n"
            "资源管理器展示要拷贝进 AI Passport 的 main/ 源码；\n"
            "保存源码会自动重新编译并热更新（不重启本窗口）。",
        )

    # -- documents -----------------------------------------------------------
    def _open_source(self, path: Path) -> None:
        key = str(path)
        if key in self._docs:
            self._activate_tab(key)
            return
        try:
            raw = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            self._log_line(f"[ide] 无法打开 {path.name}: {exc}", "err")
            return
        self._docs[key] = _Document(path=path, newline=newline_style(raw))
        editor = CodeEditor(
            self.content,
            self._theme,
            self.palette,
            self.fonts,
            on_change=lambda k=key: self._mark_dirty(k),
        )
        editor.load(normalize_newlines(raw))
        editor.grid(row=0, column=0, sticky="nsew")
        editor.grid_remove()
        self._editors[key] = editor
        self.tabs.add(key, path.name, closable=True)
        self._activate_tab(key)
        self._log_line(f"[ide] 打开 {path.name}", "build")

    def _activate_tab(self, key: str | None) -> None:
        self.active_key = key
        self.tabs.set_active(key)
        for doc_key, editor in self._editors.items():
            if doc_key == key:
                editor.grid()
            else:
                editor.grid_remove()
        editor = self._editors.get(key) if key is not None else None
        if editor is not None:
            editor.focus_text()

    def _close_active_tab(self) -> None:
        if self.active_key is not None:
            self._close_tab(self.active_key)

    def _close_tab(self, key: str) -> None:
        doc = self._docs.get(key)
        if doc is None:
            return
        if doc.dirty:
            answer = messagebox.askyesnocancel(
                "未保存的更改", f"{doc.path.name} 有未保存的更改，关闭前保存吗？"
            )
            if answer is None:
                return
            if answer and not self._save_doc(doc):
                return
        editor = self._editors.pop(key, None)
        if editor is not None:
            editor.destroy()
        self._docs.pop(key, None)
        self.tabs.remove(key)
        if self.active_key == key:
            self._activate_tab(next(iter(self._docs), None))

    def _mark_dirty(self, key: str) -> None:
        doc = self._docs.get(key)
        if doc is None or doc.dirty:
            return
        doc.dirty = True
        self.tabs.set_dirty(key, True)
        self.explorer.set_dirty(doc.path, True)

    def _save_active(self) -> None:
        doc = self._docs.get(self.active_key) if self.active_key is not None else None
        if doc is None or not doc.dirty:
            return
        if self._save_doc(doc):
            self._request_build()

    def _save_all(self) -> bool:
        for doc in self._docs.values():
            if doc.dirty and not self._save_doc(doc):
                return False
        return True

    def _save_doc(self, doc: _Document) -> bool:
        editor = self._editors.get(str(doc.path))
        if editor is None:
            return False
        if not is_within(doc.path, MAIN_DIR):
            self._log_line(f"[ide] 拒绝写入 {doc.path}（不在 main/ 之内）", "err")
            return False
        data = editor.content().replace("\n", doc.newline).encode("utf-8")
        temporary = doc.path.with_name(doc.path.name + ".tmp")
        try:
            temporary.write_bytes(data)
            os.replace(temporary, doc.path)
        except OSError as exc:
            self._log_line(f"[ide] 保存 {doc.path.name} 失败: {exc}", "err")
            return False
        doc.dirty = False
        self.tabs.set_dirty(str(doc.path), False)
        self.explorer.set_dirty(doc.path, False)
        self._note_own_write(doc.path)
        self._log_line(f"[ide] 已保存 {doc.path.name}", "build")
        return True

    # -- build and relaunch --------------------------------------------------
    def _run_build(self) -> None:
        """Command-R: save everything, then rebuild and restart regardless."""
        if not self._save_all():
            return
        self._request_build()

    def _request_build(self) -> None:
        if self._building:
            self._build_pending = True
            self._log_line("[ide] 正在构建，本次保存已排队", "build")
            return
        self._building = True
        self._build_pending = False
        self._build_note = "构建中…"
        self._log_line("[ide] 重新编译仿真库…", "build")
        threading.Thread(target=self._build_worker, daemon=True).start()

    def _build_worker(self) -> None:
        """Relink the simulator library, then hand the result to the UI thread."""
        backup = self.lib_path.with_name(self.lib_path.name + ".old")
        try:
            # Rename the mapped library aside first: overwriting it in place
            # would pull the rug out from under this process's mmap.
            if backup.exists():
                backup.unlink()
            if self.lib_path.exists():
                self.lib_path.rename(backup)
            process = subprocess.Popen(
                [str(BUILD_SCRIPT)],
                cwd=str(REPO_ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            assert process.stdout is not None
            for line in process.stdout:
                self._build_queue.put(("line", line.rstrip("\n")))
            code = process.wait()
        except Exception as exc:  # noqa: BLE001 - surface any build failure
            self._build_queue.put(("error", f"[ide] 构建无法启动: {exc}"))
            self._restore_backup(backup)
            self._build_queue.put(("done", "fail"))
            return

        if code != 0:
            self._restore_backup(backup)
            self._build_queue.put(("line", f"[ide] 构建失败（退出码 {code}）"))
            self._build_queue.put(("done", "fail"))
            return
        if self.lib_path.exists():
            # Keep the previous library until the new one has been validated and
            # swapped in, so a build that turns out to crash can be rolled back.
            self._build_queue.put(("done", "swap"))
            return
        # Nothing to relink (for example a header nobody includes): keep the
        # library that is currently loaded instead of restarting into nothing.
        self._restore_backup(backup)
        self._build_queue.put(("line", "[ide] 无需重新生成仿真库"))
        self._build_queue.put(("done", "noop"))

    def _restore_backup(self, backup: Path) -> None:
        try:
            if backup.exists():
                if self.lib_path.exists():
                    self.lib_path.unlink()
                backup.rename(self.lib_path)
        except OSError:
            pass

    def _drain_build(self) -> None:
        while True:
            try:
                kind, payload = self._build_queue.get_nowait()
            except queue.Empty:
                return
            if kind == "line":
                tag = "err" if ("error:" in payload or "错误" in payload) else "build"
                self._log_line(payload, tag)
            elif kind == "error":
                self._log_line(payload, "err")
            else:
                self._on_build_done(payload)

    def _on_build_done(self, result: str) -> None:
        self._building = False
        if result == "swap":
            self._hot_swap()
            return
        self._build_note = "构建失败" if result == "fail" else ""
        if self._build_pending:
            self._request_build()

    def _hot_swap(self) -> None:
        """Load the freshly built library in place, keeping this session.

        The window, the open editor tabs and the log all survive; only the
        simulated application is restarted, so the mirror returns to its home
        page. The candidate is booted in a throwaway process first: application
        code that crashes (a bad pointer, a failed assertion) would otherwise
        take this whole IDE down, because it runs in this process through
        ctypes. A candidate that fails leaves the running one in place.
        """
        backup = self.lib_path.with_name(self.lib_path.name + ".old")
        try:
            reason = validate_library(self.lib_path)
        except Exception as exc:  # noqa: BLE001 - a failed check must not crash
            reason = f"无法校验新仿真库: {exc}"
        if reason is not None:
            self._build_note = "运行时崩溃"
            self._log_line(f"[ide] {reason}", "err")
            self._log_line("[ide] 已跳过热更新，继续运行原仿真库", "err")
            self._restore_backup(backup)
            return

        token = f"{os.getpid()}.{self._swap_serial}"
        self._swap_serial += 1
        try:
            staged = stage_library(self.lib_path, token)
        except OSError as exc:
            self._build_note = "热更新失败"
            self._log_line(f"[ide] 无法准备新仿真库: {exc}", "err")
            self._restore_backup(backup)
            return
        # Stop the microphone first: its callback thread pushes into whichever
        # library the backend holds.
        self._close_microphone()
        try:
            self.backend.swap(staged, self.audio_ready)
        except Exception as exc:  # noqa: BLE001 - keep the running instance
            self._build_note = "热更新失败"
            self._log_line(f"[ide] 热更新失败，继续使用原仿真库: {exc}", "err")
            self._reopen_microphone()
            return
        # The new library is running: the previous one is no longer needed.
        try:
            backup.unlink(missing_ok=True)
        except OSError:
            pass
        if self._staged is not None:
            try:
                self._staged.unlink(missing_ok=True)
            except OSError:
                pass
        self._staged = staged
        # The new instance draws from scratch: drop the cached frame so the
        # mirror is decoded again even if its first frame looks unchanged.
        self._last_raw = None
        self._app_audio = self.audio_ready
        self._reopen_microphone()
        self._build_note = "已热更新"
        self._log_line("[ide] 已热更新仿真库（未重启进程）", "build")

    def _close_microphone(self) -> None:
        if self.mic is None:
            return
        try:
            self.mic.close()
        except Exception:  # noqa: BLE001 - best effort before the swap
            pass
        self.mic = None

    def _reopen_microphone(self) -> None:
        """Attach a fresh capture stream to the library that is now loaded."""
        if not self.audio_ready:
            return
        try:
            mic = MicCapture(self.backend)
            mic.open()
        except Exception as exc:  # noqa: BLE001 - audio is optional
            self.audio_ready = False
            self.audio_note = f"热更新后麦克风不可用：{exc}"
            self._log_line(f"[ide] 热更新后麦克风不可用: {exc}", "warn")
            return
        self.mic = mic

    # -- microphone ----------------------------------------------------------
    def _toggle_microphone(self) -> None:
        self._set_microphone(not self.audio_ready)

    def _set_microphone(self, enabled: bool) -> None:
        """Turn host microphone capture on or off at run time.

        Off closes the capture stream, so the tuner only sees silence; on opens
        it again. The device application only creates its capture task when it
        enters with audio available, so when that no longer matches the request
        the simulated application is restarted to adopt the new state.
        """
        if enabled:
            if self.mic is None:
                try:
                    mic = MicCapture(self.backend)
                    mic.open()
                except Exception as exc:  # noqa: BLE001 - audio is optional
                    self.audio_note = f"无法开启麦克风：{exc}"
                    self._log_line(f"[ide] 无法开启麦克风：{exc}", "warn")
                    return
                self.mic = mic
            self.audio_ready = True
            self.audio_note = "麦克风已开启，可驱动调音器。"
            self._log_line("[ide] 麦克风已开启", "build")
        else:
            self._close_microphone()
            self.audio_ready = False
            self.audio_note = "麦克风已关闭，调音器进入无信号界面。"
            self._log_line("[ide] 麦克风已关闭", "build")

        if self._app_audio != self.audio_ready:
            self._restart_app()
        self._refresh_microphone_ui()

    def _restart_app(self) -> None:
        """Restart the simulated application without rebuilding it.

        Needed so a microphone change reaches the device, which decides whether
        to run its capture task when it enters a page. The window, the editors
        and the log all survive; only the mirror returns to its home page.
        """
        if self._building:
            return  # the running build adopts the new audio state when it swaps
        token = f"{os.getpid()}.{self._swap_serial}"
        self._swap_serial += 1
        try:
            staged = stage_library(self.lib_path, token)
        except OSError as exc:
            self._log_line(f"[ide] 无法重启仿真应用：{exc}", "err")
            return
        self._close_microphone()
        try:
            self.backend.swap(staged, self.audio_ready)
        except Exception as exc:  # noqa: BLE001 - keep the running instance
            self._log_line(f"[ide] 重启仿真应用失败：{exc}", "err")
            self._reopen_microphone()
            return
        if self._staged is not None:
            try:
                self._staged.unlink(missing_ok=True)
            except OSError:
                pass
        self._staged = staged
        self._last_raw = None
        self._app_audio = self.audio_ready
        self._reopen_microphone()
        self._log_line("[ide] 已重启仿真应用以应用麦克风状态", "build")

    def _refresh_microphone_ui(self) -> None:
        """Keep the microphone controls and the audio note in sync with state."""
        on = self.audio_ready
        self.mic_button.configure(text=f"麦克风 · {'开' if on else '关'}")
        self.audio_mic_button.configure(text="关闭麦克风" if on else "开启麦克风")
        self.audio_note_var.set(self.audio_note)

    # -- external changes ----------------------------------------------------
    def _rebuild_if_sources_are_newer(self) -> None:
        """Rebuild once at startup when the sources moved on since the last build.

        Without this the mirror would keep showing the library that was built
        before an edit made while the IDE was not running.
        """
        if not self._watch_files or not is_stale(self._watch_files, self.lib_path):
            return
        self._log_line("[watch] 源码比仿真库新，正在重新编译…", "build")
        self._request_build()

    def _watch_tick(self) -> None:
        if not self._running:
            return
        self._poll_watch()
        self.root.after(WATCH_MS, self._watch_tick)

    def _poll_watch(self) -> None:
        """Notice edits made outside the IDE, then rebuild once they settle."""
        current = snapshot(self._watch_files)
        changed = changed_paths(self._watch_state, current)
        if changed:
            # Keep pushing the deadline out while files are still arriving, so a
            # burst of edits ends in a single rebuild.
            self._watch_state = current
            self._watch_changed.update(changed)
            self._watch_pending_at = time.monotonic()
            return
        if self._watch_pending_at is None or self._building:
            return
        if (time.monotonic() - self._watch_pending_at) * 1000 < WATCH_DEBOUNCE_MS:
            return
        self._watch_pending_at = None
        pending = sorted(self._watch_changed, key=str)
        self._watch_changed.clear()
        self._apply_external_changes(pending)

    def _apply_external_changes(self, changed: list[Path]) -> None:
        """Refresh the shell for external edits, then rebuild the simulator."""
        names = "、".join(path.name for path in changed)
        self._log_line(f"[watch] 检测到外部修改：{names}", "build")
        self.explorer.refresh(list_sources(MAIN_DIR))
        for doc in self._docs.values():
            self.explorer.set_dirty(doc.path, doc.dirty)
        self._reload_external_documents(changed)
        self._watch_state = snapshot(self._watch_files)
        self._request_build()

    def _reload_external_documents(self, changed: list[Path]) -> None:
        """Reload open editors whose file changed on disk.

        A document with unsaved IDE edits is left alone: the on-disk copy is not
        silently pulled over what the user is typing.
        """
        touched = {str(path) for path in changed}
        for key, doc in self._docs.items():
            if key not in touched:
                continue
            if doc.dirty:
                self._log_line(
                    f"[watch] {doc.path.name} 在 IDE 内有未保存改动，已跳过外部内容", "warn"
                )
                continue
            editor = self._editors.get(key)
            if editor is None:
                continue
            try:
                raw = doc.path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError) as exc:
                self._log_line(f"[watch] 无法重新加载 {doc.path.name}: {exc}", "err")
                continue
            editor.load(normalize_newlines(raw))
            doc.newline = newline_style(raw)
            self._log_line(f"[watch] 已重新加载 {doc.path.name}", "build")

    def _note_own_write(self, path: Path) -> None:
        """Record a save made by this IDE so the watcher ignores it."""
        try:
            info = path.stat()
        except OSError:
            return
        self._watch_state = dict(self._watch_state)
        self._watch_state[path] = (info.st_mtime_ns, info.st_size)

    # -- input ---------------------------------------------------------------
    def _register_keys(self) -> None:
        self.root.bind("<Up>", lambda _e: self._device_key(BTN_UP))
        self.root.bind("<Down>", lambda _e: self._device_key(BTN_DOWN))
        self.root.bind("<Return>", lambda _e: self._device_key(BTN_OK))
        self.root.bind("<space>", lambda _e: self._device_key(BTN_OK))
        self.root.bind("<F11>", lambda _e: self._toggle_fullscreen())
        self.root.bind("<Escape>", self._on_escape)

        mod = "Command" if platform.system() == "Darwin" else "Control"
        for sequence, handler in (
            (f"<{mod}-0>", lambda _e: self._fit()),
            (f"<{mod}-equal>", lambda _e: self._zoom_by(1)),
            (f"<{mod}-plus>", lambda _e: self._zoom_by(1)),
            (f"<{mod}-minus>", lambda _e: self._zoom_by(-1)),
            (f"<{mod}-s>", lambda _e: self._save_active()),
            (f"<{mod}-w>", lambda _e: self._close_active_tab()),
            (f"<{mod}-r>", lambda _e: self._run_build()),
            (f"<{mod}-l>", lambda _e: self._clear_log()),
            (f"<{mod}-m>", lambda _e: self._toggle_microphone()),
            (f"<{mod}-Shift-t>", lambda _e: self._toggle_theme()),
        ):
            self.root.bind(sequence, handler)

    def _device_key(self, btn: int) -> None:
        """Mirror a device key, unless the keystroke belongs to the code editor."""
        if isinstance(self.root.focus_get(), tk.Text):
            return
        self._send(btn, EV_CLICK)

    def _on_escape(self, _event) -> None:
        if self.fullscreen:
            self._toggle_fullscreen()

    def _on_release(self, btn: int) -> None:
        started = self._press_start.pop(btn, None)
        held = (time.monotonic() - started) * 1000 if started else 0
        self._send(btn, EV_LONG if held >= LONG_PRESS_MS else EV_CLICK)

    def _on_key_release(self, btn: int, label: tk.Label) -> None:
        label.configure(bg=self.palette["hover"])
        self._on_release(btn)

    def _send(self, btn: int, ev: int) -> None:
        self.backend.push_button(btn, ev)
        self._log_line(f"[host] key {BTN_LABELS[btn]} {'CLICK' if ev == EV_CLICK else 'LONG'}", "host")

    # -- loop ---------------------------------------------------------------
    def _append_logs(self) -> None:
        for line in self.backend.drain_logs():
            match = re.match(r"\s*([EWI])\s*\(", line)
            tag = {"E": "err", "W": "warn"}.get(match.group(1)) if match else None
            self._log_line(line, tag)

    def _log_line(self, line: str, tag: str | None = None) -> None:
        self._write_log_file(line, tag)
        self.log_text.configure(state="normal")
        self.log_text.insert("end", line + "\n", tag or ())
        count = int(self.log_text.index("end-1c").split(".")[0])
        if count > MAX_LOG_LINES:
            self.log_text.delete("1.0", f"{count - MAX_LOG_LINES + 1}.0")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _refresh_base(self, raw: bytes, w: int, h: int) -> None:
        png = rgb_to_png(decode_frame(raw, w, h), w, h)
        b64 = base64.b64encode(png).decode("ascii")
        self._base_photo = tk.PhotoImage(data=b64)
        self._frame_serial += 1
        self._device_size = (w, h)

    def _render_preview(self, force: bool = False) -> None:
        if self._base_photo is None:
            return
        width = self.canvas.winfo_width()
        height = self.canvas.winfo_height()
        if width <= 1 or height <= 1:
            return

        if self.fixed_scale is None:
            num, den = fit_scale(width, height, padding=EDITOR_PADDING)
            mode = "自动适配"
        else:
            num, den = self.fixed_scale, 1
            mode = f"固定 {self.fixed_scale}x"

        key = (num, den, self._frame_serial, width, height)
        if not force and key == self._render_key:
            return
        self._render_key = key

        photo = self._base_photo if (num, den) == (1, 1) else self._base_photo.zoom(num).subsample(den)
        self._render_photo = photo

        scale = zoom_factor(num, den)
        self._last_zoom = scale
        src_w, src_h = self._device_size
        image_w = round(src_w * scale)
        image_h = round(src_h * scale)

        self.canvas.delete("all")
        cx, cy = width // 2, height // 2
        self.canvas.create_rectangle(
            cx - image_w / 2 - 1, cy - image_h / 2 - 1,
            cx + image_w / 2 + 1, cy + image_h / 2 + 1,
            outline=self.palette["border"],
        )
        self.canvas.create_image(cx, cy, image=photo, anchor="center")

        self.zoom_var.set(f"{int(round(scale * 100))}%")
        self.zoom_info_var.set(f"{src_w}×{src_h}   ·   {int(round(scale * 100))}%   ·   {mode}")

    def _tick(self) -> None:
        if not self._running:
            return
        self.backend.step(STEP_MS)
        raw, w, h = self.backend.framebuffer()
        if raw and raw != self._last_raw:
            self._last_raw = raw
            self._refresh_base(raw, w, h)
        self._render_preview()
        self._append_logs()
        self._drain_build()
        self._drain_flash()
        if self.page == "flash" and (time.monotonic() - self._flash_scan_at) * 1000 >= PORT_SCAN_MS:
            self._flash_scan_at = time.monotonic()
            self._refresh_ports()

        backlight = self.backend.backlight_percent()
        self._backlight = backlight
        audio = "麦克风" if self.audio_ready else "无（静音降级）"
        self.audio_var.set(audio)
        self.audio_state_var.set(audio)
        self._refresh_microphone_ui()
        self.backlight_var.set(f"{backlight}%" if backlight >= 0 else "--")
        self.resolution_var.set(f"{w}×{h}")

        note = f"{self._build_note or self._flash_note}   ·   " if (self._build_note or self._flash_note) else ""
        location = self._document_status()
        self.status_left_var.set(
            f"{note}后端  {self.backend_var.get()}   ·   分辨率  {w}×{h}   ·   缩放  {self.zoom_var.get()}{location}"
        )
        self.status_right_var.set(
            f"背光  {backlight}%   ·   音频  {audio}   ·   {THEME_LABELS[self.theme]}"
        )
        self.root.after(STEP_MS, self._tick)

    def _document_status(self) -> str:
        """``· file.c · 行 L:C`` for the active source tab, empty for the mirror."""
        editor = self._editors.get(self.active_key)
        doc = self._docs.get(self.active_key)
        if editor is None or doc is None:
            return ""
        line, column = editor.cursor_line().split(".")
        return f"   ·   {doc.path.name}   ·   行 {line}:{int(column) + 1}"

    def _on_close(self) -> None:
        dirty = [doc for doc in self._docs.values() if doc.dirty]
        if dirty:
            names = "、".join(doc.path.name for doc in dirty)
            answer = messagebox.askyesnocancel(
                "未保存的更改", f"以下文件有未保存的更改：\n{names}\n\n退出前保存吗？"
            )
            if answer is None:
                return
            if answer and not self._save_all():
                return
        self._running = False
        if self._log_handle is not None:
            try:
                self._log_handle.close()
            except OSError:
                pass
        try:
            if self.mic is not None:
                self.mic.close()
            self.backend.shutdown()
        finally:
            self.root.destroy()

    def run(self) -> None:
        self.root.after(0, self._tick)
        self.root.after(WATCH_MS, self._watch_tick)
        self.root.mainloop()


def main() -> int:
    parser = argparse.ArgumentParser(description="AI Passport desktop IDE")
    parser.add_argument("--backend", default="sim", choices=("sim",),
                        help="display/input backend (only 'sim' implemented)")
    parser.add_argument("--lib", type=Path, default=None,
                        help="path to the simulator shared library")
    parser.add_argument("--scale", type=int, default=None,
                        help="fixed integer zoom; omit for auto-fit to the window")
    parser.add_argument("--theme", choices=("dark", "light"), default="dark",
                        help="colour theme (default dark)")
    parser.add_argument("--fullscreen", action="store_true",
                        help="start in full screen")
    parser.add_argument("--audio", choices=("auto", "on", "off"), default="auto",
                        help="microphone input: auto (default, use it if available), "
                             "on, or off")
    args = parser.parse_args()

    lib_path = Path(args.lib) if args.lib else default_library_path()
    be = create_backend(args.backend, lib_path)

    mic = None
    audio_ready = False
    audio_note = ""
    if args.audio != "off":
        try:
            mic = MicCapture(be)
            mic.open()
            audio_ready = True
            print(f"Microphone enabled ({SAMPLE_RATE} Hz mono); "
                  "hum or pluck near the mic to drive the tuner.", flush=True)
        except Exception as exc:  # noqa: BLE001 - microphone is optional
            mic = None
            audio_ready = False
            audio_note = f"麦克风不可用，已降级为无信号模式：{exc}"
            print(f"Microphone unavailable, running the 'no signal' path: {exc}",
                  file=sys.stderr, flush=True)
            if args.audio == "on":
                print("Hint: install sounddevice (pip install sounddevice) and allow "
                      "microphone access for your terminal.", file=sys.stderr, flush=True)

    try:
        be.init(audio_ready)
    except Exception as exc:  # noqa: BLE001 - surface a clear launch error
        print(f"Failed to start {args.backend} backend: {exc}", file=sys.stderr)
        if mic is not None:
            mic.close()
        return 1

    root = tk.Tk()
    IdeApp(
        root,
        be,
        theme=args.theme,
        fixed_scale=args.scale,
        mic=mic,
        audio_ready=audio_ready,
        audio_note=audio_note,
        fullscreen=args.fullscreen,
        lib_path=lib_path,
    ).run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
