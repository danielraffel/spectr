#!/usr/bin/env python3
"""Rebuild Spectr's materialized runtime from an owned JS payload.

The emitter is deliberately small and deterministic: metadata is captured from
one checked-in runtime artifact, while the HTML/JS payload is the owned source.
A source digest and structural React.createElement sequence are frozen in the
manifest so key swaps, dropped effect dependencies, and sibling reorders fail
closed instead of silently changing the imported UI.
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, re, sys

CREATE = re.compile(r"React\.createElement\s*\(\s*(?:(?:/\*.*?\*/|//[^\n]*?)\s*)*(?:([\"']).*?\1|([A-Za-z_$][\w$]*))", re.S)

def digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()

def sequence(source: bytes) -> list[str]:
    text = source.decode()
    return [m.group(1) or m.group(2) for m in CREATE.finditer(text)]

def freeze(artifact: pathlib.Path, out: pathlib.Path) -> dict[str, object]:
    doc = json.loads(artifact.read_bytes())
    html = doc.get("html")
    if not isinstance(html, str) or not html:
        raise ValueError("artifact html must be a non-empty string")
    source = html.encode()
    seq = sequence(source)
    if len(seq) != html.count("React.createElement"):
        raise ValueError("unsupported createElement shape")
    out.mkdir(parents=True, exist_ok=True)
    source_path = out / "editor.owned.js"
    metadata_path = out / "editor.owned.metadata.json"
    manifest_path = out / "editor.owned.manifest.json"
    source_path.write_bytes(source)
    metadata = {k: v for k, v in doc.items() if k != "html"}
    metadata_path.write_text(json.dumps(metadata, ensure_ascii=False, separators=(",", ":")) + "\n")
    manifest = {
        "schema": "spectr-owned-materialized-runtime-v1",
        "source_sha256": digest(source),
        "source_bytes": len(source),
        "create_element_sequence_sha256": digest("\0".join(seq).encode()),
        "create_element_calls": len(seq),
        "metadata_sha256": digest(metadata_path.read_bytes()),
        "artifact_sha256": digest(artifact.read_bytes()),
    }
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")
    return manifest

def build(source_path: pathlib.Path, metadata_path: pathlib.Path, manifest_path: pathlib.Path, output: pathlib.Path) -> dict[str, object]:
    source = source_path.read_bytes()
    metadata_raw = metadata_path.read_bytes()
    metadata = json.loads(metadata_raw)
    manifest = json.loads(manifest_path.read_bytes())
    if digest(source) != manifest.get("source_sha256"):
        raise ValueError("owned source digest mismatch; refusing rebuild")
    seq = sequence(source)
    if len(seq) != manifest.get("create_element_calls"):
        raise ValueError("createElement call count changed; refusing rebuild")
    if digest("\0".join(seq).encode()) != manifest.get("create_element_sequence_sha256"):
        raise ValueError("createElement sequence changed; refusing rebuild")
    if digest(metadata_raw) != manifest.get("metadata_sha256"):
        raise ValueError("metadata digest mismatch; refusing rebuild")
    doc = dict(metadata)
    doc["html"] = source.decode()
    # Preserve the canonical artifact key order: metadata keys followed by html
    # is not sufficient for old artifacts, so reorder according to the original
    # insertion order recorded by JSON metadata (html belongs after version).
    ordered = {"schema": doc.pop("schema"), "version": doc.pop("version"), "html": doc.pop("html"), **doc}
    payload = (json.dumps(ordered, ensure_ascii=False, separators=(",", ":")) + "\n").encode()
    output.write_bytes(payload)
    return {"output": str(output), "bytes": len(payload), "sha256": digest(payload), "matches_frozen_artifact": digest(payload) == manifest.get("artifact_sha256")}

def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="command", required=True)
    f = sub.add_parser("freeze"); f.add_argument("artifact", type=pathlib.Path); f.add_argument("output_dir", type=pathlib.Path)
    b = sub.add_parser("build"); b.add_argument("source", type=pathlib.Path); b.add_argument("metadata", type=pathlib.Path); b.add_argument("manifest", type=pathlib.Path); b.add_argument("output", type=pathlib.Path)
    a = p.parse_args(argv)
    result = freeze(a.artifact, a.output_dir) if a.command == "freeze" else build(a.source, a.metadata, a.manifest, a.output)
    print(json.dumps(result, sort_keys=True, indent=2)); return 0
if __name__ == "__main__": raise SystemExit(main(sys.argv[1:]))
