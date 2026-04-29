#!/usr/bin/env python3
"""regen-asset.py — fast replacement for pulp_add_binary_data's hex
conversion of native-react/dist/editor.js + resources/editor.html into
build/spectr_editor_assets_data.cpp.

The SDK function does this inside a CMake while-loop on a 1MB byte
string, which is O(n²) and takes 10–15 minutes per reconfigure. This
script does the same thing in <1 second so the iterate-fix cycle for
spectr#28 stays usable.

Usage:
    python3 regen-asset.py [build_dir]

Default build_dir: ../build (spectr's standard layout). Writes:
    <build_dir>/spectr_editor_assets_data.cpp
    <build_dir>/spectr_editor_assets_data.hpp

Then run `cmake --build <build_dir> --target Spectr_Standalone` — the
existing build rule will pick the regenerated cpp up because its
timestamp is now newer than the .o file.
"""
from __future__ import annotations
import os
import sys
from pathlib import Path

NAMESPACE = "spectr_editor"
ASSET_TARGET = "spectr_editor_assets"


def c_identifier(name: str) -> str:
    out = []
    for ch in name:
        out.append(ch if ch.isalnum() else "_")
    if out and out[0].isdigit():
        out.insert(0, "_")
    return "".join(out)


def emit_array(name: str, data: bytes) -> str:
    chunks = []
    for i in range(0, len(data), 16):
        line = ", ".join(f"0x{b:02x}" for b in data[i:i + 16])
        chunks.append(f"    {line}")
    return ",\n".join(chunks)


def main() -> int:
    here = Path(__file__).resolve().parent
    spectr = here.parent
    build_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else spectr / "build"
    if not build_dir.exists():
        print(f"build dir not found: {build_dir}", file=sys.stderr)
        return 1

    sources = [
        spectr / "resources" / "editor.html",
        spectr / "native-react" / "dist" / "editor.js",
    ]
    missing = [p for p in sources if not p.exists()]
    if missing:
        print("missing source(s): " + ", ".join(str(m) for m in missing), file=sys.stderr)
        return 1

    cpp_lines = [f'#include "{ASSET_TARGET}_data.hpp"\n']
    hpp_lines = ["#pragma once\n#include <cstddef>\n", f"\nnamespace {NAMESPACE} {{\n"]

    total_bytes = 0
    for src in sources:
        var = c_identifier(src.name)
        data = src.read_bytes()
        total_bytes += len(data)
        hpp_lines.append(f"\nextern const unsigned char {var}[];")
        hpp_lines.append(f"\nextern const size_t {var}_size;")
        cpp_lines.append(f"\nnamespace {NAMESPACE} {{")
        cpp_lines.append(f"\nconst unsigned char {var}[] = {{")
        cpp_lines.append("\n" + emit_array(var, data))
        cpp_lines.append("\n};")
        cpp_lines.append(f"\nconst size_t {var}_size = {len(data)};")
        cpp_lines.append(f"\n}} // namespace {NAMESPACE}\n")

    hpp_lines.append(f"\n\n}} // namespace {NAMESPACE}\n")

    cpp_path = build_dir / f"{ASSET_TARGET}_data.cpp"
    hpp_path = build_dir / f"{ASSET_TARGET}_data.hpp"
    cpp_path.write_text("".join(cpp_lines))
    hpp_path.write_text("".join(hpp_lines))

    print(f"wrote {cpp_path} ({cpp_path.stat().st_size:,} bytes, {total_bytes:,} embedded)")
    print(f"wrote {hpp_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
