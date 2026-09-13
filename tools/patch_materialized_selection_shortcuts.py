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

NOT TOUCHED, deliberately: resources/editor.html and
native-ui/materialized/materialized-document.json. Neither is a shipping
native asset (CMakeLists refuses editor.html for `spectr_native_assets` by
name), both carry the pre-compile JSX rather than the React.createElement
form that ships, and both have ALREADY diverged from the shipping document
here -- their help panel still advertises `DBL-CLICK` for a row the shipping
document calls `CLICK`. Editing them would add merge surface across the live
lanes and change nothing that ships.
"""
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCUMENT = os.path.join(REPO, 'native-ui/materialized/materialized-document.runtime.json')
RUNTIME = os.path.join(REPO, 'native-ui/materialized/runtime.js')
HELP_STATE = os.path.join(REPO, 'native-ui/materialized/states/help.materialized.json')

# ---------------------------------------------------------------- behaviour

def enc(snippet):
    """A JS snippet as it appears inside the document's JSON `html` string."""
    return json.dumps(snippet)[1:-1]


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
    ('the dead SHIFT+CLICK row becomes the gesture that exists',
     'React.createElement(Hrow, { k: "\\u21E7+CLICK" }, "Add/remove from selection")',
     'React.createElement(Hrow, { k: "\\u2318\\u21E7+DRAG" }, "Add/remove selection")',
     'React.createElement(Hrow, { k: "\\u2318\\u21E7+DRAG" }, "Add/remove selection")'),

    # 250 - 94 = 156px of description, i.e. 24 characters, against a longest
    # surviving row of 22. That is not headroom. 330 buys 206px / 31.
    ('the panel carries width headroom',
     'minWidth: 280,', 'minWidth: 330,', 'minWidth: 330,'),
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
CHIP_W = 84.0
CHIP_H = 19.296875
CHIP_TOP_SINGLE = 2.0
CHIP_TOP_WRAPPED = 9.34375
DESC_LEFT = 94.0
DESC_TOP_SINGLE = 3.140625
DESC_H_SINGLE = 17.0
DESC_TOP_WRAPPED = 2.0
DESC_H_WRAPPED = 34.0
DESC_LINE_H = 13.0
CHAR_W = 6.5                # exact for every captured ASCII single-line row

OLD_PANEL_W = 280.0
NEW_PANEL_W = 330.0


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


def layout_model(rows, panel_w):
    """Every help-subtree box, keyed by path suffix, for one row list."""
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
            width=CHIP_W, height=CHIP_H)
        boxes[(index, 'span', 1)] = dict(
            left=DESC_LEFT,
            top=DESC_TOP_WRAPPED if wrapped else DESC_TOP_SINGLE,
            width=wrap_budget(panel_w) if wrapped else width,
            height=DESC_H_WRAPPED if wrapped else DESC_H_SINGLE)
        top += height
    panel_h = top + PANEL_PAD + PANEL_BORDER
    boxes[()] = dict(left=PANEL_RIGHT - panel_w, top=PANEL_BOTTOM - panel_h,
                     width=panel_w, height=panel_h)
    return boxes


BEFORE = layout_model(BEFORE_ROWS, OLD_PANEL_W)
AFTER = layout_model(AFTER_ROWS, NEW_PANEL_W)


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
    Returns True when the file still holds the pre-change capture.
    """
    if matches(bindings, BEFORE) is not None:
        return True
    if matches(bindings, AFTER) is not None:
        return False
    sys.exit('FAIL model: the help capture in %s matches neither the '
             'pre-change nor the post-change model -- the panel was '
             'restructured and this script cannot render a verdict' % where)


TEXT_EDITS = [
    # (row index, span index, old text, new text)
    (4, 1, 'Edit bands (mode-dependent)', 'Edit bands'),
    (11, 1, 'Add/remove from selection', 'Add/remove selection'),
    (11, 0, '\u21e7+CLICK', '\u2318\u21e7+DRAG'),
    # Relabelled in the source without their captures following, so both
    # render on live metrics today. Identical glyph counts, so the captured
    # advances stay correct and only the string moves.
    (1, 0, '1 / 2 / 3', 'S / L / B'),
    (2, 0, '4 / 5', 'F / G'),
]


def retext(binding, new, span):
    """Point one captured text binding at its new string.

    A chip keeps its boxes: both replacements have the identical glyph count
    in the same monospaced face, so the captured advance is still correct and
    fabricating a new one would only add rounding. A description is
    re-measured, because changing its length is the whole point.
    """
    binding['text'] = new
    for face in binding['basis'].get('resolved_faces') or []:
        face['glyph_count'] = len(new)
    if span == 0:
        return
    width = CHAR_W * len(new)
    binding['basis']['width'] = tidy(width)
    binding['boxes'] = tidy([dict(left=0.0, top=DESC_TOP_SINGLE, width=width,
                                  height=DESC_LINE_H, start=0,
                                  length=len(new))])


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


def apply_help_state(path):
    """Rewrite the captured help metadata in the pretty-printed state record."""
    raw = open(path, encoding='utf-8').read()
    document = json.loads(raw)
    classify([b for b in document['layout_bindings']
              if path_suffix(b['path']) is not None], path)
    for binding in document['layout_bindings']:
        key = path_suffix(binding['path'])
        if key is not None and key in AFTER:
            binding['box'] = tidy(dict(AFTER[key]))
    by_key = {}
    for binding in document['text_bindings']:
        key = path_suffix(binding['path'])
        if key is not None:
            by_key[key] = binding
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


def path_literal(key):
    steps = list(PANEL)
    if key:
        steps.append(('div', key[0]))
        if len(key) == 3:
            steps.append(('span', key[2]))
    return '"path": [%s]' % ', '.join(
        '{ "tag": "%s", "index": %d }' % (tag, index) for tag, index in steps)


def mirror_runtime():
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

    for key in sorted(BEFORE, key=lambda k: (len(k), k)):
        anchor = path_literal(key)
        old_site = anchor + ', "box": ' + box_literal(BEFORE[key])
        new_site = anchor + ', "box": ' + box_literal(AFTER[key])
        if new_site in segment:
            continue
        if segment.count(old_site) != 1:
            sys.exit('FAIL runtime: help box %s occurs %d times'
                     % (key, segment.count(old_site)))
        segment = segment.replace(old_site, new_site, 1)

    for row, span, old, new in TEXT_EDITS:
        old_literal = '"text": "%s"' % js_escape(old)
        new_literal = '"text": "%s"' % js_escape(new)
        if new_literal in segment:
            continue
        if segment.count(old_literal) != 1:
            sys.exit('FAIL runtime: text %r occurs %d times in the help state'
                     % (old, segment.count(old_literal)))
        segment = segment.replace(old_literal, new_literal, 1)
        segment = retext_runtime(segment, new_literal, new, span)

    if segment == original:
        print('already applied  captured help metadata (runtime.js)')
        return
    open(RUNTIME, 'w', encoding='utf-8').write(
        raw[:start] + segment + raw[end:])
    print('applied          captured help metadata (runtime.js)')


def retext_runtime(segment, anchor, new, span):
    """Re-measure one binding's glyph count, and a description's boxes."""
    at = segment.index(anchor) + len(anchor)
    tail = segment[at:]
    tail = splice(tail, '"glyph_count": ', ' }', str(len(new)))
    if span == 1:
        width = CHAR_W * len(new)
        tail = splice(tail, '"width": ', ', "resolved_face"', number(width))
        tail = splice(tail, '"boxes": ', ', "runtime_font_family"',
                      '[{ "left": 0, "top": %s, "width": %s, "height": %s, '
                      '"start": 0, "length": %d }]'
                      % (number(DESC_TOP_SINGLE), number(width),
                         number(DESC_LINE_H), len(new)))
    return segment[:at] + tail


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
        encoded_old = enc(old)
        count = document_raw.count(encoded_old)
        if count != 1:
            sys.exit('FAIL %s: document patch point occurs %d times'
                     % (label, count))
        document_raw = document_raw.replace(encoded_old, enc(new), 1)
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

    apply_help_state(HELP_STATE)
    mirror_runtime()


if __name__ == '__main__':
    main()
