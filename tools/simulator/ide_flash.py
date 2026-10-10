"""Device discovery and flashing helpers for the desktop IDE.

The IDE normally renders the simulator only; this module also lets it find the
serial ports attached to the computer and hand the current application to a real
AI Passport over USB. It stays free of Tkinter and side effects so the host test
suite covers port parsing, ESP-IDF discovery and command construction without a
display.

Flashing needs a POSIX shell (macOS/Linux) and an ESP-IDF 5.5.3 checkout: the
generated commands source that checkout's ``export.sh`` and then run ``idf.py``
(segmented development flash) or ``tools/validate.sh`` plus ``esptool`` (verified
merged image from ``0x0``).
"""

from __future__ import annotations

import glob as _glob
import os
import shlex
import shutil
import sys
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

# Chip the repository targets.
CHIP = "esp32c3"
# Expected ESP-IDF series, matching the repository baseline.
IDF_VERSION = "5.5.3"
# Baud for the merged-image write; segmented ``idf.py flash`` picks its own.
FLASH_BAUD = 460800
# Verified merged image the firmware gate leaves behind, written from offset 0.
MERGED_IMAGE = "build/FoloToy-AI-Passport-full.bin"
MERGED_IMAGE_OFFSET = "0x0"

MODE_INCREMENTAL = "incremental"
MODE_FULL = "full"

# Espressif's USB vendor ID (native USB-Serial/JTAG), plus the USB-UART bridge
# chips that development boards commonly carry.
ESPRESSIF_VID = 0x303A
BRIDGE_VIDS = frozenset({0x10C4, 0x1A86, 0x0403, 0x067B})
# Name fragments that identify a real USB serial node in the glob fallback,
# which also filters out Bluetooth pseudo-ports (``cu.Bluetooth-...``).
_USB_NODE_HINTS = ("usbserial", "usbmodem", "wchusbserial", "slab_usbtoquart")


@dataclass(frozen=True)
class SerialPort:
    """A serial device the computer currently exposes."""

    device: str
    description: str = ""
    hwid: str = ""
    likely_esp: bool = False

    def summary(self) -> str:
        return f"{self.device} · {self.description}" if self.description else self.device


def list_serial_ports() -> list[SerialPort]:
    """Serial devices currently attached, ESP32-likely ones first.

    pyserial's enumeration is preferred because it also reports the USB vendor
    and product IDs, so a device can be recognised as an ESP32. When pyserial is
    not importable, the platform's device nodes are scanned instead; that path
    cannot see vendor IDs, so it keeps only names that look like a USB serial
    adapter.
    """
    ports = _pyserial_ports()
    if ports is None:
        ports = _device_node_ports()
    return _dedupe_sorted(ports)


def _pyserial_ports() -> list[SerialPort] | None:
    """Ports from pyserial, or ``None`` when pyserial is unavailable."""
    try:
        from serial.tools import list_ports  # type: ignore[import-not-found]
    except Exception:  # noqa: BLE001 - any import failure falls back to globbing
        return None
    return _ports_from_infos(list_ports.comports())


def _ports_from_infos(infos: Iterable) -> list[SerialPort]:
    """Map pyserial's ``ListPortInfo`` objects onto ``SerialPort``."""
    ports: list[SerialPort] = []
    for info in infos:
        vid = getattr(info, "vid", None)
        description = (getattr(info, "description", "") or "").strip()
        manufacturer = (getattr(info, "manufacturer", "") or "").strip()
        text = f"{description} {manufacturer}".lower()
        likely = (
            vid == ESPRESSIF_VID
            or vid in BRIDGE_VIDS
            or "espressif" in text
            or "jtag" in text
        )
        ports.append(
            SerialPort(
                device=str(getattr(info, "device", "")),
                description=description,
                hwid=(getattr(info, "hwid", "") or "").strip(),
                likely_esp=bool(likely),
            )
        )
    return ports


def _device_node_ports() -> list[SerialPort]:
    """Scan the platform's serial device nodes when pyserial is missing."""
    if sys.platform == "darwin":
        paths = [p for p in _glob.glob("/dev/cu.*") if _looks_like_usb_serial(p)]
    else:
        paths = [p for pattern in ("/dev/ttyUSB*", "/dev/ttyACM*") for p in _glob.glob(pattern)]
    # A USB serial adapter seen without vendor IDs is the most likely target on
    # a machine that is set up for this board, so it is marked as a candidate.
    return [SerialPort(device=path, likely_esp=True) for path in paths]


def _looks_like_usb_serial(path: str) -> bool:
    name = os.path.basename(path).lower()
    return any(hint in name for hint in _USB_NODE_HINTS)


def _dedupe_sorted(ports: Iterable[SerialPort]) -> list[SerialPort]:
    """Drop duplicate devices and order ESP32-likely devices first."""
    unique: dict[str, SerialPort] = {}
    for port in ports:
        if port.device:
            unique.setdefault(port.device, port)
    return sorted(unique.values(), key=lambda port: (not port.likely_esp, port.device))


def find_idf_root(env: dict[str, str] | None = None) -> Path | None:
    """Locate an ESP-IDF 5.5.3 checkout, or ``None`` when none is found.

    ``AI_PASSPORT_IDF_ROOT`` and ``IDF_PATH`` win, then the usual install
    locations. Reproduction of the exact checkout matters for firmware, so an
    ordinary activated shell (``IDF_PATH``) is honoured before the fallbacks.
    """
    values = os.environ if env is None else env
    candidates: list[Path] = []
    for name in ("AI_PASSPORT_IDF_ROOT", "IDF_PATH"):
        value = values.get(name)
        if value:
            candidates.append(Path(value).expanduser())
    home = Path.home()
    candidates += [
        home / "esp" / f"esp-idf-v{IDF_VERSION}",
        home / "esp" / "esp-idf",
        home / "esp-idf",
        Path("/opt/esp-idf"),
    ]
    for candidate in candidates:
        if _looks_like_idf(candidate):
            return candidate
    return None


def _looks_like_idf(path: Path) -> bool:
    return (path / "export.sh").is_file() and (path / "tools" / "idf.py").is_file()


@dataclass(frozen=True)
class FlashJob:
    """A prepared flashing command for the IDE's worker thread."""

    mode: str
    argv: list[str]
    description: str


def build_flash_job(
    mode: str, idf_root: Path, port: str, project_dir: Path
) -> FlashJob:
    """Build the shell command that flashes ``project_dir`` to ``port``.

    ``mode`` is ``MODE_INCREMENTAL`` (build + segmented ``idf.py flash``, which
    keeps the device's NVS) or ``MODE_FULL`` (verified merged image written from
    ``0x0``, which can reset NVS).
    """
    root = Path(idf_root)
    project = Path(project_dir)
    shell = shutil.which("bash") or "/bin/bash"
    description = (
        "增量编译并烧录" if mode == MODE_INCREMENTAL else "完整镜像烧录（0x0）"
    )
    return FlashJob(mode=mode, argv=[shell, "-c", _flash_script(mode, root, port, project)],
                    description=description)


def _flash_script(mode: str, idf_root: Path, port: str, project_dir: Path) -> str:
    lines = [
        "set -e",
        f"cd {shlex.quote(str(project_dir))}",
        f". {shlex.quote(str(idf_root / 'export.sh'))} >/dev/null",
    ]
    if mode == MODE_INCREMENTAL:
        lines.append(f"idf.py -p {shlex.quote(port)} flash")
    elif mode == MODE_FULL:
        lines.append("./tools/validate.sh --firmware")
        lines.append(
            f"python -m esptool --chip {CHIP} -p {shlex.quote(port)} "
            f"-b {FLASH_BAUD} write_flash {MERGED_IMAGE_OFFSET} {MERGED_IMAGE}"
        )
    else:
        raise ValueError(f"unknown flash mode: {mode!r}")
    return "\n".join(lines)
