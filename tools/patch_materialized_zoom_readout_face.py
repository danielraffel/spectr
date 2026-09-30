#!/usr/bin/env python3
"""Put the zoom readout on the header's bound monospace face, so it sits on the header's line.

In the top bar, the `1.00x zoom` readout painted one point below the captions
of the BARS / RESPONSE / BOTH control beside it. Its layout box was never the
problem: that box is centred on the controls' line (the span's box is 15..28,
and the segmented buttons and the bands trigger both span 10.5..32.5, all
centred at 21.5). The problem was the face that box was measured with.

Every captured text node in the header carries a text binding, which gives it
the document's bound face, `"JetBrains Mono [<asset id>]"`: the embedded font
file at a stable, content-addressed name. The readout is a leaf that
`patch_materialized_zoom_readout.py` introduced after the capture, so it has
no binding and resolves the stylesheet's `var(--mono)`, `'JetBrains Mono',
ui-monospace, monospace`, by name. That name reaches the same glyphs, but not
the same vertical metrics: at 10pt the label reports an ascent of 11.2 against
10.2 for the bound face. Native text centres a single line as
`(box height - ink) / 2 + ascent`, so the extra point of ascent lands the
baseline one point low. Measured on a Skia raster of the shipping editor at
1320x860 (2x), glyph ink rows:

    BOTH, RESPONSE   17.5 .. 25.0
    1.00x zoom       18.5 .. 26.0    unbound face
    1.00x zoom       17.5 .. 25.0    bound face (this patch)

So the fix is a face declaration, not an offset: the readout names the same
bound family the captured labels resolve to, followed by the family chain
they carry, and the native centring formula then places its line exactly
where it places the captions next to it. The asset id is read from the
document's own `font_bindings` (the JetBrains Mono 400 face covering Basic
Latin), never typed in, so a re-captured document with a new id cannot
leave this pointing at a font that no longer exists.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts the patch point occurs exactly once before
writing, re-checks the result, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 the patch point or the bound face
is missing or ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# Present only on the node being patched, so another span that happens to
# share the style text cannot be hit by mistake.
CONTEXT = 'function SpectrZoomReadout({ bankRef }) {'

OLD = ('React.createElement("span", { className: "tnum", style: '
       '{ whiteSpace: "nowrap", flexShrink: 0, minWidth: 84 } }, '
       'zoom, "\\xD7 zoom")')


def new_readout(runtime_family):
    family = ('\'"%s", "JetBrains Mono", ui-monospace, monospace\''
              % runtime_family)
    return ('React.createElement("span", { className: "tnum", style: '
            '{ whiteSpace: "nowrap", flexShrink: 0, minWidth: 84, '
            'fontFamily: %s } }, zoom, "\\xD7 zoom")' % family)


def bound_mono_face(document):
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


def main():
    raw = open(PATH, encoding="utf-8").read()
    new = new_readout(bound_mono_face(json.loads(raw)))

    context_count = raw.count(escaped(CONTEXT))
    if context_count != 1:
        sys.exit("FAIL: %r occurs %d times, expected 1; the zoom readout is "
                 "not where this script expects it" % (CONTEXT, context_count))

    if raw.count(escaped(new)) == 1 and raw.count(escaped(OLD)) == 0:
        print("already applied  the zoom readout uses the bound monospace face")
        return 0

    count = raw.count(escaped(OLD))
    if count != 1:
        sys.exit("FAIL: the readout span occurs %d times, expected 1" % count)

    raw = raw.replace(escaped(OLD), escaped(new), 1)

    if raw.count(escaped(new)) != 1 or raw.count(escaped(OLD)) != 0:
        sys.exit("FAIL: the readout span did not take the bound face")
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")
    if bound_mono_face(document) not in document["html"]:
        sys.exit("FAIL: the bound face is absent from the patched html")

    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied          the zoom readout uses the bound monospace face")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
