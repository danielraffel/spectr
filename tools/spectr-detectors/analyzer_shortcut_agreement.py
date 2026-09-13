#!/usr/bin/env python3
"""Assert every surface that names the analyzer-cycle key names a key that works.

WHY THIS EXISTS

    Two surfaces advertised two different keys, and only one of them was bound:

        SHORTCUTS popover   `6`  -- Cycle analyzer
        ANALYZER popover    "ANALYZER . A to cycle"

    `A` did nothing.  It was not bound in any state -- `modeKeys` is
    s/l/b/f/g/1..5 and the analyzer branch tested `k === "6"` alone -- so the
    popover a user reads WHILE LOOKING AT THE ANALYZER told them to press a key
    the app ignores.

    Nothing else in the suite can see this.  Both popovers render pixel for
    pixel the same whether the key they name works or not, so every screenshot,
    every layout assertion and every caption check passes on a dead shortcut.
    That is the same class of defect as the EDIT MODE chips, and the same
    reason `edit_mode_shortcut_keys.py` exists -- but that detector drives the
    built app, so it cannot run on a fixture-only job.  This one reads the
    checked-in artifact, so it needs no build, no GPU, no app and no
    third-party module, and registers on every runner configuration including
    the chrome-less acceptance one.

HOW IT MEASURES

    It reads the three literals out of the shipping document and compares the
    SETS, in both directions:

        advertised  = the SHORTCUTS row's keycap, split on "/",
                      plus the ANALYZER popover header's letter
        accepted    = every key the analyzer branch of the keydown handler
                      compares against

    Every advertised key must be accepted (or a surface lies), and every
    accepted key must be advertised (or a working shortcut is a secret).  The
    handler lower-cases `e.key` before comparing, so the comparison is
    case-folded here too.

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

ROW = re.compile(r'React\.createElement\(Hrow, \{ k: "([^"]+)" \}, "Cycle analyzer"\)')
HEADER = re.compile(r'"ANALYZER \\xB7 ([^ ]+) to cycle"')
# The analyzer branch, located by the state machine only it contains, then read
# backwards to its guard. Anchoring on the guard text itself would make the
# detector agree with whatever the guard happens to say.
BRANCH = re.compile(
    r'if \(([^)]*(?:\)[^)]*)?)\) \{\n        e\.preventDefault\(\);\n'
    r'        setAnalyzerMode\(')
KEY_LITERAL = re.compile(r'k === "([^"]+)"')

PLANTS = {
    # The shipping state before this landed: the letter two surfaces advertise
    # is not accepted by anything.
    "restore-lie": lambda h: h.replace(
        'if (k === "a" || k === "6") {', 'if (k === "6") {'),
    # The other direction. `6` stays bound but stops being advertised, so a
    # shipped shortcut becomes undiscoverable.
    "drop-digit-from-chip": lambda h: h.replace(
        'Hrow, { k: "A / 6" }, "Cycle analyzer"',
        'Hrow, { k: "A" }, "Cycle analyzer"'),
    # A key nothing tells the user about.
    "bind-unadvertised": lambda h: h.replace(
        'if (k === "a" || k === "6") {',
        'if (k === "a" || k === "6" || k === "q") {'),
    # The ANALYZER popover drifts to a different letter. This is the exact
    # shape of the original defect, one letter over.
    "header-drifts": lambda h: h.replace(
        '"ANALYZER \\xB7 A to cycle"', '"ANALYZER \\xB7 Z to cycle"'),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plant", choices=sorted(PLANTS))
    args = ap.parse_args()

    with open(DOC, encoding="utf-8") as handle:
        html = json.load(handle)["html"]

    # CONTROL, read BEFORE any plant. Every rule below is about three specific
    # literals; a document that does not carry all three is one this detector
    # cannot adjudicate, and "no disagreement found" would read as a clean pass
    # on exactly that document.
    control = len(ROW.findall(html)) + len(HEADER.findall(html)) \
        + len(BRANCH.findall(html))
    print("control: %d analyzer-key surfaces located (row + header + branch)"
          % control)
    if control != 3:
        print("FAIL: expected 3 surfaces, found %d -- the detector is reading "
              "the wrong document or the analyzer branch was restructured"
              % control, file=sys.stderr)
        return 1

    if args.plant:
        planted = PLANTS[args.plant](html)
        if planted == html:
            print("FAIL: plant %r changed nothing, so it proves nothing"
                  % args.plant, file=sys.stderr)
            return 1
        html = planted
        print("planted: %s" % args.plant)

    row = ROW.findall(html)
    header = HEADER.findall(html)
    branch = BRANCH.findall(html)
    if len(row) != 1 or len(header) != 1 or len(branch) != 1:
        print("FAIL: after the plant the three surfaces no longer resolve "
              "uniquely (row=%d header=%d branch=%d)"
              % (len(row), len(header), len(branch)), file=sys.stderr)
        return 1

    row_keys = {part.strip().lower() for part in row[0].split("/") if part.strip()}
    advertised = set(row_keys)
    advertised.add(header[0].strip().lower())
    accepted = {key.lower() for key in KEY_LITERAL.findall(branch[0])}

    print("  SHORTCUTS row advertises   %s" % sorted(row_keys))
    print("  ANALYZER header advertises %r" % header[0])
    print("  handler accepts            %s" % sorted(accepted))

    bad = []
    lying = sorted(advertised - accepted)
    if lying:
        bad.append("advertised but not bound: %s -- a surface names a key the "
                   "app ignores, and no screenshot or layout assertion can "
                   "see that" % lying)
    secret = sorted(accepted - advertised)
    if secret:
        bad.append("bound but not advertised: %s -- a working shortcut no "
                   "surface names" % secret)

    if bad:
        for line in bad:
            print("FAIL: " + line, file=sys.stderr)
        return 1
    print("PASS: every analyzer-cycle key both surfaces advertise is accepted "
          "by the handler, and the handler accepts nothing they do not name "
          "(%s)" % sorted(accepted))
    return 0


if __name__ == "__main__":
    sys.exit(main())
