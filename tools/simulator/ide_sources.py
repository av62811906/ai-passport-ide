"""Source-model helpers for the desktop IDE's explorer and code editor.

Pure logic with no Tkinter and no side effects beyond listing a directory:
which files the explorer shows, whether a path may be written, the newline
style of a file, and a regex tokenizer that drives C syntax highlighting.
Keeping this separate from the widgets lets the host test suite cover it
without a display.
"""

from __future__ import annotations

import re
from pathlib import Path

# Files the explorer shows: the application component that is copied into the
# firmware. The directory is flat today, so no recursion is needed.
SOURCE_SUFFIXES = (".c", ".h")
EXTRA_SOURCE_NAMES = ("CMakeLists.txt",)

# C keywords plus the fixed-width types and qualifiers this codebase uses.
C_KEYWORDS = (
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline",
    "int", "long", "register", "restrict", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
    "void", "volatile", "while",
    "_Bool", "bool", "true", "false", "NULL",
    "size_t", "ssize_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "esp_err_t", "TickType_t", "esp_timer_handle_t",
)

# Alternation order defines precedence: comments and literals first, so their
# contents are never re-tokenized, then directives, numbers, keywords, calls.
_TOKEN_RE = re.compile(
    r"""
      (?P<cmt>//[^\n]*|/\*(?:.*?\*/|.*))
    | (?P<str>"(?:\\.|[^"\\\n])*"?)
    | (?P<chr>'(?:\\.|[^'\\\n])*'?)
    | (?P<pp>^[ \t]*\#[^\n]*)
    | (?P<num>\b(?:0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][+-]?\d+)?)[uUlLfF]*\b)
    | (?P<kw>\b(?:%s)\b)
    | (?P<fn>[A-Za-z_]\w*(?=\s*\())
    """
    % "|".join(sorted(C_KEYWORDS, key=len, reverse=True)),
    re.MULTILINE | re.DOTALL | re.VERBOSE,
)

TOKEN_KINDS = ("cmt", "str", "chr", "pp", "num", "kw", "fn")


def list_sources(root: Path) -> list[Path]:
    """Source files directly inside ``root``, ordered by name (case-insensitive)."""
    if not root.is_dir():
        return []
    entries = [
        path
        for path in root.iterdir()
        if path.is_file() and (path.suffix in SOURCE_SUFFIXES or path.name in EXTRA_SOURCE_NAMES)
    ]
    return sorted(entries, key=lambda path: path.name.lower())


def is_within(target: Path, root: Path) -> bool:
    """True when ``target`` resolves to ``root`` or a path inside it.

    Guards writes: a ``..`` escape, an absolute path elsewhere, or a symlink
    pointing outside ``root`` all resolve away from the root and are rejected.
    """
    try:
        resolved = target.resolve()
        base = root.resolve()
    except OSError:
        return False
    return resolved == base or base in resolved.parents


def newline_style(text: str) -> str:
    """Return the newline sequence to write back (CRLF only if already used)."""
    return "\r\n" if "\r\n" in text else "\n"


def normalize_newlines(text: str) -> str:
    """Collapse CRLF/CR to LF, matching how tk.Text stores line breaks."""
    return text.replace("\r\n", "\n").replace("\r", "\n")


def tokenize_c(text: str) -> list[tuple[int, int, str]]:
    """C syntax spans as ``(start, end, kind)`` with kinds from TOKEN_KINDS.

    Spans are ordered by position and never overlap: comments and literals are
    matched first and consumed whole, so their contents produce no further
    spans.
    """
    spans: list[tuple[int, int, str]] = []
    for match in _TOKEN_RE.finditer(text):
        for kind in TOKEN_KINDS:
            if match.group(kind) is not None:
                spans.append((match.start(kind), match.end(kind), kind))
                break
    return spans
