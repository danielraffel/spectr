#!/usr/bin/env python3
"""Give the status pill's PLACEMENT the same single owner its width already has.

THE DEFECT

    Press CLEAR and the status pill above the plot -- the readout carrying
    `369Hz   0.0 dB   BAND 14/32` -- jumps out of position and stays there.
    It does not drift back.  Measured on the shipping standalone (990x645
    host, 1320x860 design space), hovering a band and then pressing CLEAR:

        before CLEAR   pill x=538.0 y=104.0 w=244.0   centre 660.0  (correct)
        after  CLEAR   pill x=418.0 y= 60.0 w=244.0   centre 540.0

    A constant (-120, -44) displacement, independent of the width: an
    identical -120/-44 appears at w=244 and at w=252.  The pill is left of
    centre by exactly half its own width, which is why on screen its RIGHT
    EDGE lands on the window centre line -- the user's screenshot exactly.

    It is NOT a width defect and NOT a regression from the width fix.  With
    the per-frame width/margin writes that fix introduced removed entirely
    from the document (verified absent from the built binary by symbol
    count, against a positive control that was still present), the
    displacement is bit-identical: x=418 y=60.  The width fix neither
    caused it nor can it mask it.

THE MECHANISM, as far as it is provable from this side

    After the CLEAR commit the pill's `top` and `left` are simply GONE from
    the native widget.  That was proved rather than inferred: re-authoring
    the document with `top: 204` and `left: "25%"` moves the healthy pill to
    (282, 204) as expected, and leaves the post-CLEAR pill at (418, 60) --
    byte for byte the same place as with `top: 104; left: "50%"`.  A rect
    that does not respond AT ALL to the values it is supposed to be built
    from is not a rect computed from bad values; those two inputs are not
    reaching layout.  `position` and `marginLeft` do survive (the -122 is
    still in the x), and the shim node is not recreated -- its `__pulpId`
    is unchanged across the transition.

    Only CLEAR does this.  PRECISION -- which publishes a status message
    through the very same path, mounts the same banner and runs the same
    width transition -- leaves the pill at (538, 104).  So it is not the
    status publish and not the banner mount; it is something in the commit
    that `clearGains()` provokes.

    WHY the bridge drops those two keys there is NOT established here, and
    this patch does not claim to fix it.  That is upstream of the document,
    in the Pulp widget bridge, and it deserves its own investigation.

THE FIX

    The same rule the width fix established, applied to placement: whoever
    can be holding the pill's geometry re-asserts ALL of it, and the numbers
    live in one place.  `spectrPlaceStatusBanner` owns top, left, width and
    margin together; React's style object reads its top/left from the same
    two constants, so the authored geometry and the re-assertion cannot
    drift apart.  It is called from the two owners:

      * `updateLiveHoverStatus`, the per-frame writer -- which already
        resolved the node and already wrote two style properties, so this
        adds two more writes and no extra query.  This is the path that
        covers the state in the user's report: a live hover reading on
        screen after a CLEAR.
      * a `StatusBanner` effect with no dependency list, so it runs after
        every commit of that component -- which covers the `CLEARED GAINS`
        pill itself, before any hover reading replaces it.

    Re-asserting works and is durable: writing `top` and `left` back onto
    the node restores the rect to its authored position and it stays there
    for every later frame.  Measured through the probe, at w=244:
    (418, 60) -> (208, 204) with 204/25% authored, i.e. exactly the healthy
    geometry.

    Deliberately NOT done: re-asserting on every frame regardless of
    whether the reading changed.  That would cost a querySelector pair per
    frame in the draw loop the zoom-readout work shrank.

ALSO HERE, and deliberately: `data-spectr-rail-action`.

    The rule this fix restores was already being checked -- the status-pill
    width detector asserts "pill centre == viewport centre" as its control
    -- and it still missed this, because nothing could drive CLEAR.  Its
    `--app` driver presses a band, and in that population the pill is
    centred at exactly 660.0 in every dump, which is the truth about the
    states it captured and says nothing about the one it could not reach.
    A rule that cannot be aimed at the defect is not a rule that failed; it
    is a rule that was never pointed at it.

    So CLEAR gets a stable hook, exactly like the snapshot buttons'
    `data-spectr-snapshot-action`.  Selecting it as "the first `button` in
    the document" happens to work today and would rot silently the first
    time a button is added above it.

WHY A SCRIPT AND NOT AN ARTIFACT DIFF

    `native-ui/materialized/materialized-document.runtime.json` is a
    checked-in artifact and neither generator runs on this checkout.  So
    the edit is exact-text substitution against the `html` payload, every
    patch point asserted unique before anything is written, and the result
    re-checked -- replayable, reviewable, and re-appliable after a merge
    conflict.  Modelled on tools/patch_materialized_status_pill_width.py,
    which must be applied first: two of the patch points below are text it
    introduces.

    resources/editor.html is deliberately NOT mirrored: it is the browser
    bootstrap, not the shipping surface, and test_import_fidelity.cpp pins
    its pre-patch shape on purpose.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# One owner for the four numbers that place the pill.  React's style object
# reads top/left from the same two constants, so the authored geometry and
# the re-assertion below cannot say different things.
# Function declarations rather than `var`: a `var` is hoisted but its
# ASSIGNMENT is not, so any throw earlier in this script block would leave
# React reading `top: undefined`.  A function declaration is bound before
# the block runs a single statement, which is also what lets the FilterBank
# block -- a different <script> sharing the same global scope -- call it.
PLACEMENT = '''function spectrStatusBannerTop() {
  return 104;
}
function spectrStatusBannerLeft() {
  return "50%";
}
function spectrPlaceStatusBanner(node, bannerWidth) {
  if (!node || !node.style) return;
  node.style.top = spectrStatusBannerTop();
  node.style.left = spectrStatusBannerLeft();
  node.style.width = bannerWidth;
  node.style.marginLeft = -bannerWidth / 2;
}
'''

WIDTH_HELPER = '''function spectrStatusBannerWidth(text) {
  return Math.max(96, Math.min(520, (text ? text.length : 0) * 8 + 28));
}
'''

EDITS = [
    ('pill placement is one shared rule',
     WIDTH_HELPER + 'function StatusBanner({ message, disabled }) {',
     WIDTH_HELPER + PLACEMENT + 'function StatusBanner({ message, disabled }) {'),

    ('react places the pill through the shared constants',
     '        top: 104,\n'
     '        left: "50%",\n'
     '        marginLeft: -bannerWidth / 2,',
     '        top: spectrStatusBannerTop(),\n'
     '        left: spectrStatusBannerLeft(),\n'
     '        marginLeft: -bannerWidth / 2,'),

    ('the per-frame writer places the box it paints into',
     '      const bannerWidth = spectrStatusBannerWidth(label);\n'
     '      shown.style.width = bannerWidth + "px";\n'
     '      shown.style.marginLeft = -bannerWidth / 2 + "px";',
     '      // Placement, not just size.  A CLEAR commit drops this node\'s\n'
     '      // `top` and `left` from the native widget -- proved by\n'
     '      // re-authoring both values and watching the post-CLEAR rect not\n'
     '      // move at all -- which parks the pill at (-120, -44) from where\n'
     '      // it belongs, half its own width left of centre, and leaves it\n'
     '      // there.  Re-asserting all four numbers is durable; the node is\n'
     '      // already resolved here, so it costs two more writes and no\n'
     '      // extra query.\n'
     '      spectrPlaceStatusBanner(shown, spectrStatusBannerWidth(label));'),

    ('the banner watches its own placement after a status commit',
     '  const bannerWidth = spectrStatusBannerWidth(text);\n'
     '  return /* @__PURE__ */ React.createElement(',
     '  const bannerWidth = spectrStatusBannerWidth(text);\n'
     '  // No dependency list on purpose, and a watch rather than a single\n'
     '  // write: the commit that loses this node\'s placement is not this\n'
     '  // one and is not a commit of this component at all. A gains publish\n'
     '  // (CLEAR) goes out to native and the fan-out lands about ten frames\n'
     '  // later -- measured, with no render of this component in between --\n'
     '  // and that is what drops `top` and `left`. There is no React event\n'
     '  // here to hang a repair on, so watch for a bounded window after\n'
     '  // every status commit instead.\n'
     '  //\n'
     '  // A healthy pill costs one cached rect read per frame and NO writes:\n'
     '  // the tick repairs only when the placement has actually moved. That\n'
     '  // is the whole reason it reads before it writes -- an unconditional\n'
     '  // re-assert would put four bridge style writes per frame into the\n'
     '  // one interaction the zoom-readout work made cheap.\n'
     '  useEffectChrome(() => {\n'
     '    const node = document.querySelector("[data-spectr-status-shell]");\n'
     '    spectrPlaceStatusBanner(node, bannerWidth);\n'
     '    if (!node || typeof requestAnimationFrame !== "function") return;\n'
     '    let handle = 0;\n'
     '    const until = performance.now() + 900;\n'
     '    const tick = () => {\n'
     '      let rect = null;\n'
     '      try { rect = node.getBoundingClientRect(); } catch (e) { rect = null; }\n'
     '      if (!rect || Math.abs(rect.top - spectrStatusBannerTop()) > 0.5)\n'
     '        spectrPlaceStatusBanner(node, bannerWidth);\n'
     '      else if (rect.width > 0\n'
     '          && Math.abs(rect.width - bannerWidth) > 0.5)\n'
     '        node.style.marginLeft = -rect.width / 2;\n'
     '      if (performance.now() < until) handle = requestAnimationFrame(tick);\n'
     '    };\n'
     '    handle = requestAnimationFrame(tick);\n'
     '    return () => cancelAnimationFrame(handle);\n'
     '  });\n'
     '  return /* @__PURE__ */ React.createElement('),

    ('rail buttons carry a stable action hook',
     'function RailBtn({ children, onClick, active, popupKind }) {',
     'function RailBtn({ children, onClick, active, popupKind, railAction }) {'),

    ('the rail action reaches the DOM',
     '      "data-spectr-menu-trigger": popupKind ? true : void 0,',
     '      "data-spectr-rail-action": railAction || void 0,\n'
     '      "data-spectr-menu-trigger": popupKind ? true : void 0,'),

    ('CLEAR is reachable by name',
     'React.createElement(RailBtn, { onClick: onClearAll }, "CLEAR")',
     'React.createElement(RailBtn, { railAction: "clear", onClick: onClearAll }, "CLEAR")'),
]

# The old literals must survive nowhere: a second copy is exactly how two
# owners drift apart again.
FORBIDDEN_AFTER = (
    '        top: 104,\n        left: "50%",',
    '      shown.style.width = bannerWidth + "px";',
)
REQUIRED_AFTER = (
    'function spectrPlaceStatusBanner(node, bannerWidth) {',
    'function spectrStatusBannerTop() {',
    'function spectrStatusBannerLeft() {',
    'top: spectrStatusBannerTop(),',
    'spectrPlaceStatusBanner(shown, spectrStatusBannerWidth(label));',
    'const node = document.querySelector("[data-spectr-status-shell]");',
    'node.style.marginLeft = -rect.width / 2;',
    '"data-spectr-rail-action": railAction || void 0,',
    'railAction: "clear"',
)


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    # Every replacement must be distinctive enough to be its own
    # already-applied marker.  Several of these keep their patch point
    # INSIDE the replacement, so "is the old text still present" cannot
    # decide whether the edit landed -- it is present either way.
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
