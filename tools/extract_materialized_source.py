#!/usr/bin/env python3
"""Conservative first step of Spectr's owned-source extraction.

The extractor copies the shipping HTML payload byte-for-byte and emits a
deterministic structural manifest. It deliberately refuses createElement calls
whose first argument is not a static tag string or a component identifier;
those shapes need an explicit parser rule before extraction can claim support.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys

CREATE = re.compile(r"React\.createElement\s*\(\s*(?:(?:/\*.*?\*/|//[^\n]*?)\s*)*(?:([\"']).*?\1|([A-Za-z_$][\w$]*))", re.S)


def extract(artifact: pathlib.Path, output_dir: pathlib.Path | None = None) -> dict[str, object]:
    doc = json.loads(artifact.read_bytes())
    html = doc.get("html")
    if not isinstance(html, str) or not html:
        raise ValueError("artifact html must be a non-empty string")
    occurrences = html.count("React.createElement")
    matches = list(CREATE.finditer(html))
    if len(matches) != occurrences:
        raise ValueError(f"unsupported createElement shape: parsed {len(matches)} of {occurrences}")
    components = sum(m.group(2) is not None for m in matches)
    tags = sum(m.group(1) is not None for m in matches)
    payload = html.encode()
    result: dict[str, object] = {
        "artifact": str(artifact),
        "html_bytes": len(payload),
        "html_sha256": hashlib.sha256(payload).hexdigest(),
        "create_element_calls": occurrences,
        "static_tag_calls": tags,
        "component_identifier_calls": components,
        "unsupported_shapes": 0,
        "roundtrip": "byte-exact",
    }
    if output_dir is not None:
        output_dir.mkdir(parents=True, exist_ok=True)
        source = output_dir / "editor.owned.js"
        manifest = output_dir / "editor.owned.manifest.json"
        source.write_bytes(payload)
        manifest.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
        if source.read_bytes() != payload:
            raise AssertionError("owned-source extraction changed payload bytes")
        result["source"] = str(source)
        result["manifest"] = str(manifest)
    return result


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=pathlib.Path)
    parser.add_argument("--output-dir", type=pathlib.Path)
    args = parser.parse_args(argv)
    print(json.dumps(extract(args.artifact, args.output_dir), sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
