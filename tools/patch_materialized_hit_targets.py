#!/usr/bin/env python3
"""Make three controls reachable by the pointer over the area they appear to own.

THE DEFECTS, as reported and as measured

    (5) "in settings tapping anywhere on the toggle should adjust it -- right
        now if i tap on the circle part it does nothing."

        A real dispatch bug, and the most diagnostic of the three.  The toggle
        is a 40x20 <button> with an absolutely positioned 16x16 <span> knob
        inside it.  In a browser a press on the knob bubbles to the button and
        the switch flips.  Under Pulp's hit testing it does not, and the reason
        is specific: `View::hit_test` returns the DEEPEST hit-testable view, and
        the bubble walk that follows stops at the first ancestor that carries an
        `on_click` -- but the knob carries one of its OWN.  Measured, with the
        toggle ON so the knob sits at the right-hand end:

            probe x=844 (bare track)  owner=spectr-status-info-toggle  -> flipped
            probe x=855 (bare track)  owner=spectr-status-info-toggle  -> flipped
            probe x=864 (the knob)    owner=__behavior_pr_3v           -> DEAD
            [chain] 0 __behavior_pr_3v 16x16 handlers=click pointer dom-pointer

        The knob is 16 of the toggle's 40px, so 40% of the control was dead --
        and because the knob travels to whichever end matches the value, the
        dead 40% is always the end the eye is drawn to.  Bubbling cannot fix
        this: the knob's own click handler shadows the button's.  The control
        has to stop being two hit targets.

    (2) "i can't seem to tap on snapshot a or b and get that to work (maybe
        it's just a small tap area and needs to be enlarged)."

        Measured, this is NOT a dispatch bug.  A press at the centre of
        `spectr-snapshot-capture-a` resolves to the button itself and fires:
        the status line went from `(none)` to `SNAPSHOT A CAPTURED`.  The hit
        rect equals the painted rect exactly, with nothing swallowing it.  What
        is true is the size: 35x26 design px is 26.3x19.5 pt at the shipping
        990x645 window, because the authored box is 1320x860 and the host pins
        it at 0.75.  The two RECALL buttons additionally refuse a press by
        design while their slot is empty (`disabled`), which is enforced but
        only signalled by opacity -- that half is a separate affordance
        question and is deliberately not touched here.

    (1) "in settings can the thumb be easier to grab once it's enlarged near
        hover."

        Two different complaints are possible here and only one is real.
        Tap-to-move WORKS: a press at 25% of the track moved the thumb from
        x=824 to x=705 on both the click and the drag channel.  What does not
        work is that the grab target never followed the paint.  Measured on a
        156x16 track:

            idle     thumb 14x14 at (824.0,420.5)  7px past the track's right edge
            hovered  thumb 18x18 at (822.0,418.5)  9px past the right edge,
                                                   1px above and 1px below it

        The thumb is `pointerEvents: none` by construction, so the track owns
        every press -- and the track is exactly 156x16 whatever the thumb does.
        At either end of travel half the painted thumb sits outside the only
        rect that can be pressed, and the hover growth the sibling lane shipped
        made the overhang worse rather than better.

THE RULE, stated once and applied to all three

    A control's hit rect must (a) contain every pixel it paints, and (b) be at
    least 38 design px in the axis where it is short -- 28.5 pt at the shipping
    990x645 window, which is the HIG figure for a comfortable pointer target.
    Nothing here changes the paint or the layout: SET-9 is binding, the visual
    design is settled, and `hitSlop` is React Native's exact tool for this --
    `View::hit_bounds()` is `local_bounds()` grown by it, consulted by
    `hit_test` and by nothing that draws.

    The growth is capped by the neighbours, measured rather than assumed on a
    dump taken with BOTH LFOs enabled (the densest the settings panel gets):

        tightest toggle <-> toggle gap   26.0  ->  9+9  leaves 8.0
        tightest slider <-> slider gap   30.0  -> 11+11 leaves 8.0
        snapshot button horizontal gap    8.0  ->  3+3  leaves 2.0
        snapshot row vertical clearance  14.5  ->  6    leaves 8.5

    So:

        settings toggle        40x20 + v9      -> 40x38
        settings slider track 156x16 + v11 h9  -> 174x38
        snapshot capture A/B   35x26 + v6 h3   -> 41x38
        snapshot recall A/B    39x26 + v6 h3   -> 45x38

    The slider's horizontal 9 is not an ergonomics number: it is exactly the
    hover thumb's overhang, and it is what makes rule (a) true at both ends of
    travel.

    `pointerEvents: "box-only"` is what closes (5).  It is React Native's "this
    view receives events, its children do not", and the shipping document
    already relies on it: `SnapBtn` carries it, which is why a press on the
    capture button's 6x6 dot resolves to the BUTTON and not the dot.  That is
    the in-document positive control for the mechanism -- the toggle simply
    never got the same treatment.

DELIBERATELY NOT DONE

    * The morph slider.  It is in the same row and has the same 16px height,
      but `tools/patch_materialized_morph_affordance.py` owns its track style
      and a second writer there would collide for no benefit this lane can
      measure.
    * Making the recall buttons pressable while their slot is empty.  The
      refusal is correct; only its legibility is arguable, and that is the
      affordance lane's call, not a hit-target fix.
    * Any change to paint, layout, type size, or the 990x645 window.

WHY A SCRIPT AND NOT AN ARTIFACT DIFF

    `native-ui/materialized/materialized-document.runtime.json` is a checked-in
    artifact and BOTH generators are broken on main -- the materialized
    generator exits 1 having written 0 patches, and
    `tools/patch_materialized_editor.py` exits 1 at a stale needle.  So the
    edit is exact-text substitution against the `html` payload, every patch
    point is asserted unique before anything is written, and the result is
    re-checked.  Replayable, reviewable, and re-appliable after a merge
    conflict.  Modelled on tools/patch_materialized_status_pill_width.py.

    resources/editor.html is deliberately NOT mirrored: it is the browser
    bootstrap, not the shipping surface, and test_import_fidelity.cpp pins its
    pre-patch shape on purpose.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# `hitSlop` shorthand follows `margin`'s fill rules: two numbers are
# top/right, with bottom copying top and left copying right. See
# core/view/js/web-compat-style-decl-misc.js.
TOGGLE_OLD = '''        borderRadius: 11,
        background: value ? "hsl(200,70%,45%)" : "rgba(255,255,255,0.1)",
        border: "1px solid rgba(255,255,255,0.12)",
        position: "relative",
        cursor: "pointer",
        padding: 0
      }'''

TOGGLE_NEW = '''        borderRadius: 11,
        background: value ? "hsl(200,70%,45%)" : "rgba(255,255,255,0.1)",
        border: "1px solid rgba(255,255,255,0.12)",
        position: "relative",
        cursor: "pointer",
        padding: 0,
        // The knob is a child that carries its own click handler, so a press
        // on it resolved to the knob and the bubble walk stopped there -- 16
        // of the toggle's 40px were dead, always at whichever end the value
        // put the knob. box-only makes the whole switch ONE hit target: this
        // view receives events, its children do not. SnapBtn in this same
        // document already relies on it for its status dot.
        pointerEvents: "box-only",
        // 20px tall is 15pt at the shipping 990x645 window. Grown to 38 design
        // px (28.5pt) by hit area alone -- paint and layout are untouched, and
        // the tightest toggle-to-toggle gap is 26px, so 9 a side leaves 8.
        hitSlop: "9 0"
      }'''

SLIDER_OLD = '''      style: { position: "relative", flex: 1, height: 16, cursor: "pointer" }'''

SLIDER_NEW = '''      // The thumb is pointerEvents:none, so this track is the only thing a
      // press can land on -- and it was exactly 156x16 whatever the thumb did.
      // The hovered 18px thumb sits 9px past each end of travel and 1px above
      // and below the track, so at either extreme half of what the user is
      // aiming at could not be pressed. The horizontal 9 is that overhang
      // exactly; the vertical 11 brings a 16px (12pt) track to 38px (28.5pt).
      style: { position: "relative", flex: 1, height: 16, cursor: "pointer",
               hitSlop: "11 9" }'''

SNAPBTN_OLD = '''        opacity: !isCapture && !filled ? 0.45 : 1,
        pointerEvents: "box-only"
      },'''

SNAPBTN_NEW = '''        opacity: !isCapture && !filled ? 0.45 : 1,
        pointerEvents: "box-only",
        // 35x26 design px is 26.3x19.5pt at 990x645. The row has no width to
        // give -- the gap to the next button is 8px -- so take 3 a side there
        // (leaving 2) and 6 top and bottom, where the row has 14.5px of
        // clearance inside itself. 26 -> 38 design px = 28.5pt.
        hitSlop: "6 3"
      },'''

EDITS = [
    ("settings toggle is one hit target, 38px tall", TOGGLE_OLD, TOGGLE_NEW),
    ("settings slider track contains its own thumb", SLIDER_OLD, SLIDER_NEW),
    ("snapshot buttons reach 38px in the short axis", SNAPBTN_OLD, SNAPBTN_NEW),
]

# Each replacement carries a marker unique to itself, so "already applied" is a
# decidable question. Every patch point here survives inside its replacement,
# so presence of the OLD text cannot decide it.
REQUIRED_AFTER = (
    'hitSlop: "9 0"',
    'hitSlop: "11 9"',
    'hitSlop: "6 3"',
    '// The knob is a child that carries its own click handler',
)


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for label, _old, new in EDITS:
        if not new:
            sys.exit("FAIL %s: empty replacement has no applied marker" % label)

    raw = open(PATH, encoding="utf-8").read()
    changed = False
    applied = 0
    already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) >= 1:
            print("already applied ", label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                     % (label, count))
        raw = raw.replace(old_e, new_e)
        changed = True
        applied += 1
        print("applied         ", label)

    if already and applied:
        sys.exit("FAIL: the document is half patched; refusing to write")

    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit("FAIL: %r is absent after patching" % (token,))

    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")

    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
