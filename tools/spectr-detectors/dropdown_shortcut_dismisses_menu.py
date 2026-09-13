#!/usr/bin/env python3
"""Assert a shortcut letter pressed with the EDIT MODE menu OPEN selects that
mode and closes the menu.

WHY THIS EXISTS

    `edit_mode_shortcut_keys.py` proves the letters work from the CLOSED
    toolbar.  That is not where a user meets them: the chips that advertise
    S / L / B / F / G are rendered on the menu's own rows, so the menu is open
    at the moment the letter is read.  And in that state the letters were dead.

    The global shortcut handler is guarded by `overlayBlocksShortcut()`, and
    every toolbar popover is mounted carrying `data-spectr-overlay="true"` --
    so with a menu open the guard is correctly true and the handler returns
    before reading the key.  Nothing else in the suite could see this: the
    closed-toolbar detector passes, every screenshot passes, every caption
    check passes, and the user reports that the keys "don't select and close".

HOW IT MEASURES

    The layout dump carries one text box per mode label.  With the menu CLOSED
    exactly one of the five names is on screen -- the toolbar trigger, which
    renders `editMode.toUpperCase()`.  With it OPEN there are six: the five
    rows plus the trigger.  So the box count is the open/closed verdict and the
    surviving name is the selection verdict, from one dump.

    `SPECTR_KEY_JS` prints `listeners_fired=N` from a listener the fixture
    installs itself.  A dispatch that reaches nobody looks exactly like a key
    the app ignored, so a zero there is INCONCLUSIVE, never a pass and never a
    fail.

    Two controls, both of which must hold before any case is believed.  The
    no-key run must find the menu OPEN -- otherwise the click preamble never
    opened it and every "the menu closed" reading below is vacuous.  The
    unbound-key run must leave it open -- otherwise it is the act of
    dispatching a key that closes the menu, not the letter, and the cases are
    measuring the fixture rather than the app.

    Two ways in. Given the app it drives it and captures its own dumps.
    Given `--dump-open` and `--dump-after-key` it adjudicates committed dumps
    instead, which is what lets the selftest prove this rule still fires
    without a build; `--plant` there feeds it the OPEN dump as the after-key
    dump, which is exactly the defect -- a letter that changed nothing.

Exit codes: 0 pass, 1 fail, 2 inconclusive (the probe could not measure).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(REPO, "build-now", "Spectr.app", "Contents", "MacOS", "Spectr")

MODES = ("SCULPT", "LEVEL", "BOOST", "FLARE", "GLIDE")
OPEN_EDIT = '[data-spectr-menu-root="edit"] [data-spectr-menu-trigger]'
# Boxes on screen with the menu open: five rows plus the toolbar trigger.
OPEN_BOXES = len(MODES) + 1

# (key, expected mode on the trigger once the menu has closed)
CASES = [
    ("s", "SCULPT"),
    ("l", "LEVEL"),
    ("b", "BOOST"),
    ("f", "FLARE"),
    ("g", "GLIDE"),
]


def probe(app, tmp, tag, key):
    dump = os.path.join(tmp, tag + ".json")
    env = dict(os.environ)
    env.update(PULP_HEADLESS="1", PULP_FRAMES="90",
               PULP_SCREENSHOT=dump + ".png", SPECTR_LAYOUT_DUMP=dump,
               SPECTR_CLICK=OPEN_EDIT)
    if key:
        env["SPECTR_KEY_JS"] = key
    out = subprocess.run([app], env=env, capture_output=True, timeout=240)
    text = (out.stdout or b"").decode("utf-8", "replace") \
        + (out.stderr or b"").decode("utf-8", "replace")
    with open(os.path.join(tmp, tag + ".log"), "w") as fh:
        fh.write(text)
    fired = None
    for line in text.splitlines():
        if "listeners_fired=" in line:
            fired = line.split("listeners_fired=", 1)[1].split()[0]
            break
    if not os.path.exists(dump):
        return None, None, fired
    with open(dump) as fh:
        doc = json.load(fh)
    boxes = []
    for node in doc.get("nodes") or []:
        for box in node.get("measured_text_boxes") or []:
            token = box["text"].strip().rstrip("▾").strip()
            if token in MODES:
                boxes.append(token)
    return len(boxes), sorted(set(boxes)), fired


def read_dump(path):
    with open(path) as fh:
        doc = json.load(fh)
    boxes = []
    for node in doc.get("nodes") or []:
        for box in node.get("measured_text_boxes") or []:
            token = box["text"].strip().rstrip("▾").strip()
            if token in MODES:
                boxes.append(token)
    return len(boxes), sorted(set(boxes))


def offline(args):
    open_count, open_names = read_dump(args.dump_open)
    after = args.dump_open if args.plant else args.dump_after_key
    after_count, after_names = read_dump(after)
    print("CONTROL menu open, no key   -> boxes=%d names=%r"
          % (open_count, open_names))
    if open_count != OPEN_BOXES:
        print("INCONCLUSIVE: the open fixture carries %d mode boxes, expected "
              "%d -- it is not a capture of an open menu" % (open_count, OPEN_BOXES))
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2
    print("  after key    -> boxes=%d names=%r expected [%r]"
          % (after_count, after_names, args.expect))
    if after_count != 1:
        print("FAIL: the letter left the menu open (%d mode boxes, expected 1)"
              % after_count)
        return 1
    if after_names != [args.expect]:
        print("FAIL: the letter selected %r, expected %r"
              % (after_names, args.expect))
        return 1
    print("PASS: the letter selected %s AND dismissed the menu" % args.expect)
    return 0


def main():
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("app", nargs="?", default=APP)
    parser.add_argument("--dump-open")
    parser.add_argument("--dump-after-key")
    parser.add_argument("--expect", default="BOOST")
    parser.add_argument("--plant", action="store_true")
    args = parser.parse_args()
    if args.dump_open:
        if not args.dump_after_key and not args.plant:
            parser.error("--dump-open needs --dump-after-key (or --plant)")
        return offline(args)

    app = args.app
    if not os.path.exists(app):
        print("INCONCLUSIVE: app not built at %s" % app)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        count, names, _ = probe(app, tmp, "ctl-open", None)
        if count is None:
            print("INCONCLUSIVE: the no-key control produced no layout dump")
            print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
            return 2
        print("CONTROL menu open, no key   -> boxes=%d names=%r" % (count, names))
        if count != OPEN_BOXES:
            print("INCONCLUSIVE: the click preamble left %d mode boxes on screen, "
                  "expected %d -- the menu never opened, so nothing below can be "
                  "read as 'the letter closed it'" % (count, OPEN_BOXES))
            print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
            return 2

        count, names, fired = probe(app, tmp, "ctl-unbound", "z")
        if count is None:
            print("INCONCLUSIVE: the unbound-key control produced no layout dump")
            print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
            return 2
        if fired is None or fired == "0":
            print("INCONCLUSIVE: the unbound-key control dispatched to %r listeners "
                  "-- the key fixture is dead, so no key result means anything"
                  % (fired,))
            print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
            return 2
        print("CONTROL unbound key 'z'     -> boxes=%d names=%r listeners_fired=%s"
              % (count, names, fired))
        if count != OPEN_BOXES:
            failures.append("an unbound key closed the menu (%d boxes, expected %d)"
                            % (count, OPEN_BOXES))

        for key, expect in CASES:
            count, names, fired = probe(app, tmp, "key-" + key, key)
            if count is None:
                print("INCONCLUSIVE: key %r produced no layout dump" % key)
                print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
                return 2
            if fired is None or fired == "0":
                print("INCONCLUSIVE: key %r dispatched to %r listeners" % (key, fired))
                print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
                return 2
            dismissed = count == 1
            chose = names == [expect]
            ok = dismissed and chose
            print("  key %-3r -> boxes=%d names=%-12r expected [%r] dismissed=%s %s"
                  % (key, count, names, expect, dismissed, "ok" if ok else "FAIL"))
            if not dismissed:
                failures.append("key %r left the menu open (%d mode boxes)"
                                % (key, count))
            elif not chose:
                failures.append("key %r selected %r, expected %r"
                                % (key, names, expect))

    if failures:
        print("FAIL:")
        for f in failures:
            print("  " + f)
        return 1
    print("PASS: with the EDIT MODE menu open, each of %d advertised letters "
          "selects its mode AND dismisses the menu; both controls hold"
          % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
