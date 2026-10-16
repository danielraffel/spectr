#!/usr/bin/env python3
"""Release admission must reject stale or modified Windows packages."""

import hashlib
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools.ship.check_windows_release import check_package


SHA = "a" * 40
ARCH = "arm64ec-win"
INFO = f"VST3/Spectr.vst3/Contents/{ARCH}/Spectr.pulp-build-info.json"


def make_package(path: Path, *, source=SHA, tamper=False, arch=ARCH):
    info = {
        "schema": "pulp.build-info.v1",
        "product": {"name": "Spectr", "version": "1.0.9",
                    "source_git_sha": source, "source_git_dirty": False},
        "build": {"type": "Release", "archs": [arch]},
    }
    data = json.dumps(info).encode()
    manifest = {
        "schema": 1,
        "architecture": arch,
        "artifact_architectures": {"standalone": arch, "vst3": arch,
                                   "clap": arch, "runtime": "arm64-win"},
        "files": [{"path": INFO, "bytes": len(data),
                   "sha256": hashlib.sha256(data).hexdigest()}],
    }
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr(INFO, data + (b"tampered" if tamper else b""))
        archive.writestr("manifest.json", json.dumps(manifest))


class WindowsReleaseTest(unittest.TestCase):
    def test_matching_package(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "arm.zip"
            make_package(path)
            self.assertEqual(check_package(path, ARCH, "1.0.9", SHA),
                             hashlib.sha256(path.read_bytes()).hexdigest())

    def test_stale_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "arm.zip"
            make_package(path, source="b" * 40)
            with self.assertRaisesRegex(ValueError, "source does not match"):
                check_package(path, ARCH, "1.0.9", SHA)

    def test_mutated_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "arm.zip"
            make_package(path, tamper=True)
            with self.assertRaisesRegex(ValueError, "hash/size mismatch"):
                check_package(path, ARCH, "1.0.9", SHA)

    def test_wrong_architecture_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "arm.zip"
            make_package(path)
            with self.assertRaisesRegex(ValueError, "wrong package schema or architecture"):
                check_package(path, "x86_64-win", "1.0.9", SHA)


if __name__ == "__main__":
    unittest.main()
