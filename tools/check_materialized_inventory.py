#!/usr/bin/env python3
"""Fail-closed inventory for Spectr's shipping materialized editor artifact.

This is an extraction-freeze prerequisite: it does not rewrite the artifact or
claim that resources/editor.html is its source.  It records the exact shipping
payload and the structural facts needed by a future owned-source extractor.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
import tempfile

REQUIRED_KEYS = {
    "schema", "version", "html", "mime_type", "assets", "font_bindings",
    "surface_style", "coordinate_space", "presentation_time_ms",
    "canvas_bindings", "semantic_bindings", "layout_bindings", "text_bindings",
    "paint_bindings", "runtime_canonicalization",
}


def inspect(path: pathlib.Path) -> dict[str, object]:
    raw = path.read_bytes()
    doc = json.loads(raw)
    missing = sorted(REQUIRED_KEYS - doc.keys())
    if missing:
        raise ValueError(f"missing required keys: {', '.join(missing)}")
    if doc["schema"] != "pulp-materialized-browser-document-v1":
        raise ValueError(f"unexpected schema: {doc['schema']!r}")
    if not isinstance(doc["html"], str) or not doc["html"]:
        raise ValueError("html payload must be a non-empty string")
    # The checked-in artifact is one logical line with one conventional final
    # newline. Escaped newlines inside the JSON string are expected.
    if raw.count(b"\n") != 1 or not raw.endswith(b"\n") or b"\r" in raw:
        raise ValueError("runtime artifact must remain compact (one logical line)")
    return {
        "path": str(path),
        "bytes": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "schema": doc["schema"],
        "version": doc["version"],
        "html_bytes": len(doc["html"].encode()),
        "html_components": doc["html"].count("function Spectr"),
        "binding_counts": {
            k: len(doc[k]) for k in ("canvas_bindings", "semantic_bindings",
                                     "layout_bindings", "text_bindings", "paint_bindings")
        },
        "runtime_canonicalization": doc["runtime_canonicalization"],
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=pathlib.Path)
    parser.add_argument("--negative-control", action="store_true",
                        help="mutate a temporary copy and require validation to fail")
    args = parser.parse_args(argv)
    report = inspect(args.artifact)
    if args.negative_control:
        with tempfile.TemporaryDirectory(prefix="spectr-artifact-negative-") as td:
            bad = pathlib.Path(td) / args.artifact.name
            bad.write_bytes(args.artifact.read_bytes().replace(
                b'"schema":"pulp-materialized-browser-document-v1"',
                b'"schema":"tampered"', 1))
            try:
                inspect(bad)
            except ValueError:
                report["negative_control"] = "passed"
            else:
                raise SystemExit("negative control failed: tampered schema accepted")
    print(json.dumps(report, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
