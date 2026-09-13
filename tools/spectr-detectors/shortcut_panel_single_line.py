#!/usr/bin/env python3
"""No row of the SHORTCUTS panel may wrap -- and none may be ABLE to.

Two rows wrapped in the shipped build ("Edit bands (mode-dependent)" and
"Add/remove from selection"), which is what a person reported. Shortening
them fixes those two and nothing else: the next entry someone appends wraps
in exactly the same silence, because nothing in the product can tell.

`white-space: nowrap` is not the fix. It trades a visible wrap for an
INVISIBLE clip, which is strictly worse -- a clipped row still reads as a
sentence and the missing half leaves no trace. So the panel is sized to its
content instead, and this asserts both halves of that:

  RENDERED   every description in the help panel occupies ONE line box in a
             capture of the shipping native editor. Measured, not asserted:
             a wrapped row reports twice the line height of a single one, and
             the rows that already fit are the control -- if they do not
             measure as one line either, the bound cannot discriminate and
             this reports NO VERDICT rather than a pass. The rows must also
             sit on an even PITCH, which is the only thing that catches a row
             whose text fits but whose BOX is still the two-line box the
             panel was captured with -- the shape a label shortened without
             re-measuring the capture produces, and one that reports a
             perfectly healthy single line.

  BUDGET     every description, measured the way the capture measures one,
             fits the panel's own captured width with room to spare. This is
             the half that protects the row nobody has written yet: it reads
             the shipping artifact, needs no build, and fails at review time
             instead of on someone's screen.

  KEY CHIP   the same two questions for the key chip, which this detector did
             not measure at all until a row needed `CMD+SHIFT+DRAG` and the
             chip was a fixed 84px holding eleven characters. A chip is
             `white-space: nowrap`, so it cannot wrap and does not clip: it
             OVERFLOWS its captured box and prints over the description beside
             it. That is invisible to every check above -- the description
             still measures one line, on an even pitch, inside budget -- so
             the chip is measured against its own captured width here, and no
             rendered ink may cross from the chip column into the description
             column.

Exit codes: 0 pass, 1 fail, 2 no verdict (the instrument could not measure).
"""

import argparse
import json
import math
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCUMENT = os.path.join(REPO, "native-ui/materialized/materialized-document.runtime.json")
RUNTIME = os.path.join(REPO, "native-ui/materialized/runtime.js")

HELP_TRIGGER = '[data-spectr-menu-root="help"] [data-spectr-menu-trigger]'

# A description's line box measures 16px natively and a wrapped one 32. The
# bound sits between the two rather than on either, so neither the browser's
# 17px capture nor a half-pixel of Skia leading can tip it.
ONE_LINE_MAX_H = 22.0
# Every captured single-line row in this panel measures exactly 6.5px per
# character in its monospaced face; the two rows that carry non-ASCII glyphs
# (· and −∞) land within a thirty-second of a pixel of it.
CHAR_W = 6.5
# Rows sit on an even pitch. Anything beyond this is a row holding a box
# taller than its text, which is what a stale capture looks like.
PITCH_TOLERANCE_PX = 1.0
# Headroom below the panel's own budget. A row that merely fits is a row that
# wraps the next time somebody adds a word.
SLACK_PX = 26.0
# The chip's own padding and border, from the Hrow style in the document.
CHIP_PAD_X = 6.0
CHIP_BORDER = 1.0
# The chip renders at 9px with 0.5 letter-spacing where a description renders
# at 10px. The glyph run quantises to a 64th of a pixel before the tracking is
# added per glyph, which is why this is not a flat per-character width.
CHIP_FONT_SIZE = 9.0
CHIP_LETTER_SPACING = 0.5
MONO_ADVANCE_RATIO = 0.6
# Slack below the chip budget. Less than one character (5.9px), because the
# chip column is sized to the longest key that exists rather than carrying a
# spare column -- but enough that a key cannot land exactly on the edge.
CHIP_SLACK_PX = 5.0
# Hrow's flex gap between the chip and the description.
CHIP_GAP = 10.0


def chip_ink(text):
    """The measured width of one key chip's text."""
    count = len(text)
    return (math.ceil(round(CHIP_FONT_SIZE * MONO_ADVANCE_RATIO * count * 64.0,
                            6)) / 64.0
            + CHIP_LETTER_SPACING * count)


def rows_from_document(path):
    """The panel's rows, read off the artifact that ships them."""
    html = json.load(open(path, encoding="utf-8"))["html"]
    at = html.find('"SHORTCUTS"')
    if at < 0:
        return None
    block = html[at:html.find("\n}", at)]
    found = re.findall(
        r'React\.createElement\(Hrow, \{ k: "((?:[^"\\]|\\.)*)" \}, '
        r'"((?:[^"\\]|\\.)*)"\)', block)
    return [(unescape(k), unescape(v)) for k, v in found] or None


def unescape(text):
    """The label as a person reads it.

    The compiled document escapes non-ASCII both ways -- `\\u2318` for the
    Command glyph and `\\xB7` for the interpunct -- and a width measured over
    the ESCAPE is six characters too wide, which reads as a row in trouble
    that is nowhere near it.
    """
    text = text.replace('\\"', '"').replace("\\\\", "\\")
    text = re.sub(r"\\u([0-9a-fA-F]{4})",
                  lambda m: chr(int(m.group(1), 16)), text)
    return re.sub(r"\\x([0-9a-fA-F]{2})",
                  lambda m: chr(int(m.group(1), 16)), text)


def budget_from_runtime(path):
    """The panel's own captured wrap budget: row width minus the description's
    left offset. Read from the shipping bundle so the bound cannot drift from
    the geometry the product actually renders."""
    raw = open(path, encoding="utf-8").read()
    try:
        start = raw.index('"id": "help", "image"')
        end = raw.index('"id": "band-context", "image"')
    except ValueError:
        return None
    segment = raw[start:end]
    row = re.search(
        r'\{ "tag": "div", "index": 16 \}, \{ "tag": "div", "index": 1 \}, '
        r'\{ "tag": "div", "index": 1 \}\], "box": \{ "left": [\d.-]+, '
        r'"top": [\d.-]+, "width": ([\d.]+),', segment)
    desc = re.search(
        r'\{ "tag": "div", "index": 16 \}, \{ "tag": "div", "index": 1 \}, '
        r'\{ "tag": "div", "index": 1 \}, \{ "tag": "span", "index": 1 \}\], '
        r'"box": \{ "left": ([\d.]+),', segment)
    chip = re.search(
        r'\{ "tag": "div", "index": 16 \}, \{ "tag": "div", "index": 1 \}, '
        r'\{ "tag": "div", "index": 1 \}, \{ "tag": "span", "index": 0 \}\], '
        r'"box": \{ "left": [\d.-]+, "top": [\d.-]+, "width": ([\d.]+),',
        segment)
    if not row or not desc or not chip:
        return None
    return (float(row.group(1)) - float(desc.group(1)),
            float(chip.group(1)))


def capture(app, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    png = os.path.join(out_dir, "shortcut_panel.png")
    dump = os.path.join(out_dir, "shortcut_panel.layout.json")
    # Clear BEFORE launching. The out-dir default is a fixed path and the only
    # liveness check is os.path.exists, so a launch that produces nothing
    # would otherwise adjudicate the PREVIOUS run and report a confident
    # verdict about code that is no longer under test.
    for stale in (png, dump):
        if os.path.exists(stale):
            os.remove(stale)
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1",
        "PULP_SCREENSHOT": png,
        "PULP_FRAMES": "90",
        "SPECTR_CLICK": HELP_TRIGGER,
        "SPECTR_LAYOUT_DUMP": dump,
    })
    log = os.path.join(out_dir, "shortcut_panel.log")
    with open(log, "w") as fh:
        rc = subprocess.call([app], env=env, stdout=fh, stderr=subprocess.STDOUT)
    if rc != 0 or not os.path.exists(dump):
        print("no verdict: the standalone exited %d; see %s" % (rc, log))
        sys.exit(2)
    return dump


def text_boxes(nodes):
    for node in nodes:
        for box in node.get("measured_text_boxes") or []:
            yield box


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app")
    ap.add_argument("--out-dir", default="/tmp/spectr-shortcut-panel")
    ap.add_argument("--layout", help="evaluate an archived layout dump instead")
    ap.add_argument("--budget-only", action="store_true",
                    help="skip the rendered half (no app, no capture)")
    ap.add_argument("--plant", choices=("long-label", "wrapped-row",
                                        "narrow-panel", "stretched-row",
                                        "long-chip", "narrow-chip",
                                        "chip-overflow"))
    args = ap.parse_args()

    rows = rows_from_document(DOCUMENT)
    if not rows:
        print("no verdict: no SHORTCUTS rows found in %s -- the panel was "
              "restructured and this detector cannot measure it" % DOCUMENT)
        return 2
    geometry = budget_from_runtime(RUNTIME)
    if geometry is None or geometry[0] <= 0 or geometry[1] <= 0:
        print("no verdict: could not read the help panel's captured row "
              "geometry out of %s" % RUNTIME)
        return 2
    budget, chip_w = geometry
    chip_budget = chip_w - 2.0 * (CHIP_PAD_X + CHIP_BORDER)

    if args.plant == "long-label":
        rows = list(rows)
        rows[3] = (rows[3][0], rows[3][1] + " (mode-dependent, per metaphor)")
    if args.plant == "narrow-panel":
        budget = 156.0
    if args.plant == "long-chip":
        rows = list(rows)
        rows[10] = ("CMD+SHIFT+OPTION+DRAG", rows[10][1])
    if args.plant == "narrow-chip":
        # The chip column exactly as it was before this panel needed a
        # three-modifier key: 84px outer, 70px of usable ink. A different
        # wrong implementation, not a broken file -- the panel it describes
        # shipped for months.
        chip_budget = 84.0 - 2.0 * (CHIP_PAD_X + CHIP_BORDER)

    failures = []

    # ------------------------------------------------------------ BUDGET
    print("budget    %.1fpx (%d characters) across %d rows"
          % (budget, int(budget // CHAR_W), len(rows)))
    for key, text in rows:
        width = CHAR_W * len(text)
        flag = "OK "
        if width > budget:
            flag = "WRAP"
            failures.append('the row "%s" measures %.1fpx against a %.1fpx '
                            "budget, so it wraps" % (text, width, budget))
        elif width > budget - SLACK_PX:
            flag = "TIGHT"
            failures.append('the row "%s" measures %.1fpx and the budget is '
                            "%.1fpx: it fits by %.1fpx, which is less than the "
                            "%.0fpx of slack this panel keeps so the NEXT "
                            "entry cannot wrap silently"
                            % (text, width, budget, budget - width, SLACK_PX))
        print("  %-5s %-12s %-34s %6.1fpx" % (flag, key, text, width))

    # --------------------------------------------------------- KEY CHIP
    # A chip is nowrap. It does not wrap and it does not clip -- it grows past
    # the box the capture pinned and prints on top of the description, which
    # every other check here reads as healthy.
    print("chip      %.1fpx of ink inside a %.1fpx chip (%d characters)"
          % (chip_budget, chip_w, int(chip_budget // chip_ink("M"))))
    for key, text in rows:
        ink = chip_ink(key)
        flag = "OK "
        if ink > chip_budget:
            flag = "OVER"
            failures.append('the key "%s" measures %.1fpx of ink in a %.1fpx '
                            "chip, so it overflows and prints over \"%s\""
                            % (key, ink, chip_budget, text))
        elif ink > chip_budget - CHIP_SLACK_PX:
            flag = "TIGHT"
            failures.append('the key "%s" measures %.1fpx against a %.1fpx '
                            "chip: it fits by %.1fpx, less than the %.0fpx of "
                            "slack this panel keeps so the NEXT key cannot "
                            "overflow silently"
                            % (key, ink, chip_budget, chip_budget - ink,
                               CHIP_SLACK_PX))
        print("  %-5s %-16s %6.1fpx" % (flag, key, ink))

    # ---------------------------------------------------------- RENDERED
    if not args.budget_only:
        dump = args.layout or capture(args.app, args.out_dir)
        boxes = list(text_boxes(json.load(open(dump))["nodes"]))
        wanted = {text for _k, text in rows}
        anchors = [b for b in boxes if (b.get("text") or "") in wanted]
        if len(anchors) < 4:
            print("no verdict: only %d of %d shortcut rows were found in %s -- "
                  "the help panel never opened" % (len(anchors), len(rows), dump))
            return 2
        # Measure the COLUMN, not the label list. A detector that looks up the
        # strings the document currently ships is blind to the row it is
        # supposed to catch: an older capture carrying the wording that
        # wrapped simply goes unfound and reads as clean. The descriptions all
        # share one left edge, so that edge is the subject.
        column = max(set(round(b["rect"]["x"], 1) for b in anchors),
                     key=lambda x: sum(1 for b in anchors
                                       if round(b["rect"]["x"], 1) == x))
        measured = [b for b in boxes
                    if abs(b["rect"]["x"] - column) < 0.5
                    and (b.get("text") or "")]
        if len(measured) != len(rows):
            print("no verdict: the description column at x=%.1f holds %d text "
                  "boxes and the panel declares %d rows -- this is not "
                  "measuring the panel it thinks it is"
                  % (column, len(measured), len(rows)))
            return 2
        measured.sort(key=lambda b: b["rect"]["y"])
        if args.plant == "wrapped-row":
            measured[0]["rect"]["h"] *= 2
        if args.plant == "stretched-row":
            for box in measured[4:]:
                box["rect"]["y"] += 15.0
        controls = [b for b in measured if b["rect"]["h"] <= ONE_LINE_MAX_H]
        if len(controls) < 2:
            print("no verdict: %d of %d measured rows are within the one-line "
                  "bound, so the bound cannot discriminate a wrap from the "
                  "panel's normal metrics" % (len(controls), len(measured)))
            return 2
        for box in measured:
            height = box["rect"]["h"]
            print("  render %-34s h=%5.1f %s"
                  % (box.get("text"), height,
                     "" if height <= ONE_LINE_MAX_H else "<-- WRAPPED"))
            if height > ONE_LINE_MAX_H:
                failures.append('the rendered row "%s" occupies %.1fpx, more '
                                "than the %.1fpx of a single line box -- it "
                                "wrapped" % (box.get("text"), height,
                                             ONE_LINE_MAX_H))
        # Uniform pitch. A row whose text fits but whose BOX is still the
        # two-line box it was captured with reports a single line here and
        # renders with its label floating above its own key chip and a hole
        # beneath -- which is exactly what shortening a label without
        # re-measuring the panel's capture produces. Line height cannot see
        # that; the distance between rows can.
        pitch = [round(b["rect"]["y"] - a["rect"]["y"], 2)
                 for a, b in zip(measured, measured[1:])]
        spread = max(pitch) - min(pitch) if pitch else 0.0
        print("rendered  %d rows in the description column at x=%.1f, "
              "%d single-line controls, pitch %.2f..%.2f"
              % (len(measured), column, len(controls), min(pitch), max(pitch)))
        if spread > PITCH_TOLERANCE_PX:
            # Name the rows against the panel's OWN typical pitch. Measuring
            # against the smallest makes every healthy row look guilty, which
            # buries the one or two that are not.
            typical = sorted(pitch)[len(pitch) // 2]
            tall = [measured[i].get("text") for i, p in enumerate(pitch)
                    if abs(p - typical) > PITCH_TOLERANCE_PX]
            failures.append("the rows are %.1fpx apart at the tightest and "
                            "%.1fpx at the loosest (%s) -- a row is still "
                            "occupying a box taller than its one line of text"
                            % (min(pitch), max(pitch), ", ".join(map(str, tall))))

        # No key chip may reach into the description column. This is measured
        # by COLUMN, not by looking up the keys the document currently ships,
        # so it still reads an archived capture whose keys have since been
        # respelled -- the same reason the description half measures a column.
        top = min(b["rect"]["y"] for b in measured) - 4.0
        bottom = max(b["rect"]["y"] + b["rect"]["h"] for b in measured) + 4.0
        # Confine to the chip column itself. Bounding only on "left of the
        # description" swept in unrelated chrome that happens to share these
        # rows' vertical band, and any of it reaching past the column would
        # have read as a chip overflowing.
        chip_left = column - (chip_w + CHIP_GAP) - 1.0
        chips = [b for b in boxes
                 if (b.get("text") or "")
                 and chip_left <= b["rect"]["x"] < column - 0.5
                 and top <= b["rect"]["y"] <= bottom]
        if args.plant == "chip-overflow" and chips:
            chips[0]["rect"]["w"] += 80.0
        if len(chips) < 2:
            print("no verdict: %d text boxes sit left of the description "
                  "column at x=%.1f, so the chip column cannot be measured"
                  % (len(chips), column))
            return 2
        reach = max(b["rect"]["x"] + b["rect"]["w"] for b in chips)
        print("chips     %d boxes left of x=%.1f, furthest right edge %.1f"
              % (len(chips), column, reach))
        for box in chips:
            right = box["rect"]["x"] + box["rect"]["w"]
            if right > column + 0.5:
                failures.append('the rendered key "%s" reaches x=%.1f, past '
                                "the description column at x=%.1f -- a nowrap "
                                "chip that outgrew its box prints over its own "
                                "row" % (box.get("text"), right, column))

    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print("PASS: every SHORTCUTS row is one line, with %.0fpx of slack to "
          "spare." % SLACK_PX)
    return 0


if __name__ == "__main__":
    sys.exit(main())
