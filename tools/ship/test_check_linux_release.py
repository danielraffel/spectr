"""Linux archives must be exact-source, safe, and self-checking."""

import hashlib
import io
import tarfile
import tempfile
import unittest
from pathlib import Path

from tools.ship.check_linux_release import check_archive


SHA = "a" * 40


def make_archive(path: Path, *, source: str = SHA, arch: str = "aarch64",
                 tamper: bool = False) -> None:
    root = "Spectr-v1.0.10"
    vdir = "aarch64-linux" if arch == "aarch64" else "x86_64-linux"
    machine = 183 if arch == "aarch64" else 62
    elf = bytearray(b"\x7fELF" + bytes([2]) + b"\0" * 13)
    elf[18:20] = machine.to_bytes(2, "little")
    files = {
        "Spectr": bytes(elf),
        "Spectr.clap": bytes(elf),
        f"Spectr.vst3/Contents/{vdir}/Spectr.so": bytes(elf),
        "install.sh": (b"#!/bin/sh\n"
                        b"SPECTR_VST3_DIR SPECTR_CLAP_DIR SPECTR_BIN_DIR\n"),
        "uninstall.sh": (b"#!/bin/sh\n"
                          b"SPECTR_VST3_DIR SPECTR_CLAP_DIR SPECTR_BIN_DIR\n"),
        "README.md": b"Spectr Linux\n",
    }
    manifest = ("Spectr Linux qualification build\n"
                f"source_git_sha={source}\n"
                "pulp_sdk_version=1.132.0\n"
                "build_type=Release\n"
                f"architecture={arch}\n").encode()
    files["BUILD-MANIFEST.txt"] = manifest
    sums = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                    for name, data in files.items())
    files["SHA256SUMS"] = sums.encode()
    with tarfile.open(path, "w:xz") as archive:
        for name, data in files.items():
            if tamper and name == "Spectr.clap":
                data += b"tampered"
            info = tarfile.TarInfo(f"{root}/{name}")
            info.size = len(data)
            info.mode = 0o755 if name.endswith(".sh") or name in {
                "Spectr", "Spectr.clap",
                f"Spectr.vst3/Contents/{vdir}/Spectr.so",
            } else 0o644
            archive.addfile(info, io.BytesIO(data))


class LinuxReleaseTest(unittest.TestCase):
    def test_matching_archive(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Spectr-v1.0.10-linux-aarch64.tar.xz"
            make_archive(path)
            self.assertEqual(len(check_archive(path, "aarch64", "1.0.10", SHA)), 64)

    def test_stale_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Spectr-v1.0.10-linux-aarch64.tar.xz"
            make_archive(path, source="b" * 40)
            with self.assertRaisesRegex(ValueError, "source does not match"):
                check_archive(path, "aarch64", "1.0.10", SHA)

    def test_bad_sum_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Spectr-v1.0.10-linux-aarch64.tar.xz"
            make_archive(path, tamper=True)
            with self.assertRaisesRegex(ValueError, "SHA256SUMS mismatch"):
                check_archive(path, "aarch64", "1.0.10", SHA)


if __name__ == "__main__":
    unittest.main()
