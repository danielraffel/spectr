#!/usr/bin/env python3
"""The output cluster's text sits on the header controls' line.

THE DEFECT

  In the top bar, "OUTPUT" painted about a point below the captions of the
  BARS / RESPONSE / BOTH control, the bands trigger and the zoom readout. The
  peak chip's "PEAK -179.6" and the trim readout ("0.0") beside it share the
  cause.

  Every captured header label carries a text binding and so resolves the
  document's bound face, "JetBrains Mono [<asset id>]". The output cluster is a
  component added after the capture, with no bindings, and its three text runs
  declare `fontFamily: "var(--mono)"`, which resolves the family by name. That
  reaches the same glyphs but reports a larger ascent than the bound face, and
  native text centres a line as (box - ink) / 2 + ascent, so the baseline lands
  low. It is the defect the zoom readout had
  (tools/patch_materialized_zoom_readout_face.py), in three more places.

  OUTPUT had a second cause, worth another point and a half. It declared
  `lineHeight: 1`, a 10pt box for a face whose line is 13.2pt. A browser
  centres that overflowing line in the box (negative half-leading); native
  label layout hangs it from the box top instead, so the baseline sits at box
  top + ascent. The trim readout beside it, which declares no line height and
  gets the natural 13.2pt box, was never low. Measured Skia raster ink rows at
  1320x860 against the captions' 17.5..25.0: OUTPUT 20.5..28.0 before, 19.5..27.0
  with the face alone; PEAK and its number 18.5..26.0 before.

THE FIX

  The OUTPUT caption drops `lineHeight: 1` and takes the natural line box, as
  the trim readout does. The OUTPUT caption, the peak chip (whose label
  inherits the chip's face) and the trim readout name the bound face first, read from the document's own
  `font_bindings` (the JetBrains Mono 400 face covering Basic Latin), never
  typed in, so a re-captured document with a new id cannot leave them pointing
  at a font that no longer exists.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once, refuses a
half-patched document, and reports "already applied" on a second run.

A later script may rewrite a patch point this one already applied:
patch_materialized_trim_readout_gap.py turns the readout's
`width: 34, textAlign: "right", ` into `width: 31, textAlign: "left", ` and
keeps the bound face. That superseded form counts as this edit being applied,
so a re-run on a fully patched document still reports "already applied"
rather than "half patched".

Exit codes: 0 applied or already applied, 1 a patch point or the bound face is
missing or ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# Present only in the component being patched.
CONTEXT = 'function SpectrOutputMeter({ latchOver = false } = {}) {'


def family(runtime_family):
    return '\'"%s", "JetBrains Mono", ui-monospace, monospace\'' % runtime_family


def superseded(face):
    """Later forms of a patch point that still carry this script's edit.

    Keyed by the edit's label; each value is text that, when present, means
    the edit was applied and then revised by a follow-up script.
    """
    return {
        'the trim readout uses the bound face':
            'style: { width: 31, textAlign: "left", whiteSpace: "nowrap", '
            'flexShrink: 0, fontFamily: %s, fontSize: 10, '
            'color: "rgba(255,255,255,0.72)" }' % face,
    }


def edits(face):
    return [
        ('the OUTPUT caption uses the bound face and its natural line box',
         'style: { fontFamily: "var(--mono)", fontSize: 10, letterSpacing: 0.8, '
         'color: "rgba(255,255,255,0.72)", lineHeight: 1, whiteSpace: "nowrap", '
         'flexShrink: 0 }\n    }, "OUTPUT")',
         'style: { fontFamily: %s, fontSize: 10, letterSpacing: 0.8, '
         'color: "rgba(255,255,255,0.72)", whiteSpace: "nowrap", '
         'flexShrink: 0 }\n    }, "OUTPUT")' % face),
        ('the trim readout uses the bound face',
         'style: { width: 34, textAlign: "right", whiteSpace: "nowrap", '
         'flexShrink: 0, fontFamily: "var(--mono)", fontSize: 10, '
         'color: "rgba(255,255,255,0.72)" }',
         'style: { width: 34, textAlign: "right", whiteSpace: "nowrap", '
         'flexShrink: 0, fontFamily: %s, fontSize: 10, '
         'color: "rgba(255,255,255,0.72)" }' % face),
        ('the peak chip, and so its PEAK label, uses the bound face',
         '      minHeight: 22,\n      boxSizing: "border-box",\n'
         '      borderRadius: 3,\n      fontFamily: "var(--mono)",\n',
         '      minHeight: 22,\n      boxSizing: "border-box",\n'
         '      borderRadius: 3,\n      fontFamily: %s,\n' % face),
    ]


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
    face = family(bound_mono_face(json.loads(raw)))
    steps = edits(face)
    later = superseded(face)

    if raw.count(escaped(CONTEXT)) != 1:
        sys.exit("FAIL: %r occurs %d times, expected 1; the output cluster is "
                 "not where this script expects it"
                 % (CONTEXT, raw.count(escaped(CONTEXT))))

    def is_applied(label, old, new):
        if raw.count(escaped(old)) != 0:
            return False
        if raw.count(escaped(new)) == 1:
            return True
        revised = later.get(label)
        return revised is not None and raw.count(escaped(revised)) == 1

    applied = [is_applied(label, old, new) for label, old, new in steps]
    if all(applied):
        print("already applied  the output cluster uses the bound monospace face")
        return 0
    if any(applied):
        sys.exit("FAIL: the document is half patched; refusing to guess")

    for label, old, new in steps:
        if raw.count(escaped(old)) != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                     % (label, raw.count(escaped(old))))
    for label, old, new in steps:
        raw = raw.replace(escaped(old), escaped(new), 1)
        print("applied         ", label)

    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
