#!/usr/bin/env python3
"""Validate a Linux Spectr archive before it is attached to a release."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import tarfile
from pathlib import Path


def _safe_name(name: str) -> str:
    name = name.replace("\\", "/")
    if (not name or name.startswith("/") or re.match(r"^[A-Za-z]:", name)
            or any(part in ("", ".", "..") for part in name.split("/"))):
        raise ValueError(f"unsafe archive entry: {name!r}")
    return name


def check_archive(path: Path, arch: str, version: str, source_sha: str) -> str:
    expected_name = f"Spectr-v{version}-linux-{arch}.tar.xz"
    if path.name != expected_name:
        raise ValueError(f"archive name must be {expected_name}")
    # The filename carries the architecture; the archive has one stable
    # product directory so the same install scripts and README work in every
    # Linux lane.
    root = f"Spectr-v{version}"
    with tarfile.open(path, mode="r:xz") as archive:
        files: dict[str, tarfile.TarInfo] = {}
        for member in archive.getmembers():
            raw_name = member.name.rstrip("/")
            name = _safe_name(raw_name)
            if name == root and member.isdir():
                continue
            if not name.startswith(root + "/"):
                raise ValueError(f"entry is outside release root: {name}")
            if name in files:
                raise ValueError(f"duplicate archive entry: {name}")
            if member.isdir():
                continue
            if member.issym() or member.islnk() or member.isdev():
                raise ValueError(f"special archive entry is not allowed: {name}")
            if member.isfile():
                files[name] = member
        def read(relative: str) -> bytes:
            name = f"{root}/{relative}"
            member = files.get(name)
            if member is None:
                raise ValueError(f"missing archive entry: {relative}")
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError(f"cannot read archive entry: {relative}")
            return stream.read()

        manifest = read("BUILD-MANIFEST.txt").decode("utf-8")
        fields = {}
        for line in manifest.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                fields[key] = value
        if fields.get("source_git_sha") != source_sha:
            raise ValueError("BUILD-MANIFEST source does not match release commit")
        if fields.get("build_type") != "Release":
            raise ValueError("Linux archive is not a Release build")
        if fields.get("architecture") != arch:
            raise ValueError("Linux archive architecture does not match filename")
        required = (
            "Spectr",
            "Spectr.clap",
            "Spectr.vst3/Contents/" + ("aarch64-linux" if arch == "aarch64" else "x86_64-linux") + "/Spectr.so",
            "install.sh",
            "uninstall.sh",
            "README.md",
            "SHA256SUMS",
        )
        for relative in required:
            read(relative)
        binary_paths = (
            "Spectr",
            "Spectr.clap",
            "Spectr.vst3/Contents/" + ("aarch64-linux" if arch == "aarch64" else "x86_64-linux") + "/Spectr.so",
        )
        expected_machine = 183 if arch == "aarch64" else 62
        for relative in binary_paths:
            member = files[f"{root}/{relative}"]
            if member.mode & 0o111 == 0:
                raise ValueError(f"release binary is not executable: {relative}")
            data = read(relative)
            if len(data) < 20 or data[:4] != b"\x7fELF" or data[4] != 2:
                raise ValueError(f"release binary is not a 64-bit ELF: {relative}")
            machine = int.from_bytes(data[18:20], "little")
            if machine != expected_machine:
                raise ValueError(f"release binary architecture differs from {arch}: {relative}")
        for script in ("install.sh", "uninstall.sh"):
            member = files[f"{root}/{script}"]
            if member.mode & 0o111 == 0:
                raise ValueError(f"{script} is not executable")
            script_text = read(script).decode("utf-8")
            for marker in ("SPECTR_VST3_DIR", "SPECTR_CLAP_DIR", "SPECTR_BIN_DIR"):
                if marker not in script_text:
                    raise ValueError(f"{script} is not a functional installer")
        sums = read("SHA256SUMS").decode("utf-8")
        listed = set()
        for line in sums.splitlines():
            if not line.strip():
                continue
            digest, relative = line.strip().split(maxsplit=1)
            relative = relative.lstrip("*")
            if relative.startswith(root + "/"):
                relative = relative[len(root) + 1:]
            if relative == "SHA256SUMS" or relative not in {
                    name[len(root) + 1:] for name in files}:
                raise ValueError(f"SHA256SUMS names unexpected file: {relative}")
            actual = hashlib.sha256(read(relative)).hexdigest()
            if digest.lower() != actual:
                raise ValueError(f"SHA256SUMS mismatch: {relative}")
            listed.add(relative)
        if not (set(required) - {"SHA256SUMS"}).issubset(listed):
            raise ValueError("SHA256SUMS does not cover every required payload")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--aarch64", type=Path)
    parser.add_argument("--x86_64", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("--version must be X.Y.Z")
    if not re.fullmatch(r"[0-9a-f]{40}", args.source_sha):
        parser.error("--source-sha must be an exact lowercase 40-character Git SHA")
    requested = [(args.aarch64, "aarch64"), (args.x86_64, "x86_64")]
    if not any(path for path, _ in requested):
        parser.error("at least one Linux archive is required")
    try:
        for path, arch in requested:
            if path:
                digest = check_archive(path, arch, args.version, args.source_sha)
                print(f"PASS {arch} {path} sha256={digest} source={args.source_sha}")
    except (OSError, EOFError, ValueError, KeyError, tarfile.TarError) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
