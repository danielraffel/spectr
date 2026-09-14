#!/usr/bin/env python3
"""Adjudicate the long-form help overlay: its route in, its copy, and its scroll.

It reads the shipping document (`materialized-document.runtime.json`) and the
shipping copy (`help-content.js`) rather than a capture, because three of the
four rules are about things no single frame can show: whether the copy lives in
its own asset, whether the panel can move its content at all, and whether a
character that would truncate the copy is present.

  ROUTE   The `?` popover carries a Learn more control, and that control hands
          a callback back to Chrome which closes the popover and opens the
          guide. All three halves are checked: a control with no callback is a
          button that does nothing, and a callback nothing calls is dead code.

  ASSET   The copy is in `native-ui/materialized/help-content.js`, bundled as
          its own editor asset, and is NOT inlined into the document. This is
          the rule with teeth: the document is a checked-in artifact neither
          generator on main can rebuild, so copy that leaked into it could only
          be reworded by patching an 800 KB single-line blob. The check is
          two-directional -- the asset must contain the copy, and the document
          must not.

  SCROLL  The panel is a screenful taller than the editor, so the content has
          to move. It is NOT enough that the viewport declares `overflow:
          hidden`: a clipping box with no way to translate its content silently
          discards everything below the fold, which is strictly worse than the
          popover this replaces. So the viewport must clip AND the content must
          carry the negative-margin offset AND something must be able to change
          that offset (a wheel handler and a key handler).

          The runtime will not do this for us. A runtime-created node declaring
          `overflow: "scroll"` clips but is never lowered to a
          `pulp::view::ScrollView`: measured on the built app with the panel
          open, the host's own scroll fixture walked the view tree and found
          exactly one ScrollView -- the Settings body -- and driving it moved
          zero pixels of this panel while moving 106,686 of Settings'.

  COPY    The asset is intact and says what the code says. A backtick or a
          `${` would terminate its template literal early and take the rest of
          the guide with it; an em dash violates the copy's own rule; and the
          latency figure has to be the product's own. `kSpectralLatency` is
          `kSpectralFftSize + kSpectralAnalysisHop` = 8192 + 2048 = 10240
          samples, 213 ms at 48 kHz. (README.md's "8,191 samples / 170.65 ms"
          is `fft - 1` and describes nothing the code reports.)

Exit codes: 0 pass, 1 fail.
"""
import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOC = os.path.join(REPO, "native-ui", "materialized",
                   "materialized-document.runtime.json")
ASSET = os.path.join(REPO, "native-ui", "materialized", "help-content.js")

LEARN_MORE = '"data-spectr-help-learn-more": true,'
LEARN_CALLBACK = "onLearnMore && onLearnMore()"
LEARN_WIRED = "onLearnMore: () => { setHelpOpen(false); setHelpGuideOpen(true); }"
GUIDE_MOUNT = "helpGuideOpen && /* @__PURE__ */ React.createElement("
READS_ASSET = 'globalThis.SPECTR_HELP_TEXT === "string"'
VIEWPORT = '"data-spectr-help-scroll": true,'
CLIPS = 'overflow: "hidden",\n      position: "relative",'
CONTENT_OFFSET = "marginTop: -offset"
WHEEL = "onWheel: function (event) {"
KEYS = 'if (event.key === "ArrowDown") step = 60;'
# The scrim is positioned in ANCHOR space and has to reach ROOT space, which it
# does by measuring the bottom rail. That only works while the rail is still the
# full-width, bottom-anchored box whose top edge is where the in-flow content
# ends, so its declared geometry is pinned here rather than assumed.
RAIL_NAMED = '"data-spectr-bottom-rail": true,'
RAIL_GEOMETRY = ('position: "absolute",\n    bottom: 0,\n    left: 0,\n'
                 '    right: 0,\n    height: 56,')
RAIL_MEASURED = 'document.querySelector("[data-spectr-bottom-rail]")'

# Sentences from the approved copy. Present in the asset, absent from the
# document: that is the whole point of shipping the copy separately.
COPY_MARKERS = (
    "Think of it as a precise way",
    "spaced more like how we hear pitch",
    "move toward that snapshot and back again",
    "shows them together so you can see the difference",
    "Good to know",
)
HEADINGS = (
    "How the bands work", "Zooming", "Drawing", "The analyzer",
    "Snapshots and morph", "Movement", "Automation", "Presets",
    "Live and Precision", "Good to know",
)

# -- REACHABILITY ---------------------------------------------------------
#
# A source-text check cannot see geometry, so this asserts the two DECLARATIONS
# that decide whether the overlay can be touched at all: a real box, and a
# z-index above every sibling. Both shipped wrong in the same release and a
# screenshot could not tell -- the guide painted perfectly while its close
# button, the band readout underneath and the cursor were all unreachable.
ANCHOR_BOX = ('style: { width: vw, height: vh, marginTop: origin ? origin.y : 0,'
              ' flexShrink: 0, overflow: "visible", zIndex: 70 }')
ANCHOR_Z = "zIndex: 70"
# With the anchor in root space the scrim must stop re-applying the offset.
SCRIM_ROOT = '      top: 0,\n      left: 0,\n      width: vw,\n      height: vh,\n'

# -- THE POPOVER'S TAIL, MEASURED FROM THE CAPTURE ------------------------
#
# This panel is laid out from its capture, so the Learn more button having a
# BOX is not a style question -- it is a data question, answerable here exactly.
# Shipped without one, it fell to the content-box origin and printed across the
# first two shortcut rows.
STATE = os.path.join(REPO, "native-ui", "materialized", "states",
                     "help.materialized.json")
PANEL_PATH = [("div", 0), ("div", 3), ("div", 16), ("div", 1)]
# Matched on the TAG, not on a sibling index. The button's index is a function
# of how many shortcut rows precede it -- twelve rows put it at 13, thirteen at
# 14 -- so a pinned index turns "the panel gained a row" into "the tail has no
# box", which is this detector's headline finding and would be a false one.
TAIL_TAG = "button"


# Plants that corrupt the CAPTURE rather than the source. They are separate
# from PLANTS because the capture is a third file, and because these two are the
# only way to reproduce the shipped defect exactly: the button existed, was
# styled, was wired, and simply had no box.
CAPTURE_PLANTS = {
    # #115 as it shipped: the tail has no captured box at all.
    "tailless-capture": lambda panel, tail: (panel, None),
    # The tail has a box but the panel never grew for it, so the button
    # overhangs the popover's own background and border.
    "short-panel": lambda panel, tail: (dict(panel, height=332.859375), tail),
}


def capture_boxes(plant=None):
    """(panel box, tail box) from the checked-in help capture."""
    with open(STATE, encoding="utf-8") as handle:
        bindings = json.load(handle)["layout_bindings"]
    panel = tail = None
    for binding in bindings:
        key = [(step["tag"], step["index"]) for step in binding["path"]]
        if key == PANEL_PATH:
            panel = binding["box"]
        elif (len(key) == len(PANEL_PATH) + 1
              and key[:len(PANEL_PATH)] == PANEL_PATH
              and key[-1][0] == TAIL_TAG):
            tail = binding["box"]
    if plant:
        panel, tail = CAPTURE_PLANTS[plant](panel, tail)
    return panel, tail


# -- THE TAIL'S CAPTION, AND THE ONE CONSTRUCTION THAT CENTRES IT ---------
#
# A lowercase `<button>` is already built as a Row with `align_items: center`
# and its caption in flow, so button text is centred by construction -- until
# the button carries a CAPTURED BOX. Then `fillCapturedCaption2` re-pins the
# caption `position: absolute` with all four insets 0 to hold it at the width
# its line layout was measured against, the caption fills the whole box, and a
# Label defaults to `TextVerticalAlign::top`.
#
# This button carries a captured box (it must -- see TAIL above), so its text
# rode 7.100px high in a 31.2px box: ink 769.800..777.800 against a box centre
# of 780.900, with 1.500px of clear above and 15.700px below. Adding
# `alignItems` changed the ink by zero, measured.
#
# The fix is NESTED MARKUP: with no `asText(props.children)` the stub caption
# becomes a zero-contribution overlay and the authored span is a real flex
# child of a Row that was already centring. Measured after: ink
# 776.800..784.800, centre 780.800 against 780.900 -- 8.500 clear above,
# 8.700 below, and 0 of 387,840 pixels changed across the thirteen shortcut
# rows above it.
#
# `aria-label` is part of the same rule, not a separate nicety: the runtime
# calls `setAccessibilityLabel` only `if (text)`, and nesting the caption takes
# the button's own text away. Without it the control goes unnamed, which no
# screenshot and no centring measurement can see.
TAIL_CAPTION = '"data-spectr-help-learn-more-label": true,'
TAIL_LABEL = '"aria-label": "Learn more",'

# -- THE WHEEL DOES NOT GO THROUGH REACT ---------------------------------
#
# The panel scrolls itself, and while the offset was React state every wheel
# sample was a commit -- and any commit that dirties the materialized tree
# re-applies the WHOLE captured atlas before the layout pass behind it.
# Measured on the built standalone, 48 samples through the host's own
# `deliver_mouse_wheel`, with the atlas hook swapped for a counting no-op in
# the second arm (it counted 48 of 48, so the swap provably took):
#
#     with the atlas re-apply     p50 42.167 ms per sample
#     without it                  p50 13.011 ms
#
# The offset moves one node's margin and one thumb's top, neither of which any
# captured binding describes, so it is written straight to the nodes:
#
#     BEFORE   p50 20.166 / 20.413 / 20.221 ms   (three runs)
#     AFTER    p50  0.023 /  0.025 /  0.024 ms
#
# Corroborated by Perfetto, on a trace SDK built from the EXACT pinned Pulp sha
# (3563c835, v0.850.0 -- 43 perfetto symbols; the released pin of the same sha
# links 0 and can never be traced, so this needs a local build). Native spans
# only, because api_registry wrapping makes JS-opened durations and parentage
# untrustworthy:
#
#                              BEFORE     AFTER
#     dom_event_dispatch          48         48   <- the control: same work driven
#     avg per sample          20,251 us      27 us
#     js_native (bridge calls) 31,584         96
#         ... per sample             658          2   <- the two style writes
#     layout_children             290          2
#     yoga_calculate            81 ms       0 ms
#
# 658 bridge calls per wheel sample is the captured atlas being re-applied:
# ~123 active bindings at five writes plus two getLayoutBoxMetrics reads each.
#
# and the SCROLL ITSELF IS UNCHANGED, which is the half that matters: in both
# arms the mid-burst frame differs from the top by 15.25% of the viewport, the
# end frame differs by 0.00% (the burst is symmetric), 0 of 56,000 pixels
# differ below the viewport's bottom edge, and twelve ArrowDown presses move
# 201,367 pixels -- the same number to the pixel.
#
# All three markers are checked because each failure is silent in a different
# way. Without the writer the wheel goes back to a commit per sample and only
# costs more. Without the content ref the writer can never find its node, so
# the wheel does NOTHING while every other marker is still in place. Without
# the thumb ref the content moves and the scrollbar sits frozen at the top,
# which a still frame cannot tell from a correct one.
IMPERATIVE_WRITE = ("if (content && content.style) "
                    "content.style.marginTop = -next;")
CONTENT_REF = "ref: contentRef,"
THUMB_REF = "ref: thumbRef,"
REACT_OFFSET = "setScrollTop"

# -- THE BODY IS BUILT ONCE, NOT ONCE PER WHEEL SAMPLE --------------------
#
# The panel scrolls itself, so a wheel sample is a `setScrollTop` and a full
# re-render. Unmemoised, that re-parses the help asset and re-creates ~90 spans
# per sample for the reconciler to diff against unchanged twins. The memo makes
# the elements reference-identical so React bails out of them instead.
#
# Checked here because it is invisible: an unmemoised guide renders pixel for
# pixel the same and only costs more, so nothing else in this repository would
# notice it being undone.
BODY_MEMO = "var body = React.useMemo(function () {"
BLOCKS_MEMO = "var blocks = React.useMemo(spectrHelpBlocks, [helpText]);"
MEMO_DEPS = "}, [blocks, textW]);"

# -- THE COPY AFFORDANCE --------------------------------------------------
#
# Structural, deliberately. "Does not interfere with the x" is proved by WHERE
# the button sits in the tree -- inside the title's left group, with the close
# button still the header's last child under `space-between` -- rather than by
# a pixel gap, which would need re-measuring on every font change. Measured on
# the built standalone when this landed: title right edge 532, copy 542..634,
# close 893 -- 302px of clearance.
COPY_BUTTON = '"data-spectr-help-copy": true,'
COPY_GROUP = '"data-spectr-help-guide-titlegroup": true,'
COPY_VERB = 'postMessage("clipboard_write"'
COPY_PLAIN = "function spectrHelpPlainText() {"
COPY_CONFIRM = 'settle("Copied");'
TITLE_IS_ABOUT = "# About Spectr"

PLANTS = {
    # The affordance disappears: the popover is a keycap list again with no way
    # into the guide, which is the state this whole lane exists to leave.
    "no-learn-more": lambda h, a: (h.replace(LEARN_MORE, ""), a),
    # The button is still there and still does nothing -- the shape the SHORTCUTS
    # popover's own "A to cycle" had for months, and the one a screenshot cannot
    # tell from a working one.
    "dead-button": lambda h, a: (h.replace(LEARN_CALLBACK, "void 0"), a),
    # The copy leaks back into the artifact. A wording change would then mean
    # patching the compiled document.
    "inline-copy": lambda h, a: (
        h.replace(READS_ASSET,
                  '"Think of it as a precise way to reach into a sound" && true'), a),
    # The viewport clips but nothing can move the content: everything past the
    # first screenful is unreachable, and the panel still looks correct in a
    # screenshot of its first screenful.
    "frozen-content": lambda h, a: (h.replace(CONTENT_OFFSET, "marginTop: 0"), a),
    "no-wheel": lambda h, a: (h.replace(WHEEL, "onWheelDisabled: function (event) {"), a),
    # The rail loses the name the overlay measures, so the guide has no way to
    # find the editor box and paints at the anchor's own position, across the
    # bottom toolbar.
    "unnamed-rail": lambda h, a: (h.replace(RAIL_NAMED, ""), a),
    # The copy's own rules, and the figure the code reports.
    "em-dash": lambda h, a: (h, a.replace("Good to know", "Good to know — really")),
    "stale-latency": lambda h, a: (h, a.replace("213 ms", "170.65 ms")),
    # A backtick would end the template literal early and take the rest of the
    # guide with it, silently.
    "backtick": lambda h, a: (h, a.replace("## Zooming", "## Zoo`ming")),
    # A DIFFERENT WRONG IMPLEMENTATION of the same thing: the anchor collapses
    # back to a point. It still paints, because the scrim is absolute -- and
    # nothing in it can be touched, because Rect::contains is half-open so a
    # 0x0 rect contains no point at any coordinate.
    "collapsed-anchor": lambda h, a: (
        h.replace(ANCHOR_BOX,
                  'style: { width: 0, height: 0, flexShrink: 0,'
                  ' overflow: "visible" }'), a),
    # The anchor keeps its box but sinks under the status banner (6), so the
    # band readout paints over the guide again.
    "sunken-anchor": lambda h, a: (h.replace(ANCHOR_Z, "zIndex: 4"), a),
    # The scrim pays the rail offset twice and paints a screen above the editor.
    # The affordance vanishes and the guide is read-only again.
    "no-copy": lambda h, a: (h.replace(COPY_BUTTON, ""), a),
    # Wired to a verb no handler answers: it sits on "Copying" forever and
    # never confirms -- indistinguishable from working in a screenshot.
    "dead-copy": lambda h, a: (
        h.replace(COPY_VERB, 'postMessage("clipboard_nowhere"'), a),
    # It copies, and never tells anyone it did.
    "silent-copy": lambda h, a: (h.replace(COPY_CONFIRM, 'settle("Copy");'), a),
    # The button leaves the title group, so `space-between` drives it across
    # the header and against the close button -- what the report asked to avoid.
    "copy-in-the-corner": lambda h, a: (h.replace(COPY_GROUP, '"data-x": true,'), a),
    # Raw markup on the clipboard: `## Zooming` and `**Sculpt**` in a notes app.
    "markup-leaks": lambda h, a: (
        h.replace(COPY_PLAIN, "function spectrHelpPlainTextUnused() {"), a),
    "stale-title": lambda h, a: (h, a.replace(TITLE_IS_ABOUT, "# What Spectr does")),
    "double-offset": lambda h, a: (
        h.replace(SCRIM_ROOT,
                  '      top: origin.y,\n      left: origin.x,\n'
                  '      width: vw,\n      height: vh,\n'), a),
    # A DIFFERENT WRONG IMPLEMENTATION of the centring, and the one that
    # shipped: the caption goes back to being the button's own text. Nothing is
    # deleted and nothing looks broken -- the label is still there, still
    # horizontally centred, still the right colour and size, and still 7.1px
    # too high. Reconstructed from the nested form rather than pasted, so it
    # cannot drift away from the text it is supposed to invert.
    "flat-caption": lambda h, a: (flatten_caption(h), a),
    # Nested markup WITHOUT the label the nesting took away. Centred, pretty,
    # and unnamed to assistive technology.
    "unnamed-tail": lambda h, a: (h.replace(TAIL_LABEL, ""), a),
    # The body is rebuilt on every wheel sample again. Pixel-identical.
    "unmemoised-body": lambda h, a: (
        h.replace(BODY_MEMO, "var body = (function () {"), a),
    # A DIFFERENT WRONG IMPLEMENTATION: correct, and 840x slower. The offset
    # goes back through React state, so every wheel sample commits and every
    # commit re-applies the captured atlas. Nothing looks wrong in a frame.
    "react-state-offset": lambda h, a: (
        h.replace(IMPERATIVE_WRITE,
                  "setScrollTop(next);").replace(
                      "var offsetRef = React.useRef(0);",
                      "var [scrollTop, setScrollTop] = React.useState(0);"), a),
    # The writer survives and can never reach its node: the wheel moves
    # nothing at all, cheaply.
    "unreffed-content": lambda h, a: (h.replace(CONTENT_REF, ""), a),
    # The content moves and the scrollbar does not, which no still frame and no
    # timing measurement can see.
    "unreffed-thumb": lambda h, a: (h.replace(THUMB_REF, ""), a),
    "unmemoised-blocks": lambda h, a: (
        h.replace(BLOCKS_MEMO, "var blocks = spectrHelpBlocks();"), a),
    # The memo that can never retry: a one-frame asset race becomes a permanent
    # "The help content asset did not load." with every marker still in place.
    "frozen-blocks-memo": lambda h, a: (
        h.replace(BLOCKS_MEMO, "var blocks = React.useMemo(spectrHelpBlocks, []);"), a),
}


CAPTION_NEST_AT = ('  }, /* @__PURE__ */ React.createElement("span", {\n'
                   '    "data-spectr-help-learn-more-label": true,')
CAPTION_NEST_END = '"Learn more \\u2192")));'
CAPTION_FLAT = '  }, "Learn more \\u2192"));'


def flatten_caption(html):
    """Undo the nesting: hand the caption back to the button as its own text.

    Rebuilt from the shipping text instead of carrying a second copy of it, so
    a later wording or style change cannot leave this plant silently matching
    nothing -- which would turn a plant that proves the rule into one that
    proves the rule is unreachable.
    """
    start = html.find(CAPTION_NEST_AT)
    if start < 0:
        return html
    end = html.find(CAPTION_NEST_END, start)
    if end < 0:
        return html
    return html[:start] + CAPTION_FLAT + html[end + len(CAPTION_NEST_END):]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plant", choices=sorted(PLANTS))
    ap.add_argument("--plant-capture", choices=sorted(CAPTURE_PLANTS))
    args = ap.parse_args()

    with open(DOC, encoding="utf-8") as handle:
        html = json.load(handle)["html"]
    with open(ASSET, encoding="utf-8") as handle:
        asset = handle.read()

    # CONTROL, read BEFORE any plant. Every rule below is about the help
    # popover, the guide and the copy asset; a run that cannot find all three is
    # one where "no problems found" would be a statement about the instrument.
    control = (html.count("function HelpPopover(")
               + html.count("function HelpGuideOverlay(")
               + (1 if "SPECTR_HELP_TEXT" in asset else 0))
    print("control: %d of 3 surfaces located (popover + guide + copy asset)"
          % control)
    if control != 3:
        print("FAIL: expected 3, found %d -- the detector is reading the wrong "
              "document or the wrong asset" % control, file=sys.stderr)
        return 1

    if args.plant:
        planted_html, planted_asset = PLANTS[args.plant](html, asset)
        if planted_html == html and planted_asset == asset:
            print("FAIL: plant %r changed nothing, so it proves nothing"
                  % args.plant, file=sys.stderr)
            return 1
        html, asset = planted_html, planted_asset
        print("planted: %s" % args.plant)

    bad = []

    # -- ROUTE ------------------------------------------------------------
    route = {
        "Learn more control": html.count(LEARN_MORE),
        "control calls back": html.count(LEARN_CALLBACK),
        "Chrome wires the callback": html.count(LEARN_WIRED),
        "Chrome mounts the guide": html.count(GUIDE_MOUNT),
    }
    print("  ROUTE  " + ", ".join("%s=%d" % kv for kv in route.items()))
    for label, count in route.items():
        if count != 1:
            bad.append("%s appears %d times, expected 1 -- the `?` popover does "
                       "not reach the guide" % (label, count))

    # -- REACH ------------------------------------------------------------
    reach = {
        "anchor box": html.count(ANCHOR_BOX),
        "anchor above the banner": html.count(ANCHOR_Z),
        "scrim in root space": html.count(SCRIM_ROOT),
    }
    print("  REACH  " + ", ".join("%s=%d" % kv for kv in reach.items()))
    for label, count in reach.items():
        if count != 1:
            bad.append("%s appears %d times, expected 1 -- the guide paints but "
                       "cannot be touched" % (label, count))

    # -- TAIL, from the capture rather than the source --------------------
    if args.plant_capture:
        print("planted: %s (capture)" % args.plant_capture)
    panel_box, tail_box = capture_boxes(args.plant_capture)
    if panel_box is None:
        bad.append("the help capture has no panel binding -- wrong capture")
    elif tail_box is None:
        bad.append("the help capture gives the Learn more button no box, so it "
                   "falls to the panel's content-box origin and prints across "
                   "the first shortcut rows")
        print("  TAIL   panel h=%.6f, tail=ABSENT" % panel_box["height"])
    else:
        room = panel_box["height"] - (tail_box["top"] + tail_box["height"])
        print("  TAIL   panel h=%.6f, tail top=%.6f h=%.6f, room below=%.6f"
              % (panel_box["height"], tail_box["top"], tail_box["height"], room))
        if room < 0:
            bad.append("the panel is %.3fpx too short for its own tail -- the "
                       "button overhangs the popover" % -room)
        if tail_box["width"] <= 0 or tail_box["height"] <= 0:
            bad.append("the tail's captured box is empty")

    # -- CAPTION ----------------------------------------------------------
    caption = {
        "caption is nested markup": html.count(TAIL_CAPTION),
        "button names itself": html.count(TAIL_LABEL),
    }
    print("  CAPTION " + ", ".join("%s=%d" % kv for kv in caption.items()))
    for label, count in caption.items():
        if count != 1:
            bad.append("%s appears %d times, expected 1 -- a captured-box "
                       "button's own text is stretched over the whole box and "
                       "painted from its top edge" % (label, count))

    # -- OFFSET -----------------------------------------------------------
    offset_rules = {
        "writes the offset to the node": html.count(IMPERATIVE_WRITE),
        "content is reffed": html.count(CONTENT_REF),
        "thumb is reffed": html.count(THUMB_REF),
    }
    print("  OFFSET " + ", ".join("%s=%d" % kv for kv in offset_rules.items()))
    for label, count in offset_rules.items():
        if count != 1:
            bad.append("%s appears %d times, expected 1 -- the wheel is back to "
                       "a React commit per sample, or its writer cannot reach "
                       "what it moves" % (label, count))
    react_offset = html.count(REACT_OFFSET)
    print("  OFFSET react state for the offset: %d (expected 0)" % react_offset)
    if react_offset != 0:
        bad.append("the scroll offset is React state again (%d occurrence(s)): "
                   "every wheel sample commits, and every commit re-applies the "
                   "whole captured atlas" % react_offset)

    # -- BODY -------------------------------------------------------------
    body = {
        "body memoised": html.count(BODY_MEMO),
        "blocks memoised": html.count(BLOCKS_MEMO),
        "memo closes over blocks and width": html.count(MEMO_DEPS),
    }
    print("  BODY   " + ", ".join("%s=%d" % kv for kv in body.items()))
    for label, count in body.items():
        if count != 1:
            bad.append("%s appears %d times, expected 1 -- the guide rebuilds "
                       "its whole body on every wheel sample" % (label, count))

    # -- AFFORD -----------------------------------------------------------
    afford = {
        "copy button": html.count(COPY_BUTTON),
        "in the title group": html.count(COPY_GROUP),
        "calls the clipboard verb": html.count(COPY_VERB),
        "copies prose": html.count(COPY_PLAIN),
        "confirms": html.count(COPY_CONFIRM),
    }
    print("  AFFORD " + ", ".join("%s=%d" % kv for kv in afford.items()))
    for label, count in afford.items():
        if count != 1:
            bad.append("%s appears %d times, expected 1 -- the guide cannot be "
                       "copied, or copies without saying so" % (label, count))
    # Declaration ORDER is the "does not interfere with the x" guarantee.
    at_copy = html.find(COPY_BUTTON)
    at_close = html.find('"data-spectr-help-guide-close"')
    if at_copy >= 0 and at_close >= 0 and at_copy > at_close:
        bad.append("the copy button is declared after the close button, so "
                   "`space-between` puts it against the x")
    if TITLE_IS_ABOUT not in asset:
        bad.append("the guide is not titled %r" % TITLE_IS_ABOUT)

    # -- SCROLL -----------------------------------------------------------
    scroll = {
        "viewport": html.count(VIEWPORT),
        "clips": html.count(CLIPS),
        "content offset": html.count(CONTENT_OFFSET),
        "wheel": html.count(WHEEL),
        "keys": html.count(KEYS),
        "rail named": html.count(RAIL_NAMED),
        "rail geometry": html.count(RAIL_GEOMETRY),
        "rail measured": html.count(RAIL_MEASURED),
    }
    print("  SCROLL " + ", ".join("%s=%d" % kv for kv in scroll.items()))
    for label, count in scroll.items():
        if count != 1:
            bad.append("scroll contract %r appears %d times, expected 1 -- a "
                       "panel that clips without moving discards everything "
                       "below the fold" % (label, count))

    # -- ASSET ------------------------------------------------------------
    reads = html.count(READS_ASSET)
    print("  ASSET  document reads the asset: %d" % reads)
    if reads != 1:
        bad.append("the overlay does not read globalThis.SPECTR_HELP_TEXT, so "
                   "its copy is not coming from the bundled asset")
    leaked = [m for m in COPY_MARKERS if m in html]
    if leaked:
        bad.append("the copy has leaked into the compiled document (%s); a "
                   "wording change would then mean patching a checked-in "
                   "one-line artifact" % leaked[:2])
    missing = [m for m in COPY_MARKERS if m not in asset]
    if missing:
        bad.append("the asset is missing approved copy: %s" % missing)

    # -- COPY -------------------------------------------------------------
    body = asset.split("globalThis.SPECTR_HELP_TEXT = `", 1)
    text = body[1].rsplit("`;", 1)[0] if len(body) == 2 else ""
    heads = [h for h in HEADINGS if ("## " + h) not in text]
    # Counted in the BODY, not the file: the file's own header comment
    # documents the markup and necessarily mentions a backtick and a `${`.
    # Only a stray one INSIDE the literal can truncate the guide.
    print("  COPY   %d chars, %d/%d headings, em-dash=%d backtick=%d dollar=%d"
          % (len(text), len(HEADINGS) - len(heads), len(HEADINGS),
             text.count("\u2014"), text.count("`"), text.count("${")))
    if not text:
        bad.append("the asset does not assign a template literal to "
                   "globalThis.SPECTR_HELP_TEXT")
    if heads:
        bad.append("the copy is missing approved sections: %s" % heads)
    if "—" in text:
        bad.append("the copy contains an em dash, which this copy does not use")
    if "`" in text:
        bad.append("the copy contains a backtick; it would end the template "
                   "literal early and silently truncate the guide")
    if "${" in text:
        bad.append("the copy contains `${`, which the template literal would "
                   "interpolate rather than print")
    if "213 ms" not in text:
        bad.append("the copy does not state the latency the code reports: "
                   "kSpectralFftSize + kSpectralAnalysisHop = 8192 + 2048 = "
                   "10240 samples, 213 ms at 48 kHz")

    if bad:
        for line in bad:
            print("FAIL: " + line, file=sys.stderr)
        return 1
    print("PASS: the ? popover reaches the guide, the guide clips and moves its "
          "own content, and the approved copy lives in the bundled asset rather "
          "than the compiled document")
    return 0


if __name__ == "__main__":
    sys.exit(main())
