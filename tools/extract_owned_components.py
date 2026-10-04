#!/usr/bin/env python3
"""Split the materialized editor into deterministic owned component modules.

This is the first source ownership step after the byte exact freeze.  The
shipping payload remains the canonical input; each module is an exact slice of
one top level React function from that payload.  Keeping the slices exact lets
the eventual TSX authoring pass add imports and types without losing a
reviewable mapping back to the frozen runtime.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys


FUNCTION = re.compile(r"function\s+([A-Za-z_$][\w$]*)\s*\([^)]*\)\s*\{")


def _function_end(text: str, body_start: int) -> int | None:
    """Return the exclusive end of a function body, respecting JS literals."""
    depth = 1
    quote: str | None = None
    escaped = False
    line_comment = False
    block_comment = False
    i = body_start
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if line_comment:
            if c == "\n":
                line_comment = False
        elif block_comment:
            if c == "*" and n == "/":
                block_comment = False
                i += 1
        elif quote:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == quote:
                quote = None
        elif c in "'\"`":
            quote = c
        elif c == "/" and n == "/":
            line_comment = True
            i += 1
        elif c == "/" and n == "*":
            block_comment = True
            i += 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return None


def component_slices(source: bytes) -> list[dict[str, object]]:
    text = source.decode("utf-8")
    out: list[dict[str, object]] = []
    for match in FUNCTION.finditer(text):
        name = match.group(1)
        # React component functions are the uppercase declarations.  Helpers
        # with lower-case names remain in the owned bundle until dependency
        # extraction has a resolver rather than being silently discarded.
        if not name[:1].isupper():
            continue
        end = _function_end(text, match.end())
        if end is None:
            raise ValueError(f"unterminated function {name}")
        body = text[match.end():end]
        if "React.createElement" not in body:
            continue
        raw = source[match.start():end]
        out.append({
            "name": name,
            "start": match.start(),
            "end": end,
            "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "source": raw,
        })
    names = [str(item["name"]) for item in out]
    if len(names) != len(set(names)):
        raise ValueError("duplicate component function name")
    return out


def extract(artifact: pathlib.Path, output_dir: pathlib.Path) -> dict[str, object]:
    doc = json.loads(artifact.read_bytes())
    html = doc.get("html")
    if not isinstance(html, str) or not html:
        raise ValueError("artifact html must be a non-empty string")
    source = html.encode("utf-8")
    components = component_slices(source)
    if len(components) != 63:
        raise ValueError(f"expected 63 component functions, found {len(components)}")

    modules = output_dir / "components"
    modules.mkdir(parents=True, exist_ok=True)
    manifest_components: list[dict[str, object]] = []
    for item in components:
        name = str(item["name"])
        path = modules / f"{name}.tsx"
        raw = bytes(item["source"])
        path.write_bytes(raw)
        manifest_components.append({k: item[k] for k in ("name", "start", "end", "bytes", "sha256")})

    manifest = {
        "schema": "spectr-owned-component-slices-v1",
        "artifact_sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "source_bytes": len(source),
        "component_count": len(manifest_components),
        "components": manifest_components,
    }
    manifest_path = output_dir / "owned-components.manifest.json"
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")
    # Re-read every module to make the ownership mapping fail closed.
    for item in manifest_components:
        raw = (modules / f"{item['name']}.tsx").read_bytes()
        if len(raw) != item["bytes"] or hashlib.sha256(raw).hexdigest() != item["sha256"]:
            raise AssertionError(f"component module changed while writing: {item['name']}")
    return {"manifest": str(manifest_path), "component_count": len(manifest_components), "source_sha256": manifest["source_sha256"]}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=pathlib.Path)
    parser.add_argument("output_dir", type=pathlib.Path)
    args = parser.parse_args(argv)
    print(json.dumps(extract(args.artifact, args.output_dir), sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
