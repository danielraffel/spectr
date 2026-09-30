#!/usr/bin/env python3
"""Show the tracing reminder on the header's line, as part of the header.

A PULP_TRACING=ON build stamps a "◉ TRACING" pill on every frame so nobody
forgets Perfetto is compiled in. Pulp paints it from the root View at a fixed
spot -- 8px from the top-right corner, 11px system font, its baseline estimated
as 72% of the pill height -- which knows nothing about the application under
it. In Spectr's header that put the pill four points above the line every other
header control shares (captions centred at 21.5 in the 1320x860 design), in a
different face, and flush against the end of the zoom readout.

The editor now draws the reminder itself, as a header element: a leaf
component appended as the LAST child of the header row, so no captured sibling
path moves, and absolutely positioned so it takes no room from the flex row.
It is 20px tall at top 11.5 -- centred on 21.5 like the segmented buttons --
right-aligned 8px from the header's edge like Pulp's, and set in the same bound
JetBrains Mono face the captions resolve to, so native text centring puts its
line exactly where it puts theirs. Because it is part of the document it scales
with the header at every window size.

It renders nothing until the native editor calls `__spectrShowTracingBadge()`,
which it does only in a tracing build, after hiding Pulp's own pill -- so a
shipping build carries the component but never shows it, and a tracing build
shows exactly one reminder.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once, refuses a
half-patched document, and reports "already applied" on a second run. The
bound face's asset id is read from the document's own font_bindings.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


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


def edits(face):
    family = '\'"%s", "JetBrains Mono", ui-monospace, monospace\'' % face
    component = (
        'function SpectrTracingBadge() {\n'
        '  const [shown, setShown] = React.useState(false);\n'
        '  React.useEffect(() => {\n'
        '    globalThis.__spectrShowTracingBadge = () => setShown(true);\n'
        '    return () => { globalThis.__spectrShowTracingBadge = void 0; };\n'
        '  }, []);\n'
        '  if (!shown) return null;\n'
        '  // Centred on the header controls\' line (21.5 in the design), in the\n'
        '  // face the captions resolve to, right-aligned like Pulp\'s own pill.\n'
        '  return /* @__PURE__ */ React.createElement("div", {\n'
        '    "data-spectr-tracing-badge": true,\n'
        '    "aria-hidden": true,\n'
        '    // Sized explicitly: an absolutely positioned box does not size\n'
        '    // itself to its text here. 9 glyphs of 10px mono is 54px; 66 wide\n'
        '    // at right 8 starts at 1246, clear of the zoom readout\'s text up\n'
        '    // to "10.00x zoom" (which ends at 1242).\n'
        '    style: { position: "absolute", right: 8, top: 11.5, width: 66, height: 20,\n'
        '      display: "flex", alignItems: "center", justifyContent: "center",\n'
        '      boxSizing: "border-box", borderRadius: 10,\n'
        '      background: "rgba(20,20,24,0.86)", pointerEvents: "none",\n'
        '      fontFamily: %s,\n'
        '      fontSize: 10, color: "rgb(255,190,60)",\n'
        '      whiteSpace: "nowrap" }\n'
        '  }, /* @__PURE__ */ React.createElement("span", { style: { whiteSpace: "nowrap", flexShrink: 0 } }, "\\u25C9 TRACING"));\n'
        '}\n' % family)
    return [
        ('the tracing badge is a header leaf',
         'function SpectrZoomReadout({ bankRef }) {\n',
         component + 'function SpectrZoomReadout({ bankRef }) {\n'),
        ('the header ends with the tracing badge',
         '"\\u200B"))), /* @__PURE__ */ React.createElement(StatusBanner',
         '"\\u200B")), /* @__PURE__ */ React.createElement(SpectrTracingBadge, null)),'
         ' /* @__PURE__ */ React.createElement(StatusBanner'),
    ]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()
    applied = already = 0
    for label, old, new in edits(bound_mono_face(json.loads(raw))):
        if old in new and label != 'the tracing badge is a header leaf':
            sys.exit('FAIL %s: patch point survives its own replacement' % label)
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        applied += 1
        print('applied         ', label)
    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')
    if not applied:
        return 0
    if raw.count(escaped('function SpectrTracingBadge() {')) != 1:
        sys.exit('FAIL: the badge component is not declared exactly once')
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
