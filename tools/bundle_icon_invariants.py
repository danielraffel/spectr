#!/usr/bin/env python3
"""Every shipped Spectr macOS bundle carries its product icon asset.

Spectr's .icns reached only the standalone .app for as long as the plugin
formats had no Info.plist key to name it. Both halves are wired now, and
nothing else notices when one silently stops arriving: the plugin bundles
are not launched, the key is a plain string, and a build that drops the
icon is otherwise indistinguishable from one that does not.

This reads the BUILT bundles, never the CMake source, because the source
saying `MACOSX_BUNDLE_ICON_FILE` is not evidence that generate-time
substitution ran or that the file was copied. Two independent facts are
required per bundle:

  * Info.plist declares a NON-EMPTY CFBundleIconFile. The Pulp templates
    emit the key unconditionally and substitute CMake's target property
    into it, so a bundle with no icon wired still HAS the key -- holding
    an empty string. Presence of the key proves nothing; only its value
    does.
  * The .icns the key names actually exists in Contents/Resources and is
    a real icns (`icns` magic), not an empty placeholder.

Scope note, so this check is not read as more than it is: it asserts the
asset SHIPS, not that anything displays it. CFBundleIconFile is inert on a
non-.app bundle -- Finder draws the generic bundle icon regardless. See the
product-icon comment in CMakeLists.txt.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import plistlib
import sys

# (subdirectory under the build tree, bundle name)
BUNDLES = (
    ("", "Spectr.app"),
    ("VST3", "Spectr.vst3"),
    ("AU", "Spectr.component"),
    ("CLAP", "Spectr.clap"),
)

ICNS_MAGIC = b"icns"


def inspect(path: str) -> dict:
    """Read one bundle's icon facts. Never raises; reports what it found."""
    out = {
        "path": path,
        "exists": os.path.isdir(path),
        "key": None,
        "icns_path": None,
        "icns_size": None,
        "icns_magic_ok": False,
        "sha": None,
        "error": None,
    }
    if not out["exists"]:
        return out
    info = os.path.join(path, "Contents", "Info.plist")
    try:
        with open(info, "rb") as fh:
            pl = plistlib.load(fh)
    except Exception as exc:  # noqa: BLE001
        out["error"] = f"unreadable Info.plist: {exc}"
        return out

    # A missing key and an empty key are the same defect here, and the
    # templates produce the latter, so normalise both to "".
    out["key"] = (pl.get("CFBundleIconFile") or "").strip()
    if not out["key"]:
        return out

    # CFBundleIconFile may omit the .icns extension (Apple treats it as
    # optional); resolve both spellings rather than assuming one.
    name = out["key"]
    candidates = [name] if name.lower().endswith(".icns") else [name + ".icns", name]
    for cand in candidates:
        p = os.path.join(path, "Contents", "Resources", cand)
        if os.path.isfile(p):
            out["icns_path"] = p
            out["icns_size"] = os.path.getsize(p)
            try:
                with open(p, "rb") as fh:
                    blob = fh.read()
                out["icns_magic_ok"] = blob[:4] == ICNS_MAGIC
                out["sha"] = hashlib.sha256(blob).hexdigest()[:12]
            except Exception as exc:  # noqa: BLE001
                out["error"] = f"unreadable icns: {exc}"
            break
    return out


def verdict(rec: dict) -> tuple[bool, str]:
    if not rec["exists"]:
        return False, "bundle not built"
    if rec["error"]:
        return False, rec["error"]
    if not rec["key"]:
        return False, "CFBundleIconFile is empty or absent"
    if not rec["icns_path"]:
        return False, f"CFBundleIconFile={rec['key']!r} names no file in Contents/Resources"
    if not rec["icns_magic_ok"]:
        return False, f"{os.path.basename(rec['icns_path'])} is not an icns file"
    return True, ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build-dir", default="build",
                    help="build tree holding the four bundles")
    ap.add_argument("--plant", choices=["strip-key", "strip-icns"],
                    help="pretend a plugin bundle lost its icon, to prove "
                         "this check fails rather than passing vacuously")
    args = ap.parse_args()

    records = []
    for sub, name in BUNDLES:
        path = os.path.join(args.build_dir, sub, name) if sub else \
            os.path.join(args.build_dir, name)
        records.append(inspect(path))

    # The control mutates the READING, not the bundle on disk: planting a
    # real defect would mean writing into a build tree this check is
    # supposed to observe, and a failed restore would leave a broken
    # artifact behind. Degrading the record exercises the same verdict.
    if args.plant:
        target = next((r for r in records if r["path"].endswith(
            (".vst3", ".component", ".clap")) and r["exists"]), None)
        if target is None:
            print("INCONCLUSIVE: no plugin bundle built to plant against",
                  file=sys.stderr)
            return 3
        if args.plant == "strip-key":
            target["key"] = ""
        else:
            target["icns_path"] = None
            target["icns_magic_ok"] = False
        print(f"CONTROL: planted {args.plant} on {target['path']}")

    built = [r for r in records if r["exists"]]
    if not built:
        print(f"INCONCLUSIVE: no bundles under {args.build_dir!r} — nothing "
              f"to judge, NOT a pass")
        return 3

    print(f"build_dir={args.build_dir}  bundles_expected={len(BUNDLES)}  "
          f"bundles_found={len(built)}")

    failures = []
    for rec in records:
        ok, why = verdict(rec)
        label = "ok  " if ok else "FAIL"
        size = rec["icns_size"]
        print(f"  {label} {os.path.basename(rec['path']):<20} "
              f"CFBundleIconFile={rec['key'] or '<empty>'!s:<14} "
              f"icns={'-' if size is None else str(size) + 'B'} "
              f"sha={rec['sha'] or '-'}")
        if not ok:
            failures.append((rec["path"], why))

    # Every bundle must ship the SAME asset. A per-format divergence means
    # one of them is stale, which a per-bundle check alone would pass.
    shas = {r["sha"] for r in records if r["sha"]}
    if len(shas) > 1:
        failures.append((args.build_dir,
                         f"bundles carry {len(shas)} different icns files"))

    if len(built) != len(BUNDLES):
        missing = [os.path.basename(r["path"]) for r in records
                   if not r["exists"]]
        print(f"INCONCLUSIVE: {len(missing)} of {len(BUNDLES)} bundles were "
              f"not built ({', '.join(missing)}) — the rest cannot stand in "
              f"for them, NOT a pass")
        if args.plant:
            print("BROKEN: the plant did not fail the check", file=sys.stderr)
            return 4
        return 3

    if failures:
        for path, why in failures:
            print(f"  RED  {os.path.basename(path)}: {why}")
        print("RED    a shipped bundle is missing its product icon")
        return 0 if args.plant else 1

    print("GREEN  all 4 bundles declare CFBundleIconFile and ship the icns")
    if args.plant:
        print("BROKEN: the plant did not fail the check", file=sys.stderr)
        return 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
