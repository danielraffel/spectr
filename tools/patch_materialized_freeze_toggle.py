#!/usr/bin/env python3
"""Put the freeze toggle at the head of the output cluster and PEAK at its tail.

The header's output cluster read

    [PEAK -2.5]  OUTPUT  ---o---  0.0            (then LIVE / PRECISION)

and now reads

    [o LIVE]  OUTPUT  ---o---  0.0  [PEAK -2.5]

The freeze toggle takes the place PEAK held, nearest the title; PEAK moves to
the right of the trim's value, into the room the hidden LIVE / PRECISION
control left, at the SAME 14pt flex gap that separated PEAK from OUTPUT. The
OUTPUT label and the trim between them keep the two boxed buttons apart.

THE TOGGLE. `SpectrFreezeToggle` draws the two faces of the design's
FreezeToggle (Spectr Sampler.html): `o LIVE`, a green dot, and `[] FROZEN`, an
amber square, each with its own background, border and text colour, mono 10 at
1pt tracking, a 600-weight label, 24 tall with 10pt side padding and a 6pt
gap. It is FIXED at the FROZEN rendering's width (76pt) so a press never moves
anything beside it, and it is a leaf: its state is its own, so a press
re-renders this one button and commits nothing on any per-frame path. A press
flips the face; nothing else reads it.

THE FACE. The toggle names the bound JetBrains Mono face read from the
document's font_bindings, as the rest of the cluster does
(patch_materialized_output_cluster_face.py): resolving `var(--mono)` by name
reaches the same glyphs with a point more ascent, which paints the word below
the captions' line.

WHY A SCRIPT: the shipping document is one minified line and the materialized
generator cannot rebuild it. The PEAK button is MOVED, not retyped: its text is
cut from between two anchors and re-inserted after the readout, so this script
carries no copy of it to go stale. A second run recognises the toggle and
reports "already applied".

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

TOGGLE_FUNCTION = '''function SpectrFreezeToggle() {
  // Two faces from the design's FreezeToggle, token for token. The state is
  // this control's own: a press flips the face and nothing else reads it, so
  // the press re-renders this button alone.
  const [frozen, setFrozen] = React.useState(false);
  const accent = frozen ? "hsl(35,90%,65%)" : "hsl(150,75%,60%)";
  return /* @__PURE__ */ React.createElement("button", {
    "data-spectr-freeze-toggle": true,
    "data-spectr-freeze-state": frozen ? "frozen" : "live",
    "aria-pressed": frozen,
    "aria-label": "Freeze",
    title: "Freeze the incoming sound",
    onClick: () => setFrozen((value) => !value),
    // Fixed at the FROZEN face's width -- 2 border + 20 padding + 6 glyph +
    // 6 gap + six 10pt mono glyphs at 1pt tracking (42) -- so a press never
    // moves the output controls beside it.
    style: { width: 76, minWidth: 76, flexShrink: 0, height: 24, boxSizing: "border-box", padding: "0 10px", borderRadius: 3, display: "flex", alignItems: "center", justifyContent: "center", gap: 6, cursor: "pointer", background: frozen ? "rgba(200,140,60,0.18)" : "rgba(255,255,255,0.03)", border: "1px solid " + (frozen ? "rgba(240,180,110,0.45)" : "rgba(255,255,255,0.1)"), color: frozen ? "hsl(35,90%,75%)" : "rgba(255,255,255,0.75)", fontFamily: @FACE@, fontSize: 10, letterSpacing: 1, lineHeight: 1 }
  }, /* @__PURE__ */ React.createElement("span", {
    "data-spectr-freeze-glyph": frozen ? "square" : "dot",
    "aria-hidden": true,
    style: { width: 6, height: 6, flexShrink: 0, borderRadius: frozen ? 1 : "50%", background: accent, boxShadow: "0 0 6px " + accent }
  }), /* @__PURE__ */ React.createElement("span", {
    "data-spectr-freeze-label": true,
    // No line-height override: a one-line box at the face's natural height
    // centres its capitals on the same line as the captions beside it.
    style: { fontWeight: 600, whiteSpace: "nowrap", flexShrink: 0 }
  }, frozen ? "FROZEN" : "LIVE"));
}
'''

METER_HEAD = "function SpectrOutputMeter({ latchOver = false } = {}) {\n"

OLD_GEOMETRY = (
    "  // The header's own flex spacer is empty from x=281.7 to x=669.5 (measured\n"
    "  // from a SPECTR_LAYOUT_DUMP of the authored 1320x860 box), so this sits at\n"
    "  // 282 and runs 96 peak + 14 + ~41 OUTPUT + 14 + 156 track + 14 + 34\n"
    "  // readout, ending at ~651 -- inside that gap, with ~36pt of clear header\n"
    "  // before the LIVE button at x=687.5, and out of the flow that cannot\n"
    "  // absorb it. A longer track would eat that clearance.\n"
)
NEW_GEOMETRY = (
    "  // The header is empty from x=281.7 (the end of the title block) to the\n"
    "  // divider before BARS at x=839.5 (measured from a SPECTR_LAYOUT_DUMP of\n"
    "  // the authored 1320x860 box). This sits at 282 and runs 76 freeze + 14 +\n"
    "  // ~41 OUTPUT + 14 + 156 track + 14 + 34 readout + 14 + 96 peak, ending at\n"
    "  // ~741 -- ~98pt clear of the divider, and out of the flow that cannot\n"
    "  // absorb it. PEAK keeps the same 14pt gap to the value that it used to\n"
    "  // keep to OUTPUT.\n"
)

# The PEAK button runs from this anchor (inclusive of the element call) to the
# end of its label child. The cluster's style object closes just before it.
PEAK_START = ' } }, /* @__PURE__ */ React.createElement("button", {\n    "data-spectr-output-peak": true,\n'
PEAK_END = '  }, chipLabel(over, peakText))),\n'
READOUT_END = '    }, trimText));\n}\n'

TOGGLE_ELEMENT = ' } }, /* @__PURE__ */ React.createElement(SpectrFreezeToggle, null),\n'


def bound_mono_face(document):
    """The bound JetBrains Mono 400 Basic Latin face's runtime family."""
    faces = [binding.get("runtime_family")
             for binding in document.get("font_bindings", [])
             if binding.get("family") == "JetBrains Mono"
             and str(binding.get("weight")) == "400"
             and binding.get("style") == "normal"
             and str(binding.get("unicode_range", "")).startswith("U+0000-00FF")]
    if len(faces) != 1 or not faces[0]:
        sys.exit("FAIL: expected one bound JetBrains Mono 400 Basic Latin face, "
                 "found %d" % len(faces))
    return faces[0]


def escaped(value):
    return json.dumps(value)[1:-1]


def once(raw, text, label):
    count = raw.count(escaped(text))
    if count != 1:
        sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                 % (label, count))


def main():
    raw = open(PATH, encoding="utf-8").read()
    html = json.loads(raw)["html"]

    if "function SpectrFreezeToggle()" in html:
        for marker in (TOGGLE_ELEMENT, NEW_GEOMETRY):
            if html.count(marker) != 1:
                sys.exit("FAIL: the toggle is present but %r is not; the "
                         "document is half patched" % marker[:60])
        print("already applied  freeze toggle and header order")
        return 0

    for text, label in ((METER_HEAD, "output meter"),
                        (OLD_GEOMETRY, "cluster geometry comment"),
                        (PEAK_START, "PEAK button start"),
                        (READOUT_END, "trim readout end")):
        once(raw, text, label)

    start = html.index(PEAK_START)
    end = html.index(PEAK_END, start)
    if html.index(READOUT_END) < end:
        sys.exit("FAIL: the trim readout closes before the PEAK button ends; "
                 "the cluster is not in the order this script expects")
    peak = html[start + len(' } }, '):end + len(PEAK_END) - len(",\n")]
    if not peak.startswith('/* @__PURE__ */ React.createElement("button", {'):
        sys.exit("FAIL: the cut PEAK block does not start at its element")

    html = html[:start] + TOGGLE_ELEMENT + '    ' + html[end + len(PEAK_END):]
    if html.count(READOUT_END) != 1:
        sys.exit("FAIL: trim readout end moved while cutting PEAK")
    html = html.replace(
        READOUT_END,
        "    }, trimText),\n  " + peak + ");\n}\n", 1)
    face = ('\'"%s", "JetBrains Mono", ui-monospace, monospace\''
            % bound_mono_face(json.loads(raw)))
    html = html.replace(
        METER_HEAD, TOGGLE_FUNCTION.replace("@FACE@", face) + METER_HEAD, 1)
    html = html.replace(OLD_GEOMETRY, NEW_GEOMETRY, 1)

    order = [html.index(marker) for marker in (
        "React.createElement(SpectrFreezeToggle, null)",
        '"data-spectr-output-trim-label": true',
        '"data-spectr-output-trim": true',
        '"data-spectr-output-trim-readout": true',
        '"data-spectr-output-peak": true')]
    if order != sorted(order):
        sys.exit("FAIL: the cluster is not freeze, OUTPUT, track, value, PEAK "
                 "after patching: %s" % order)

    document = json.loads(raw)
    document["html"] = html
    # Keep every byte outside the html payload as it was, and encode the html
    # the way tools/git/merge_materialized_runtime.py does (ensure_ascii=False),
    # so an unchanged region re-encodes byte for byte.
    head, tail = raw.split('"html":"', 1)
    rest = tail.split('","mime_type":', 1)
    if len(rest) != 2:
        sys.exit("FAIL: the html payload is not followed by mime_type")
    out = (head + '"html":' + json.dumps(html, ensure_ascii=False)
           + ',"mime_type":' + rest[1])
    if json.loads(out) != document:
        sys.exit("FAIL: re-encoding the document changed more than its html")
    open(PATH, "w", encoding="utf-8").write(out)
    print("applied          freeze toggle and header order")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
