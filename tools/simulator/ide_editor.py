"""Reusable Tk widgets for the desktop IDE's explorer and code editor.

Widgets here never own a palette: each control registers its colour roles
through the caller's ``register_theme`` callback (which is ``IdeApp._theme``),
so switching the theme recolours them through the existing registry. Derived
colours that live in text tags (syntax highlighting, current line) are refreshed
explicitly through ``apply_palette``.

Everything on macOS that must be themeable is built from ``tk.Frame``/
``tk.Label``/``tk.Text``: ``tk.Button`` and ``tk.Scrollbar`` render with the
native (light) appearance there and ignore colours.
"""

from __future__ import annotations

import tkinter as tk
from pathlib import Path

from ide_sources import TOKEN_KINDS, tokenize_c

# Mirror the token kinds onto palette keys; chr reuses the string colour.
TAG_COLOURS = {
    "cmt": "syn_cmt",
    "str": "syn_str",
    "chr": "syn_str",
    "pp": "syn_pp",
    "kw": "syn_kw",
    "num": "syn_num",
    "fn": "syn_fn",
}

HIGHLIGHT_DEBOUNCE_MS = 120
EDITOR_TAB = "    "


class ThemedScrollbar(tk.Frame):
    """A slim track + draggable thumb, since tk.Scrollbar cannot be themed."""

    def __init__(self, parent: tk.Widget, command, register_theme, *, width: int = 12):
        super().__init__(parent, width=width)
        register_theme(self, bg="editor_bg")
        self._command = command
        self._anchor: tuple[float, float] = (0.0, 0.0)
        self._span = 1.0
        self.thumb = tk.Frame(self)
        register_theme(self.thumb, bg="border")
        self.thumb.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.thumb.bind("<ButtonPress-1>", self._on_press)
        self.thumb.bind("<B1-Motion>", self._on_drag)

    def set(self, first: str, last: str) -> None:
        """yscrollcommand target: keep the thumb proportional to the view."""
        start, end = float(first), float(last)
        self._span = max(0.04, end - start)
        self.thumb.place(
            relx=0, rely=min(start, 1.0 - self._span), relwidth=1, relheight=self._span
        )

    def _on_press(self, event) -> None:
        first, _last = self._command()
        self._anchor = (event.y_root, float(first))

    def _on_drag(self, event) -> None:
        anchor_y, anchor_first = self._anchor
        track_height = max(1, self.winfo_height())
        target = anchor_first + (event.y_root - anchor_y) / track_height
        self._command("moveto", min(max(target, 0.0), 1.0 - self._span))


class SourceExplorer(tk.Frame):
    """Scrollable tree of the application sources, one row per file."""

    ROW_PAD = 8

    def __init__(
        self,
        parent: tk.Widget,
        register_theme,
        palette: dict[str, str],
        on_open,
        root_label: str = "main/",
    ):
        super().__init__(parent)
        register_theme(self, bg="panel_bg")
        self._apply_theme = register_theme
        self._palette = palette
        self._on_open = on_open
        self._root_label = root_label
        self._rows: dict[Path, tk.Label] = {}
        self._names: dict[Path, str] = {}
        self._expanded = True

        self.canvas = tk.Canvas(self, highlightthickness=0, bd=0, takefocus=0)
        register_theme(self.canvas, bg="panel_bg")
        self.scroll = ThemedScrollbar(self, self.canvas.yview, register_theme)
        self.scroll.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)

        self._inner = tk.Frame(self.canvas)
        register_theme(self._inner, bg="panel_bg")
        self._window = self.canvas.create_window((0, 0), window=self._inner, anchor="nw")
        self.canvas.configure(yscrollcommand=self.scroll.set)
        self._inner.bind("<Configure>", self._on_inner_configure)
        self.canvas.bind("<Configure>", self._on_canvas_configure)

        self.root_row = tk.Label(
            self._inner, text=f"▾ {root_label}", anchor="w", padx=8, pady=3,
            cursor="hand2", takefocus=0,
        )
        register_theme(self.root_row, bg="panel_bg", fg="fg")
        self.root_row.pack(fill="x")
        self.root_row.bind("<Button-1>", self._toggle)

        self._body = tk.Frame(self._inner)
        register_theme(self._body, bg="panel_bg")
        self._body.pack(fill="x")
        self.empty_row = tk.Label(self._body, text="（无源码）", anchor="w", padx=16, pady=2)
        register_theme(self.empty_row, bg="panel_bg", fg="fg_muted")
        self.empty_row.pack(fill="x")

        for widget in (self.canvas, self._inner, self.root_row):
            self._bind_wheel(widget)

    # -- content -------------------------------------------------------------
    def refresh(self, entries: list[Path]) -> None:
        for row in self._rows.values():
            row.destroy()
        self._rows.clear()
        self._names.clear()
        if not entries:
            self.empty_row.pack(fill="x")
            return
        self.empty_row.pack_forget()
        for path in entries:
            label = tk.Label(
                self._body, text="  " + path.name, anchor="w", padx=self.ROW_PAD,
                pady=2, cursor="hand2", takefocus=0,
            )
            self._apply_theme(label, bg="panel_bg", fg="fg")
            label.pack(fill="x")
            label.bind("<Button-1>", lambda _e, p=path: self._on_open(p))
            label.bind("<Enter>", lambda _e, w=label: w.configure(bg=self._palette["hover"]))
            label.bind("<Leave>", lambda _e, w=label: w.configure(bg=self._palette["panel_bg"]))
            self._bind_wheel(label)
            self._rows[path] = label
            self._names[path] = path.name

    def set_dirty(self, path: Path, dirty: bool) -> None:
        label = self._rows.get(path)
        if label is None:
            return
        label.configure(text=("● " if dirty else "  ") + self._names[path])

    def apply_palette(self, palette: dict[str, str]) -> None:
        self._palette = palette

    # -- internals -----------------------------------------------------------
    def _toggle(self, _event=None) -> None:
        self._expanded = not self._expanded
        if self._expanded:
            self._body.pack(fill="x")
        else:
            self._body.pack_forget()
        self.root_row.configure(text=f"{'▾' if self._expanded else '▸'} {self._root_label}")

    def _on_inner_configure(self, _event=None) -> None:
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _on_canvas_configure(self, event) -> None:
        self.canvas.itemconfigure(self._window, width=event.width)

    def _bind_wheel(self, widget: tk.Widget) -> None:
        widget.bind("<MouseWheel>", self._on_wheel)
        widget.bind("<Button-4>", lambda _e: self._scroll_units(-1))
        widget.bind("<Button-5>", lambda _e: self._scroll_units(1))

    def _on_wheel(self, event) -> str:
        self._scroll_units(-1 if event.delta > 0 else 1)
        return "break"

    def _scroll_units(self, step: int) -> None:
        self.canvas.yview_scroll(step, "units")


class CodeEditor(tk.Frame):
    """Line-numbered, syntax-highlighted text editor for one document."""

    def __init__(
        self,
        parent: tk.Widget,
        register_theme,
        palette: dict[str, str],
        fonts: dict[str, tuple],
        on_change=None,
    ):
        super().__init__(parent)
        register_theme(self, bg="editor_bg")
        self._apply_theme = register_theme
        self._palette = palette
        self._on_change = on_change
        self._highlight_job: str | None = None
        self._loading = False

        self.gutter = tk.Text(
            self, width=4, padx=8, pady=6, takefocus=0, wrap="none", state="disabled",
            relief="flat", bd=0, highlightthickness=0, cursor="arrow", font=fonts["mono"],
        )
        register_theme(self.gutter, bg="editor_bg", fg="gutter_fg", selectbackground="editor_bg")
        self.text = tk.Text(
            self, undo=True, wrap="none", padx=8, pady=6, relief="flat", bd=0,
            highlightthickness=0, font=fonts["mono"], maxundo=-1,
        )
        register_theme(
            self.text, bg="editor_bg", fg="fg", insertbackground="fg",
            selectbackground="selection", selectforeground="fg",
        )
        self.scroll = ThemedScrollbar(self, self.text.yview, register_theme)
        self.scroll.pack(side="right", fill="y")
        self.gutter.pack(side="left", fill="y")
        self.text.pack(side="left", fill="both", expand=True)

        self.text.configure(yscrollcommand=self._on_scroll)
        for sequence in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
            self.gutter.bind(sequence, self._on_gutter_wheel)

        self.text.bind("<<Modified>>", self._on_modified)
        self.text.bind("<KeyRelease>", self._on_cursor_moved)
        self.text.bind("<ButtonRelease-1>", self._on_cursor_moved)
        self.text.bind("<Tab>", self._on_tab)

        self.text.tag_configure("current_line")
        self.gutter.tag_configure("cur")
        self.apply_palette(palette)

    # -- content -------------------------------------------------------------
    def load(self, content: str) -> None:
        self._loading = True
        self.text.delete("1.0", "end")
        self.text.insert("1.0", content)
        self.text.edit_reset()
        self.text.edit_modified(False)
        self.text.mark_set("insert", "1.0")
        self._loading = False
        self._update_gutter()
        self._highlight()
        self._update_current_line()
        self.text.see("1.0")

    def content(self) -> str:
        return self.text.get("1.0", "end-1c")

    def cursor_line(self) -> str:
        return self.text.index("insert")

    def focus_text(self) -> None:
        self.text.focus_set()

    def apply_palette(self, palette: dict[str, str]) -> None:
        self._palette = palette
        self.text.tag_configure("current_line", background=palette["cur_line_bg"])
        for kind in TOKEN_KINDS:
            self.text.tag_configure(kind, foreground=palette[TAG_COLOURS[kind]])
        self.gutter.tag_configure("cur", foreground=palette["accent"])

    # -- internals -----------------------------------------------------------
    def _on_scroll(self, first: str, last: str) -> None:
        self.scroll.set(first, last)
        self.gutter.yview_moveto(float(first))

    def _on_gutter_wheel(self, event) -> str:
        if getattr(event, "num", None) == 4:
            self.text.yview_scroll(-1, "units")
        elif getattr(event, "num", None) == 5:
            self.text.yview_scroll(1, "units")
        else:
            self.text.yview_scroll(-1 if event.delta > 0 else 1, "units")
        return "break"

    def _on_tab(self, _event=None) -> str:
        self.text.insert("insert", EDITOR_TAB)
        return "break"

    def _on_cursor_moved(self, _event=None) -> None:
        self._update_current_line()

    def _on_modified(self, _event=None) -> None:
        if not self.text.edit_modified():
            return
        self.text.edit_modified(False)
        if self._loading:
            return
        self._update_gutter()
        self._schedule_highlight()
        self._update_current_line()
        if self._on_change is not None:
            self._on_change()

    def _schedule_highlight(self) -> None:
        if self._highlight_job is not None:
            self.after_cancel(self._highlight_job)
        self._highlight_job = self.after(HIGHLIGHT_DEBOUNCE_MS, self._highlight)

    def _highlight(self) -> None:
        self._highlight_job = None
        for kind in TOKEN_KINDS:
            self.text.tag_remove(kind, "1.0", "end")
        for start, end, kind in tokenize_c(self.content()):
            self.text.tag_add(kind, f"1.0+{start}c", f"1.0+{end}c")

    def _update_gutter(self) -> None:
        lines = int(self.text.index("end-1c").split(".")[0])
        self.gutter.configure(state="normal")
        self.gutter.delete("1.0", "end")
        self.gutter.insert("1.0", "\n".join(str(number) for number in range(1, lines + 1)))
        self.gutter.configure(state="disabled", width=max(3, len(str(lines))) + 1)

    def _update_current_line(self) -> None:
        self.text.tag_remove("current_line", "1.0", "end")
        self.gutter.tag_remove("cur", "1.0", "end")
        line = self.text.index("insert").split(".")[0]
        self.text.tag_add("current_line", f"{line}.0", f"{line}.end+1c")
        self.gutter.tag_add("cur", f"{line}.0", f"{line}.end")


class EditorTabs(tk.Frame):
    """Tab strip for the open source files; the device mirror is a fixed pane."""

    def __init__(
        self,
        parent: tk.Widget,
        register_theme,
        palette: dict[str, str],
        on_activate,
        on_close,
    ):
        super().__init__(parent)
        register_theme(self, bg="tab_inactive_bg")
        self._apply_theme = register_theme
        self._palette = palette
        self._on_activate = on_activate
        self._on_close = on_close
        self._tabs: dict[str, dict] = {}
        self._active: str | None = None

    def add(self, key: str, title: str, closable: bool = False) -> None:
        if key in self._tabs:
            return
        tab = tk.Frame(self)
        tab.pack(side="left", fill="y")
        self._apply_theme(tab, bg="tab_inactive_bg")
        accent = tk.Frame(tab, height=2)
        accent.pack(side="bottom", fill="x")
        self._apply_theme(accent, bg="tab_inactive_bg")
        row = tk.Frame(tab)
        row.pack(side="top", fill="both", expand=True)
        self._apply_theme(row, bg="tab_inactive_bg")

        label = tk.Label(row, text=title, padx=12, pady=7, cursor="hand2", takefocus=0)
        self._apply_theme(label, bg="tab_inactive_bg", fg="fg_muted")
        label.pack(side="left", fill="both", expand=True)
        label.bind("<Button-1>", lambda _e, k=key: self._on_activate(k))

        close = None
        if closable:
            close = tk.Label(row, text="×", padx=6, pady=7, cursor="hand2", takefocus=0)
            self._apply_theme(close, bg="tab_inactive_bg", fg="fg_muted")
            close.pack(side="right", fill="y")
            close.bind("<Button-1>", lambda _e, k=key: self._on_close(k))

        self._tabs[key] = {
            "tab": tab, "row": row, "accent": accent, "label": label,
            "close": close, "title": title, "dirty": False,
        }
        self._render(key)

    def remove(self, key: str) -> None:
        info = self._tabs.pop(key, None)
        if info is None:
            return
        info["tab"].destroy()
        if self._active == key:
            self._active = None

    def set_active(self, key: str | None) -> None:
        self._active = key
        for tab_key in self._tabs:
            self._render(tab_key)

    def set_dirty(self, key: str, dirty: bool) -> None:
        info = self._tabs.get(key)
        if info is None or info["dirty"] == dirty:
            return
        info["dirty"] = dirty
        self._render(key)

    def apply_palette(self, palette: dict[str, str]) -> None:
        self._palette = palette
        for tab_key in self._tabs:
            self._render(tab_key)

    def has(self, key: str) -> bool:
        return key in self._tabs

    # -- internals -----------------------------------------------------------
    def _render(self, key: str) -> None:
        info = self._tabs[key]
        active = key == self._active
        bg = self._palette["tab_active_bg"] if active else self._palette["tab_inactive_bg"]
        fg = self._palette["fg"] if active else self._palette["fg_muted"]
        info["tab"].configure(bg=bg)
        info["row"].configure(bg=bg)
        info["label"].configure(bg=bg, fg=fg)
        info["accent"].configure(bg=self._palette["accent"] if active else bg)
        if info["close"] is not None:
            info["close"].configure(bg=bg, fg=fg)
        info["label"].configure(text=("● " if info["dirty"] else "") + info["title"])
