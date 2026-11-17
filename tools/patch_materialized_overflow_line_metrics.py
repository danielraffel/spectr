#!/usr/bin/env python3
"""Preserve the overflow glyph's browser-measured primary-font metrics.

The native-only optical correction replaces the captured text binding. Its
13px run rect belongs to the JetBrains Mono cascade, with measured ascent 10
and descent 3, even though Menlo supplies U+22EF. Supplying only Menlo's face
makes SDK 1.149 infer descent 2 and paint 1px low. Pass the measured metrics
through the existing setCapturedLineBoxes optional arguments instead.

Reproduce the independent browser measurement with:
node tools/materialized_runtime_chromium_oracle.mjs --artifact
native-ui/materialized/materialized-document.runtime.json --fixture
test/fixtures/spectr-parity-analyzer-v1.json --out <empty-directory>
--dpr 2 --toolbar-baseline

The native N1 and frozen-atlas optical-centering canaries verify the resulting
16.5px baseline including the existing +0.5px product optical correction and
frame-grid snap. No framework code or official SDK bytes are modified.
"""
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/runtime.js"
OLD = '      { text: "⋯", basisWidth: 27.03125, face: "Menlo-Regular",\n        box: { left: 10, top: 6, width: 7.03125, height: 13,\n               start: 0, length: 1 } }\n    ];\n'
NEW = '      // Chromium\'s run rect uses the primary JetBrains Mono metrics even\n      // though Menlo supplies this glyph. Preserve that measured 10/3 split;\n      // deriving the descent from Menlo alone paints the ellipsis 1px low.\n      { text: "⋯", basisWidth: 27.03125, face: "Menlo-Regular",\n        ascent: 10, descent: 3,\n        box: { left: 10, top: 6, width: 7.03125, height: 13,\n               start: 0, length: 1 } }\n    ];\n'


def patch(text):
    if text.count(NEW) == 1:
        return text
    if text.count(OLD) != 1:
        raise ValueError("overflow metrics patch point missing or ambiguous")
    text = text.replace(OLD, NEW, 1)
    start = text.index("    const textButtonReceipt = [];")
    end = text.index("    g5.__spectrToolbarTextButtonCenteringReceipt__", start)
    block = text[start:end]
    if block.count("correction.face, false") != 3:
        raise ValueError("expected all three native text target paths")
    block = block.replace("correction.face, false", "correction.face, false, correction.ascent, correction.descent")
    return text[:start] + block + text[end:]


if __name__ == "__main__":
    original = PATH.read_text()
    updated = patch(original)
    if updated != original:
        PATH.write_text(updated)
        print("applied overflow primary-font metrics")
    else:
        print("already applied overflow primary-font metrics")
