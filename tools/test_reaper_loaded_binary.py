#!/usr/bin/env python3
"""Bounded receipt regression tests; no REAPER launch or installed plugin writes."""
import json
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from reaper_loaded_binary import LoadedBinaryError, bundle_executable, verify_loaded_binary


class LoadedBinaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spectr-loaded-binary-")
        self.root = Path(self.temp.name)
        self.bundle = self.root / "intended/Spectr.clap"
        self.exe = self.make_bundle(self.bundle, b"intended executable")
        self.receipt = self.root / "receipt.json"
        self.proc = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])

    def tearDown(self):
        if self.proc.poll() is None:
            self.proc.terminate()
        self.proc.wait(timeout=5)
        self.temp.cleanup()

    def make_bundle(self, path, data):
        (path / "Contents/MacOS").mkdir(parents=True)
        (path / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "Spectr"}))
        exe = path / "Contents/MacOS/Spectr"
        exe.write_bytes(data)
        return exe

    def verify(self, paths):
        with patch("reaper_loaded_binary.mapped_paths", return_value=paths):
            return verify_loaded_binary(self.proc, self.bundle, self.receipt, "unit fixture")

    def test_intended_and_identical_staged_copy(self):
        staged = self.root / "portable/Spectr.clap"
        shutil.copytree(self.bundle, staged)
        actual = staged / "Contents/MacOS/Spectr"
        result = self.verify([actual])
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["actual_path"], str(actual.resolve()))
        self.assertEqual(result["actual_sha256"], result["expected_sha256"])
        self.assertFalse(result["offline_render_processes_verified"])

    def test_stale_same_name_user_copy_fails(self):
        stale = self.make_bundle(self.root / "user/Spectr.clap", b"stale executable")
        with self.assertRaisesRegex(LoadedBinaryError, "SHA256 differs"):
            self.verify([stale])
        receipt = json.loads(self.receipt.read_text())
        self.assertEqual(receipt["status"], "FAIL")
        self.assertEqual(receipt["actual_path"], str(stale.resolve()))
        self.assertNotEqual(receipt["actual_sha256"], receipt["expected_sha256"])

    def test_ambiguous_even_if_identical(self):
        staged = self.make_bundle(self.root / "system/Spectr.clap", self.exe.read_bytes())
        with self.assertRaisesRegex(LoadedBinaryError, "found 2"):
            self.verify([self.exe, staged])

    def test_no_module(self):
        with self.assertRaisesRegex(LoadedBinaryError, "found 0"):
            self.verify([Path(sys.executable)])
        self.assertEqual(json.loads(self.receipt.read_text())["status"], "FAIL")

    @unittest.skipUnless(sys.platform == "darwin", "macOS lsof parser")
    def test_lsof_failure_and_wrong_pid_fail_closed(self):
        for result, message in [
            (subprocess.CompletedProcess([], 1, b"", b"denied"), "lsof failed"),
            (subprocess.CompletedProcess([], 0, b"p999999\0n/tmp/Spectr\0", b""), "owned PID"),
        ]:
            with self.subTest(message=message):
                with patch("reaper_loaded_binary.subprocess.run", return_value=result):
                    with self.assertRaisesRegex(LoadedBinaryError, message):
                        verify_loaded_binary(self.proc, self.bundle, self.receipt, "lsof failure")
                self.assertEqual(json.loads(self.receipt.read_text())["status"], "FAIL")

    def test_exited_owned_process(self):
        self.proc.terminate()
        self.proc.wait(timeout=5)
        with self.assertRaisesRegex(LoadedBinaryError, "not live"):
            self.verify([self.exe])

    def test_bundle_executable_cannot_escape(self):
        (self.bundle / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "../escape"}))
        with self.assertRaisesRegex(LoadedBinaryError, "invalid CFBundleExecutable"):
            bundle_executable(self.bundle)

    @unittest.skipUnless(sys.platform == "darwin" and shutil.which("clang"), "requires macOS clang/lsof")
    def test_real_owned_macho_mapping_and_unloaded_process(self):
        # Build and load one test-owned Mach-O module. No user plugins or hosts.
        source = self.root / "fixture.c"
        source.write_text("int spectr_receipt_fixture(void) { return 42; }\n")
        subprocess.run(["clang", "-dynamiclib", str(source), "-o", str(self.exe)],
                       check=True, capture_output=True, timeout=30)
        child = subprocess.Popen(
            [sys.executable, "-c", "import ctypes,sys,time; "
             "m=ctypes.CDLL(sys.argv[1]); print(m.spectr_receipt_fixture(),flush=True); time.sleep(60)",
             str(self.exe)], stdout=subprocess.PIPE, text=True)
        try:
            # select caps readiness: do not wait indefinitely on a broken child.
            import select
            self.assertTrue(select.select([child.stdout], [], [], 10)[0])
            self.assertEqual(child.stdout.readline().strip(), "42")
            result = verify_loaded_binary(child, self.bundle, self.receipt, "real owned ctypes child")
            self.assertEqual(result["status"], "PASS")
            self.assertEqual(result["pid"], child.pid)
            self.assertEqual(result["actual_path"], str(self.exe.resolve()))
            with self.assertRaisesRegex(LoadedBinaryError, "found 0"):
                verify_loaded_binary(self.proc, self.bundle, self.receipt, "unloaded owned child")
        finally:
            if child.poll() is None:
                child.terminate()
            child.wait(timeout=5)
            child.stdout.close()


if __name__ == "__main__":
    unittest.main()
