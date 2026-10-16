#!/usr/bin/env python3
"""Fail closed unless both Windows packages match a macOS release commit.

The macOS release workflow pins its package build to GITHUB_SHA. Pass that exact
release commit here before uploading Windows assets to the same GitHub release.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import Path


def check_pe(data: bytes, arch: str, name: str) -> None:
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError(f"{name}: missing PE header")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if offset + 24 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        raise ValueError(f"{name}: missing PE signature")
    machine, sections = struct.unpack_from("<HH", data, offset + 4)
    if arch == "arm64ec-win":
        optional_size = struct.unpack_from("<H", data, offset + 20)[0]
        section_table = offset + 24 + optional_size
        names = [data[section_table + index * 40:section_table + index * 40 + 8]
                 for index in range(sections)
                 if section_table + (index + 1) * 40 <= len(data)]
        if machine not in (0x8664, 0xAA64) or b".a64xrm\0" not in names:
            raise ValueError(f"{name}: ARM64EC binary lacks ARM64X section")
    elif machine != {"x86_64-win": 0x8664, "arm64-win": 0xAA64}[arch]:
        raise ValueError(f"{name}: PE machine differs from {arch}")


def check_package(path: Path, arch: str, version: str, source_sha: str) -> str:
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError(f"{path}: duplicate ZIP entry")
        manifest = json.loads(archive.read("manifest.json"))
        if manifest.get("schema") != 1 or manifest.get("architecture") != arch:
            raise ValueError(f"{path}: wrong package schema or architecture")
        artifact_archs = manifest.get("artifact_architectures", {})
        for artifact in ("standalone", "vst3", "clap"):
            if artifact_archs.get(artifact) != arch:
                raise ValueError(f"{path}: {artifact} architecture differs from {arch}")
        expected_runtime = "arm64-win" if arch == "arm64ec-win" else arch
        if artifact_archs.get("runtime") != expected_runtime:
            raise ValueError(f"{path}: unexpected runtime architecture")
        build_info_path = (
            f"VST3/Spectr.vst3/Contents/{arch}/Spectr.pulp-build-info.json"
        )
        entries = manifest.get("files")
        if not isinstance(entries, list) or not entries:
            raise ValueError(f"{path}: empty or absent file manifest")
        recorded = set()
        for entry in entries:
            name = entry["path"].replace("\\", "/")
            if name in recorded or name.startswith("/") or ".." in name.split("/"):
                raise ValueError(f"{path}: duplicate or unsafe manifest path: {name}")
            recorded.add(name)
            data = archive.read(name)
            if len(data) != entry["bytes"] or hashlib.sha256(data).hexdigest().lower() != entry["sha256"].lower():
                raise ValueError(f"{path}: manifest hash/size mismatch: {name}")
        if {name for name in names if not name.endswith("/")} != recorded | {"manifest.json"}:
            raise ValueError(f"{path}: ZIP contents differ from manifest")
        if build_info_path not in recorded:
            raise ValueError(f"{path}: missing VST3 build provenance")
        vdir = f"VST3/Spectr.vst3/Contents/{arch}"
        for name, target_arch in (
            ("Standalone/Spectr.exe", arch),
            ("CLAP/Spectr.clap", arch),
            (f"{vdir}/Spectr.vst3", arch),
            ("wgpu_native.dll", expected_runtime),
            (f"{vdir}/wgpu_native.dll", expected_runtime),
        ):
            if name not in recorded:
                raise ValueError(f"{path}: missing release binary: {name}")
            check_pe(archive.read(name), target_arch, name)
        for name in ("icudtl.dat", f"{vdir}/icudtl.dat"):
            if name not in recorded:
                raise ValueError(f"{path}: missing Skia runtime: {name}")
        info = json.loads(archive.read(build_info_path))
        product = info.get("product", {})
        build = info.get("build", {})
        if info.get("schema") != "pulp.build-info.v1":
            raise ValueError(f"{path}: unsupported build-info schema")
        if (product.get("name") != "Spectr" or product.get("version") != version
                or product.get("source_git_sha") != source_sha
                or product.get("source_git_dirty") is not False):
            raise ValueError(f"{path}: product version/source does not match release commit")
        if build.get("type") != "Release" or arch not in build.get("archs", []):
            raise ValueError(f"{path}: build type/architecture is not the release target")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-sha", required=True, help="exact macOS release commit")
    parser.add_argument("--arm64ec", required=True, type=Path)
    parser.add_argument("--x64", required=True, type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.source_sha):
        parser.error("--source-sha must be an exact lowercase 40-character Git SHA")
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("--version must be X.Y.Z")
    try:
        results = []
        for path, arch in ((args.arm64ec, "arm64ec-win"), (args.x64, "x86_64-win")):
            digest = check_package(path, arch, args.version, args.source_sha)
            results.append(f"PASS {arch} {path} sha256={digest} source={args.source_sha}")
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    for result in results:
        print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
