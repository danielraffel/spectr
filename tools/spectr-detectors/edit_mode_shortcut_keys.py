#!/usr/bin/env python3
"""Assert the EDIT MODE dropdown's shortcut chips name keys that actually work.

WHY THIS EXISTS

    The chips are the app's promise to the user.  A chip reading `G` that does
    not select GLIDE is worse than no chip at all -- it is a label that lies,
    and nothing else in the suite can see it: every screenshot, every layout
    assertion and every caption check passes on a dead shortcut.

    It was dead.  On the shipping standalone before this detector, NO edit-mode
    shortcut worked -- not the advertised digits either -- because the
    handler's overlay guard matched the always-mounted Settings dialog and
    returned before reading the key.

HOW IT MEASURES

    `SPECTR_KEY_JS` dispatches one keydown through the runtime's own listener
    path and prints `listeners_fired=N` from a listener the fixture installs
    itself.  That count is the instrument's POSITIVE CONTROL: a dispatch that
    reaches no listener looks exactly like a key the app ignores.

    The verdict is read off the toolbar trigger, which renders
    `editMode.toUpperCase()`.  Four of the five modes are proved by clicking
    GLIDE first and watching the key move it somewhere else; a companion run
    with the same click and NO key must still read GLIDE, so the key -- not
    the click -- is what moved it.  An unbound key must move nothing.

Exit codes: 0 pass, 1 fail, 2 inconclusive (the probe could not measure).
"""
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(REPO, "build-now", "Spectr.app", "Contents", "MacOS", "Spectr")

MODES = ("SCULPT", "LEVEL", "BOOST", "FLARE", "GLIDE")
PRESET_GLIDE = ('[data-spectr-menu-root="edit"] [data-spectr-menu-trigger],'
                '[data-spectr-edit-mode="glide"]')

# (key, click-preamble, expected trigger label)
CASES = [
    ("s", PRESET_GLIDE, "SCULPT"),
    ("l", PRESET_GLIDE, "LEVEL"),
    ("b", PRESET_GLIDE, "BOOST"),
    ("f", PRESET_GLIDE, "FLARE"),
    ("g", "", "GLIDE"),
    # Shipped behaviour, kept working: the digits the chips used to advertise.
    ("5", "", "GLIDE"),
    ("2", "", "LEVEL"),
]
# Controls. The click-only run proves the click, not the key, is not what the
# lettered cases are measuring; the unbound key proves a key that should do
# nothing does nothing.
CONTROLS = [
    ("click only, no key", None, PRESET_GLIDE, "GLIDE"),
    ("unbound key z", "z", "", "SCULPT"),
]


def probe(app, tmp, tag, key, click):
    dump = os.path.join(tmp, tag + ".json")
    log = os.path.join(tmp, tag + ".log")
    env = dict(os.environ)
    env.update(PULP_HEADLESS="1", PULP_FRAMES="90",
               PULP_SCREENSHOT=dump + ".png", SPECTR_LAYOUT_DUMP=dump)
    if click:
        env["SPECTR_CLICK"] = click
    if key:
        env["SPECTR_KEY_JS"] = key
    out = subprocess.run([app], env=env, capture_output=True, timeout=240)
    text = (out.stdout or b"").decode("utf-8", "replace") \
        + (out.stderr or b"").decode("utf-8", "replace")
    with open(log, "w") as fh:
        fh.write(text)
    fired = None
    for line in text.splitlines():
        if "listeners_fired=" in line:
            fired = line.split("listeners_fired=", 1)[1].split()[0]
            break
    if not os.path.exists(dump):
        return None, fired
    with open(dump) as fh:
        doc = json.load(fh)
    labels = set()
    for node in doc.get("nodes") or []:
        for box in node.get("measured_text_boxes") or []:
            token = box["text"].strip().rstrip("▾").strip()
            if token in MODES:
                labels.add(token)
    return labels, fired


def main():
    app = sys.argv[1] if len(sys.argv) > 1 else APP
    if not os.path.exists(app):
        print("INCONCLUSIVE: app not built at %s" % app)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, key, click, expect in CONTROLS:
            labels, fired = probe(app, tmp, "ctl-" + (key or "none"), key, click)
            if labels is None:
                print("INCONCLUSIVE: control %r produced no layout dump" % name)
                print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
                return 2
            if key and (fired is None or fired == "0"):
                print("INCONCLUSIVE: control %r dispatched to %r listeners -- the "
                      "key fixture is dead, so no key result below means anything"
                      % (name, fired))
                print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
                return 2
            print("CONTROL %-20s -> %-28r listeners_fired=%s" % (name, sorted(labels), fired))
            if labels != {expect}:
                failures.append("control %r read %r, expected exactly %r"
                                % (name, sorted(labels), expect))

        for key, click, expect in CASES:
            labels, fired = probe(app, tmp, "key-" + key, key, click)
            if labels is None:
                print("INCONCLUSIVE: key %r produced no layout dump" % key)
                print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
                return 2
            if fired is None or fired == "0":
                print("INCONCLUSIVE: key %r dispatched to %r listeners" % (key, fired))
                print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
                return 2
            ok = labels == {expect}
            print("  key %-3r -> %-28r expected %-8r %s listeners_fired=%s"
                  % (key, sorted(labels), expect, "ok" if ok else "FAIL", fired))
            if not ok:
                failures.append("key %r selected %r, expected %r"
                                % (key, sorted(labels), expect))

    if failures:
        print("FAIL:")
        for f in failures:
            print("  " + f)
        return 1
    print("PASS: every advertised EDIT MODE shortcut selects its mode "
          "(%d keys), and both controls hold" % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
