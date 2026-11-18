"""Point-in-time macOS mapped-plugin file receipts for an owned live host.

This hashes files named by lsof's text mappings, not process memory. Separate
render processes and later replacement of a mapped file are outside the scope.
"""
from __future__ import annotations

import hashlib
import json
import plistlib
import subprocess
import sys
from pathlib import Path


class LoadedBinaryError(RuntimeError):
    """The loaded plugin could not be tied uniquely to the intended bytes."""


def bundle_executable(bundle: Path) -> Path:
    bundle = bundle.resolve(strict=True)
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        name = plistlib.load(stream)["CFBundleExecutable"]
    if not isinstance(name, str) or not name or Path(name).name != name or name in {".", ".."}:
        raise LoadedBinaryError("invalid CFBundleExecutable")
    executable = (bundle / "Contents/MacOS" / name).resolve(strict=True)
    if not executable.is_file() or not executable.is_relative_to(bundle):
        raise LoadedBinaryError("bundle executable escapes bundle or is not a file")
    return executable


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def mapped_paths(proc: subprocess.Popen) -> list[Path]:
    """Query only this owned Popen's live PID; never stop or signal it here."""
    if sys.platform != "darwin":
        raise LoadedBinaryError("mapped-plugin receipts require macOS")
    if proc.poll() is not None:
        raise LoadedBinaryError("owned host exited before mapping inspection")
    result = subprocess.run(
        ["/usr/sbin/lsof", "-a", "-p", str(proc.pid), "-d", "txt", "-Fpn0"],
        capture_output=True, timeout=15, check=False,
    )
    if result.returncode != 0:
        raise LoadedBinaryError(f"lsof failed with exit {result.returncode}")
    fields = [field.lstrip(b"\n") for field in result.stdout.split(b"\0") if field.strip()]
    pids = [field[1:] for field in fields if field.startswith(b"p")]
    if pids != [str(proc.pid).encode()]:
        raise LoadedBinaryError("lsof did not identify exactly the owned PID")
    return sorted({Path(field[1:].decode("utf-8", errors="strict"))
                   for field in fields if field.startswith(b"n")})


def verify_loaded_binary(proc: subprocess.Popen, bundle: Path, receipt_path: Path,
                         scope: str) -> dict:
    """Write PASS/FAIL evidence, raising on missing, wrong or ambiguous mapping.

    Same-named user/system copies are candidates even outside the intended
    scan folder. Multiple candidates fail even when their hashes agree.
    """
    receipt = {"schema_version": 1, "scope": scope, "pid": proc.pid,
               "evidence": "live lsof text mapping paths and on-disk SHA256",
               "offline_render_processes_verified": False, "status": "FAIL"}
    try:
        if proc.poll() is not None:
            raise LoadedBinaryError("owned host is not live")
        expected = bundle_executable(bundle)
        receipt.update(expected_path=str(expected), expected_sha256=sha256(expected))
        candidates = set()
        for path in mapped_paths(proc):
            # Include stale same-name copies in any scan/user/system directory.
            same_bundle_executable = (path.parent.name == "MacOS"
                                      and path.parent.parent.name == "Contents"
                                      and path.parent.parent.parent.name == bundle.name)
            if path.name == expected.name or same_bundle_executable:
                candidates.add(path.resolve(strict=True))
        receipt["candidate_paths"] = sorted(map(str, candidates))
        if len(candidates) != 1:
            raise LoadedBinaryError(f"expected one mapped plugin executable, found {len(candidates)}")
        actual = candidates.pop()
        receipt.update(actual_path=str(actual), actual_sha256=sha256(actual))
        if receipt["actual_sha256"] != receipt["expected_sha256"]:
            raise LoadedBinaryError("mapped plugin executable SHA256 differs from intended executable")
        if sha256(expected) != receipt["expected_sha256"]:
            raise LoadedBinaryError("intended executable changed during inspection")
        if proc.poll() is not None:
            raise LoadedBinaryError("owned host exited during mapping inspection")
        receipt["status"] = "PASS"
    except Exception as exc:
        receipt["error"] = str(exc)
        raise LoadedBinaryError(str(exc)) from exc
    finally:
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt
