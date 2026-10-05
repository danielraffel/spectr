#!/usr/bin/env python3
"""Prove which SPECTR_* test seams a set of built binaries can read.

Spectr's test seams are environment variables read through SPECTR_TEST_ENV
(include/spectr/test_seams.hpp). A release build compiles every one of them
out, so neither the read nor the variable's name reaches the binary. This
check takes the seam names from the source itself -- every
SPECTR_TEST_ENV("NAME") under src/, include/ and the entry files -- and looks
for each name in the given binaries' bytes.

  --expect absent   (release) exit 1 if any binary carries any seam name
  --expect present  (test build, the positive control) exit 1 unless every
                    binary carries at least one seam name

Both modes also fail (exit 2) when the source names no seam, a binary is
missing or empty, or a raw std::getenv("SPECTR_...") outside the macro is
found in product source: an instrument that measures nothing is not a pass.

A bundle path (.app, .component, .vst3, .clap) expands to every Mach-O file
under its Contents/MacOS and Contents/Frameworks.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ("src", "include")
ENTRY_FILES = ("native_main.cpp", "main.cpp", "au_v2_entry.cpp", "vst3_entry.cpp",
               "clap_entry.cpp")
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".mm", ".m"}
SEAM_RE = re.compile(r'SPECTR_TEST_ENV\("(SPECTR_[A-Z0-9_]+)"\)')
RAW_RE = re.compile(r'getenv\("SPECTR_[A-Z0-9_]+"\)')
MACHO_MAGICS = {b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe",
                b"\xbe\xba\xfe\xca"}


def source_files(root: Path):
    for d in SOURCE_DIRS:
        for p in sorted((root / d).rglob("*")):
            if p.suffix in SOURCE_SUFFIXES and p.is_file():
                yield p
    for name in ENTRY_FILES:
        p = root / name
        if p.is_file():
            yield p


def seam_names(root: Path) -> tuple[list[str], list[str]]:
    names: set[str] = set()
    raw: list[str] = []
    for p in source_files(root):
        text = p.read_text(errors="replace")
        names.update(SEAM_RE.findall(text))
        for i, line in enumerate(text.splitlines(), 1):
            if RAW_RE.search(line) and not line.lstrip().startswith("//"):
                raw.append(f"{p.relative_to(root)}:{i}: {line.strip()}")
    return sorted(names), raw


def expand(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    out: list[Path] = []
    for sub in ("Contents/MacOS", "Contents/Frameworks"):
        base = path / sub
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if p.is_file() and not p.is_symlink():
                with p.open("rb") as f:
                    if f.read(4) in MACHO_MAGICS:
                        out.append(p)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--expect", choices=("absent", "present"), required=True)
    ap.add_argument("--source-root", type=Path, default=ROOT)
    ap.add_argument("paths", nargs="+", type=Path,
                    help="binaries or bundles to inspect")
    args = ap.parse_args()

    names, raw = seam_names(args.source_root)
    if not names:
        print("FAIL: the source names no SPECTR_TEST_ENV seam; nothing to look for",
              file=sys.stderr)
        return 2
    if raw:
        print("FAIL: product source reads SPECTR_* variables outside SPECTR_TEST_ENV:",
              file=sys.stderr)
        for r in raw:
            print("  " + r, file=sys.stderr)
        return 2
    patterns = {n: re.compile(rb"(?<![A-Z0-9_])" + n.encode() + rb"(?![A-Z0-9_])")
                for n in names}

    groups: list[tuple[Path, list[Path]]] = []
    for p in args.paths:
        if not p.exists():
            print(f"FAIL: {p} does not exist", file=sys.stderr)
            return 2
        found = expand(p)
        if not found:
            print(f"FAIL: {p} holds no Mach-O binary", file=sys.stderr)
            return 2
        groups.append((p, found))

    print(f"{len(names)} seam names from {args.source_root}")
    failed = False
    for path, binaries in groups:
        path_hits: set[str] = set()
        for b in binaries:
            data = b.read_bytes()
            if not data:
                print(f"FAIL: {b} is empty", file=sys.stderr)
                return 2
            hits = [n for n, rx in patterns.items() if rx.search(data)]
            path_hits.update(hits)
            print(f"{len(hits):3d}/{len(names)}  {b}")
            if args.expect == "absent":
                for n in hits:
                    print(f"      carries {n}")
        if args.expect == "absent" and path_hits:
            failed = True
        if args.expect == "present" and not path_hits:
            print(f"      {path}: no seam name in any of its binaries")
            failed = True
    if failed:
        print("FAIL: " + ("a release binary carries test seams" if args.expect == "absent"
                          else "a test-seam build carries no seam (the check sees nothing)"),
              file=sys.stderr)
        return 1
    print("PASS: " + ("no binary carries a seam name" if args.expect == "absent"
                      else "every binary or bundle carries seam names"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
