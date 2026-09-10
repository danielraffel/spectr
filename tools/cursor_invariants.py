#!/usr/bin/env python3
"""The cursor a person SEES over a named region must be the cursor the row asks for.

CUR-1..4 were once closed on a test named "cursors reach the shipping runtime".
That test proved a value arrived in a `View::CursorStyle` slot. It is not the
same claim as "a user sees a crosshair", and the rows' own notes said "Not
visible in installed builds" four times while the PASS stood beside them. So
this detector refuses the proxy and reads the style the macOS host would hand
AppKit, produced by `SPECTR_CURSOR_PROBE` inside the installed app:

    mouseMoved:   rootView->simulate_hover(pt)
                  target = rootView->hit_test(pt)
                  set_ns_cursor_for_style(target->cursor())
    mouseDown /   dragTarget = hit_test(pt); deliver_mouse_down/drag(...)
    mouseDragged: set_ns_cursor_for_style(dragTarget->cursor())

Two things this cannot see, stated so a green is never read as more than it is:

  * It stops at `View::CursorStyle`. The final `[[NSCursor crosshairCursor] set]`
    is transcribed into the probe's `ns_cursor` field from
    `set_ns_cursor_for_style`, not observed from AppKit — a headless capture has
    no cursor on screen to photograph.
  * A cursor can be correct and STATIC. An element that mounts with `crosshair`
    reports `crosshair` at every point on it, so a rule that only asserts
    "crosshair over the plot" passes whether or not the app is responding.
    `--require-responsive` names two points that must NOT agree and fails when
    they do, which is the check that separates the two.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

# The row's own vocabulary, mapped to the View::CursorStyle names the probe
# emits. Keeping the row's words here means a rule reads as the row reads.
ALIASES = {
    "crosshair": "crosshair",
    "grab": "grab",
    "open-hand": "grab",
    "grabbing": "grabbing",
    "closed-hand": "grabbing",
    "ew-resize": "horizontal-resize",
    "col-resize": "horizontal-resize",
    "horizontal-resize": "horizontal-resize",
    "pointer": "pointer",
    "default": "default",
}


def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    if doc.get("schema") != "spectr-cursor-probe-v1":
        print(f"INCONCLUSIVE: {path} is not a spectr-cursor-probe-v1 document",
              file=sys.stderr)
        sys.exit(3)
    return {p["name"]: p for p in doc.get("probes", [])}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("probe", help="JSON written by SPECTR_CURSOR_PROBE")
    ap.add_argument("--expect", action="append", default=[], metavar="NAME=CURSOR",
                    help="a probe point and the cursor a user must see there")
    ap.add_argument("--require-responsive", action="append", default=[],
                    metavar="NAME_A,NAME_B",
                    help="two points whose cursors must DIFFER; catches an "
                         "element that is merely showing its mount-time value")
    ap.add_argument("--plant", choices=["wrong-cursor", "no-hit", "freeze"],
                    help="corrupt a passing reading to prove the check can fail")
    args = ap.parse_args()

    probes = load(args.probe)
    if not probes:
        print("INCONCLUSIVE: the probe document contains no points",
              file=sys.stderr)
        return 3

    rules = []
    for spec in args.expect:
        name, _, want = spec.partition("=")
        want = ALIASES.get(want, want)
        if name not in probes:
            print(f"INCONCLUSIVE: no probe named {name!r} in {args.probe}; "
                  f"have {sorted(probes)}", file=sys.stderr)
            return 3
        rules.append((name, want))

    pairs = []
    for spec in args.require_responsive:
        a, _, b = spec.partition(",")
        for n in (a, b):
            if n not in probes:
                print(f"INCONCLUSIVE: no probe named {n!r} in {args.probe}",
                      file=sys.stderr)
                return 3
        pairs.append((a, b))

    if not rules and not pairs:
        print("INCONCLUSIVE: nothing asked for; pass --expect or "
              "--require-responsive", file=sys.stderr)
        return 3

    # The plants mutate the loaded reading, not a fixture file, so a plant
    # cannot quietly stop running the way a 5th-positional-argument flag did.
    if args.plant == "wrong-cursor" and rules:
        victim = probes[rules[0][0]]
        victim["resolved_cursor"] = "zoom-out"
        print(f"CONTROL: planted resolved_cursor=zoom-out on {rules[0][0]}")
    elif args.plant == "no-hit" and rules:
        victim = probes[rules[0][0]]
        victim["hit"] = False
        victim["resolved_cursor"] = "<no-hit>"
        print(f"CONTROL: planted a missed hit on {rules[0][0]}")
    elif args.plant == "freeze" and pairs:
        a, b = pairs[0]
        probes[b]["resolved_cursor"] = probes[a]["resolved_cursor"]
        print(f"CONTROL: planted {b} frozen to {a}'s cursor "
              f"({probes[a]['resolved_cursor']})")
    elif args.plant:
        print(f"cannot plant {args.plant}: no rule of that kind was given",
              file=sys.stderr)
        return 4

    # A miss is never scored as a cursor. "Nothing was under the pointer" and
    # "the app shows an arrow there" must not read the same.
    misses = [n for n, _ in rules if not probes[n].get("hit", False)]
    if misses:
        print(f"INCONCLUSIVE: no view under {', '.join(misses)} — the probe "
              f"point is wrong, so nothing was measured", file=sys.stderr)
        return 3

    red = []
    for name, want in rules:
        p = probes[name]
        got = p["resolved_cursor"]
        line = (f"{name:<24} point=({p['point']['x']:.0f},{p['point']['y']:.0f}) "
                f"phase={p['phase']:<5} hit={p['hit_id']:<18} "
                f"want={want:<18} got={got:<18} ns={p['ns_cursor']}")
        print(f"  {'OK ' if got == want else 'RED'}  {line}")
        if got != want:
            red.append((name, want, got))

    for a, b in pairs:
        ca, cb = probes[a]["resolved_cursor"], probes[b]["resolved_cursor"]
        if ca == cb:
            print(f"  RED  {a} and {b} both report {ca!r} — the element is "
                  f"reporting one constant, so the cursor is not tracking the "
                  f"pointer at all")
            red.append((f"{a}~{b}", "two different cursors", ca))
        else:
            print(f"  OK   {a}={ca} differs from {b}={cb} — the cursor responds "
                  f"to where the pointer is")

    print(f"probe={os.path.basename(args.probe)}  rules={len(rules)}  "
          f"responsiveness_pairs={len(pairs)}  "
          f"CONTROL points_measured={len(probes)}")

    if red:
        for name, want, got in red:
            print(f"RED    {name}: expected {want}, resolved {got}")
        return 1
    print("GREEN  every named region resolves the cursor its row requires")
    if args.plant:
        print("BROKEN: the planted negative did not fail the check",
              file=sys.stderr)
        return 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
