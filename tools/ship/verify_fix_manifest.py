#!/usr/bin/env python3
"""Prove that the fixes a package claims to carry are actually inside its binaries.

A build that succeeds and an installer that installs say nothing about whether a
given fix is present: a fix still sitting in an unmerged PR produces a perfectly
healthy package without it. This reads tokens out of the shipped Mach-O and
reports per-artifact.

Exit codes:
  0  every fix found in every artifact
  1  at least one fix missing from at least one artifact
  2  UNMEASURED - the instrument could not read an artifact, so no verdict is
     reported at all. A run that cannot see the tokens looks identical to a run
     where the tokens are absent, and the second reads as a confident finding.
"""

from __future__ import annotations

import argparse
import json
import plistlib
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = Path(__file__).resolve().parent / "fix-manifest.json"


def executable_of(bundle: Path) -> Path | None:
    """Resolve a .app/.vst3/.component/.clap to the Mach-O inside it."""
    info = bundle / "Contents" / "Info.plist"
    macos = bundle / "Contents" / "MacOS"
    if info.is_file():
        try:
            with info.open("rb") as handle:
                name = plistlib.load(handle).get("CFBundleExecutable")
            if name and (macos / name).is_file():
                return macos / name
        except Exception:
            pass
    if macos.is_dir():
        # A bundle may ship dylibs beside the executable; take the largest file,
        # which is the binary carrying the embedded document.
        files = [p for p in macos.iterdir() if p.is_file()]
        if files:
            return max(files, key=lambda p: p.stat().st_size)
    return None


def resolve_artifact(path: Path) -> Path | None:
    if path.is_file():
        return path
    if path.is_dir():
        return executable_of(path)
    return None


def strings_of(binary: Path) -> str | None:
    try:
        out = subprocess.run(
            ["strings", "-a", str(binary)],
            capture_output=True, text=True, errors="replace", timeout=300,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if out.returncode != 0:
        return None
    return out.stdout


def check_token(blob: str, token: str) -> bool:
    if '"' in token:
        # The document embeds JSON with backslash-escaped quotes, so a literal
        # quote in a token can never match. Refuse rather than silently miss.
        raise ValueError(f"token contains a literal quote and cannot match: {token!r}")
    return token in blob


def validate_manifest(manifest: dict) -> None:
    """Reject a manifest that cannot possibly match, before reading any artifact.

    This runs first and unconditionally. A quote-bearing token would otherwise
    read ABSENT forever, and if the artifact also failed the control check the
    manifest bug would stay hidden behind an UNMEASURED verdict.
    """

    tokens = list(manifest.get("control_tokens", []))
    tokens += [t for fix in manifest.get("fixes", []) for t in fix.get("tokens", [])]
    for fix in manifest.get("fixes", []):
        if fix.get("verifiable") is False and fix.get("tokens"):
            raise ValueError(
                f"fix {fix['id']!r} is marked unverifiable but declares tokens"
            )
        if fix.get("verifiable") is not False and not fix.get("tokens"):
            raise ValueError(
                f"fix {fix['id']!r} declares no tokens and is not marked "
                "verifiable: false - an untokened row would silently pass"
            )
    for token in tokens:
        if '"' in token:
            raise ValueError(
                f"token contains a literal quote and cannot match: {token!r}"
            )
        if not token.strip():
            raise ValueError("manifest contains an empty token")


def verify(artifact: Path, manifest: dict) -> tuple[str, list[tuple[str, bool, str]]]:
    """Return (status, rows). status is 'ok' | 'missing' | 'unmeasured'."""
    binary = resolve_artifact(artifact)
    if binary is None:
        return "unmeasured", [("could not resolve a binary inside the artifact", False, "")]

    blob = strings_of(binary)
    if blob is None:
        return "unmeasured", [(f"strings failed on {binary}", False, "")]

    # Positive control FIRST. If the control is absent the extraction is wrong,
    # and every fix below would read absent for a reason that has nothing to do
    # with the fixes.
    controls = manifest.get("control_tokens", [])
    if not controls:
        return "unmeasured", [("manifest declares no control tokens", False, "")]
    missing_controls = [t for t in controls if not check_token(blob, t)]
    if missing_controls:
        return "unmeasured", [
            (f"control token absent: {t} - the instrument is not reading this "
             f"binary, so no finding is reported", False, "") for t in missing_controls
        ]

    rows: list[tuple[str, bool, str]] = []
    all_present = True
    for fix in manifest["fixes"]:
        if fix.get("verifiable") is False:
            # Reported, never silently skipped. A fix nothing can prove is a
            # known hole in the gate; hiding it would make the gate look
            # more complete than it is.
            rows.append((f"#{fix['pr']}  {fix['title']}", None,
                         fix.get("reason", "no token can prove this")))
            continue
        found = [t for t in fix["tokens"] if check_token(blob, t)]
        present = len(found) == len(fix["tokens"])
        all_present &= present
        detail = "" if present else "absent: " + ", ".join(
            t for t in fix["tokens"] if t not in found
        )
        rows.append((f"#{fix['pr']}  {fix['title']}", present, detail))
    return ("ok" if all_present else "missing"), rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("artifacts", nargs="+", type=Path,
                    help=".app / .vst3 / .component / .clap bundles, or bare binaries")
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = ap.parse_args()

    manifest = json.loads(args.manifest.read_text())

    try:
        validate_manifest(manifest)
    except ValueError as exc:
        print(f"MANIFEST ERROR: {exc}")
        return 2

    worst = 0
    for artifact in args.artifacts:
        print(f"\n=== {artifact} ===")
        try:
            status, rows = verify(artifact, manifest)
        except ValueError as exc:
            print(f"  MANIFEST ERROR: {exc}")
            worst = max(worst, 2)
            continue

        if status == "unmeasured":
            for note, _, _ in rows:
                print(f"  UNMEASURED: {note}")
            worst = max(worst, 2)
            continue

        for label, present, detail in rows:
            mark = ("NOT-PROVABLE" if present is None
                    else "PRESENT" if present else "ABSENT ")
            print(f"  {mark}  {label}" + (f"   ({detail})" if detail else ""))
        if status == "missing":
            worst = max(worst, 1)

    print()
    if worst == 0:
        print("OK: every fix in the manifest is compiled into every artifact.")
    elif worst == 1:
        print("FAIL: at least one fix is missing from a shipped artifact. "
              "Do not hand this package over.")
    else:
        print("UNMEASURED: the instrument could not read at least one artifact. "
              "No verdict - fix the instrument and re-run.")
    return worst


if __name__ == "__main__":
    sys.exit(main())
