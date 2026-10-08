#!/usr/bin/env python3
"""Host tests for the desktop IDE's auto-fit layout math (no Tk, no LVGL).

`fit_scale()` decides how the 240x320 device mirror is scaled to whatever the
editor area currently is, so a wrong result shows up as a clipped, stretched or
oversized preview on screen. Covering it here keeps the window-size -> zoom
mapping deterministic; the visual result still needs a real window.
"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from math import gcd
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ide_layout", ROOT / "tools" / "simulator" / "ide_layout.py"
)
assert SPEC and SPEC.loader
LAYOUT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = LAYOUT
SPEC.loader.exec_module(LAYOUT)


class FitScaleTest(unittest.TestCase):
    def assert_well_formed(self, result: tuple[int, int]) -> float:
        num, den = result
        self.assertIsInstance(num, int)
        self.assertIsInstance(den, int)
        self.assertGreaterEqual(num, 1)
        self.assertGreaterEqual(den, 1)
        self.assertEqual(gcd(num, den), 1, "fraction should be reduced")
        return num / den

    def test_height_limited_window_is_close_to_the_ideal_fit(self) -> None:
        avail_w, avail_h, padding = 1000, 700, 16
        ideal = min((avail_w - 2 * padding) / LAYOUT.SRC_WIDTH,
                    (avail_h - 2 * padding) / LAYOUT.SRC_HEIGHT)  # = 2.0875
        scale = self.assert_well_formed(LAYOUT.fit_scale(avail_w, avail_h, padding=padding))
        self.assertLessEqual(abs(scale - ideal), 1 / (2 * 8) + 1e-9)
        # The reserved padding leaves enough slack for the approximation.
        self.assertLessEqual(LAYOUT.SRC_WIDTH * scale, avail_w)
        self.assertLessEqual(LAYOUT.SRC_HEIGHT * scale, avail_h)

    def test_width_limited_window_keeps_the_aspect_ratio(self) -> None:
        for avail in ((300, 2000), (2000, 400), (300, 400)):
            with self.subTest(avail=avail):
                avail_w, avail_h = avail
                scale = self.assert_well_formed(LAYOUT.fit_scale(avail_w, avail_h))
                ideal = min(avail_w / LAYOUT.SRC_WIDTH, avail_h / LAYOUT.SRC_HEIGHT)
                self.assertLessEqual(abs(scale - ideal), 1 / (2 * 8) + 1e-9)
                self.assertLessEqual(LAYOUT.SRC_WIDTH * scale, avail_w)
                self.assertLessEqual(LAYOUT.SRC_HEIGHT * scale, avail_h)

    def test_small_window_scales_down_instead_of_clipping(self) -> None:
        scale = self.assert_well_formed(LAYOUT.fit_scale(200, 260))
        self.assertLess(scale, 1.0)
        self.assertLessEqual(LAYOUT.SRC_WIDTH * scale, 200)
        self.assertLessEqual(LAYOUT.SRC_HEIGHT * scale, 260)

    def test_tiny_window_is_clamped_to_the_minimum_scale(self) -> None:
        self.assertEqual(LAYOUT.fit_scale(10, 10, min_scale=0.25, max_den=8), (1, 4))

    def test_huge_window_is_clamped_to_the_maximum_scale(self) -> None:
        self.assertEqual(LAYOUT.fit_scale(100000, 100000, max_scale=8.0), (8, 1))

    def test_degenerate_inputs_fall_back_to_one_to_one(self) -> None:
        for args in ((0, 0), (-5, 100), (100, 0), (30, 30)):
            with self.subTest(args=args):
                kwargs = {}
                if args == (30, 30):
                    kwargs = {"padding": 15}  # exactly consumes the area
                self.assertEqual(LAYOUT.fit_scale(*args, **kwargs), (1, 1))
        self.assertEqual(LAYOUT.fit_scale(800, 600, src_w=0), (1, 1))
        self.assertEqual(LAYOUT.fit_scale(800, 600, src_h=-1), (1, 1))

    def test_no_padding_matches_full_area(self) -> None:
        self.assertEqual(LAYOUT.fit_scale(480, 640), (2, 1))

    def test_zoom_factor_matches_the_fraction(self) -> None:
        for num, den in ((1, 1), (2, 1), (1, 4), (17, 8)):
            self.assertAlmostEqual(LAYOUT.zoom_factor(num, den), num / den)
        self.assertEqual(LAYOUT.zoom_factor(1, 0), 1.0)


if __name__ == "__main__":
    unittest.main()
