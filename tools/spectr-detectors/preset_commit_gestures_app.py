#!/usr/bin/env python3
"""Prove the Preset Manager's commit gestures on the BUILT app, by end state.

WHY THIS AND NOT THE ARTIFACT DETECTOR

    `preset_commit_gestures.py` adjudicates the structure a working gesture
    requires, from the checked-in document, in milliseconds.  It cannot know
    whether the gesture actually commits.  This drives the shipping standalone
    headlessly and reads the END STATE instead of the gesture:

      * did the BAND FIELD move -- `SPECTR_STATE_OUT`'s `gain_db[]`, the
        processor's own published state, not a label; and
      * did the DIALOG CLOSE -- the manager footer's EXPORT ALL (FILE) button
        is painted only while it is up.

    Asserting "the handler ran" would pass on a handler that applies without
    closing, which is exactly half of what the user reported.

THE INSTRUMENT, AND ITS CONTROLS

    A gesture that appears not to fire is as likely to be a dead probe as a
    dead product, so every run carries two controls of its own:

      * editor liveness -- the toolbar is painted in every state, so a dump
        that cannot see it is a fail-closed editor, not a closed dialog.  Read
        before any verdict.
      * the APPLY BUTTON case is the instrument's positive control.  It is a
        plain click on an ordinary button; if it does not commit, nothing
        below means anything and this reports no verdict rather than failure.

    `factory:comb` is the subject on purpose: `factory:flat` resolves to 32
    zero gains, which is indistinguishable from "nothing applied".

    A double-click is delivered as two activations of the same row, which is
    the click stream a real double-click produces -- the runtime dispatches no
    `dblclick`/`doubleclick` of its own, which is the whole reason the
    advertised gesture was dead.

Exit codes: 0 pass, 1 fail, 2 no verdict (the instrument could not measure).
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(REPO, "build", "Spectr.app", "Contents", "MacOS", "Spectr")

OPEN_MANAGER = ('[data-spectr-menu-root="pattern"] button,'
                '[data-spectr-pattern-manage]')
# Not factory:flat: its 32 zero gains cannot be told from "nothing happened".
SUBJECT = '[data-spectr-pattern-id="factory:comb"]'
OTHER = '[data-spectr-pattern-id="factory:vocal"]'
APPLY_BUTTON = '[data-spectr-manager-action="apply"]'
# Painted only while the manager is up.
DIALOG_MARKER = "EXPORT ALL (FILE)"
# Painted in every state; its absence means a fail-closed editor.
ALIVE_MARKERS = ("CLEAR", "SCULPT", "LEVEL", "BOOST", "FLARE", "GLIDE")

# (label, clicks, key, expect_applied, expect_dialog_open)
CASES = [
    ("double-click commits", ",".join([OPEN_MANAGER, SUBJECT, SUBJECT]), None,
     True, False),
    ("Return commits", ",".join([OPEN_MANAGER, SUBJECT]), "Enter", True, False),
]
CONTROLS = [
    # The instrument's own positive control: an ordinary button click.
    ("APPLY button commits", ",".join([OPEN_MANAGER, SUBJECT, APPLY_BUTTON]),
     None, True, False),
    ("one click only selects", ",".join([OPEN_MANAGER, SUBJECT]), None,
     False, True),
    ("two different rows do not commit",
     ",".join([OPEN_MANAGER, SUBJECT, OTHER]), None, False, True),
    ("Return with no selection does nothing", OPEN_MANAGER, "Enter",
     False, True),
    ("an unbound key does nothing", ",".join([OPEN_MANAGER, SUBJECT]), "z",
     False, True),
    ("Escape closes without committing", ",".join([OPEN_MANAGER, SUBJECT]),
     "Escape", False, False),
]


def probe(app, tmp, tag, clicks, key):
    dump = os.path.join(tmp, tag + ".json")
    state = os.path.join(tmp, tag + ".state.json")
    env = dict(os.environ)
    env.update(PULP_HEADLESS="1", PULP_FRAMES="90",
               PULP_SCREENSHOT=dump + ".png",
               SPECTR_LAYOUT_DUMP=dump, SPECTR_STATE_OUT=state)
    if clicks:
        env["SPECTR_CLICK"] = clicks
    if key:
        env["SPECTR_KEY_JS"] = key
    subprocess.run([app], env=env, capture_output=True, timeout=300)
    if not os.path.exists(dump) or not os.path.exists(state):
        return None
    with open(dump) as fh:
        doc = json.load(fh)
    texts = set()
    for node in doc.get("nodes") or []:
        for box in node.get("measured_text_boxes") or []:
            texts.add(box["text"].strip())
    with open(state) as fh:
        gains = json.load(fh).get("gain_db") or []
    return {
        "alive": any(marker in texts for marker in ALIVE_MARKERS),
        "dialog": DIALOG_MARKER in texts,
        "moved": sum(1 for value in gains if abs(value) > 1e-6),
        "bands": len(gains),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app", nargs="?", default=APP)
    args = ap.parse_args()
    if not os.path.exists(args.app):
        print("INCONCLUSIVE: app not built at %s" % args.app)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. "
              "Not a pass.")
        return 2

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        for index, (label, clicks, key, want_applied, want_open) in enumerate(
                CONTROLS + CASES):
            is_control = index < len(CONTROLS)
            result = probe(args.app, tmp, "case%d" % index, clicks, key)
            if result is None:
                print("INCONCLUSIVE: %r produced no dump/state pair" % label)
                print("RESULT: UNMEASURED (exit 2) -- nothing was "
                      "adjudicated. Not a pass.")
                return 2
            if not result["alive"]:
                print("INCONCLUSIVE: %r painted no toolbar -- the editor is "
                      "fail-closed, so no verdict below means anything"
                      % label)
                print("RESULT: UNMEASURED (exit 2) -- nothing was "
                      "adjudicated. Not a pass.")
                return 2
            applied = result["moved"] > 0
            ok = applied == want_applied and result["dialog"] == want_open
            print("  %-9s %-38s applied=%-5s (%d/%d bands) open=%-5s  %s"
                  % ("CONTROL" if is_control else "CASE", label, applied,
                     result["moved"], result["bands"], result["dialog"],
                     "ok" if ok else "FAIL"))
            if ok:
                continue
            # The APPLY button is the instrument, not the product claim: if a
            # plain button click cannot commit, every gesture verdict below is
            # measuring a broken probe.
            if label.startswith("APPLY button"):
                print("INCONCLUSIVE: the APPLY button itself did not apply "
                      "and close, so this probe cannot adjudicate any gesture")
                print("RESULT: UNMEASURED (exit 2) -- nothing was "
                      "adjudicated. Not a pass.")
                return 2
            failures.append(
                "%s: applied=%s (expected %s), manager open=%s (expected %s)"
                % (label, applied, want_applied, result["dialog"], want_open))

    if failures:
        print("FAIL:")
        for failure in failures:
            print("  " + failure)
        return 1
    print("PASS: double-click and Return each apply the preset AND close the "
          "manager, and %d controls hold" % len(CONTROLS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
