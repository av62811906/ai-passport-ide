"""Layout math for the AI Passport desktop IDE.

Pure, dependency-free helpers that turn the available editor area into an exact
integer zoom/subsample pair, so the 240x320 device mirror can be scaled to any
window size (including full screen) while keeping its aspect ratio. Keeping this
separate from the Tkinter code lets the host test suite cover it without a
display.
"""

from __future__ import annotations

from math import gcd

# Native resolution of the device panel.
SRC_WIDTH = 240
SRC_HEIGHT = 320


def zoom_factor(num: int, den: int) -> float:
    """Return the effective scale of a (zoom numerator, subsample denominator)."""
    if den <= 0:
        return 1.0
    return num / den


def fit_scale(
    avail_w: int,
    avail_h: int,
    src_w: int = SRC_WIDTH,
    src_h: int = SRC_HEIGHT,
    padding: int = 0,
    min_scale: float = 0.25,
    max_scale: float = 8.0,
    max_den: int = 8,
) -> tuple[int, int]:
    """Best integer (zoom, subsample) pair fitting a source into an area.

    ``avail_w``/``avail_h`` describe the space the mirror may occupy (the editor
    area), ``padding`` is reserved on every side. The resulting ``num/den`` is
    the closest rational approximation of the ideal fit scale with a denominator
    of at most ``max_den``, clamped to ``[min_scale, max_scale]`` so a tiny or
    huge window cannot produce a degenerate image.

    Degenerate inputs (non-positive source or available space) return ``(1, 1)``.
    """
    if src_w <= 0 or src_h <= 0:
        return (1, 1)

    usable_w = avail_w - 2 * padding
    usable_h = avail_h - 2 * padding
    if usable_w <= 0 or usable_h <= 0:
        return (1, 1)

    target = min(usable_w / src_w, usable_h / src_h)
    target = max(min_scale, min(max_scale, target))

    best = (1, 1)
    best_error = abs(target - 1.0)
    for den in range(1, max(1, max_den) + 1):
        num = max(1, round(target * den))
        error = abs(num / den - target)
        if error < best_error:
            best = (num, den)
            best_error = error

    num, den = best
    common = gcd(num, den)
    return (num // common, den // common)
