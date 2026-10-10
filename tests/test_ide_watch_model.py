#!/usr/bin/env python3
"""Host tests for the IDE's external-change watch model (no Tk, no LVGL).

Covers the decisions behind "an editor or an agent changed the sources outside
the IDE": which files are watched, the stat-based snapshot they are compared
against, and whether the built library is older than the sources. The polling
loop and the rebuild itself need a real window and a compiler, so they stay in
the IDE; this keeps the deterministic parts covered.
"""

from __future__ import annotations

import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SIMULATOR_DIR = ROOT / "tools" / "simulator"
sys.path.insert(0, str(SIMULATOR_DIR))
SPEC = importlib.util.spec_from_file_location("ide_watch", SIMULATOR_DIR / "ide_watch.py")
assert SPEC and SPEC.loader
WATCH = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = WATCH
SPEC.loader.exec_module(WATCH)


class _TreeTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="ai-passport-watch-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def write(self, relative: str, content: str = "x") -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def watched(self) -> list[str]:
        return [
            path.relative_to(self.root).as_posix()
            for path in WATCH.watch_files(self.root)
        ]


class WatchFilesTest(_TreeTest):
    def test_collects_build_inputs_and_ignores_other_files(self) -> None:
        self.write("main/app_home.c")
        self.write("main/app_home.h")
        self.write("main/CMakeLists.txt")
        self.write("main/README.md")
        self.write("components/bsp/include/bsp_display.h")
        self.write("components/bsp/src/bsp_display_rounding.c")
        self.write("assets/fonts/tuner_font_20.c")
        self.write("tools/simulator/host/sim.c")
        self.write("tools/simulator/backend.py")
        self.assertEqual(
            set(self.watched()),
            {
                "main/CMakeLists.txt",
                "main/app_home.c",
                "main/app_home.h",
                "components/bsp/include/bsp_display.h",
                "components/bsp/src/bsp_display_rounding.c",
                "assets/fonts/tuner_font_20.c",
                "tools/simulator/host/sim.c",
            },
        )

    def test_orders_each_directory_by_name(self) -> None:
        self.write("main/b.c")
        self.write("main/a.c")
        self.write("main/C.h")
        self.assertEqual(self.watched(), ["main/a.c", "main/b.c", "main/C.h"])

    def test_missing_directories_yield_nothing(self) -> None:
        self.assertEqual(WATCH.watch_files(self.root), [])


class SnapshotTest(_TreeTest):
    def test_snapshot_skips_missing_paths(self) -> None:
        first = self.write("main/a.c", "one")
        second = self.write("main/b.c", "two")
        state = WATCH.snapshot([first, second, self.root / "main" / "gone.c"])
        self.assertEqual(set(state), {first, second})
        self.assertEqual(state[first][1], len("one"))

    def test_unchanged_tree_reports_no_changes(self) -> None:
        files = [self.write("main/a.c", "one"), self.write("main/b.c", "two")]
        state = WATCH.snapshot(files)
        self.assertEqual(WATCH.changed_paths(state, WATCH.snapshot(files)), [])

    def test_edit_is_reported(self) -> None:
        edited = self.write("main/a.c", "one")
        other = self.write("main/b.c", "two")
        before = WATCH.snapshot([edited, other])
        edited.write_text("one changed", encoding="utf-8")
        self.assertEqual(WATCH.changed_paths(before, WATCH.snapshot([edited, other])), [edited])

    def test_added_and_removed_files_are_reported(self) -> None:
        kept = self.write("main/a.c", "one")
        added = self.write("main/b.c", "two")
        with_added = WATCH.snapshot([kept, added])
        self.assertEqual(
            WATCH.changed_paths(WATCH.snapshot([kept]), with_added), [added]
        )
        added.unlink()
        self.assertEqual(
            WATCH.changed_paths(with_added, WATCH.snapshot([kept, added])), [added]
        )


class StaleTest(_TreeTest):
    def test_newest_mtime_ignores_missing_paths(self) -> None:
        source = self.write("main/a.c")
        os.utime(source, ns=(5, 5))
        self.assertEqual(WATCH.newest_mtime([source, self.root / "nope.c"]), 5)
        self.assertEqual(WATCH.newest_mtime([]), 0)

    def test_missing_library_is_stale(self) -> None:
        source = self.write("main/a.c")
        self.assertTrue(WATCH.is_stale([source], self.root / "build" / "libpassport_sim.so"))

    def test_library_newer_than_sources_is_fresh(self) -> None:
        source = self.write("main/a.c")
        library = self.write("build/libpassport_sim.so")
        os.utime(source, ns=(1_000_000_000, 1_000_000_000))
        os.utime(library, ns=(2_000_000_000, 2_000_000_000))
        self.assertFalse(WATCH.is_stale([source], library))

    def test_source_newer_than_library_is_stale(self) -> None:
        source = self.write("main/a.c")
        library = self.write("build/libpassport_sim.so")
        os.utime(library, ns=(1_000_000_000, 1_000_000_000))
        os.utime(source, ns=(2_000_000_000, 2_000_000_000))
        self.assertTrue(WATCH.is_stale([source], library))

    def test_no_sources_is_never_stale(self) -> None:
        library = self.write("build/libpassport_sim.so")
        self.assertFalse(WATCH.is_stale([], library))


if __name__ == "__main__":
    unittest.main(verbosity=2)
