"""External-change detection for the desktop IDE's application sources.

Pure logic with no Tkinter and no LVGL: which files the simulator build reads, a
cheap stat-based snapshot of them, and the difference between two snapshots.
The IDE polls this from its event loop so that edits made by another editor (or
an agent) are noticed and the simulator library is rebuilt from them.

Snapshots record ``(mtime_ns, size)`` rather than content hashes: the poll runs
a few times per second over a few dozen small files, and a timestamp or size
change is enough to decide that a rebuild is worth attempting.

Keeping this separate from the widgets lets the host test suite cover it without
a display.
"""

from __future__ import annotations

from collections.abc import Iterable
from pathlib import Path

from ide_sources import list_sources

# Directories, relative to the repository root, that hold the sources the
# simulator build consumes: the application component plus the PokeWalk game,
# the BSP pieces, font and host build files it compiles. They are flat, like the
# firmware's component directory, so the listing does not recurse.
WATCH_DIRS = (
    "main",
    "main/pokewalk",
    "components/bsp/include",
    "components/bsp/src",
    "assets/fonts",
    "tools/simulator/host",
)


def watch_files(repo_root: Path) -> list[Path]:
    """Source files the simulator build reads, ordered by directory then name."""
    files: list[Path] = []
    for name in WATCH_DIRS:
        files.extend(list_sources(Path(repo_root) / name))
    return files


def snapshot(paths: Iterable[Path]) -> dict[Path, tuple[int, int]]:
    """``path -> (mtime_ns, size)`` for the paths that exist right now.

    A file that cannot be stat'ed (deleted meanwhile, no permission) is left out,
    which makes its disappearance show up as a change on the next comparison.
    """
    state: dict[Path, tuple[int, int]] = {}
    for path in paths:
        try:
            info = path.stat()
        except OSError:
            continue
        state[path] = (info.st_mtime_ns, info.st_size)
    return state


def changed_paths(
    old: dict[Path, tuple[int, int]], new: dict[Path, tuple[int, int]]
) -> list[Path]:
    """Paths added, removed or modified between two snapshots, ordered by name."""
    changed = [path for path, state in new.items() if old.get(path) != state]
    changed.extend(path for path in old if path not in new)
    return sorted(changed, key=str)


def newest_mtime(paths: Iterable[Path]) -> int:
    """Newest ``mtime_ns`` among the paths that exist; 0 when there are none."""
    newest = 0
    for path in paths:
        try:
            newest = max(newest, path.stat().st_mtime_ns)
        except OSError:
            continue
    return newest


def is_stale(sources: Iterable[Path], library: Path) -> bool:
    """True when ``library`` is missing or older than any of ``sources``."""
    try:
        library_mtime = library.stat().st_mtime_ns
    except OSError:
        return True
    return newest_mtime(sources) > library_mtime
