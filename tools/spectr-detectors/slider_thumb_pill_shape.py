#!/usr/bin/env python3
"""Assert every slider thumb in the shipping document is a pill inside its track.

Spectr draws three slider surfaces from two components -- the home morph slider
(`MorphSlider`) and the four settings sliders, which all render through one
`SpectrSettingsSlider`.  The design source drew both as a native
`<input type="range">`; the native editor cannot host one, so both were
reimplemented with a hand-drawn thumb and that reimplementation picked a circle.
This adjudicates the shape that replaced it.

It reads the CHECKED-IN artifact rather than a captured fixture, deliberately.
A fixture records one build; this records what ships.  The companion detector
`slider_thumb_hover_growth.py` reads captures, and captures cannot say anything
about a thumb that no captured state ever paints -- every state
`Spectr-native-shot` captures has the morph slider DISABLED, where the thumb is
`opacity: 0` and the "SET A + B" caption shows instead.  So the morph half of
this contract is unreachable from a capture and is checked here.

Two properties, per thumb, in BOTH its idle and its hovered state:

  PILL     width > height, and borderRadius >= height/2 so the ends are
           semicircular rather than merely soft.  A square of any size fails
           the first; a rounded rectangle fails the second.

  INSIDE   the travel is inset by the thumb's own width --
           `marginLeft: -(width * ratio)` -- so the painted thumb runs
           `0 .. trackWidth - width` and never overhangs the painted track.
           The circle used a FIXED `marginLeft` of half its width, so it hung
           half outside at both ends: on the home row that put it 7px past the
           left edge of a 90px track, straight onto the flanking "A" label.

`--plant circle` rewrites the artifact in memory back to the circle spelling.
A detector that still passes under `--plant` is measuring nothing, so the plant
run MUST fail.
"""
import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOC = os.path.join(REPO, "native-ui", "materialized",
                   "materialized-document.runtime.json")

# Each thumb declares one style object. The flag naming its hovered state
# differs per component (`grown` on the morph slider, `hovered` in settings),
# so it is captured rather than assumed.
THUMB = re.compile(
    r'"data-spectr-(?P<which>morph|setting-slider)-thumb":\s*true,'
    r'(?P<body>.*?)\}\s*\}\)', re.S)
SIZE = re.compile(
    r'width:\s*(?P<flag>\w+)\s*\?\s*(?P<wb>[\d.]+)\s*:\s*(?P<wa>[\d.]+),\s*'
    r'height:\s*\w+\s*\?\s*(?P<hb>[\d.]+)\s*:\s*(?P<ha>[\d.]+),\s*'
    r'borderRadius:\s*\w+\s*\?\s*(?P<rb>[\d.]+)\s*:\s*(?P<ra>[\d.]+)')
# The inset travel, whatever the ratio expression happens to be spelled as.
INSET = re.compile(r'marginLeft:\s*-\(\(\s*\w+\s*\?\s*[\d.]+\s*:\s*[\d.]+\s*\)\s*\*')

# Two independent controls, because this detector makes two independent
# claims. `circle` reverts the SHAPE on both components; `overhang` keeps the
# pill and reverts only the TRAVEL, so the containment clause cannot pass on
# the strength of the shape clause.
PLANTS = {
    "circle": (
        ("width: grown ? 26 : 22, height: grown ? 16 : 14, "
         "borderRadius: grown ? 8 : 7",
         "width: grown ? 18 : 14, height: grown ? 18 : 14, "
         "borderRadius: grown ? 9 : 7"),
        ("width: hovered ? 26 : 22, height: hovered ? 16 : 14, "
         "borderRadius: hovered ? 8 : 7",
         "width: hovered ? 18 : 14, height: hovered ? 18 : 14, "
         "borderRadius: hovered ? 9 : 7"),
    ),
    "overhang": (
        ("marginLeft: -((grown ? 26 : 22) * ratio)",
         "marginLeft: grown ? -13 : -11"),
        ("marginLeft: -((hovered ? 26 : 22) * ((value - min) / "
         "((max - min) || 1)))",
         "marginLeft: hovered ? -13 : -11"),
    ),
}

EXPECTED_THUMBS = 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--doc", default=DOC)
    ap.add_argument("--plant", choices=sorted(PLANTS),
                    help="negative control: circle puts the circle shape back, "
                         "overhang puts the fixed half-width margin back")
    args = ap.parse_args()

    document = json.load(open(args.doc, encoding="utf-8"))
    html = document.get("html")
    if not isinstance(html, str):
        sys.exit(f"{args.doc}: no html payload -- nothing to adjudicate")
    if args.plant:
        for old, new in PLANTS[args.plant]:
            if old not in html:
                sys.exit(f"plant: {old!r} is absent, so the control never "
                         f"changed anything and its verdict is meaningless")
            html = html.replace(old, new)

    found = THUMB.findall(html)
    # Positive control. Zero matches means the component was renamed or
    # restructured, not that every thumb is a pill -- reporting that as a pass
    # is exactly how this gate would go hollow.
    print(f"control: {len(found)} thumb declaration(s) in the shipping document")
    if len(found) != EXPECTED_THUMBS:
        sys.exit(f"FAIL: expected {EXPECTED_THUMBS} thumb declarations, found "
                 f"{len(found)} -- the detector is measuring the wrong surface")

    bad = []
    for which, body in found:
        size = SIZE.search(body)
        if size is None:
            bad.append(f"{which}: no width/height/borderRadius triple found")
            continue
        g = {k: (float(v) if v.replace('.', '').isdigit() else v)
             for k, v in size.groupdict().items()}
        states = (("idle", g["wa"], g["ha"], g["ra"]),
                  ("hover", g["wb"], g["hb"], g["rb"]))
        for state, w, h, radius in states:
            shape = "pill" if w > h else ("circle" if w == h else "tall")
            ok_pill = w > h
            ok_round = radius >= h / 2 - 0.001
            print(f"  {which} {state}: {w:g}x{h:g} r{radius:g} "
                  f"aspect {w / h:.2f} -> {shape}"
                  f"{'' if ok_round else ', NOT fully rounded'}")
            if not ok_pill:
                bad.append(f"{which} {state}: {w:g}x{h:g} is not a pill")
            if not ok_round:
                bad.append(f"{which} {state}: borderRadius {radius:g} < "
                           f"{h / 2:g}, so the ends are not semicircular")
        if INSET.search(body) is None:
            bad.append(f"{which}: travel is not inset by the thumb width, so "
                       f"the thumb overhangs its track at both ends")
        else:
            print(f"  {which}: travel inset by the thumb's own width "
                  f"-> stays inside the track")

    if bad:
        for line in bad:
            print(f"FAIL: {line}", file=sys.stderr)
        sys.exit(1)
    print(f"PASS: {len(found)} slider thumb(s) are pills that stay inside "
          f"their track")


if __name__ == "__main__":
    main()
