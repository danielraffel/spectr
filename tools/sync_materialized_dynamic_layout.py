#!/usr/bin/env python3
"""Inherit Pulp's dynamic-state geometry fix into the retained runtime bundle.

The full bundle also carries product adapters. Apply the shared importer change
through unique anchors so those adapters and positional bindings stay intact.
"""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pulp-source", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    args = parser.parse_args()
    source = subprocess.check_output([
        "git", "-C", str(args.pulp_source), "show",
        args.revision + ":tools/import-design/jsx-runtime/materialized_dynamic_layout.mjs",
    ], text=True)
    assert source.count("export function ") == 2
    helper = source.replace("export function ", "function ")
    path = Path(__file__).resolve().parents[1] / "native-ui/materialized/runtime.js"
    text = path.read_text()
    marker = "  // Shared Pulp dynamic-state geometry, source revision "
    end = "  function applyMaterializedImportMetadata(metadata) {"
    if marker in text:
        assert text.count(marker) == 1 and text.count(end) == 1
        start = text.index(marker)
        finish = text.index(end, start)
        text = text[:start] + text[finish:]
    edits = [
        ("shared geometry helpers", "  function applyMaterializedImportMetadata(metadata) {",
         "  // Shared Pulp dynamic-state geometry, source revision " + args.revision + "\n"
         + helper + "\n  function applyMaterializedImportMetadata(metadata) {"),
        ("authored initial props", "          shim.__pulpId = id;",
         "          shim.__pulpId = id;\n          shim.__pulpAuthoredLayout__ = normalizedProps;"),
        ("authored updated props", "        const dom = instance._dom;\n        const committedText",
         "        const dom = instance._dom;\n        dom.__pulpAuthoredLayout__ = newN;\n        const committedText"),
        ("state geometry ownership", "  var activeMaterializedMetadata = capturedHomeMetadata;",
         "  var activeMaterializedMetadata = capturedHomeMetadata;\n"
         "  var activeMaterializedMatch = null;\n  var capturedGeometryNodes = new WeakSet();"),
        ("state match", "      activeMaterializedMetadata = state && state.metadata ? state.metadata : capturedHomeMetadata;",
         "      activeMaterializedMetadata = state && state.metadata ? state.metadata : capturedHomeMetadata;\n"
         "      activeMaterializedMatch = state && state.match || null;"),
        ("dynamic scope", "    let applied = 0;\n    const diagnostics = {",
         """    const scope = activeMaterializedMatch
      ? g5.__pulpFindMaterializedElement__(activeMaterializedMatch.selector,
          activeMaterializedMatch.ancestor) : null;
    const dynamicNodes = materializedDynamicLayoutScope(scope, activeLayoutBindings,
      values, pathIndex, (binding, nodes, index) =>
        materializedNodeAtPath(binding, nodes, true, index),
      materializedElementChildren, materializedNodeTag);
    for (const node of dynamicNodes) {
      if (!capturedGeometryNodes.has(node)) continue;
      restoreMaterializedLayout(node, g5);
      capturedGeometryNodes.delete(node);
    }
    let applied = 0;
    const diagnostics = {"""),
        ("geometry diagnostics", "      layout_node_miss: 0,",
         "      layout_node_miss: 0,\n      layout_dynamic_nodes: dynamicNodes.size,"),
        ("layout skip", "        const node = materializedNodeAtPath(binding, values, true, pathIndex);\n        const id = node",
         "        const node = materializedNodeAtPath(binding, values, true, pathIndex);\n        if (dynamicNodes.has(node)) continue;\n        const id = node"),
        ("geometry tracking", '        g5.setFlex(String(id), "height", binding.box.height);',
         '        g5.setFlex(String(id), "height", binding.box.height);\n        capturedGeometryNodes.add(node);'),
        ("paint skip", "      const node = materializedNodeAtPath(binding, values, true, pathIndex);\n      const id = node",
         "      const node = materializedNodeAtPath(binding, values, true, pathIndex);\n      if (dynamicNodes.has(node)) continue;\n      const id = node"),
        ("text skip", "      const node = materializedNodeAtPath(binding, values, true, pathIndex) || (optional ? materializedOptionalTextNode(binding, values) : null);",
         "      const node = materializedNodeAtPath(binding, values, true, pathIndex) || (optional ? materializedOptionalTextNode(binding, values) : null);\n      if (dynamicNodes.has(node)) continue;"),
        ("text geometry tracking", "        binding.basis.resolved_face,\n        false\n      );\n      ++applied;",
         "        binding.basis.resolved_face,\n        false\n      );\n      capturedGeometryNodes.add(node);\n      ++applied;"),
    ]
    for label, old, new in edits:
        if text.count(new) == 1:
            print("already applied:", label)
            continue
        if text.count(old) != 1:
            raise SystemExit(f"{label}: expected one anchor, found {text.count(old)}")
        text = text.replace(old, new, 1)
        print("applied:", label)
    for label, _, new in edits:
        if text.count(new) != 1:
            raise SystemExit(f"{label}: final identity is ambiguous")
    path.write_text(text)


if __name__ == "__main__":
    main()
