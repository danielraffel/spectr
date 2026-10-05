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
MANIFEST_SCHEMA = "spectr-owned-component-slices-v1"
MANIFEST_KEYS = frozenset({
    "schema", "artifact_sha256", "source_sha256", "source_bytes",
    "component_count", "components",
})
COMPONENT_KEYS = frozenset({"name", "start", "end", "bytes", "sha256"})


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
    # Regex offsets are positions in the decoded Python string, while the
    # owned modules and their hashes are byte slices.  Build the explicit
    # UTF-8 boundary map once so a non-ASCII character before a component
    # cannot shift the emitted module into the preceding declaration.
    char_to_byte = [0]
    for char in text:
        char_to_byte.append(char_to_byte[-1] + len(char.encode("utf-8")))
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
        start_byte = char_to_byte[match.start()]
        end_byte = char_to_byte[end]
        raw = source[start_byte:end_byte]
        out.append({
            "name": name,
            "start": start_byte,
            "end": end_byte,
            "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "source": raw,
        })
    names = [str(item["name"]) for item in out]
    if len(names) != len(set(names)):
        raise ValueError("duplicate component function name")
    return out


def _remove_stale_component_outputs(modules: pathlib.Path,
                                    expected_names: set[str]) -> None:
    """Remove only stale component files from the generated components dir.

    The extractor owns ``*.tsx`` files in this directory.  Other files are
    intentionally left alone so a local note or marker cannot be destroyed by
    a refresh.  Symlinks are unlinked as directory entries; their targets are
    never followed or removed.  A directory named ``*.tsx`` is rejected rather
    than recursively deleted.
    """
    for path in modules.iterdir():
        if not path.name.endswith(".tsx") or path.name in expected_names:
            continue
        if path.is_dir() and not path.is_symlink():
            raise ValueError(
                f"refusing to remove stale component directory: {path.name}")
        path.unlink()


def _manifest_shape(manifest: object) -> dict[str, object]:
    """Validate the v1 manifest envelope before any component is trusted."""
    if not isinstance(manifest, dict):
        raise ValueError("component manifest must be an object")
    if set(manifest) != MANIFEST_KEYS:
        missing = sorted(MANIFEST_KEYS - set(manifest))
        extra = sorted(set(manifest) - MANIFEST_KEYS)
        details = []
        if missing:
            details.append("missing " + ", ".join(missing))
        if extra:
            details.append("unexpected " + ", ".join(extra))
        raise ValueError("component manifest keys changed: " + "; ".join(details))
    if manifest["schema"] != MANIFEST_SCHEMA:
        raise ValueError(f"unexpected component manifest schema: {manifest['schema']!r}")
    for key in ("artifact_sha256", "source_sha256"):
        value = manifest[key]
        if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
            raise ValueError(f"invalid component manifest {key}")
    for key in ("source_bytes", "component_count"):
        value = manifest[key]
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"invalid component manifest {key}")
    components = manifest["components"]
    if not isinstance(components, list):
        raise ValueError("component manifest entries must be a list")
    names: set[str] = set()
    for entry in components:
        if not isinstance(entry, dict):
            raise ValueError("component manifest entry must be an object")
        if set(entry) != COMPONENT_KEYS:
            missing = sorted(COMPONENT_KEYS - set(entry))
            extra = sorted(set(entry) - COMPONENT_KEYS)
            details = []
            if missing:
                details.append("missing " + ", ".join(missing))
            if extra:
                details.append("unexpected " + ", ".join(extra))
            raise ValueError("component manifest entry keys changed: "
                             + "; ".join(details))
        name = entry["name"]
        if not isinstance(name, str) or not name:
            raise ValueError("invalid component manifest name")
        if name in names:
            raise ValueError(f"duplicate component manifest entry: {name}")
        names.add(name)
        for key in ("start", "end", "bytes"):
            value = entry[key]
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                raise ValueError(f"invalid component manifest {key}: {name}")
        digest = entry["sha256"]
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"invalid component manifest sha256: {name}")
    return manifest


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
    if modules.is_symlink():
        raise ValueError("refusing to write through symlinked components directory")
    modules.mkdir(parents=True, exist_ok=True)
    manifest_components: list[dict[str, object]] = []
    for item in components:
        name = str(item["name"])
        path = modules / f"{name}.tsx"
        raw = bytes(item["source"])
        if path.is_symlink():
            # Remove only the link itself so a pre-existing link cannot cause
            # write_bytes() to modify a file outside the generated directory.
            path.unlink()
        elif path.exists() and not path.is_file():
            raise ValueError(f"refusing to replace non-file component: {name}")
        path.write_bytes(raw)
        manifest_components.append({k: item[k] for k in ("name", "start", "end", "bytes", "sha256")})

    manifest = {
        "schema": MANIFEST_SCHEMA,
        "artifact_sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "source_bytes": len(source),
        "component_count": len(manifest_components),
        "components": manifest_components,
    }
    manifest_path = output_dir / "owned-components.manifest.json"
    # Re-read every module to make the ownership mapping fail closed.
    for item in manifest_components:
        raw = (modules / f"{item['name']}.tsx").read_bytes()
        if len(raw) != item["bytes"] or hashlib.sha256(raw).hexdigest() != item["sha256"]:
            raise AssertionError(f"component module changed while writing: {item['name']}")
    _remove_stale_component_outputs(
        modules, {f"{item['name']}.tsx" for item in manifest_components})
    if manifest_path.is_symlink():
        raise ValueError("refusing to write through symlinked component manifest")
    if manifest_path.exists() and not manifest_path.is_file():
        raise ValueError("refusing to replace non-file component manifest")
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")
    return {"manifest": str(manifest_path), "component_count": len(manifest_components), "source_sha256": manifest["source_sha256"]}


def verify(artifact: pathlib.Path, output_dir: pathlib.Path) -> dict[str, object]:
    """Fail closed if the generated modules drift from the frozen artifact."""
    manifest = _manifest_shape(json.loads(
        (output_dir / "owned-components.manifest.json").read_bytes()))
    artifact_bytes = artifact.read_bytes()
    if hashlib.sha256(artifact_bytes).hexdigest() != manifest.get("artifact_sha256"):
        raise ValueError("artifact digest changed; refusing component verification")
    html = json.loads(artifact_bytes).get("html")
    if not isinstance(html, str):
        raise ValueError("artifact html must be a string")
    source = html.encode("utf-8")
    if hashlib.sha256(source).hexdigest() != manifest.get("source_sha256"):
        raise ValueError("source digest changed; refusing component verification")
    if manifest["source_bytes"] != len(source):
        raise ValueError("source byte count changed; refusing component verification")
    expected = component_slices(source)
    entries = manifest["components"]
    if manifest["component_count"] != len(expected) or len(entries) != len(expected):
        raise ValueError("component count changed; refusing component verification")
    modules = output_dir / "components"
    if modules.is_symlink() or not modules.is_dir():
        raise ValueError("component output directory changed; refusing component verification")
    expected_module_names = {f"{item['name']}.tsx" for item in expected}
    for path in modules.iterdir():
        if path.name.endswith(".tsx") and path.name not in expected_module_names:
            raise ValueError(f"unexpected component module: {path.name}")
    for item, frozen in zip(expected, entries):
        fields = {k: item[k] for k in ("name", "start", "end", "bytes", "sha256")}
        if fields != frozen:
            raise ValueError(f"component boundary changed: {item['name']}")
        module = modules / f"{item['name']}.tsx"
        if not module.is_file() or module.read_bytes() != item["source"]:
            raise ValueError(f"component module changed: {item['name']}")
    return {"verified": True, "component_count": len(expected), "source_sha256": manifest["source_sha256"]}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=pathlib.Path)
    parser.add_argument("output_dir", type=pathlib.Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)
    action = verify if args.verify else extract
    print(json.dumps(action(args.artifact, args.output_dir), sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
