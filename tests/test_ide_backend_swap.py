#!/usr/bin/env python3
"""Host tests for the IDE's in-place library swap helper (no Tk, no LVGL).

`stage_library()` prepares a freshly built shared library for the running IDE to
load. The process cannot reload a library from a path it has already mapped, so
every hot swap loads a private copy under a name this process has not used yet.
Covering the naming and the copy keeps the swap deterministic; driving a swap
for real needs the simulator library and a real window.

`validate_library()` boots a freshly built library in a throwaway process before
the IDE loads it, so code that crashes on start is reported instead of taking
the IDE down. These tests cover its message handling and failure paths.
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SIMULATOR_DIR = ROOT / "tools" / "simulator"
sys.path.insert(0, str(SIMULATOR_DIR))
SPEC = importlib.util.spec_from_file_location("backend", SIMULATOR_DIR / "backend.py")
assert SPEC and SPEC.loader
BACKEND = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BACKEND
SPEC.loader.exec_module(BACKEND)


class StageLibraryTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="ai-passport-swap-")
        self.addCleanup(temporary.cleanup)
        self.library = Path(temporary.name).resolve() / "libpassport_sim.dylib"
        self.library.write_bytes(b"first")

    def test_stages_a_copy_under_a_unique_name(self) -> None:
        staged = BACKEND.stage_library(self.library, "123.0")
        self.assertEqual(staged.name, "libpassport_sim.123.0.dylib")
        self.assertEqual(staged.parent, self.library.parent)
        self.assertEqual(staged.read_bytes(), b"first")
        self.assertTrue(self.library.exists(), "the built library must stay in place")

    def test_each_token_stages_its_own_file(self) -> None:
        first = BACKEND.stage_library(self.library, "123.0")
        second = BACKEND.stage_library(self.library, "123.1")
        self.assertNotEqual(first, second)
        self.assertTrue(first.exists())
        self.assertTrue(second.exists())

    def test_stages_the_content_as_of_the_call(self) -> None:
        staged = BACKEND.stage_library(self.library, "1.0")
        self.library.write_bytes(b"rebuilt")
        self.assertEqual(staged.read_bytes(), b"first")
        self.assertEqual(BACKEND.stage_library(self.library, "1.1").read_bytes(), b"rebuilt")

    def test_missing_library_raises(self) -> None:
        with self.assertRaises(OSError):
            BACKEND.stage_library(self.library.with_name("nope.dylib"), "1.0")


class DescribeExitTest(unittest.TestCase):
    """A crash in the candidate library reaches the IDE as a negative exit."""

    def test_signal_reports_the_signal_name(self) -> None:
        reason = BACKEND.describe_exit(-11, "Segmentation fault: 11")
        self.assertIn("SIGSEGV", reason)
        self.assertIn("Segmentation fault: 11", reason)

    def test_plain_failure_reports_the_exit_code(self) -> None:
        reason = BACKEND.describe_exit(1, "Traceback (most recent call last):")
        self.assertIn("退出码 1", reason)
        self.assertIn("Traceback", reason)

    def test_silent_failure_has_no_trailing_blank(self) -> None:
        self.assertEqual(BACKEND.describe_exit(-6, "\n  \n"), BACKEND.describe_exit(-6, ""))

    def test_output_is_trimmed_to_the_last_lines(self) -> None:
        output = "\n".join(f"line {n}" for n in range(20))
        reason = BACKEND.describe_exit(1, output)
        self.assertIn("line 19", reason)
        self.assertNotIn("line 0", reason)


class ValidateLibraryTest(unittest.TestCase):
    """validate_library() boots the candidate out-of-process before it is used."""

    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="ai-passport-validate-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def test_missing_library_is_reported(self) -> None:
        reason = BACKEND.validate_library(self.root / "nope.dylib")
        self.assertIsNotNone(reason)
        self.assertIn("未找到仿真库", reason)

    def test_unloadable_file_is_reported(self) -> None:
        # Not a shared library: the child fails to load it and exits non-zero.
        bogus = self.root / "libpassport_sim.dylib"
        bogus.write_text("not a mach-o file\n", encoding="utf-8")
        reason = BACKEND.validate_library(bogus)
        self.assertIsNotNone(reason)
        self.assertIn("退出码", reason)


if __name__ == "__main__":
    unittest.main(verbosity=2)
