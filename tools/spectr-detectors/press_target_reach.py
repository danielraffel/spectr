#!/usr/bin/env python3
"""PRESS REACH — a control that advertises a press target must be reachable by a
press at the rect it paints.

The gap this closes
-------------------
`hit_target_reach.py` compares a control's own hit rect against its own painted
rect. That can only see a defect a control commits against ITSELF, and it is
scoped by construction to the shapes `classify()` recognises — settings toggles,
settings sliders, and four named transport buttons.

The expensive defect is the one an ANCESTOR commits. The help guide's close `x`
shipped as a correctly sized 32x32 button, correctly wired, inside a wrapper
whose box had collapsed to 0x0. `Rect::contains` is half-open, so a zero-area box
admits no point at all — and every self-vs-self rect comparison in this repo was
green, because nothing was wrong with the button. The only reason the guide was
ever dismissable at all was the symmetric ~500px slack `View::hit_test` grants an
`overflow: visible` child, measured from the wrapper's in-flow position at y=804
rather than from where the `x` paints at y=73.

So this detector does not do rect arithmetic. It reads the verdicts of a real
`View::hit_test` run at each named control's painted centre, performed natively
by `SPECTR_PRESS_REACH=1 Spectr-native-shot`. The reasoning that matters is the
hit test, and the hit test is C++; this file's job is to turn its receipt into a
gate, and to refuse to return a verdict when the receipt is empty.

Input
-----
A `spectr-press-reach-v1` document with a `required` array, one row per named
control: `selector`, `element_id`, `resolved`, `painted`, `probe`, `reached`,
`ok`, `note`.

Exit codes
----------
0  every required control was reached by a press at its own painted rect
1  at least one was not
2  usage / IO / schema error
3  the population was empty or incoherent, so the run proved nothing
"""

import argparse
import json
import sys

SCHEMA = "spectr-press-reach-v1"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="a *.press-reach.json written by the native probe")
    ap.add_argument("--plant", action="store_true",
                    help="corrupt every row's reach in memory and require the "
                         "gate to go red, proving it can fail on this input")
    args = ap.parse_args()

    try:
        with open(args.dump, encoding="utf-8") as handle:
            doc = json.load(handle)
    except OSError as exc:
        sys.exit("%s: %s" % (args.dump, exc))
    except json.JSONDecodeError as exc:
        sys.exit("%s: not JSON (%s)" % (args.dump, exc))

    if doc.get("schema") != SCHEMA:
        sys.exit("%s: unexpected schema %r" % (args.dump, doc.get("schema")))

    rows = doc.get("required")
    if rows is None:
        sys.exit("%s: no `required` array; this document is a whole-tree sweep, "
                 "which is a diagnostic rather than a gated population" % args.dump)

    # An empty population is not a clean run. A press-reach gate that examined
    # nothing and a press-reach gate that found nothing wrong print the same
    # "OK" unless this is checked, and the blind one is the one that ships a
    # defect.
    if not rows:
        print("CONTROL FAILED: the required population is empty, so this run "
              "proved nothing about any control.")
        return 3

    if args.plant:
        # Self-test arm. Every row is forced unreachable, so a run that still
        # reports OK is a broken detector rather than a healthy build.
        for row in rows:
            row["ok"] = False
            row["reached"] = "(planted)"
            row["note"] = "planted by --plant"

    findings = []
    for row in rows:
        selector = row.get("selector", "(unnamed)")
        if not row.get("resolved"):
            findings.append("UNRESOLVED %s — no addressable view; %s"
                            % (selector, row.get("note") or "no note"))
            continue
        painted = row.get("painted") or [0, 0, 0, 0]
        if len(painted) != 4:
            sys.exit("%s: row %r has a malformed `painted`" % (args.dump, selector))
        if painted[2] <= 0 or painted[3] <= 0:
            findings.append(
                "ZERO-AREA %s — paints %gx%g, and Rect::contains is half-open, "
                "so no press can land in it" % (selector, painted[2], painted[3]))
            continue
        if not row.get("ok"):
            findings.append(
                "UNREACHABLE %s — paints (%g,%g %gx%g); a press at (%g,%g) "
                "reached %s; %s"
                % (selector, painted[0], painted[1], painted[2], painted[3],
                   (row.get("probe") or [0, 0])[0], (row.get("probe") or [0, 0])[1],
                   row.get("reached", "(unknown)"), row.get("note") or "no note"))

    print("control: %d required press target(s) in %s"
          % (len(rows), doc.get("surface", "(unnamed surface)")))
    for row in rows:
        painted = row.get("painted") or [0, 0, 0, 0]
        print("  %-42s paints (%g,%g %gx%g) press-> %s  %s"
              % (row.get("selector", "(unnamed)"), painted[0], painted[1],
                 painted[2], painted[3], row.get("reached", "(unknown)"),
                 "OK" if row.get("ok") else "UNREACHABLE"))

    if findings:
        print()
        for finding in findings:
            print(finding)
        print("\n%d finding(s):" % len(findings))
        return 1

    print("OK: every required control is reached by a press at the rect it paints.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
