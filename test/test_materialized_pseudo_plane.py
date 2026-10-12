#!/usr/bin/env python3
"""Fail-closed provenance and paint-order contract for body::before."""

from __future__ import annotations

import hashlib
import json
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "native-ui" / "materialized" / "pseudo-plane.provenance.json"
EXPECTED_SCHEMA = "spectr-materialized-pseudo-binding-v1"
EXPECTED_AUTHORED_LAYERS = [
    "radial-gradient(ellipse 80% 60% at 50% 20%, var(--glow-1), transparent 60%)",
    "radial-gradient(ellipse 60% 40% at 20% 80%, var(--glow-2), transparent 60%)",
    "var(--bg)",
]
EXPECTED_MATERIALIZED = {
    "node_id": "Pseudo_body_before",
    "parent_id": "root",
    "position": "absolute",
    "left": 0,
    "top": 0,
    "width": 1320,
    "height": 860,
    "background": "transparent",
    "background_gradient": "radial-gradient(80% 60% at 50% 20%, rgba(40, 90, 140, 0.08), rgba(0, 0, 0, 0) 60%), radial-gradient(60% 40% at 20% 80%, rgba(140, 40, 120, 0.05), rgba(0, 0, 0, 0) 60%), none",
    "opacity": 1,
    "z_index": 1,
    "pointer_events": "none",
    "compositing": "overlay-above-opaque-paint-authority",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fail(message: str) -> None:
    raise AssertionError(message)


def read_manifest(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read provenance manifest {path}: {error}")
    if value.get("schema") != EXPECTED_SCHEMA or value.get("version") != 1:
        fail("unsupported pseudo binding provenance schema")
    if not isinstance(value.get("source"), dict) or not isinstance(value.get("materialized"), dict):
        fail("pseudo binding provenance must contain source and materialized records")
    return value


def validate(manifest_path: Path, *, source_path: Path | None = None,
             design_path: Path | None = None, verify_design_hash: bool = True) -> None:
    manifest = read_manifest(manifest_path)
    source = manifest["source"]
    materialized = manifest["materialized"]
    source_path = source_path or ROOT / source["path"]
    design_path = design_path or ROOT / materialized["path"]

    if source.get("path") != "resources/editor.html":
        fail("pseudo source path drifted")
    if source.get("selector") != "body::before":
        fail("pseudo selector drifted")
    if sha256(source_path) != source.get("sha256"):
        fail("editor.html does not match the pseudo binding provenance")
    source_text = source_path.read_text(encoding="utf-8")
    selector_at = source_text.find("body::before")
    if selector_at < 0:
        fail("authored body::before declaration is missing")
    authored = source["authored"]
    source_window = source_text[selector_at:selector_at + 1000]
    for token in ("position: fixed", "inset: 0", "pointer-events: none",
                  "z-index: 0", "radial-gradient(ellipse 80% 60%",
                  "radial-gradient(ellipse 60% 40%"):
        if token not in source_window:
            fail(f"authored body::before is missing {token!r}")
    if authored.get("position") != "fixed" or authored.get("inset") != "0":
        fail("authored pseudo geometry drifted")
    if authored.get("pointer_events") != "none" or authored.get("z_index") != 0:
        fail("authored pseudo interaction or paint order drifted")
    if authored.get("background_layers") != EXPECTED_AUTHORED_LAYERS:
        fail("authored pseudo background layers drifted")

    if materialized.get("path") != "native-ui/materialized/design.js":
        fail("materialized pseudo path drifted")
    for key, expected in EXPECTED_MATERIALIZED.items():
        if materialized.get(key) != expected:
            fail(f"materialized pseudo {key} drifted")
    if verify_design_hash and sha256(design_path) != materialized.get("sha256"):
        fail("design.js does not match the pseudo binding provenance")
    design_text = design_path.read_text(encoding="utf-8")
    node_id = materialized.get("node_id")
    parent_id = materialized.get("parent_id")
    if design_text.count(f"createCol('{node_id}', '{parent_id}');") != 1:
        fail("pseudo plane must have exactly one generated node")
    expected_lines = {
        "position": f"setPosition('{node_id}', '{materialized['position']}');",
        "left": f"setLeft('{node_id}', {materialized['left']});",
        "top": f"setTop('{node_id}', {materialized['top']});",
        "width": f"setFlex('{node_id}', 'width', {materialized['width']});",
        "height": f"setFlex('{node_id}', 'height', {materialized['height']});",
        "background": f"setBackground('{node_id}', '{materialized['background']}');",
        "gradient": f"setBackgroundGradient('{node_id}', '{materialized['background_gradient']}');",
        "opacity": f"setOpacity('{node_id}', {materialized['opacity']});",
        "z_index": f"setZIndex('{node_id}', {materialized['z_index']});",
        "pointer_events": f"setPointerEvents('{node_id}', '{materialized['pointer_events']}');",
    }
    for name, line in expected_lines.items():
        if design_text.count(line) != 1:
            fail(f"generated pseudo plane {name} binding is missing or duplicated")
    authority_prefix = "setZIndex('Accepted_Chromium_chrome0',"
    if design_text.count(authority_prefix) != 1 or "setZIndex('Accepted_Chromium_chrome0', 0);" not in design_text:
        fail('opaque Chromium paint authority must have exactly one lower z-index setter')
    if materialized.get('z_index', 0) <= 0:
        fail('pseudo plane must be composited above the opaque paint authority')
    for node_id, z_index in (("Browser_canvas_11", 2), ("Browser_canvas_22", 3)):
        line = f"setZIndex('{node_id}', {z_index});"
        if design_text.count(line) != 1:
            fail(f'{node_id} must have an explicit layer above the pseudo plane')


def expect_rejected(mutated_manifest: Path, *, design_path: Path | None = None) -> None:
    try:
        # Skip only the immutable hash check so the planted mutation exercises
        # the actual static binding assertion rather than failing at provenance
        # bookkeeping first.
        validate(mutated_manifest, design_path=design_path, verify_design_hash=False)
    except AssertionError:
        return
    fail("planted pseudo-plane defect was accepted")


def main() -> int:
    validate(MANIFEST)
    with tempfile.TemporaryDirectory(prefix="spectr-pseudo-plane-") as temp:
        temp_path = Path(temp)
        mutated_design = temp_path / "design.js"
        design_text = (ROOT / "native-ui" / "materialized" / "design.js").read_text(encoding="utf-8")
        mutated_design.write_text(
            design_text.replace("setZIndex('Pseudo_body_before', 1);",
                                "setZIndex('Pseudo_body_before', 2);", 1),
            encoding="utf-8")
        expect_rejected(MANIFEST, design_path=mutated_design)

        duplicate_design = temp_path / "design-duplicate.js"
        duplicate_design.write_text(
            design_text.replace("createCol('Pseudo_body_before', 'root');",
                                "createCol('Pseudo_body_before', 'root');\n  createCol('Pseudo_body_before', 'root');", 1),
            encoding="utf-8")
        expect_rejected(MANIFEST, design_path=duplicate_design)

        authority_override = temp_path / "design-authority-override.js"
        authority_override.write_text(
            design_text + "\nsetZIndex('Accepted_Chromium_chrome0', 5);\n",
            encoding="utf-8",
        )
        expect_rejected(MANIFEST, design_path=authority_override)

        mutated_manifest = temp_path / "manifest-mutated.json"
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        manifest["materialized"]["z_index"] = 3
        mutated_manifest.write_text(json.dumps(manifest), encoding="utf-8")
        expect_rejected(mutated_manifest)
    print("PASS: body::before pseudo plane provenance and negative controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
