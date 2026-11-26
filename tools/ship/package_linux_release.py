#!/usr/bin/env python3
"""Package and smoke-test a provenance-checked Spectr Linux build."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

SHA_RE = re.compile(r"[0-9a-f]{40}\Z")
ARCHES = {"aarch64": 183, "x86_64": 62}


def _cmake_cache(src: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in (src / "build" / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.startswith(("#", "//")):
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


def validate_source_provenance(src: Path, source_sha: str, sdk_sha: str) -> None:
    """Refuse packaging when checkout or build provenance differs."""
    for label, value in (("source SHA", source_sha), ("SDK SHA", sdk_sha)):
        if not SHA_RE.fullmatch(value):
            raise ValueError(f"{label} must be one exact lowercase 40-character Git SHA")
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=src, text=True).strip()
    if head != source_sha:
        raise ValueError(f"source checkout is {head}, expected {source_sha}")
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=src, text=True).strip()
    if dirty:
        raise ValueError("source checkout is dirty")
    cache = _cmake_cache(src)
    expected = {
        "SPECTR_SOURCE_GIT_SHA": source_sha,
        "PULP_SDK_SOURCE_GIT_SHA": sdk_sha,
        "CMAKE_BUILD_TYPE": "Release",
        "SPECTR_SOURCE_GIT_DIRTY": "FALSE",
        "PULP_SDK_DISTRIBUTION_ELIGIBLE": "TRUE",
        "PULP_SDK_PROVENANCE_KIND": "release",
    }
    for key, value in expected.items():
        if cache.get(key) != value:
            raise ValueError(f"CMake provenance mismatch for {key}: {cache.get(key)!r} != {value!r}")
    for key in (
        "SPECTR_ENABLE_TEST_SEAMS",
        "SPECTR_ENABLE_PERF_FIXTURES",
        "SPECTR_SHARED_PRODUCT_ACCEPTANCE",
        "SPECTR_SHARED_NATIVE_HOST_PROBE",
    ):
        if cache.get(key, "OFF") not in ("OFF", "FALSE", "0"):
            raise ValueError(f"release-only cache option is enabled: {key}")


def package_release(src: Path, out: Path, arch: str, version: str, source_sha: str, sdk_sha: str) -> None:
    if arch not in ARCHES:
        raise ValueError(f"unsupported Linux architecture: {arch}")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("version must be X.Y.Z")
    validate_source_provenance(src, source_sha, sdk_sha)
    out.mkdir(exist_ok=False)
    stage = out / f"Spectr-v{version}"
    stage.mkdir()
    inputs = {
        "Spectr": "Spectr",
        "Spectr.clap": "CLAP/Spectr.clap",
        f"Spectr.vst3/Contents/{arch}-linux/Spectr.so": "VST3/libSpectr.so",
    }
    modules = {}
    for destination, relative in inputs.items():
        path = src / "build" / relative
        data = path.read_bytes()
        if data[:5] != b"\x7fELF\x02" or int.from_bytes(data[18:20], "little") != ARCHES[arch]:
            raise ValueError(f"release binary architecture differs from {arch}: {relative}")
        target = stage / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        target.chmod(0o755)
        modules[destination] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}

    common = '''#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SPECTR_VST3_DIR="${SPECTR_VST3_DIR:-$HOME/.vst3}"
SPECTR_CLAP_DIR="${SPECTR_CLAP_DIR:-$HOME/.clap}"
SPECTR_BIN_DIR="${SPECTR_BIN_DIR:-$HOME/.local/bin}"
'''
    install = common + f'''cd "$ROOT"
sha256sum -c SHA256SUMS
install -d "$SPECTR_VST3_DIR/Spectr.vst3/Contents/{arch}-linux" "$SPECTR_CLAP_DIR" "$SPECTR_BIN_DIR"
install -m 755 Spectr "$SPECTR_BIN_DIR/Spectr"
install -m 755 Spectr.clap "$SPECTR_CLAP_DIR/Spectr.clap"
install -m 755 Spectr.vst3/Contents/{arch}-linux/Spectr.so "$SPECTR_VST3_DIR/Spectr.vst3/Contents/{arch}-linux/Spectr.so"
echo 'Spectr {version} installed.'
'''
    uninstall = common + f'''cd "$ROOT"
# Only remove this version's exact payload; preserve changed/newer files.
remove_matching() {{
  if [[ -f "$2" ]] && cmp -s -- "$1" "$2"; then rm -- "$2"; fi
}}
remove_matching "$ROOT/Spectr" "$SPECTR_BIN_DIR/Spectr"
remove_matching "$ROOT/Spectr.clap" "$SPECTR_CLAP_DIR/Spectr.clap"
remove_matching "$ROOT/Spectr.vst3/Contents/{arch}-linux/Spectr.so" "$SPECTR_VST3_DIR/Spectr.vst3/Contents/{arch}-linux/Spectr.so"
rmdir -- "$SPECTR_VST3_DIR/Spectr.vst3/Contents/{arch}-linux" "$SPECTR_VST3_DIR/Spectr.vst3/Contents" "$SPECTR_VST3_DIR/Spectr.vst3" 2>/dev/null || true
echo 'Matching Spectr {version} files removed.'
'''
    for name, value in (("install.sh", install), ("uninstall.sh", uninstall)):
        path = stage / name
        path.write_text(value, encoding="utf-8")
        path.chmod(0o755)
    (stage / "README.md").write_text(
        f"# Spectr {version} for Linux {arch}\n\n"
        "Run `./install.sh` as your normal user to install the standalone, VST3 and CLAP into ~/.local/bin, ~/.vst3 and ~/.clap. Run Spectr from ~/.local/bin/Spectr. Rescan plugins in your host. Runtime libraries required: Wayland/X11, Vulkan for hosted editors, ALSA/PipeWire audio and standard C++/glibc libraries.\n\n"
        "Override destinations with SPECTR_VST3_DIR, SPECTR_CLAP_DIR and SPECTR_BIN_DIR; use the same values for `./uninstall.sh`. Keep this extracted directory for uninstall. The uninstaller preserves files that differ from this package.\n\n"
        f"Source: https://github.com/danielraffel/spectr/commit/{source_sha}\n"
        f"Framework: official Pulp SDK 1.149.0, source {sdk_sha}.\n",
        encoding="utf-8",
    )
    (stage / "BUILD-MANIFEST.txt").write_text(
        f"version={version}\nsource_git_sha={source_sha}\nbuild_type=Release\n"
        f"architecture={arch}\npulp_sdk_source_git_sha={sdk_sha}\npulp_sdk_version=1.149.0\n",
        encoding="utf-8",
    )
    (stage / "SHA256SUMS").write_text(
        "".join(
            hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.relative_to(stage).as_posix() + "\n"
            for path in sorted(stage.rglob("*"))
            if path.is_file()
        ),
        encoding="utf-8",
    )
    smoke = out / "install-smoke"
    env = dict(os.environ, SPECTR_VST3_DIR=str(smoke / "vst3"), SPECTR_CLAP_DIR=str(smoke / "clap"), SPECTR_BIN_DIR=str(smoke / "bin"))
    subprocess.run([str(stage / "install.sh")], env=env, check=True)
    installed = {
        "Spectr": smoke / "bin" / "Spectr",
        "Spectr.clap": smoke / "clap" / "Spectr.clap",
        f"Spectr.vst3/Contents/{arch}-linux/Spectr.so": smoke / "vst3" / "Spectr.vst3" / "Contents" / f"{arch}-linux" / "Spectr.so",
    }
    for name, path in installed.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != modules[name]["sha256"]:
            raise ValueError(f"installed payload differs: {name}")
    subprocess.run([str(stage / "uninstall.sh")], env=env, check=True)
    if any(path.exists() for path in installed.values()):
        raise ValueError("uninstall smoke left a payload behind")
    subprocess.run([str(stage / "install.sh")], env=env, check=True)
    archive = out / f"Spectr-v{version}-linux-{arch}.tar.xz"
    with tarfile.open(archive, "w:xz", preset=3) as tar:
        tar.add(stage, arcname=stage.name)
    receipt = {
        "source": source_sha,
        "sdk_source": sdk_sha,
        "architecture": arch,
        "modules": modules,
        "archive": str(archive),
        "archive_bytes": archive.stat().st_size,
        "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "install_uninstall_smoke": True,
        "installed": {key: str(value) for key, value in installed.items()},
        "runtime_qualified": False,
    }
    (out / "package-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(receipt, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("architecture", choices=sorted(ARCHES))
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--sdk-sha", required=True)
    args = parser.parse_args()
    try:
        package_release(args.source, args.output, args.architecture, args.version, args.source_sha, args.sdk_sha)
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
