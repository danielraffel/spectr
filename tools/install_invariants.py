#!/usr/bin/env python3
"""Exactly one Spectr AU component may be installed, across BOTH search paths.

macOS scans `~/Library/Audio/Plug-Ins/Components` and
`/Library/Audio/Plug-Ins/Components`. A stale copy in either is not merely
untidy: two bundles with the same component type/subtype/manufacturer are
indistinguishable to a host, so which one a DAW loads is not determined by
anything a user or a build can see. Reporting a version is no help when both
report the same one, which is exactly the state this found.

Checks the AU component specifically because that is the format whose
registration is global. Backup siblings (`.bak*`, `.pre-*`) are reported
separately: they do not register, but they are how a stale bundle survives a
reinstall and later gets restored by hand.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import plistlib
import subprocess
import sys

SEARCH_PATHS = (
    os.path.expanduser("~/Library/Audio/Plug-Ins/Components"),
    "/Library/Audio/Plug-Ins/Components",
)


def bundle_identity(path: str) -> dict:
    info = os.path.join(path, "Contents", "Info.plist")
    out = {"path": path, "version": None, "binary_sha": None, "size": None}
    try:
        with open(info, "rb") as fh:
            pl = plistlib.load(fh)
        out["version"] = pl.get("CFBundleVersion")
        exe = pl.get("CFBundleExecutable")
        if exe:
            binary = os.path.join(path, "Contents", "MacOS", exe)
            if os.path.exists(binary):
                out["size"] = os.path.getsize(binary)
                with open(binary, "rb") as fh:
                    out["binary_sha"] = hashlib.sha1(fh.read()).hexdigest()[:12]
    except Exception as exc:  # noqa: BLE001
        out["error"] = str(exc)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--name", default="Spectr")
    ap.add_argument("--plant", choices=["extra-install"],
                    help="pretend a second copy exists, to prove the check fails")
    args = ap.parse_args()

    live, backups = [], []
    for root in SEARCH_PATHS:
        if not os.path.isdir(root):
            continue
        for entry in sorted(os.listdir(root)):
            if args.name.lower() not in entry.lower():
                continue
            full = os.path.join(root, entry)
            (live if entry.endswith(".component") else backups).append(full)

    if args.plant == "extra-install":
        live = live + [live[0] + " (PLANTED)"] if live else ["A (PLANTED)", "B (PLANTED)"]
        print("CONTROL: planted a second installed component")

    print(f"search_paths={len(SEARCH_PATHS)}  installed={len(live)}  "
          f"backup_siblings={len(backups)}")
    ids = []
    for p in live:
        if "(PLANTED)" in p:
            print(f"  {p}")
            continue
        idn = bundle_identity(p)
        ids.append(idn)
        print(f"  {p}\n      version={idn['version']} size={idn['size']} "
              f"sha={idn['binary_sha']}")

    if not live:
        print("INCONCLUSIVE: no component installed — nothing to judge, NOT a pass")
        return 3
    if len(live) == 1:
        print("GREEN  exactly one component is installed")
        if args.plant:
            print("BROKEN: the plant did not fail the check", file=sys.stderr)
            return 4
        return 0

    print(f"  RED  {len(live)} components are installed across the two search "
          f"paths; a host cannot be told which to load")
    distinct_v = {i.get("version") for i in ids if i.get("version")}
    distinct_b = {i.get("binary_sha") for i in ids if i.get("binary_sha")}
    if len(distinct_b) > 1 and len(distinct_v) == 1:
        print(f"  RED  they are DIFFERENT binaries ({len(distinct_b)} hashes) "
              f"reporting the SAME version {distinct_v.pop()} — nothing "
              f"distinguishes them to a DAW or a user")
    print("RED    duplicate AU installation")
    return 1


if __name__ == "__main__":
    sys.exit(main())
