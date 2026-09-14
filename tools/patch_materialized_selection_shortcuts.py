#!/usr/bin/env python3
"""Additive marquee selection, and a SHORTCUTS panel that cannot wrap.

Applied by hand to the shipping pair, because no recipe in this repo
reproduces the checked-in materialized document (danielraffel/spectr#48) and
both generators are broken on main. This script IS the durable record of the
change; re-running it after a successful pass reports "already applied".

WHAT IT CHANGES, and why each half needs the other:

1. BEHAVIOUR (native-ui/materialized/materialized-document.runtime.json).
   The marquee already stored its result in a `Set`, so a discontiguous
   selection was always representable -- only the input handler threw the old
   set away on every pointer sample. Holding SHIFT with the marquee modifier
   now freezes the selection the gesture started from and TOGGLES the covered
   bands against that frozen base, so one drag can add a new range and take
   back a range it already holds. Recomputing from the frozen base on every
   sample (rather than toggling live) is what keeps the drag idempotent: a
   live toggle flips a band again on every sample it stays inside, so the
   selection would strobe while the pointer is still.

   The modifier the handler actually reads is `e.metaKey || e.ctrlKey`, so
   Command and Control are both already accepted and the panel's existing
   Command notation stays correct.

2. THE PANEL'S OWN GEOMETRY (native-ui/materialized/runtime.js, mirrored into
   native-ui/materialized/states/help.materialized.json).
   The help panel is NOT laid out live natively. Its captured layout bindings
   pin every row's absolute position and explicit width/height, and a text
   binding pins each row's line boxes -- proved by measuring the shipping
   standalone: every captured row top reproduced to the exact fraction of a
   pixel. So shortening a label in the React source alone leaves its row
   pinned at the two-line height it was captured with: correct text, wrong
   box. The capture has to move with the label.

   A text binding is skipped when the live text no longer equals the captured
   text (runtime.js counts it as `text_content_mismatch` and falls back to
   live metrics), which is why the change is safe in either order -- but a
   skipped binding is a silently stale capture, not a fix.

3. WIDTH HEADROOM. Every captured single-line row width in this panel is
   exactly `6.5 * len(text)`, and the wrap budget is the row width minus the
   description's left offset: 250 - 94 = 156px, i.e. 24 characters. The
   longest surviving row ("Mute/unmute band range") is 22. That is not
   headroom, it is a coincidence waiting to expire, and a row that outgrows
   it wraps SILENTLY. The panel goes to 330px wide (budget 206px / 31
   characters) and tools/spectr-detectors/shortcut_panel_single_line.py
   asserts the bound so the next entry fails loudly instead.

4. TWO STALE CHIP CAPTURES. The `1 / 2 / 3` and `4 / 5` chips were relabelled
   to `S / L / B` and `F / G` in the source without their captures following,
   so both render on live metrics today (13.8px line box against the 12px
   every other chip gets). Both replacements have the identical glyph count,
   so this is a pure text swap with the captured boxes untouched.

5. ONE NOTATION FOR MODIFIER KEYS. The panel spelled SHIFT and ALT as words
   and Command and Shift as glyphs two rows later -- the same modifier drawn
   two ways in one twelve-row list. Every row now uses the word form
   (`CMD`, `SHIFT`, `ALT`), because ten of the twelve already did and because
   a glyph is only legible to a reader who already knows it.

   That makes `CMD+SHIFT+DRAG` the longest key in the panel at 14 characters,
   and the chip it sits in was a fixed 84px holding at most 11. A chip is
   `white-space: nowrap`, so it does not wrap -- it OVERFLOWS its captured
   box and lands on top of the description beside it. So the chip column goes
   to 104px (15 characters) and the panel goes with it, 330 -> 350, which
   leaves the description budget exactly where the previous pass put it
   (206px) rather than paying for the chip out of the description's headroom.
   tools/spectr-detectors/shortcut_panel_single_line.py now measures the chip
   column too; it only ever measured descriptions, which is why an 84px chip
   could not have been caught.

NOT TOUCHED, deliberately: resources/editor.html and
native-ui/materialized/materialized-document.json. Neither is a shipping
native asset (CMakeLists refuses editor.html for `spectr_native_assets` by
name), both carry the pre-compile JSX rather than the React.createElement
form that ships, and both have ALREADY diverged from the shipping document
here -- their help panel still advertises `DBL-CLICK` for a row the shipping
document calls `CLICK`. Editing them would add merge surface across the live
lanes and change nothing that ships.
"""
import copy
import json
import math
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCUMENT = os.path.join(REPO, 'native-ui/materialized/materialized-document.runtime.json')
RUNTIME = os.path.join(REPO, 'native-ui/materialized/runtime.js')
HELP_STATE = os.path.join(REPO, 'native-ui/materialized/states/help.materialized.json')

# ---------------------------------------------------------------- behaviour

def enc(snippet):
    """A JS snippet as it appears inside the document's JSON `html` string."""
    return json.dumps(snippet)[1:-1]


def predecessors(old):
    """An edit's `old` is one text, or several ordered OLDEST FIRST.

    This script is replayed against documents at different revisions -- a
    freshly generated one, and the checked-in artifact that already carries an
    earlier revision of the same edit. Naming every predecessor keeps ONE
    writer for a patch point instead of a second edit competing for the same
    line and then fighting it on the next run.
    """
    return (old,) if isinstance(old, str) else tuple(old)


def choose(old, raw):
    """The most advanced predecessor present, or a reason there is none.

    Newest first: an older predecessor is often still a substring of a newer
    one, so "the first candidate that matches" would re-apply from a state the
    document has already moved past.
    """
    for cand in reversed(predecessors(old)):
        count = raw.count(enc(cand))
        if count == 1:
            return cand, None
        if count > 1:
            return None, ('patch point %r occurs %d times, expected 1'
                          % (cand, count))
    return None, 'no known predecessor text is present'


DOCUMENT_EDITS = [
    # The marquee remembers what it started from, but only under SHIFT. A bare
    # Command drag keeps replacing, which is what every other marquee does.
    ('marquee captures its additive base',
     '''    if (meta) {
      pointerRef.current = { mode: "marquee", startX: x, startY: y };
      setMarquee({ x1: x, y1: y, x2: x, y2: y });
      return;
    }''',
     '''    if (meta) {
      pointerRef.current = {
        mode: "marquee",
        startX: x,
        startY: y,
        // Frozen at press, not read live. The move handler rebuilds the
        // whole selection from this base on every sample, so the drag is
        // idempotent: a band the rectangle still covers must not flip again
        // just because the pointer moved a pixel inside it.
        baseSelection: shift ? new Set(selection) : null
      };
      setMarquee({ x1: x, y1: y, x2: x, y2: y });
      return;
    }''',
     'baseSelection: shift ? new Set(selection) : null'),

    ('marquee toggles against its base instead of replacing',
     '''    if (p.mode === "marquee") {
      setMarquee({ x1: p.startX, y1: p.startY, x2: x, y2: y });
      const x1 = Math.min(p.startX, x), x2 = Math.max(p.startX, x);
      const sel = /* @__PURE__ */ new Set();
      for (let i = 0; i < N; i++) {
        const cx = bandCenterX(i, g);
        if (cx >= x1 && cx <= x2) sel.add(i);
      }
      setSelection(sel);
      return;
    }''',
     '''    if (p.mode === "marquee") {
      setMarquee({ x1: p.startX, y1: p.startY, x2: x, y2: y });
      const x1 = Math.min(p.startX, x), x2 = Math.max(p.startX, x);
      // No base: the plain Command marquee, which replaces. With a base: the
      // covered bands are XORed against it, so the same drag adds a range
      // the selection does not hold and removes one it does.
      const base = p.baseSelection;
      const sel = base ? new Set(base) : /* @__PURE__ */ new Set();
      for (let i = 0; i < N; i++) {
        const cx = bandCenterX(i, g);
        if (cx < x1 || cx > x2) continue;
        if (base && sel.has(i)) sel.delete(i);
        else sel.add(i);
      }
      setSelection(sel);
      return;
    }''',
     'const base = p.baseSelection;'),

    # The selection is the thing the gesture is FOR, and nothing could read it.
    # Asserting that a handler ran proves the handler ran.
    ('the render hook exposes the selection set',
     '''      unmutePulse: Array.from(unmutePulseRef.current),
      snapshots: {''',
     '''      unmutePulse: Array.from(unmutePulseRef.current),
      selection: Array.from(selection).sort((a, b) => a - b),
      snapshots: {''',
     'selection: Array.from(selection).sort((a, b) => a - b),'),

    # "mode-dependent" was the longest string in the panel and bought nothing
    # the three mode chips above it do not already say.
    ('the DRAG row stops wrapping',
     'React.createElement(Hrow, { k: "DRAG" }, "Edit bands (mode-dependent)")',
     'React.createElement(Hrow, { k: "DRAG" }, "Edit bands")',
     'React.createElement(Hrow, { k: "DRAG" }, "Edit bands")'),

    # SHIFT+CLICK never modified the selection. `shiftKey` is read in exactly
    # one pointer handler and it starts the mute brush -- which the row two
    # above already documents as SHIFT+DRAG. The row was advertising a gesture
    # that does not exist, so it is replaced by the one that now does.
    ('the dead SHIFT+CLICK row becomes the gesture that exists, spelled out',
     ('React.createElement(Hrow, { k: "\\u21E7+CLICK" }, "Add/remove from selection")',
      'React.createElement(Hrow, { k: "\\u2318\\u21E7+DRAG" }, "Add/remove selection")'),
     'React.createElement(Hrow, { k: "CMD+SHIFT+DRAG" }, "Add/remove selection")',
     'React.createElement(Hrow, { k: "CMD+SHIFT+DRAG" }, "Add/remove selection")'),

    # 250 - 94 = 156px of description, i.e. 24 characters, against a longest
    # surviving row of 22. That was not headroom; 330 bought 206px / 31, and
    # 350 keeps that same 206px after the chip column took 20px (see below).
    # Both earlier widths are named so this replays from either.
    ('the panel is wide enough for its rows',
     ('minWidth: 280,', 'minWidth: 330,'), 'minWidth: 350,', 'minWidth: 350,'),

    # One notation for the whole panel. Ten rows already spelled their
    # modifier; these two drew it as a glyph. The word form wins because a
    # glyph teaches nobody, and because SHIFT and ALT could not become glyphs
    # without inventing a second convention for ALT on non-Apple keyboards.
    ('the marquee row spells its modifier',
     'React.createElement(Hrow, { k: "\\u2318+DRAG" }, "Marquee select")',
     'React.createElement(Hrow, { k: "CMD+DRAG" }, "Marquee select")',
     'React.createElement(Hrow, { k: "CMD+DRAG" }, "Marquee select")'),

    # A chip is nowrap, so an oversized key does not wrap -- it overflows the
    # box the capture pinned and prints over the description. 84px held 11
    # characters and CMD+SHIFT+DRAG is 14. 104px holds 15.
    ('the key chip fits the longest key',
     'fontSize: 9,\n    minWidth: 84,\n    textAlign: "center",',
     'fontSize: 9,\n    minWidth: 104,\n    textAlign: "center",',
     'fontSize: 9,\n    minWidth: 104,\n    textAlign: "center",'),

    # The row that advertises group mute on `m`. It goes LAST so every
    # existing row keeps the top it was captured at (see MUTE_SEL_ROW below),
    # and it lands immediately before the Learn more button, which is the
    # panel's tail.
    #
    # The chip is one character, so the 104px column absorbs it with room to
    # spare; the description is 21 characters = 136.5px against a 206px
    # budget. Both are re-checked by shortcut_panel_single_line.py, whose
    # --budget-only half needs no binary and protects exactly the row nobody
    # has written yet.
    ('the panel advertises group mute on `m`',
     'React.createElement(Hrow, { k: "DRAG SEL" }, "Group move"), '
     '/* @__PURE__ */ React.createElement("button", {',
     'React.createElement(Hrow, { k: "DRAG SEL" }, "Group move"), '
     '/* @__PURE__ */ React.createElement(Hrow, { k: "M" }, '
     '"Mute/unmute selection"), '
     '/* @__PURE__ */ React.createElement("button", {',
     'React.createElement(Hrow, { k: "M" }, "Mute/unmute selection")'),
]

# ------------------------------------------------------- captured help panel
#
# Every number below was read off the checked-in capture, and `classify`
# re-derives that capture from them before anything is written. A model that
# cannot reproduce what is already there has no business emitting a
# replacement.

PANEL = ('div', 0), ('div', 3), ('div', 16), ('div', 1)
PANEL_PAD = 14.0
PANEL_BORDER = 1.0
PANEL_RIGHT = 26.0          # box right edge, pinned under the help button
PANEL_BOTTOM = -8.0         # box bottom edge, likewise
TITLE_TOP = 15.0
TITLE_H = 15.296875
TITLE_GAP = 8.0
ROW_H_SINGLE = 23.296875
ROW_H_WRAPPED = 38.0
CHIP_W = 84.0               # the chip column before this change
FINAL_CHIP_W = 104.0        # 14-character CMD+SHIFT+DRAG needs 96.61; 104 holds 15
CHIP_GAP = 10.0             # Hrow's flex gap, so DESC_LEFT is CHIP_W + CHIP_GAP
CHIP_PAD_X = 6.0            # the chip's own horizontal padding
CHIP_BORDER = 1.0
CHIP_FONT_SIZE = 9.0
CHIP_LETTER_SPACING = 0.5
CHIP_TEXT_TOP = 3.0
CHIP_TEXT_H = 12.0
MONO_ADVANCE_RATIO = 0.6    # JetBrains Mono's advance, as a fraction of em
MONO_FACE = 'JetBrainsMono-Regular'
MONO_FAMILY = 'JetBrains Mono'
CHIP_H = 19.296875
CHIP_TOP_SINGLE = 2.0
CHIP_TOP_WRAPPED = 9.34375
DESC_LEFT = 94.0
FINAL_DESC_LEFT = FINAL_CHIP_W + CHIP_GAP
DESC_TOP_SINGLE = 3.140625
DESC_H_SINGLE = 17.0
DESC_TOP_WRAPPED = 2.0
DESC_H_WRAPPED = 34.0
DESC_LINE_H = 13.0
CHAR_W = 6.5                # exact for every captured ASCII single-line row

# THE POPOVER'S TAIL.
#
# `patch_materialized_help_overlay.py` owns the Learn more BUTTON ITSELF -- its
# markup, its handler, its style. This file owns the popover's CAPTURE, and the
# capture has to know the button exists or the button has no box.
#
# It shipped without one. Every captured child of this panel is pinned
# absolutely, so the in-flow cursor never advances past them, and a child the
# capture does not name lands at the CONTENT-BOX ORIGIN instead of after its
# siblings. Measured on the built standalone: the button rendered at root
# y=503.64 when the content box starts at 493.64 -- exactly its own `marginTop:
# 10` from the top of the panel, 302.859375px above where it belongs, printing
# across the first two shortcut rows with 17.90px of overlap on the first. Its
# paint order is above theirs, so both texts occupied the same pixels.
#
# 302.859375 is not a coincidence: it is TITLE_H + TITLE_GAP + 12 * ROW_H,
# i.e. the entire height the in-flow cursor failed to accumulate.
#
# TAIL_H is MEASURED, not derived. The button does not inherit the panel's
# `lineHeight: 1.7` -- its line box measures 15.2, not 17.0 -- so deriving it
# would have made the panel 1.8px too tall.
TAIL_MARGIN = 10.0          # the button's own marginTop
TAIL_H = 31.2               # 15.2 line box + 7px padding a side + 1px border a side
# The tail is keyed CANONICALLY rather than by its sibling index, because that
# index is a function of how many rows precede it: twelve rows put the button
# at 13, thirteen put it at 14. Keying it by position meant a capture written
# for one row count became unreadable the moment the count changed -- the
# binding resolved to None, was skipped by every rewrite, and survived with a
# stale box AND a stale path. `tail_index` is the one place that lives now.
TAIL_KEY = ('tail',)


def tail_index(rows):
    """The button's sibling index: the title, then one div per row."""
    return len(rows) + 1

OLD_PANEL_W = 280.0
NEW_PANEL_W = 330.0
# The chip column took 20px, so the panel takes 20px. Paying for the chips out
# of the description budget instead would have silently undone the headroom
# the previous pass bought, and nothing measures a budget that merely shrank.
FINAL_PANEL_W = 350.0


def ceil64(value):
    """Round up to a 64th of a pixel."""
    return math.ceil(round(value * 64.0, 6)) / 64.0


def floor64(value):
    """Round down to a 64th of a pixel."""
    return math.floor(round(value * 64.0, 6)) / 64.0


def chip_ink(text):
    """The measured width of one chip's text, in its 9px monospaced face.

    Derived, then CHECKED against every ASCII chip already in the capture by
    `verify_chip_model` -- arithmetic nobody verified is how a panel ends up
    subtly wrong with every test green. The glyph run quantises to a 64th of a
    pixel and letter-spacing is added per glyph afterwards, which is why this
    is not simply `advance * len`.
    """
    count = len(text)
    return (ceil64(CHIP_FONT_SIZE * MONO_ADVANCE_RATIO * count)
            + CHIP_LETTER_SPACING * count)


def chip_text_left(text, chip_w):
    """Where that text sits inside a `text-align: center` chip."""
    inner = chip_w - 2.0 * (CHIP_PAD_X + CHIP_BORDER)
    return floor64((inner - chip_ink(text)) / 2.0) + CHIP_PAD_X + CHIP_BORDER


def content_w(panel_w):
    return panel_w - 2 * (PANEL_PAD + PANEL_BORDER)


def wrap_budget(panel_w):
    """How wide a description may be before the row wraps."""
    return content_w(panel_w) - DESC_LEFT


# Panel order. `width` is the description's captured width, carried verbatim
# for every row this change does not touch -- a row is not re-measured just
# because the panel moved, and `Toggle mute (-Inf)` is exactly why: its two
# non-ASCII glyphs measure 104.03125 where 6.5 * 16 would say 104.0, so a
# model that re-derived every row would silently move a row it never meant to.
BEFORE_ROWS = [
    ('Sculpt \u00b7 Level \u00b7 Boost', 143.0, False),
    ('Flare \u00b7 Glide', 84.5, False),
    ('Cycle analyzer', 91.0, False),
    ('Edit bands (mode-dependent)', None, True),
    ('Toggle mute (\u2212\u221e)', 104.03125, False),
    ('Mute/unmute band range', 143.0, False),
    ('Band context menu', 110.5, False),
    ('Zoom frequency view', 123.5, False),
    ('Pan viewport', 78.0, False),
    ('Marquee select', 91.0, False),
    ('Add/remove from selection', None, True),
    ('Group move', 65.0, False),
]
AFTER_ROWS = [
    (text, width, wrapped) for text, width, wrapped in BEFORE_ROWS]
AFTER_ROWS[3] = ('Edit bands', CHAR_W * len('Edit bands'), False)
AFTER_ROWS[10] = ('Add/remove selection',
                  CHAR_W * len('Add/remove selection'), False)


def layout_model(rows, panel_w, chip_w=CHIP_W, desc_left=DESC_LEFT, tail=False):
    """Every help-subtree box, keyed by path suffix, for one row list.

    `tail` adds the Learn more button. It is a parameter rather than always-on
    because the older states this script still has to RECOGNISE on disk predate
    the button, and a model carrying a box those captures never had would match
    none of them.
    """
    row_w = content_w(panel_w)
    boxes = {}
    top = TITLE_TOP + TITLE_H + TITLE_GAP
    boxes[(0,)] = dict(left=PANEL_PAD + PANEL_BORDER, top=TITLE_TOP,
                       width=row_w, height=TITLE_H)
    for index, (text, width, wrapped) in enumerate(rows, start=1):
        height = ROW_H_WRAPPED if wrapped else ROW_H_SINGLE
        boxes[(index,)] = dict(left=PANEL_PAD + PANEL_BORDER, top=top,
                               width=row_w, height=height)
        boxes[(index, 'span', 0)] = dict(
            left=0.0, top=CHIP_TOP_WRAPPED if wrapped else CHIP_TOP_SINGLE,
            width=chip_w, height=CHIP_H)
        boxes[(index, 'span', 1)] = dict(
            left=desc_left,
            top=DESC_TOP_WRAPPED if wrapped else DESC_TOP_SINGLE,
            width=(content_w(panel_w) - desc_left) if wrapped else width,
            height=DESC_H_WRAPPED if wrapped else DESC_H_SINGLE)
        top += height
    if tail:
        top += TAIL_MARGIN
        boxes[TAIL_KEY] = dict(left=PANEL_PAD + PANEL_BORDER, top=top,
                               width=row_w, height=TAIL_H)
        top += TAIL_H
    panel_h = top + PANEL_PAD + PANEL_BORDER
    boxes[()] = dict(left=PANEL_RIGHT - panel_w, top=PANEL_BOTTOM - panel_h,
                     width=panel_w, height=panel_h)
    return boxes


# Three states this file has held, oldest first. Every one is kept because the
# script has to recognise whichever is on disk and refuse anything else.
BEFORE = layout_model(BEFORE_ROWS, OLD_PANEL_W)
AFTER = layout_model(AFTER_ROWS, NEW_PANEL_W)
# The wide-chip panel as it shipped, before the tail had a box. Kept so this
# script can still recognise -- and upgrade -- a checkout that predates it.
WIDE_CHIP = layout_model(AFTER_ROWS, FINAL_PANEL_W, FINAL_CHIP_W,
                         FINAL_DESC_LEFT)
FINAL = layout_model(AFTER_ROWS, FINAL_PANEL_W, FINAL_CHIP_W, FINAL_DESC_LEFT,
                     tail=True)

# THE THIRTEENTH ROW: group mute on `m`.
#
# It goes LAST, and that is not a cosmetic choice. Rows stack top-down from a
# fixed title, so appending is the only insertion that leaves all twelve
# existing boxes at exactly the tops they were captured at -- any other
# position rewrites the geometry of every row below it, and the capture is the
# one artifact here that cannot be re-derived from the product. Last also
# groups it with the three selection rows that already end the panel.
#
# The description's width uses the plain ASCII model (CHAR_W per character),
# which is exact for every captured single-line ASCII row; the only rows that
# defeat it are the two carrying non-ASCII glyphs, and this is not one.
MUTE_SEL_ROW = ('Mute/unmute selection',
                CHAR_W * len('Mute/unmute selection'), False)
MUTE_SEL_ROWS = AFTER_ROWS + [MUTE_SEL_ROW]
MUTE_SEL = layout_model(MUTE_SEL_ROWS, FINAL_PANEL_W, FINAL_CHIP_W,
                        FINAL_DESC_LEFT, tail=True)

KNOWN_STATES = (('pre-change', BEFORE), ('narrow-chip', AFTER),
                ('wide-chip', WIDE_CHIP), ('final', FINAL),
                ('mute-selection', MUTE_SEL))

# What this run converges the capture ON. Everything below writes TARGET
# rather than naming a state, so the row list and the geometry cannot drift.
TARGET = MUTE_SEL
TARGET_ROWS = MUTE_SEL_ROWS
TARGET_TAIL_INDEX = tail_index(TARGET_ROWS)


def path_suffix(path):
    """The help-subtree-relative key for a binding path, or None."""
    head = tuple((step['tag'], step['index']) for step in path[:4])
    if head != PANEL:
        return None
    rest = path[4:]
    if not rest:
        return ()
    if len(rest) == 1 and rest[0]['tag'] == 'div':
        return (rest[0]['index'],)
    if (len(rest) == 2 and rest[0]['tag'] == 'div'
            and rest[1]['tag'] == 'span'):
        return (rest[0]['index'], 'span', rest[1]['index'])
    # The tail is a <button>, not a <div>. Sibling indices in this capture run
    # across tags -- verified against captures holding
    # [('button',0),('div',1)] and [...('span',8),('button',9)...] -- so the
    # button after twelve rows and a title is index 13, not button 0.
    # Matched on the TAG rather than the index, so a capture written for a
    # different row count still resolves here and gets re-pathed instead of
    # silently surviving with a stale box.
    if len(rest) == 1 and rest[0]['tag'] == 'button':
        return TAIL_KEY
    return None


def matches(bindings, model):
    seen = 0
    for binding in bindings:
        key = path_suffix(binding['path'])
        if key is None or key not in model:
            continue
        want, have = model[key], binding['box']
        for field in ('left', 'top', 'width', 'height'):
            if abs(want[field] - have[field]) > 1e-9:
                return None
        seen += 1
    return seen if seen == len(model) else None


def classify(bindings, where):
    """Positive control: the model must reproduce the capture already on disk.

    Without it the emitted geometry is arithmetic nobody checked, and the
    failure mode is a panel that renders subtly wrong with every test green.
    Returns the name of the state the file currently holds.
    """
    for name, model in KNOWN_STATES:
        if matches(bindings, model) is not None:
            return name
    sys.exit('FAIL model: the help capture in %s matches none of the %d known '
             'states (%s) -- the panel was restructured and this script cannot '
             'render a verdict'
             % (where, len(KNOWN_STATES),
                ', '.join(name for name, _ in KNOWN_STATES)))


# Descriptions. (row index, span index, old text, new text)
TEXT_EDITS = [
    (4, 1, 'Edit bands (mode-dependent)', 'Edit bands'),
    (11, 1, 'Add/remove from selection', 'Add/remove selection'),
    # Row 13 arrives as a clone of row 12, so it is relabelled through the
    # same path every other description takes -- which is also what
    # re-measures its glyph count, basis width and ink box.
    (13, 1, 'Group move', 'Mute/unmute selection'),
]

# Every KEY CHIP in the panel is re-measured, not only the two this change
# relabels: the chip column's width changes and the text inside it is centred,
# so all twelve move. A row left out would keep a box measured for an 84px chip
# inside a 104px one and sit 10px left of its neighbours -- precisely the stale
# capture the pitch check exists to catch, and invisible to any line-height
# check.
#
# The DOCUMENT is the source of truth for what each chip says. This script owns
# the two Command rows and re-measures the rest wherever they happen to read,
# so a sibling lane that relabels a chip (`6` -> `A / 6` when the analyzer got
# a second key) gets its capture corrected here instead of fought over.
CHIP_COUNT = len(TARGET_ROWS)
CHIP_ROW_RE = re.compile(
    r'React\.createElement\(Hrow, \{ k: "((?:[^"\\]|\\.)*)" \}')


def js_unescape(text):
    """A chip label as a person reads it, from its compiled spelling."""
    text = re.sub(r'\\u([0-9a-fA-F]{4})',
                  lambda m: chr(int(m.group(1), 16)), text)
    text = re.sub(r'\\x([0-9a-fA-F]{2})',
                  lambda m: chr(int(m.group(1), 16)), text)
    return text.replace('\\"', '"').replace('\\\\', '\\')


def chip_texts_from_document(document_raw):
    """What the twelve chips say in the document this run just patched."""
    html = json.loads(document_raw)['html']
    at = html.find('"SHORTCUTS"')
    if at < 0:
        sys.exit('FAIL chip: the SHORTCUTS panel is not in the document')
    block = html[at:html.find('\n}', at)]
    found = [js_unescape(k) for k in CHIP_ROW_RE.findall(block)]
    if len(found) != CHIP_COUNT:
        sys.exit('FAIL chip: the panel declares %d rows, expected %d -- it was '
                 'restructured and this script cannot re-measure it'
                 % (len(found), CHIP_COUNT))
    return found


def verify_chip_model(by_key, where):
    """Positive control: `chip_ink`/`chip_text_left` must reproduce the capture.

    Each chip is checked against ITS OWN recorded basis width, so this runs
    whichever of the three states the file holds and does not need to know
    which. Chips carrying a non-ASCII glyph are skipped: they resolve through
    a fallback face whose advance this model does not carry, which is exactly
    why `CMD+DRAG` is an improvement on `\u2318+DRAG` for something that has
    to be measured.

    A model that cannot reproduce what is already on disk has no business
    emitting a replacement, and a control that checked nothing would be worse
    than none -- so too few checks is a failure, not a pass.
    """
    checked = 0
    # However many rows the capture on disk holds -- one FEWER than the target
    # on the run that adds a row. A control that demanded the new row be
    # present before it was written could never pass.
    present = sorted(key[0] for key in by_key
                     if len(key) == 3 and key[1] == 'span' and key[2] == 0)
    if not present:
        sys.exit('FAIL chip: %s holds no captured chip at all' % where)
    for index in present:
        binding = by_key.get((index, 'span', 0))
        if binding is None:
            sys.exit('FAIL chip: no captured chip at row %d in %s'
                     % (index, where))
        text = binding['text']
        if any(ord(ch) > 127 for ch in text):
            continue
        box = binding['boxes'][0]
        want_w = chip_ink(text)
        want_x = chip_text_left(text, float(binding['basis']['width']))
        if (abs(box['width'] - want_w) > 1e-9
                or abs(box['left'] - want_x) > 1e-9):
            sys.exit('FAIL chip model: row %d %r is captured at left=%s '
                     'width=%s but the model says left=%s width=%s -- the '
                     'model cannot reproduce %s and must not rewrite it'
                     % (index, text, box['left'], box['width'],
                        want_x, want_w, where))
        checked += 1
    if checked < 8:
        sys.exit('FAIL chip model: only %d of %d chips were measurable in %s, '
                 'so the control cannot discriminate a wrong model from a '
                 'right one' % (checked, len(present), where))
    return checked





def retext(binding, new, span):
    """Point one captured DESCRIPTION binding at its new string."""
    binding['text'] = new
    for face in binding['basis'].get('resolved_faces') or []:
        face['glyph_count'] = len(new)
    width = CHAR_W * len(new)
    binding['basis']['width'] = tidy(width)
    binding['boxes'] = tidy([dict(left=0.0, top=DESC_TOP_SINGLE, width=width,
                                  height=DESC_LINE_H, start=0,
                                  length=len(new))])


def retext_chip(binding, new):
    """Re-measure one key chip for the final chip column.

    Every chip is rewritten, not only the two whose text changed: the column
    went 84 -> 104, the text inside it is centred, so all twelve move. The
    face list is rebuilt rather than edited because two of these chips used to
    carry a Command/Shift glyph and resolved through a Menlo fallback
    alongside the mono face; their replacements are pure ASCII and resolve
    through one face, so carrying the old two-face list forward would describe
    a shaping that no longer happens.
    """
    binding['text'] = new
    binding['basis']['width'] = tidy(FINAL_CHIP_W)
    binding['basis']['resolved_face'] = MONO_FACE
    binding['basis']['resolved_faces'] = [dict(
        family_name=MONO_FAMILY, post_script_name=MONO_FACE,
        is_custom_font=True, glyph_count=len(new))]
    binding['boxes'] = tidy([dict(
        left=chip_text_left(new, FINAL_CHIP_W), top=CHIP_TEXT_TOP,
        width=chip_ink(new), height=CHIP_TEXT_H, start=0, length=len(new))])


def tidy(value):
    """Integral floats back to ints.

    Cosmetic, and worth it: the capture tool writes `15`, Python writes
    `15.0`, and without this every untouched box in the record shows up as a
    changed line. A diff that is 90% noise is a diff nobody reads.
    """
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if isinstance(value, dict):
        return {k: tidy(v) for k, v in value.items()}
    if isinstance(value, list):
        return [tidy(v) for v in value]
    return value


def repath(binding, key):
    """Point one binding's path at `key`, keeping the panel prefix."""
    steps = list(PANEL)
    if key == TAIL_KEY:
        steps.append(('button', TARGET_TAIL_INDEX))
    elif key:
        steps.append(('div', key[0]))
        if len(key) == 3:
            steps.append(('span', key[2]))
    binding['path'] = [{'tag': tag, 'index': index} for tag, index in steps]


def grow_capture(document, row):
    """Add the three bindings for a row the capture on disk predates.

    CLONED from the row above rather than synthesised. A row binding carries a
    font basis, a resolved-face list and a `requested` block describing the
    shaping the capture tool observed -- none of which this script models, and
    inventing them would describe a shaping that never happened. Everything
    this script DOES model (the box, the text, the glyph count, the ink boxes)
    is rewritten immediately afterwards by the same helpers every other row
    goes through, so the clone supplies structure and nothing else.

    The clone drops the capture tool's own `index` field rather than
    duplicating it -- the tail binding this script already inserts carries
    none either, and nothing here resolves a binding by it.
    """
    source = row - 1
    # A span carries a binding in BOTH arrays -- a layout box and a text
    # record -- and cloning only the text record leaves the new row's chip and
    # description with no box at all. That failure is silent in the geometry
    # pass (an absent key is simply never written) and only surfaced here
    # because `classify` re-derives the capture on the NEXT run and found two
    # keys missing. Which is the whole reason that control exists.
    for store, keys in (('layout_bindings', [(source,),
                                             (source, 'span', 0),
                                             (source, 'span', 1)]),
                        ('text_bindings', [(source, 'span', 0),
                                           (source, 'span', 1)])):
        for key in keys:
            target = (row,) if len(key) == 1 else (row, 'span', key[2])
            if any(path_suffix(b['path']) == target
                   for b in document[store]):
                continue
            at = next((i for i, b in enumerate(document[store])
                       if path_suffix(b['path']) == key), None)
            if at is None:
                sys.exit('FAIL grow: row %d has no %s to clone from'
                         % (source, store))
            clone = copy.deepcopy(document[store][at])
            clone.pop('index', None)
            repath(clone, target)
            document[store].insert(at + 1, clone)


def apply_help_state(path, chip_texts):
    """Rewrite the captured help metadata in the pretty-printed state record."""
    raw = open(path, encoding='utf-8').read()
    document = json.loads(raw)
    state = classify([b for b in document['layout_bindings']
                      if path_suffix(b['path']) is not None], path)
    # The capture has to hold every row the panel now declares BEFORE the
    # geometry pass, or the new row's boxes have nothing to be written onto and
    # it renders at the content-box origin -- printing over the first rows,
    # which is exactly the defect the tail's own missing box once caused.
    for row in range(2, len(TARGET_ROWS) + 1):
        grow_capture(document, row)
    for binding in document['layout_bindings']:
        key = path_suffix(binding['path'])
        if key is not None and key in TARGET:
            binding['box'] = tidy(dict(TARGET[key]))
            # The tail's sibling index moves with the row count, so a capture
            # written for twelve rows carries a path this panel no longer has.
            if key == TAIL_KEY:
                repath(binding, TAIL_KEY)
    # Same insertion as the runtime mirror, in the readable record: append the
    # tail's binding if this capture predates the button having a box. Placed
    # directly after the panel's own binding so the two files read alike.
    if not any(path_suffix(b['path']) == TAIL_KEY
               for b in document['layout_bindings']):
        panel_at = next(i for i, b in enumerate(document['layout_bindings'])
                        if path_suffix(b['path']) == ())
        document['layout_bindings'].insert(panel_at + 1, {
            'anchor': document['layout_bindings'][panel_at]['anchor'],
            'path': [{'tag': tag, 'index': index}
                     for tag, index in
                     list(PANEL) + [('button', TARGET_TAIL_INDEX)]],
            'box': tidy(dict(TARGET[TAIL_KEY])),
        })
    by_key = {}
    for binding in document['text_bindings']:
        key = path_suffix(binding['path'])
        if key is not None:
            by_key[key] = binding
    checked = verify_chip_model(by_key, path)
    print('control          %s capture, chip model reproduces %d chips (%s)'
          % (state, checked, os.path.basename(path)))
    for index, text in enumerate(chip_texts, start=1):
        retext_chip(by_key[(index, 'span', 0)], text)
    for row, span, old, new in TEXT_EDITS:
        binding = by_key.get((row, 'span', span))
        if binding is None:
            sys.exit('FAIL text: no captured binding at row %d span %d in %s'
                     % (row, span, path))
        if binding['text'] not in (old, new):
            sys.exit('FAIL text: row %d span %d reads %r, expected %r or %r'
                     % (row, span, binding['text'], old, new))
        retext(binding, new, span)
    out = json.dumps(document, indent=2, ensure_ascii=False) + '\n'
    if out == raw:
        print('already applied  captured help metadata (%s)'
              % os.path.basename(path))
        return
    open(path, 'w', encoding='utf-8').write(out)
    print('applied          captured help metadata (%s)'
          % os.path.basename(path))


def number(value):
    return str(int(value)) if float(value).is_integer() else repr(float(value))


def js_escape(text):
    return ''.join(c if ord(c) < 128 else '\\u%04X' % ord(c) for c in text)


def box_literal(box):
    return ('{ "left": %s, "top": %s, "width": %s, "height": %s }'
            % (number(box['left']), number(box['top']),
               number(box['width']), number(box['height'])))


def path_literal(key, tail_at=None):
    steps = list(PANEL)
    if key == TAIL_KEY:
        steps.append(('button',
                      TARGET_TAIL_INDEX if tail_at is None else tail_at))
    elif key:
        steps.append(('div', key[0]))
        if len(key) == 3:
            steps.append(('span', key[2]))
    return '"path": [%s]' % ', '.join(
        '{ "tag": "%s", "index": %d }' % (tag, index) for tag, index in steps)


def scan_object(segment, at):
    """(start, stop) of the `{ ... }` binding containing offset `at`.

    Brace-counted rather than regexed, and quote-aware in BOTH quote styles:
    `requested.font_family` is a JS single-quoted string containing a double
    quote, so a scanner that only knew `"` would lose its place inside it and
    return a span ending in the middle of the next binding.
    """
    start = segment.rindex('{ "anchor"', 0, at)
    depth = 0
    quote = None
    i = start
    while i < len(segment):
        c = segment[i]
        if quote:
            if c == '\\':
                i += 2
                continue
            if c == quote:
                quote = None
        elif c in '"\'':
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return start, i + 1
        i += 1
    sys.exit('FAIL runtime: a binding near offset %d is unbalanced' % at)


def binding_sites(segment, path_lit):
    """Every (field, start, stop) binding carrying this path.

    A ROW resolves to one binding, its layout box. A SPAN resolves to TWO --
    a layout box and a text record -- and they live in different arrays, which
    is why each is located separately and why a clone is inserted immediately
    after its OWN source rather than all of them after the last.
    """
    sites = []
    for field in ('box', 'text'):
        needle = path_lit + ', "' + field + '": '
        count = segment.count(needle)
        if count == 0:
            continue
        if count != 1:
            sys.exit('FAIL runtime: %s occurs %d times with a %r field'
                     % (path_lit, count, field))
        at = segment.index(needle)
        start, stop = scan_object(segment, at)
        sites.append((field, start, stop))
    return sites


def clone_runtime_row(segment, row):
    """Duplicate one row's bindings for a row the shipping bundle predates.

    runtime.js is the copy that SHIPS, so a row present in the document and
    absent here renders with no captured box -- and every captured child of
    this panel is pinned absolutely, so a child the capture does not name
    lands at the CONTENT-BOX ORIGIN and prints across the first rows. That is
    not hypothetical: it is exactly what the Learn more button did before it
    was given a box.

    Cloned rather than synthesised because a binding carries a basis, a
    resolved-face list and a `requested` block describing shaping this script
    does not model. Layout boxes are written to their final value here; the
    text records are re-measured by the passes in mirror_runtime.
    """
    source = row - 1
    for key in ((source,), (source, 'span', 0), (source, 'span', 1)):
        target = (row,) if len(key) == 1 else (row, 'span', key[2])
        if path_literal(target) in segment:
            continue
        src_lit, dst_lit = path_literal(key), path_literal(target)
        sites = binding_sites(segment, src_lit)
        if not sites:
            sys.exit('FAIL runtime: row %d has no %s binding to clone from'
                     % (source, key))
        # Reverse order so each insertion leaves the earlier offsets valid.
        for field, start, stop in reversed(sites):
            clone = segment[start:stop].replace(src_lit, dst_lit, 1)
            if field == 'box':
                clone = clone.replace(box_literal(TARGET[key]),
                                      box_literal(TARGET[target]), 1)
            segment = segment[:stop] + ', ' + clone + segment[stop:]
    return segment


def text_site(segment, key):
    """(start, stop) of one binding's `"text": "..."` literal, found by PATH.

    By path and by OFFSET, never by a global replace of the literal: a row
    cloned from its neighbour carries the neighbour's text until this pass
    rewrites it, so `segment.replace(literal, new, 1)` would rewrite the
    NEIGHBOUR -- the first occurrence -- and leave the clone alone.
    """
    needle = path_literal(key) + ', "text": '
    if segment.count(needle) != 1:
        sys.exit('FAIL runtime: %s has %d text records, expected 1'
                 % (key, segment.count(needle)))
    at = segment.index(needle) + len(path_literal(key)) + 2
    stop = segment.index(', "basis"', at)
    return at, stop


def mirror_runtime(chip_texts):
    """Mirror the captured help metadata into the SHIPPING runtime bundle.

    runtime.js embeds its own copy of every captured state, and it is the copy
    that ships: CMakeLists names runtime.js in `spectr_native_assets` and the
    states/ directory nowhere. The state record is the readable source of
    truth; this is the one the product reads.
    """
    raw = open(RUNTIME, encoding='utf-8').read()
    start = raw.index('"id": "help", "image"')
    end = raw.index('"id": "band-context", "image"')
    segment = raw[start:end]
    original = segment

    # Grow first: every pass below resolves a binding by its path, so a row
    # the bundle does not hold yet has nothing for them to find.
    for row in range(2, len(TARGET_ROWS) + 1):
        segment = clone_runtime_row(segment, row)

    for key in sorted(TARGET, key=lambda k: (len(k), tuple(str(x) for x in k))):
        anchor = path_literal(key)
        new_site = anchor + ', "box": ' + box_literal(TARGET[key])
        if new_site in segment:
            continue
        if key == TAIL_KEY:
            # The tail's sibling index is a function of the row count, so an
            # existing tail is looked for at every index a known state would
            # have put it at -- and only INSERTED if there is none, which is
            # the case for a bundle predating the button having a box at all.
            moved = False
            for rows in (AFTER_ROWS, MUTE_SEL_ROWS):
                for _name, model in reversed(KNOWN_STATES):
                    if TAIL_KEY not in model:
                        continue
                    old_site = (path_literal(TAIL_KEY, tail_index(rows))
                                + ', "box": ' + box_literal(model[TAIL_KEY]))
                    if segment.count(old_site) == 1:
                        segment = segment.replace(old_site, new_site, 1)
                        moved = True
                        break
                if moved:
                    break
            if moved:
                continue
            # Nothing to replace -- insert after the panel's own binding. The
            # panel sorts first (its key is the empty tuple), so by now it
            # already carries its final, taller box and is a unique needle.
            host = (path_literal(()) + ', "box": ' + box_literal(TARGET[()])
                    + ' }')
            if segment.count(host) != 1:
                sys.exit('FAIL runtime: the panel binding is not a unique '
                         'insertion site for the tail (%d matches)'
                         % segment.count(host))
            segment = segment.replace(
                host, host + ', { "anchor": "#root", ' + new_site + ' }', 1)
            continue
        for name, model in reversed(KNOWN_STATES):
            if key not in model:
                continue
            old_site = anchor + ', "box": ' + box_literal(model[key])
            if segment.count(old_site) == 1:
                segment = segment.replace(old_site, new_site, 1)
                break
        else:
            sys.exit('FAIL runtime: help box %s matches no known state, so '
                     'this script cannot tell what it is replacing' % (key,))

    # Every chip, because the column width moved all of them. The text comes
    # from the document, so a chip another lane relabelled is re-measured at
    # its new spelling rather than reverted to one this script remembers.
    for index, final in enumerate(chip_texts, start=1):
        final_literal = '"text": "%s"' % js_escape(final)
        at, stop = text_site(segment, (index, 'span', 0))
        if segment[at:stop] != final_literal:
            segment = segment[:at] + final_literal + segment[stop:]
        segment = respace_chip_runtime(segment, index, final)

    for row, span, old, new in TEXT_EDITS:
        old_literal = '"text": "%s"' % js_escape(old)
        new_literal = '"text": "%s"' % js_escape(new)
        at, stop = text_site(segment, (row, 'span', span))
        current = segment[at:stop]
        if current == new_literal:
            continue
        if current != old_literal:
            sys.exit('FAIL runtime: row %d span %d reads %s, expected %s '
                     'or %s' % (row, span, current, old_literal, new_literal))
        segment = segment[:at] + new_literal + segment[stop:]
        segment = retext_runtime(segment, (row, 'span', span), new, span)

    if segment == original:
        print('already applied  captured help metadata (runtime.js)')
        return
    open(RUNTIME, 'w', encoding='utf-8').write(
        raw[:start] + segment + raw[end:])
    print('applied          captured help metadata (runtime.js)')


def respace_chip_runtime(segment, index, text):
    """Re-measure one chip for the final chip column. Idempotent."""
    _at, stop = text_site(segment, (index, 'span', 0))
    tail = segment[stop:]
    tail = splice(tail, '"width": ', ', "resolved_face"', number(FINAL_CHIP_W))
    tail = splice(tail, '"resolved_faces": ', ', "requested"',
                  '[{ "family_name": "%s", "post_script_name": "%s", '
                  '"is_custom_font": true, "glyph_count": %d }]'
                  % (MONO_FAMILY, MONO_FACE, len(text)))
    tail = splice(tail, '"boxes": ', ', "runtime_font_family"',
                  '[{ "left": %s, "top": %s, "width": %s, "height": %s, '
                  '"start": 0, "length": %d }]'
                  % (number(chip_text_left(text, FINAL_CHIP_W)),
                     number(CHIP_TEXT_TOP), number(chip_ink(text)),
                     number(CHIP_TEXT_H), len(text)))
    return segment[:stop] + tail


def retext_runtime(segment, key, new, span):
    """Re-measure one description binding's glyph count and boxes."""
    _at, stop = text_site(segment, key)
    tail = segment[stop:]
    tail = splice(tail, '"glyph_count": ', ' }', str(len(new)))
    if span == 1:
        width = CHAR_W * len(new)
        tail = splice(tail, '"width": ', ', "resolved_face"', number(width))
        tail = splice(tail, '"boxes": ', ', "runtime_font_family"',
                      '[{ "left": 0, "top": %s, "width": %s, "height": %s, '
                      '"start": 0, "length": %d }]'
                      % (number(DESC_TOP_SINGLE), number(width),
                         number(DESC_LINE_H), len(new)))
    return segment[:stop] + tail


def splice(tail, prefix, suffix, value):
    at = tail.index(prefix)
    stop = tail.index(suffix, at)
    return tail[:at + len(prefix)] + value + tail[stop:]


def main():
    document_raw = open(DOCUMENT, encoding='utf-8').read()
    changed = False
    for label, old, new, sentinel in DOCUMENT_EDITS:
        encoded_sentinel = enc(sentinel)
        if encoded_sentinel in document_raw:
            print('already applied ', label)
            continue
        cand, why = choose(old, document_raw)
        if cand is None:
            sys.exit('FAIL %s: %s' % (label, why))
        document_raw = document_raw.replace(enc(cand), enc(new), 1)
        changed = True
        print('applied         ', label)
        # The sentinel is the only thing that makes the edit idempotent; a
        # re-run without one stacks a second copy of the block.
        if encoded_sentinel not in document_raw:
            sys.exit('FAIL %s: sentinel absent after applying the edit, so a '
                     're-run would stack a duplicate' % label)
    if changed:
        open(DOCUMENT, 'w', encoding='utf-8').write(document_raw)
        print('written', os.path.relpath(DOCUMENT, REPO))

    chip_texts = chip_texts_from_document(document_raw)
    apply_help_state(HELP_STATE, chip_texts)
    mirror_runtime(chip_texts)


if __name__ == '__main__':
    main()
