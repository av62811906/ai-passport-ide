#!/usr/bin/env python3
"""Host tests for the IDE's flashing helpers (no Tk, no ESP-IDF, no hardware).

Covers what the flash page depends on but cannot exercise on a build machine:
serial-port discovery (both the pyserial path and the device-node fallback),
ESP-IDF checkout discovery, and the shell command each flash mode produces. The
real write to a board still needs a device.
"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ide_flash", ROOT / "tools" / "simulator" / "ide_flash.py"
)
assert SPEC and SPEC.loader
FLASH = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = FLASH
SPEC.loader.exec_module(FLASH)


def _info(device, description="", manufacturer="", vid=None, hwid=""):
    return SimpleNamespace(
        device=device, description=description, manufacturer=manufacturer, vid=vid, hwid=hwid
    )


class PortMappingTest(unittest.TestCase):
    def test_espressif_and_bridge_vids_are_flagged(self):
        ports = FLASH._ports_from_infos([
            _info("/dev/a", vid=FLASH.ESPRESSIF_VID, description="USB JTAG/serial debug unit"),
            _info("/dev/b", vid=0x10C4, description="CP2102 USB to UART Bridge"),
            _info("/dev/c", vid=0x1234, description="Some device"),
        ])
        self.assertTrue(ports[0].likely_esp)
        self.assertTrue(ports[1].likely_esp)
        self.assertFalse(ports[2].likely_esp)

    def test_espressif_or_jtag_text_flags_a_port_without_vid(self):
        ports = FLASH._ports_from_infos([
            _info("/dev/d", manufacturer="Espressif"),
            _info("/dev/e", description="USB JTAG/serial"),
        ])
        self.assertTrue(all(port.likely_esp for port in ports))

    def test_summary_appends_the_description_when_present(self):
        self.assertEqual(FLASH.SerialPort("/dev/x").summary(), "/dev/x")
        self.assertEqual(FLASH.SerialPort("/dev/x", "Bridge").summary(), "/dev/x · Bridge")


class DedupeSortTest(unittest.TestCase):
    def test_esp_likely_first_and_duplicates_dropped(self):
        ports = [
            FLASH.SerialPort("/dev/z"),
            FLASH.SerialPort("/dev/a", likely_esp=True),
            FLASH.SerialPort("/dev/a", "second sighting", likely_esp=True),
        ]
        result = FLASH._dedupe_sorted(ports)
        self.assertEqual([port.device for port in result], ["/dev/a", "/dev/z"])
        self.assertEqual(result[0].description, "")

    def test_empty_device_is_ignored(self):
        self.assertEqual(FLASH._dedupe_sorted([FLASH.SerialPort("")]), [])


class DeviceNodeFallbackTest(unittest.TestCase):
    def test_darwin_keeps_only_usb_serial_names(self):
        names = [
            "/dev/cu.usbmodem2101",
            "/dev/cu.Bluetooth-Incoming-Port",
            "/dev/cu.debug-console",
        ]
        with mock.patch.object(FLASH.sys, "platform", "darwin"), \
                mock.patch.object(FLASH._glob, "glob", lambda pattern: list(names)):
            ports = FLASH._device_node_ports()
        self.assertEqual([port.device for port in ports], ["/dev/cu.usbmodem2101"])
        self.assertTrue(ports[0].likely_esp)

    def test_linux_scans_tty_usb_and_acm(self):
        def fake_glob(pattern):
            return {
                "/dev/ttyUSB*": ["/dev/ttyUSB0"],
                "/dev/ttyACM*": ["/dev/ttyACM1"],
            }.get(pattern, [])

        with mock.patch.object(FLASH.sys, "platform", "linux"), \
                mock.patch.object(FLASH._glob, "glob", fake_glob):
            ports = FLASH._device_node_ports()
        self.assertEqual(sorted(port.device for port in ports), ["/dev/ttyACM1", "/dev/ttyUSB0"])


class ListSerialPortsTest(unittest.TestCase):
    def test_prefers_pyserial_when_available(self):
        with mock.patch.object(FLASH, "_pyserial_ports", return_value=[FLASH.SerialPort("/dev/ps")]), \
                mock.patch.object(FLASH, "_device_node_ports", return_value=[FLASH.SerialPort("/dev/node")]):
            self.assertEqual([p.device for p in FLASH.list_serial_ports()], ["/dev/ps"])

    def test_falls_back_to_device_nodes_when_pyserial_is_missing(self):
        fallback = [FLASH.SerialPort("/dev/node", likely_esp=True)]
        with mock.patch.object(FLASH, "_pyserial_ports", return_value=None), \
                mock.patch.object(FLASH, "_device_node_ports", return_value=fallback):
            self.assertEqual([p.device for p in FLASH.list_serial_ports()], ["/dev/node"])


class FindIdfRootTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="ai-passport-idf-")
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name).resolve()
        self.home = self.base / "home"
        self.home.mkdir()

    def make_idf(self, root: Path) -> Path:
        (root / "tools").mkdir(parents=True)
        (root / "export.sh").write_text("#!/bin/sh\n", encoding="utf-8")
        (root / "tools" / "idf.py").write_text("# idf.py\n", encoding="utf-8")
        return root

    def test_project_variable_wins_over_the_activated_shell(self):
        custom = self.make_idf(self.base / "custom-idf")
        other = self.make_idf(self.base / "other-idf")
        with mock.patch.object(Path, "home", return_value=self.home):
            found = FLASH.find_idf_root(
                {"AI_PASSPORT_IDF_ROOT": str(custom), "IDF_PATH": str(other)}
            )
        self.assertEqual(found, custom)

    def test_idf_path_is_used_when_the_project_variable_is_absent(self):
        root = self.make_idf(self.base / "idfenv")
        with mock.patch.object(Path, "home", return_value=self.home):
            self.assertEqual(FLASH.find_idf_root({"IDF_PATH": str(root)}), root)

    def test_home_fallback_uses_the_pinned_version_location(self):
        root = self.home / "esp" / f"esp-idf-v{FLASH.IDF_VERSION}"
        self.make_idf(root)
        with mock.patch.object(Path, "home", return_value=self.home):
            self.assertEqual(FLASH.find_idf_root({}), root)

    def test_directory_without_idf_py_is_not_accepted(self):
        incomplete = self.base / "incomplete"
        (incomplete / "tools").mkdir(parents=True)
        (incomplete / "export.sh").write_text("", encoding="utf-8")
        with mock.patch.object(Path, "home", return_value=self.home):
            self.assertIsNone(FLASH.find_idf_root({"IDF_PATH": str(incomplete)}))


class BuildFlashJobTest(unittest.TestCase):
    def setUp(self) -> None:
        self.project = Path("/tmp/project")
        self.idf = Path("/opt/esp-idf-v5.5.3")

    def test_incremental_job_runs_idf_flash_with_the_port(self):
        job = FLASH.build_flash_job(
            FLASH.MODE_INCREMENTAL, self.idf, "/dev/ttyUSB0", self.project
        )
        script = job.argv[-1]
        self.assertTrue(job.argv[0].endswith("bash"))
        self.assertIn("/opt/esp-idf-v5.5.3/export.sh", script)
        self.assertIn("idf.py -p /dev/ttyUSB0 flash", script)
        self.assertNotIn("validate.sh", script)
        self.assertNotIn("esptool", script)

    def test_full_job_verifies_then_writes_the_merged_image_from_zero(self):
        job = FLASH.build_flash_job(FLASH.MODE_FULL, self.idf, "/dev/ttyUSB0", self.project)
        script = job.argv[-1]
        self.assertIn("./tools/validate.sh --firmware", script)
        self.assertIn("-p /dev/ttyUSB0", script)
        self.assertIn(f"write_flash {FLASH.MERGED_IMAGE_OFFSET} {FLASH.MERGED_IMAGE}", script)

    def test_paths_with_spaces_are_quoted(self):
        job = FLASH.build_flash_job(
            FLASH.MODE_INCREMENTAL, self.idf, "/dev/ttyUSB0", Path("/tmp/my project")
        )
        self.assertIn("cd '/tmp/my project'", job.argv[-1])

    def test_unknown_mode_is_rejected(self):
        with self.assertRaises(ValueError):
            FLASH.build_flash_job("bogus", self.idf, "/dev/x", self.project)

    def test_description_differs_per_mode(self):
        incremental = FLASH.build_flash_job(
            FLASH.MODE_INCREMENTAL, self.idf, "/dev/x", self.project
        )
        full = FLASH.build_flash_job(FLASH.MODE_FULL, self.idf, "/dev/x", self.project)
        self.assertNotEqual(incremental.description, full.description)


if __name__ == "__main__":
    unittest.main()
