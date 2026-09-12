#!/usr/bin/env python3
"""Give the status pill's width the same owner as the string it paints.

THE DEFECT

    The status pill -- the readout near the top of the plot that carries
    `281Hz   10.8 dB   BAND 25/64` while a pointer is on a band -- has its
    width computed from React's copy of the message:

        const bannerWidth = Math.max(96, Math.min(520, text.length * 8 + 28));

    but the string it PAINTS has a second writer.  `updateLiveHoverStatus`
    runs from the draw loop and writes the live reading straight onto the DOM
    node:

        if (shown && text) text.textContent = label;

    That fast path exists for a good reason and is not the bug: routing every
    frame's reading through React would commit the whole captured import
    document per frame, which is the ~22ms cost the zoom-readout work removed.
    The bug is that it wrote ONE of the two properties the pill needs.  React
    re-derives the width only when ITS copy of the string changes, and that
    publish is throttled to 700ms and skipped entirely while the reading is
    unchanged -- so the pill kept whatever size React last saw.

    The user's report is the worst case of exactly this.  CLEAR publishes
    "CLEARED GAINS" through React; a band click publishes "BAND n MUTED".
    Both are 13 characters, so React sizes the pill to 13*8+28 = 132px.  The
    very next live reading is a full hover label, and the fast path paints it
    into that 132px box without resizing it.

MEASURED, on the shipping standalone (990x645 host, 1320x860 design space),
band click at (400,300) via SPECTR_DRAG, geometry read off the layout dump:

    before  pill x=538.0 w=244.0  text box w=214.0  ink w=203.0   fits
    after   pill x=594.0 w=132.0  text box w=102.0  ink w=173.0   ink
                                                                  overhangs
                                                                  by 71.0px

    The pill centre is 660.0 in BOTH -- centring never moved.  This is a
    width defect, not a centring defect, which is the same shape as the
    Settings copy button that was "fixed" three times for centring.
    It is also persistent, not a flash: identical at +55, +158, +415, +906
    and +1600ms after the release.

THE FIX

    Whoever writes the string owns the box.  The rule that turns a string
    into a width becomes one function, `spectrStatusBannerWidth`, and the
    per-frame fast path writes the width and the margin alongside the text.
    No extra React commit: two inline style writes on a node the fast path
    had already resolved.

    Deliberately NOT done: removing the fast path so React owns everything.
    That reintroduces a whole-document commit per frame on the one
    interaction that has to stay cheap.

RESIDUAL, stated rather than hidden: if React commits a DIFFERENT message of
the IDENTICAL length while a live reading is on screen (mute band 10, then
mute band 11 -- "BAND 10 MUTED" and "BAND 11 MUTED" are both 13 characters),
React's prop diff sees no width change and writes only the text, leaving the
pill at the width the fast path set for the previous live reading.  That is a
pill too WIDE for its text, cosmetic rather than broken, and the next live
reading -- which the mute itself produces, since the band's dB changes --
resizes it on the following frame.  Closing it outright would cost a
querySelector pair on every frame of the draw loop, in the hot path the
previous lane worked to shrink.

WHY A SCRIPT AND NOT AN ARTIFACT DIFF

    `native-ui/materialized/materialized-document.runtime.json` is a
    checked-in artifact.  The materialized generator does not run on this
    checkout, and neither does `tools/patch_materialized_editor.py`.  So the
    edit is made by exact-text substitution against the `html` payload, every
    patch point is asserted unique before anything is written, and the result
    is re-checked -- replayable and reviewable, and re-appliable after a merge
    conflict.  Modelled on tools/patch_materialized_zoom_readout.py.

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

# One rule, one place.  The two writers live in different <script> blocks --
# FilterBank in the first, StatusBanner in the chrome block -- but those are
# classic scripts sharing one global scope (App in the last block already
# calls FilterBank from the first), and both calls happen at run time, long
# after every block has been evaluated.  So a single top-level declaration
# reaches both.
HELPER = '''function spectrStatusBannerWidth(text) {
  return Math.max(96, Math.min(520, (text ? text.length : 0) * 8 + 28));
}
'''

EDITS = [
    ('status pill width is one shared rule',
     'function StatusBanner({ message, disabled }) {',
     HELPER + 'function StatusBanner({ message, disabled }) {'),

    ('react sizes the pill through the shared rule',
     '  const bannerWidth = Math.max(96, Math.min(520, text.length * 8 + 28));',
     '  const bannerWidth = spectrStatusBannerWidth(text);'),

    ('the per-frame writer sizes the box it paints into',
     '    if (shown && text) text.textContent = label;',
     '    // Whoever writes the string owns the box.  The width is a pure\n'
     '    // function of the string being painted, and React re-derives it\n'
     '    // only when ITS copy changes -- which the 700ms throttle below\n'
     '    // defers, and skips outright while the reading is unchanged.  A\n'
     '    // fast path that wrote only the text therefore left the pill sized\n'
     '    // for whatever React last saw: after a 13-character message\n'
     '    // ("CLEARED GAINS" from CLEAR, "BAND n MUTED" from a band click,\n'
     '    // both 132px) the next hover reading painted 212px of text into it,\n'
     '    // overhanging 35px past each edge, and stayed that way for the\n'
     '    // whole hold.\n'
     '    if (shown && text) {\n'
     '      text.textContent = label;\n'
     '      const bannerWidth = spectrStatusBannerWidth(label);\n'
     '      shown.style.width = bannerWidth + "px";\n'
     '      shown.style.marginLeft = -bannerWidth / 2 + "px";\n'
     '    }'),
]

# The literal formula must survive nowhere: a second copy is exactly how the
# two owners drift apart again.
FORBIDDEN_AFTER = ('Math.min(520, text.length * 8 + 28)',
                   'if (shown && text) text.textContent = label;')
REQUIRED_AFTER = ('function spectrStatusBannerWidth(text) {',
                  'const bannerWidth = spectrStatusBannerWidth(text);',
                  'const bannerWidth = spectrStatusBannerWidth(label);',
                  'shown.style.width = bannerWidth + "px";',
                  'shown.style.marginLeft = -bannerWidth / 2 + "px";')


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    # Every replacement text must be distinctive enough to be its own
    # already-applied marker.  Two of these edits keep their patch point
    # inside the replacement (the helper is PREPENDED to StatusBanner, the
    # fast path is WRAPPED), so "is the old text still present" cannot decide
    # whether the edit landed -- it is still present either way, and deciding
    # that way re-prepended the helper on a second run.
    for label, _old, new in EDITS:
        if not new:
            sys.exit('FAIL %s: empty replacement has no applied marker' % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) >= 1:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e)
        changed = True
        applied += 1
        print('applied         ', label)

    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change needed')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
