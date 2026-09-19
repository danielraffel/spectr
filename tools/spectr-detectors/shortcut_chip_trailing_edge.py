#!/usr/bin/env python3
"""Assert every keyboard-shortcut chip sits at its row's trailing edge, and
that the preset menu's MANAGE chip is inset by the SAME amount as the EDIT
MODE chips.

WHY THE SECOND HALF MATTERS

    The two menus are laid out by different owners (`menuItem` for the preset
    rows, an inline style for the EDIT MODE rows) and carry different
    horizontal padding.  So "the chip is right-aligned" is true of each menu
    separately while the two still look nothing alike.  The claim this
    detector exists to defend is that the MANAGE chip MATCHES the EDIT MODE
    chip, and that is an inset comparison, not an absolute-x comparison.

WHAT IT CANNOT SEE

    Geometry only.  A chip drawn at the right x with the wrong border colour,
    the wrong font, or no border at all passes here.  Its styling is shared in
    the document by construction (one `spectrShortcutChipStyle()`), and the
    PNG captures are what adjudicate the look.

Exit codes: 0 pass, 1 fail, 2 inconclusive (the probe could not measure).
"""
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(REPO, "build-now", "Spectr.app", "Contents", "MacOS", "Spectr")

PRESET_TRIGGER = '[data-spectr-menu-root="pattern"] button'
EDIT_TRIGGER = '[data-spectr-menu-root="edit"] [data-spectr-menu-trigger]'

# The chip glyphs each menu is expected to publish.
PRESET_CHIPS = ("Cmd+Shift+P",)
EDIT_CHIPS = ("S", "L", "B", "F", "G")
# Captions that must be measurable in the same dump.  They are the POSITIVE
# CONTROL: if these are missing the menu never opened, and "no chips found"
# would otherwise read as a clean failure of the chips rather than of the probe.
PRESET_CONTROL = ("MANAGE…", "SAVE CURRENT…")
EDIT_CONTROL = ("SCULPT", "GLIDE")

TOL = 0.75  # design px


def run(app, dump, trigger):
    env = dict(os.environ)
    env.update(
        PULP_HEADLESS="1",
        PULP_FRAMES="90",
        PULP_SCREENSHOT=dump + ".png",
        SPECTR_CLICK=trigger,
        SPECTR_LAYOUT_DUMP=dump,
    )
    subprocess.run([app], env=env, capture_output=True, timeout=240)
    with open(dump) as fh:
        doc = json.load(fh)
    with open(dump + ".depths.json") as fh:
        depths = json.load(fh)
    if isinstance(depths, dict):
        depths = depths.get("depths") or depths.get("node_depths") or []
    return doc["nodes"], depths


def parents(depths):
    """Pre-order depth list -> parent index per node."""
    out = [None] * len(depths)
    stack = []
    for i, d in enumerate(depths):
        while len(stack) > d:
            stack.pop()
        out[i] = stack[-1] if stack else None
        stack.append(i)
    return out


def texts(node):
    return [b["text"].strip() for b in (node.get("measured_text_boxes") or [])]


def right(rect):
    return rect["x"] + rect["w"]


def subtree_end(depths, i):
    """Pre-order lists are contiguous: the subtree of i runs until the next
    node at or above its own depth."""
    d = depths[i]
    j = i + 1
    while j < len(depths) and depths[j] > d:
        j += 1
    return j


def scope_root(nodes, depths, control):
    """The DEEPEST node whose subtree carries every control caption.

    Scoping matters: the EDIT MODE hints are single letters, and `B` also
    names two snapshot buttons in the toolbar. Searching the whole tree
    measured seven "chips" for five rows -- a miscount that would have read as
    a layout defect rather than as the wrong search.
    """
    best = None
    for i in range(len(nodes)):
        end = subtree_end(depths, i)
        seen = {t for n in nodes[i:end] for t in texts(n)}
        if all(c in seen for c in control):
            if best is None or depths[i] > depths[best[0]]:
                best = (i, end)
    return best


def find_chip_rows(nodes, depths, chips, scope):
    """Each chip's own rect plus the rect of the ROW button that owns it.

    The row is the nearest ancestor at least four times the chip's width: a
    chip is a few glyphs inside a full-width menu row, so that separates the
    row from the inline spans between them without depending on a node kind
    the snapshot does not carry.
    """
    par = parents(depths) if depths and len(depths) == len(nodes) else None
    lo, hi = scope
    found = []
    for i in range(lo, hi):
        node = nodes[i]
        hit = [t for t in texts(node) if t in chips]
        if not hit:
            continue
        rect = node["rect"]
        if rect["w"] <= 0:
            continue
        # The ROW is the WIDEST ancestor still inside the open menu.  Rows are
        # authored `width: 100%`, so that is the row button and nothing else --
        # picking "the first ancestor much wider than the chip" instead stops at
        # the inline flex span that ENDS at the chip and reports an inset of 0.00
        # for every EDIT MODE row, which reads as perfect alignment and is
        # simply the wrong box.
        row = None
        if par is not None:
            j, best = par[i], None
            while j is not None and j > lo:
                cand = nodes[j]["rect"]
                if best is None or cand["w"] > best["w"]:
                    best = cand
                j = par[j]
            row = best
        found.append((hit[0], rect, row))
    return found


def measure(label, app, trigger, chips, control, tmp):
    dump = os.path.join(tmp, label + ".json")
    nodes, depths = run(app, dump, trigger)
    print("CONTROL %s layout nodes = %d" % (label, len(nodes)))
    if len(nodes) < 310:
        print("INCONCLUSIVE: %s menu did not open (node count %d)" % (label, len(nodes)))
        return None
    seen = {t for n in nodes for t in texts(n)}
    missing = [c for c in control if c not in seen]
    print("CONTROL %s captions present = %r" % (label, [c for c in control if c in seen]))
    if missing:
        print("INCONCLUSIVE: %s control captions absent: %r" % (label, missing))
        return None
    if not depths or len(depths) != len(nodes):
        print("INCONCLUSIVE: %s depth sidecar missing or misaligned "
              "(%d depths for %d nodes)" % (label, len(depths or []), len(nodes)))
        return None
    scope = scope_root(nodes, depths, control)
    if scope is None:
        print("INCONCLUSIVE: %s could not scope to the open menu" % label)
        return None
    print("CONTROL %s menu subtree = nodes [%d, %d)" % (label, scope[0], scope[1]))
    rows = find_chip_rows(nodes, depths, chips, scope)
    print("CONTROL %s chips measured = %d (expected %d)" % (label, len(rows), len(chips)))
    if len(rows) != len(chips):
        print("INCONCLUSIVE: %s measured %d chips, expected %d"
              % (label, len(rows), len(chips)))
        return None
    if any(r is None for _t, _c, r in rows):
        print("INCONCLUSIVE: %s could not resolve an owning row for every chip" % label)
        return None
    for text, chip, row in rows:
        print("  %-6r chip right=%8.2f  row right=%8.2f  inset=%6.2f"
              % (text, right(chip), right(row), right(row) - right(chip)))
    return rows


def main():
    app = sys.argv[1] if len(sys.argv) > 1 else APP
    if not os.path.exists(app):
        print("INCONCLUSIVE: app not built at %s" % app)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        edit = measure("editmode", app, EDIT_TRIGGER, EDIT_CHIPS, EDIT_CONTROL, tmp)
        preset = measure("preset", app, PRESET_TRIGGER, PRESET_CHIPS, PRESET_CONTROL, tmp)

    if edit is None or preset is None:
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    failures = []

    edit_insets = [right(r) - right(c) for _t, c, r in edit]
    spread = max(edit_insets) - min(edit_insets)
    if spread > TOL:
        failures.append("EDIT MODE chip insets disagree by %.2f px: %r"
                        % (spread, [round(v, 2) for v in edit_insets]))

    edit_rights = [right(c) for _t, c, _r in edit]
    if max(edit_rights) - min(edit_rights) > TOL:
        failures.append("EDIT MODE chip right edges disagree: %r"
                        % [round(v, 2) for v in edit_rights])

    preset_inset = right(preset[0][2]) - right(preset[0][1])
    edit_inset = sum(edit_insets) / len(edit_insets)
    delta = abs(preset_inset - edit_inset)
    print("MANAGE inset=%.2f  EDIT MODE inset=%.2f  delta=%.2f (tol %.2f)"
          % (preset_inset, edit_inset, delta, TOL))
    if delta > TOL:
        failures.append("MANAGE chip is inset %.2f px from its row's trailing edge "
                        "but the EDIT MODE chips are inset %.2f px -- the two "
                        "menus do not match" % (preset_inset, edit_inset))
    if preset_inset < 0:
        failures.append("MANAGE chip overhangs its row's trailing edge by %.2f px"
                        % -preset_inset)

    if failures:
        print("FAIL:")
        for f in failures:
            print("  " + f)
        return 1
    print("PASS: %d EDIT MODE chips and the MANAGE chip all sit %.2f px inside "
          "their row's trailing edge" % (len(edit), edit_inset))
    return 0


if __name__ == "__main__":
    sys.exit(main())
