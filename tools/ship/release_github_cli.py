#!/usr/bin/env python3
"""Select the Homebrew GitHub CLI, never a fleet PATH authentication shim.

Only the named main Actions workflow uses its ordinary GITHUB_TOKEN this way.
Local dry-run reads continue using the normal Shipyard authentication wrapper.
"""
from __future__ import annotations

import hashlib
import os
import re
import subprocess
from pathlib import Path

REPOSITORY = "danielraffel/spectr"
HOMEBREW = Path("/opt/homebrew")


def actions_context() -> bool:
    return (os.environ.get("GITHUB_ACTIONS") == "true"
            and os.environ.get("GITHUB_REPOSITORY") == REPOSITORY
            and os.environ.get("GITHUB_REF") == "refs/heads/main"
            and os.environ.get("GITHUB_WORKFLOW_REF") ==
            f"{REPOSITORY}/.github/workflows/release-intel-backfill.yml@refs/heads/main"
            and bool(os.environ.get("GH_TOKEN")))


def resolve_binary(prefix: Path = HOMEBREW) -> Path:
    binary = (prefix / "opt/gh/bin/gh").resolve(strict=True)
    cellar = (prefix / "Cellar/gh").resolve(strict=True)
    relative = binary.relative_to(cellar)
    # A real formula version's bin/gh, not a symlink to ~/.local/bin/ghapp.
    if len(relative.parts) != 3 or relative.parts[1:] != ("bin", "gh"):
        raise ValueError("release gh is not a Homebrew gh formula binary")
    with binary.open("rb") as stream:
        magic = stream.read(4)
    if (not binary.is_file() or not os.access(binary, os.X_OK)
            or binary.stat().st_mode & 0o022
            or magic not in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe",
                             b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca")):
        raise ValueError("release gh must be an executable, non-writable-by-others Mach-O binary")
    return binary


def digest(binary: Path) -> str:
    with binary.open("rb") as stream:
        result = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def actions_gh() -> str:
    if not actions_context():
        raise ValueError("release gh requires the named main-only Actions workflow")
    binary = resolve_binary()
    if (os.environ.get("SPECTR_RELEASE_GH") != str(binary)
            or os.environ.get("SPECTR_RELEASE_GH_SHA256") != digest(binary)):
        raise ValueError("release gh differs from the verified workflow selection")
    return str(binary)


def main() -> None:
    if not actions_context():
        raise SystemExit("release gh selection requires the named main-only Actions workflow")
    binary = resolve_binary()
    result = subprocess.run([str(binary), "--version"], check=True, capture_output=True, text=True)
    if not re.match(r"gh version \d+\.\d+\.\d+", result.stdout):
        raise SystemExit("Homebrew gh printed no GitHub CLI version")
    print(f"SPECTR_RELEASE_GH={binary}")
    print(f"SPECTR_RELEASE_GH_SHA256={digest(binary)}")


if __name__ == "__main__":
    main()
