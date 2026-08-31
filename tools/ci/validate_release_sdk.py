#!/usr/bin/env python3
"""Fail closed unless Spectr resolved the exact official Release Pulp SDK."""
import argparse, json, re
from pathlib import Path

def fail(message): raise SystemExit(f"release SDK validation failed: {message}")

def cache_values(path):
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^([^#/:=]+):[^=]+=(.*)$", line)
        if not match: continue
        key, value = match.groups()
        if key in result: fail(f"duplicate CMake cache key: {key}")
        result[key] = value
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--pin", type=Path, required=True)
    args = parser.parse_args()
    pin = json.loads(args.pin.read_text(encoding="utf-8"))
    version, sha = pin["release_tag"].removeprefix("v"), pin["source_git_sha"]
    if not re.fullmatch(r"v\d+\.\d+\.\d+", pin["release_tag"]): fail("invalid release tag")
    if not re.fullmatch(r"[0-9a-f]{40}", sha): fail("source SHA is not full lowercase hex")
    if not re.fullmatch(r"[0-9a-f]{64}", pin["asset_sha256"]): fail("invalid asset SHA-256")

    cache = cache_values(args.build_dir / "CMakeCache.txt")
    expected_cache = {
        "CMAKE_BUILD_TYPE": "Release",
        "PULP_SDK_DISTRIBUTION_ELIGIBLE": "TRUE",
        "PULP_SDK_AUDIO_PROBES_ENABLED": "FALSE",
        "PULP_SDK_INSPECTOR_ENABLED": "TRUE",
        "PULP_SDK_PLATFORM": pin["platform"],
        "PULP_SDK_SOURCE_GIT_SHA": sha,
        "SPECTR_EXPECTED_PULP_SDK_SHA": sha,
    }
    for key, expected in expected_cache.items():
        if cache.get(key) != expected: fail(f"{key}={cache.get(key)!r}, expected {expected!r}")
    expected_dir = (args.sdk_root / "lib/cmake/Pulp").resolve()
    if Path(cache.get("Pulp_DIR", "")).resolve() != expected_dir: fail("Pulp_DIR is not the extracted SDK")

    provenance = json.loads((args.sdk_root / "sdk-provenance.json").read_text(encoding="utf-8"))
    expected = {
        "schema": "pulp.sdk-provenance.v1", "kind": "release",
        "profile": "official-release", "distribution_eligible": True,
        "sdk_version": version, "source_git_ref": pin["release_tag"],
        "source_git_sha": sha, "source_git_dirty": False,
        "platform": pin["platform"], "build_type": "Release",
        "features": {"audio_probes": False, "inspector": True},
    }
    mismatches = {k: (provenance.get(k), v) for k, v in expected.items() if provenance.get(k) != v}
    if mismatches: fail(f"unsafe provenance contract: {mismatches}")
    if (args.sdk_root / "sdk_build_type.txt").read_text().strip() != "Release": fail("SDK marker is not Release")
    if (args.sdk_root / "version.txt").read_text().strip() != version: fail("SDK version marker mismatch")

    targets = (args.sdk_root / "lib/cmake/Pulp/PulpTargets.cmake").read_text()
    required = ("Pulp::clap", "Pulp::vst3-sdk", "Pulp::ausdk", "Pulp::render",
                "Pulp::standalone", "Pulp::format", "Pulp::view-native")
    missing = [target for target in required if target not in targets]
    if missing: fail(f"missing exported targets: {missing}")
    info = (args.sdk_root / "include/pulp/runtime/build_info.hpp").read_text()
    if 'kBuildType   = "Release"' not in info: fail("build info is not Release")
    if "kGitDirty                = false" not in info: fail("build info is dirty")
    match = re.search(r'kGitSha\s*=\s*"([0-9a-f]{7,40})"', info)
    if not match or not sha.startswith(match.group(1)): fail("build info SHA mismatch")
    print(f"official Pulp {pin['release_tag']} ({sha}) Release SDK accepted")

if __name__ == "__main__": main()
