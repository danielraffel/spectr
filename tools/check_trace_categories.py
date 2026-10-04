#!/usr/bin/env python3
"""Every PULP_TRACE_* category Spectr names must be one the Pulp SDK declares.

Perfetto checks a trace macro's category at compile time, but only in a
tracing-enabled build (PULP_TRACING=ON). In the shipping build the macros are
no-ops, so a category the SDK never declared compiles, ships, and then breaks
the first traced build anyone makes -- which is when a trace is needed. This
reads the declared list from the SDK's own pulp/runtime/trace.hpp (the
PERFETTO_DEFINE_CATEGORIES block) and every literal category in Spectr's
sources, and fails on any category that is not declared.

  check_trace_categories.py --trace-header <sdk>/include/pulp/runtime/trace.hpp
  check_trace_categories.py --self-test   # the check must catch a planted category
"""
from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = ("src", "include", "tools", "test")
SOURCE_FILES = ("au_v2_entry.cpp", "vst3_entry.cpp", "clap_entry.cpp", "main.cpp", "native_main.cpp")
SUFFIXES = {".cpp", ".hpp", ".h", ".mm", ".cc"}

DECLARED = re.compile(r'perfetto::Category\(\s*"([^"]+)"\s*\)')
# The category is the macro's first argument; it may sit on the next line.
USED = re.compile(r'\b(PULP_TRACE_[A-Z_]+)\s*\(\s*"([^"]*)"')
# Macro definitions and the SDK's own wrappers are not uses.
DEFINE = re.compile(r'^\s*#\s*define\b')


def declared_categories(header: Path) -> set[str]:
    names = set(DECLARED.findall(header.read_text(encoding="utf-8", errors="replace")))
    if not names:
        raise SystemExit(f"check_trace_categories: no perfetto::Category(...) in {header}; "
                         "the SDK header changed shape, so this check can no longer see "
                         "the declared list")
    return names


def used_categories(root: Path) -> list[tuple[Path, int, str, str]]:
    files: list[Path] = []
    for name in SOURCE_DIRS:
        base = root / name
        if base.is_dir():
            files += [p for p in base.rglob("*") if p.suffix in SUFFIXES and p.is_file()]
    files += [root / f for f in SOURCE_FILES if (root / f).is_file()]
    uses = []
    for path in sorted(files):
        text = path.read_text(encoding="utf-8", errors="replace")
        lines = text.splitlines()
        for match in USED.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            if DEFINE.match(lines[line - 1]):
                continue
            uses.append((path, line, match.group(1), match.group(2)))
    return uses


def check(root: Path, header: Path) -> list[str]:
    declared = declared_categories(header)
    uses = used_categories(root)
    if not uses:
        return [f"no PULP_TRACE_* uses found under {root}; the scan is looking in the wrong place"]
    return [f"{path.relative_to(root)}:{line}: {macro}(\"{category}\") -- not a Pulp trace "
            f"category (declared: {', '.join(sorted(declared))})"
            for path, line, macro, category in uses if category not in declared]


def self_test(header: Path) -> int:
    clean = check(ROOT, header)
    if clean:
        print("self-test: the tree itself fails, fix that first:\n  " + "\n  ".join(clean))
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        planted = Path(tmp)
        (planted / "src").mkdir()
        (planted / "src" / "planted.cpp").write_text(
            'void f() {\n    PULP_TRACE_SCOPE_NAMED(\n        "audio", "planted");\n'
            '    PULP_TRACE_SCOPE_NAMED("dsp", "fine");\n}\n')
        errors = check(planted, header)
        if len(errors) != 1 or '"audio"' not in errors[0]:
            print(f"self-test: a planted undeclared category was not caught exactly once: {errors}")
            return 1
    print("self-test: an undeclared category is caught, the tree is clean")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--trace-header", type=Path, required=True)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not args.trace_header.is_file():
        print(f"check_trace_categories: {args.trace_header} does not exist")
        return 2
    if args.self_test:
        return self_test(args.trace_header)
    errors = check(ROOT, args.trace_header)
    for error in errors:
        print("FAIL " + error)
    if errors:
        return 1
    print(f"check_trace_categories: {len(used_categories(ROOT))} trace macro uses, "
          "every category declared by the SDK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
