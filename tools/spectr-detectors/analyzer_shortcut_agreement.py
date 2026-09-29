#!/usr/bin/env python3
"""Assert no key cycles the analyzer, and no surface says one does.

WHY THIS EXISTS

    The analyzer had a keyboard shortcut: A (and its alias 6) cycled PEAK ->
    AVG -> BOTH -> OFF, advertised by the ANALYZER popover's "A to cycle" and a
    SHORTCUTS row. A is a note in Logic's Musical Typing, so playing notes from
    the computer keyboard kept flipping the analyzer, OFF included, which
    changes the whole display. The shortcut was removed, key and hint together.

    A half-removal is the failure to guard against, and it is invisible in
    every screenshot: a hint that names a key that no longer does anything, or
    a handler still bound to a key no surface names -- a secret shortcut that
    still fights the host's keys. This detector reads the checked-in artifact,
    so it needs no build, no GPU, no app and no third-party module.

HOW IT MEASURES

    CONTROL first: the ANALYZER popover's header and the editor's global
    keydown handler must both be located, or "nothing found" would read as a
    clean pass on a document this cannot see into. Then:

        no "... to cycle" text anywhere in the document,
        no SHORTCUTS row captioned "Cycle analyzer",
        no branch of the global keydown handler that calls setAnalyzerMode.

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

POPOVER = "function AnalyzerPopover({ value, onChange, onClose }) {"
HEADER = '"ANALYZER")'
HANDLER_START = "    const modeKeys = {"
HANDLER_END = '    window.addEventListener("keydown", onKey);'

ROW = re.compile(r'Hrow, \{ k: "[^"]+" \}, "Cycle analyzer"')
CYCLE_TEXT = re.compile(r'to cycle"')

REMOVED_NOTE = ("      // No key cycles the analyzer. A and 6 did, and A is a Musical Typing\n"
                "      // note; the ANALYZER menu is the one way to change it.\n")

PLANTS = {
    # The header names the key again.
    "restore-header": lambda h: h.replace(
        POPOVER + h.split(POPOVER, 1)[1].split(HEADER, 1)[0] + HEADER,
        POPOVER + h.split(POPOVER, 1)[1].split(HEADER, 1)[0]
        + '"ANALYZER \\xB7 A to cycle")', 1),
    # The SHORTCUTS popover lists it again.
    "restore-row": lambda h: h.replace(
        'React.createElement(Hrow, { k: "M" }, "Mute/unmute selection")',
        'React.createElement(Hrow, { k: "A / 6" }, "Cycle analyzer"), '
        'React.createElement(Hrow, { k: "M" }, "Mute/unmute selection")', 1),
    # The binding comes back with no surface naming it: a secret shortcut.
    "restore-binding": lambda h: h.replace(
        REMOVED_NOTE,
        '      if (k === "a") {\n'
        '        e.preventDefault();\n'
        '        setAnalyzerMode((m) => m === "peak" ? "avg" : "peak");\n'
        '      }\n', 1),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plant", choices=sorted(PLANTS))
    args = ap.parse_args()

    with open(DOC, encoding="utf-8") as handle:
        html = json.load(handle)["html"]

    popover = html.split(POPOVER, 1)
    handler_at = html.find(HANDLER_START)
    control_header = len(popover) == 2 and HEADER in popover[1][:2000]
    control_handler = handler_at >= 0 and html.find(HANDLER_END, handler_at) > handler_at
    print("control: analyzer header located=%s, global key handler located=%s"
          % (control_header, control_handler))
    if not (control_header and control_handler):
        print("FAIL: the detector cannot see the surfaces it adjudicates -- the "
              "document or its structure is not the one it reads",
              file=sys.stderr)
        return 1

    if args.plant:
        planted = PLANTS[args.plant](html)
        if planted == html:
            print("FAIL: plant %r changed nothing, so it proves nothing"
                  % args.plant, file=sys.stderr)
            return 1
        html = planted
        print("planted: %s" % args.plant)

    start = html.find(HANDLER_START)
    handler = html[start:html.find(HANDLER_END, start)]
    bad = []
    if CYCLE_TEXT.search(html):
        bad.append("a surface still says a key cycles something")
    if ROW.search(html):
        bad.append("the SHORTCUTS popover still lists a Cycle analyzer key")
    if "setAnalyzerMode(" in handler:
        bad.append("the global keydown handler still changes the analyzer")
    if bad:
        for line in bad:
            print("FAIL: " + line, file=sys.stderr)
        return 1
    print("PASS: no key cycles the analyzer, and no surface says one does")
    return 0


if __name__ == "__main__":
    sys.exit(main())
