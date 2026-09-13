#!/usr/bin/env python3
"""Adjudicate three Preset Manager contracts in the shipping document.

It reads `native-ui/materialized/materialized-document.runtime.json` -- what
actually ships -- rather than a capture, because two of the three rules are
about code that no single captured frame can show: a dependency array, and the
ABSENCE of an element.

  RESET   Opening the manager clears the search.

          `PatternManager` is never unmounted -- App renders it
          unconditionally and `open` is a prop, so `if (!open) return null`
          tears the DOM down and leaves every `usePM` cell standing. Measured
          on the built app before the fix: a fresh open listed 8 rows, a
          search left 0, and closing and reopening still listed 0, under a
          header that went on reporting "0 user . 8 factory". Nothing clears
          it by hand either -- the panel's X is `onClose`, the rail's CLEAR is
          `onClearAll`.

          The dep array is HALF the contract and is checked as such. The
          sibling keydown effect lists `[open, onClose, setSelectedId]`, and
          `onClose` is an inline arrow at the App call site -- a fresh
          identity on every App render. A reset keyed on that re-runs
          constantly and empties the field WHILE THE USER IS TYPING IN IT. So
          the reset must be keyed on `[open]` and nothing else, and
          `--plant unstable-deps` writes the other spelling to prove this
          detector can tell them apart.

  CHIP    No row paints a trailing F / U chip.

          It was drawn in the same bordered pill the editor gives REAL
          keyboard shortcuts, so it read as a keybinding, and it was not one:
          no onClick, no ref, no data attribute, no role, no tabIndex.
          (`f` IS a live accelerator -- `modeKeys.f` -> FLARE -- which is what
          made the chip misleading rather than merely redundant. It is
          suppressed while this dialog is up: measured, `f` with the manager
          closed paints "EDIT -> FLARE" and `f` with it open paints no status
          at all. `u` is bound nowhere.)

  NAME    The heading's preset name is measured in a box that declares its own
          type, and carries exactly ONE child.

          The name shipped painting "FLA" of "FLAT". Not an overlap -- the
          title's box ended at 648.000 and the FACTORY badge began at 658.000,
          ten clear pixels later -- but a clip: the name's box was 25.094px
          for ~37.6px of ink, and `overflow: hidden` cut the rest.

          The child count is the load-bearing half. With the default star
          beside it the name's box came back 21.000 for 38.000 of ink; the
          same box on a preset with no star came back 123.000 for 123.000.
          One text child measures right, two do not, so the heading carries
          one.

Exit codes: 0 pass, 1 fail.
"""
import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOC = os.path.join(REPO, "native-ui", "materialized",
                   "materialized-document.runtime.json")

RESET_BODY = ('setQuery("");\n    setShowImport(false);\n    '
              'setImportText("");\n  }, [open]);')
RESET_UNSTABLE = ('setQuery("");\n    setShowImport(false);\n    '
                  'setImportText("");\n  }, [open, onClose, setSelectedId]);')
CHIP = 'pattern.source === "factory" ? "F" : "U"'
TITLE_OPEN = 'React.createElement("div", { "data-spectr-manager-title": true,'
TITLE_TEXT = 'maxWidth: 190 } }, pattern.name)'
TITLE_SPAN = 'React.createElement("span", { "data-spectr-manager-title"'
HEADING_STAR = '{ style: { color: "hsl(50,90%,65%)", marginRight: 6 } }, "★")'
BADGE_DIV = 'React.createElement("div", { "data-spectr-manager-source": true,'
BADGE_SHRINK = 'flexShrink: 0,\n    whiteSpace: "nowrap",'

PLANTS = {
    # main's behaviour, restored exactly: no reset at all. A suite that
    # cannot reject this does not cover the defect that was reported.
    "no-reset": lambda h: h.replace(RESET_BODY, "}, [open]);"),
    # The OTHER wrong implementation, and the one that looks right in a diff:
    # a reset keyed on a per-render identity, which clears the field mid-type.
    "unstable-deps": lambda h: h.replace(RESET_BODY, RESET_UNSTABLE),
    # The chip back on every row.
    "chip": lambda h: h.replace(
        'onClick,\n      style: {\n        padding: "6px 14px",',
        'onClick,\n      "data-chip": pattern.source === "factory" ? "F" : "U",'
        '\n      style: {\n        padding: "6px 14px",'),
    # The heading title back to a span, which is what stopped its declared
    # font size from reaching the measure.
    "title-span": lambda h: h.replace(TITLE_OPEN, TITLE_SPAN + ': true,'),
    # A second child in the heading: the exact shape that measured 21 for 38.
    "title-two-children": lambda h: h.replace(
        TITLE_TEXT,
        'maxWidth: 190 } }, isDefault && /* @__PURE__ */ React.createElement'
        '("span", { style: { color: "hsl(50,90%,65%)", marginRight: 6 } }, '
        '"★"), pattern.name)'),
    # A badge free to be crushed instead of the name ellipsising.
    "badge-shrinks": lambda h: h.replace(BADGE_SHRINK, 'whiteSpace: "nowrap",'),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plant", choices=sorted(PLANTS))
    args = ap.parse_args()

    html = json.load(open(DOC, encoding="utf-8"))["html"]

    # CONTROL, read BEFORE any plant. Every rule below is about the Preset
    # Manager, so a document that does not contain it is a document this
    # detector cannot adjudicate -- and "0 chips found" would read as a clean
    # pass on exactly that document.
    control = html.count("function PatternManager(") + html.count(
        "function PatternRow(") + html.count("function PatternDetail(")
    print("control: %d preset-manager component definitions" % control)
    if control != 3:
        sys.exit("FAIL: expected 3 preset-manager components, found %d -- the "
                 "detector is reading the wrong document" % control)

    if args.plant:
        planted = PLANTS[args.plant](html)
        if planted == html:
            sys.exit("FAIL: plant %r changed nothing, so it proves nothing"
                     % args.plant)
        html = planted
        print("planted: %s" % args.plant)

    bad = []

    # -- RESET ------------------------------------------------------------
    resets = html.count(RESET_BODY)
    print("  RESET  reset-on-open effect keyed on [open]: %d" % resets)
    if resets != 1:
        bad.append("the manager has %d reset-on-open effect(s) keyed on "
                   "[open], expected 1 -- a reopened manager keeps the "
                   "previous search, and the list reads filtered under a "
                   "header that does not" % resets)
    unstable = html.count(RESET_UNSTABLE)
    if unstable:
        bad.append("the reset is keyed on [open, onClose, setSelectedId]; "
                   "`onClose` is an inline arrow at the App call site, so that "
                   "effect re-runs every App render and would clear the search "
                   "field while the user is still typing in it")

    # -- CHIP -------------------------------------------------------------
    chips = html.count(CHIP)
    print("  CHIP   trailing F / U chip declarations: %d" % chips)
    if chips:
        bad.append("a preset row still paints a trailing %r chip in the "
                   "surface the editor reserves for real keyboard shortcuts; "
                   "it binds nothing, and the rows are already grouped under "
                   "FACTORY / USER headings" % "F/U")

    # -- NAME -------------------------------------------------------------
    title_div = html.count(TITLE_OPEN)
    title_text = html.count(TITLE_TEXT)
    star = html.count(HEADING_STAR)
    badge = html.count(BADGE_DIV)
    shrink = html.count(BADGE_SHRINK)
    print("  NAME   title box as a div: %d, single text child: %d, "
          "heading star: %d" % (title_div, title_text, star))
    print("  NAME   badge box as a div: %d, badge flexShrink 0: %d"
          % (badge, shrink))
    if title_div != 1:
        bad.append("the heading title is not a div; a span's declared font "
                   "size does not reach the measure here, so its box comes "
                   "back too small and the name is clipped inside it")
    if title_text != 1:
        bad.append("the heading title does not carry exactly one text child "
                   "with its own type and maxWidth budget")
    if star:
        bad.append("the heading carries a second child beside the name; that "
                   "is the case this runtime measures wrong (21px box for "
                   "38px of ink)")
    if badge != 1:
        bad.append("the FACTORY / USER badge is not a div")
    if shrink != 1:
        bad.append("the badge can shrink, so a tight heading is resolved by "
                   "crushing the badge instead of ellipsising the name")

    if bad:
        for line in bad:
            print("FAIL: " + line, file=sys.stderr)
        return 1
    print("PASS: the manager resets its search on open (keyed on [open] "
          "alone), no row paints an F / U chip, and the heading name owns a "
          "single-child box beside a badge that cannot shrink")
    return 0


if __name__ == "__main__":
    sys.exit(main())
