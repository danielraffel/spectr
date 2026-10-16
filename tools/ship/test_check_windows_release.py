#!/usr/bin/env python3
"""Release admission must reject stale or modified Windows packages."""

import hashlib
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools.ship.check_windows_release import check_package, check_pe


SHA = "a" * 40
ARCH = "arm64ec-win"
INFO = f"VST3/Spectr.vst3/Contents/{ARCH}/Spectr.pulp-build-info.json"


def pe(machine: int, arm64x=False) -> bytes:
    data = bytearray(0x100)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, machine, 1 if arm64x else 0)
    if arm64x:
        data[0x98:0xA0] = b".a64xrm\0"
    return bytes(data)


def make_package(path: Path, *, source=SHA, tamper=False, arch=ARCH):
    info = {
        "schema": "pulp.build-info.v1",
        "product": {"name": "Spectr", "version": "1.0.9",
                    "source_git_sha": source, "source_git_dirty": False},
        "build": {"type": "Release", "archs": [arch]},
    }
    data = json.dumps(info).encode()
    vdir = f"VST3/Spectr.vst3/Contents/{arch}"
    contents = {
        INFO: data,
        "Standalone/Spectr.exe": pe(0x8664, arm64x=True),
        "CLAP/Spectr.clap": pe(0x8664, arm64x=True),
        f"{vdir}/Spectr.vst3": pe(0x8664, arm64x=True),
        "wgpu_native.dll": pe(0xAA64),
        f"{vdir}/wgpu_native.dll": pe(0xAA64),
        "icudtl.dat": b"icu data",
        f"{vdir}/icudtl.dat": b"icu data",
    }
    manifest = {
        "schema": 1,
        "architecture": arch,
        "artifact_architectures": {"standalone": arch, "vst3": arch,
                                   "clap": arch, "runtime": "arm64-win"},
        "files": [{"path": name, "bytes": len(payload),
                   "sha256": hashlib.sha256(payload).hexdigest()}
                  for name, payload in contents.items()],
    }
    with zipfile.ZipFile(path, "w") as archive:
        for name, payload in contents.items():
            archive.writestr(name, payload + (b"tampered" if tamper and name == INFO else b""))
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

    def test_arm64ec_without_arm64x_section_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "ARM64X section"):
            check_pe(pe(0x8664), ARCH, "Spectr.vst3")

    def test_x64_binary_cannot_claim_arm64_runtime(self):
        with self.assertRaisesRegex(ValueError, "PE machine differs"):
            check_pe(pe(0x8664), "arm64-win", "wgpu_native.dll")


if __name__ == "__main__":
    unittest.main()
