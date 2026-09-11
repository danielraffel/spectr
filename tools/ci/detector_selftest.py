#!/usr/bin/env python3
"""Prove the Spectr detector suite can still both PASS and FAIL.

Two of these detectors shipped DEAD -- `content_invariants.py` and
`control_invariants.py` both crashed on import of an `appearance_invariants`
function that was never written, from the day they landed. Nothing noticed,
because nothing ran them: the acceptance workflow invoked exactly one detector
out of a dozen, and the rest were manual-only. A detector nobody runs is
indistinguishable from a detector that passes.

So this runs the whole fixture-driven half of the suite on every acceptance
job, and for each one asserts BOTH directions against committed evidence:

  * a case that must come back clean (exit 0), and
  * a case that must come back red -- a known-bad fixture, or the detector's
    own `--plant`, which mutates a healthy input so the rule has to fire.

A detector that only ever passes is not coverage; a plant that stops firing
means the rule rotted. Either is reported here rather than in six months.

Every input is a committed fixture under docs/evidence/, so this needs no
build, no app, no GPU and no network. It is seconds, not minutes.

Exit codes: 0 all cases as expected, 1 at least one detector disagrees,
2 the harness itself could not run (missing fixture, missing Pillow).
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
E07 = os.path.join("docs", "evidence", "2026-09-07")
E11 = os.path.join("docs", "evidence", "2026-09-11")

D = os.path.join("tools", "spectr-detectors")
T = "tools"


def f(*parts: str) -> str:
    return os.path.join(*parts)


CURSOR_EXPECT = [
    "--expect", "CUR-1-canvas=crosshair",
    "--expect", "CUR-2-viewport=grab",
    "--expect", "CUR-4-trim-left=horizontal-resize",
    "--expect", "CUR-4-trim-right=horizontal-resize",
    "--expect", "CUR-3-viewport-drag=grabbing",
    "--expect", "CONTROL-same-view-off-plot=default",
    "--expect", "CONTROL-toolbar-button=pointer",
]

# (detector, case label, expected exit, argv after `python3`)
CASES: list[tuple[str, str, int, list[str]]] = [
    # --- the two that shipped dead -------------------------------------
    ("control_invariants", "healthy slider surface", 0,
     [f(T, "control_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json")]),
    ("control_invariants", "known-bad fixture (4 thumbless tracks)", 1,
     [f(T, "control_invariants.py"), f(E07, "SLIDER-no-thumb.layout.json")]),
    ("control_invariants", "plant: flatten a healthy track", 1,
     [f(T, "control_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--plant", "flatten-track"]),

    ("content_invariants", "required strings are paintable", 0,
     [f(T, "content_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--present", "APPEARANCE", "--present", "Bands",
      "--absent", "NOT-A-REAL-STRING"]),
    ("content_invariants", "plant: drop a required string", 1,
     [f(T, "content_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--present", "APPEARANCE", "--plant", "drop-present"]),
    ("content_invariants", "plant: surface a string that must stay hidden", 1,
     [f(T, "content_invariants.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--absent", "NOT-A-REAL-STRING", "--plant", "add-absent"]),

    # --- the user's named issues ---------------------------------------
    # issue 4: Settings copy button width
    ("copy_button_width_invariance", "width is state-invariant", 0,
     [f(D, "copy_button_width_invariance.py"), f(E11, "COPY-WIDTH-GREEN.layout.json")]),
    ("copy_button_width_invariance", "known-bad fixture (stretched to panel)", 1,
     [f(D, "copy_button_width_invariance.py"), f(E11, "COPY-WIDTH-RED.layout.json")]),
    ("copy_button_width_invariance", "plant", 1,
     [f(D, "copy_button_width_invariance.py"), f(E11, "COPY-WIDTH-GREEN.layout.json"),
      "--plant"]),

    # issue 3: settings slider thumb grows on hover
    ("slider_thumb_hover_growth", "thumbs grow under hover", 0,
     [f(D, "slider_thumb_hover_growth.py"),
      "--idle", f(E11, "slider-hover-GREEN-idle.layout.json"),
      "--hover", f(E11, "slider-hover-GREEN-hover.layout.json")]),
    ("slider_thumb_hover_growth", "known-bad fixture (no growth)", 1,
     [f(D, "slider_thumb_hover_growth.py"),
      "--idle", f(E11, "slider-hover-RED-idle.layout.json"),
      "--hover", f(E11, "slider-hover-RED-hover.layout.json")]),
    ("slider_thumb_hover_growth", "plant", 1,
     [f(D, "slider_thumb_hover_growth.py"),
      "--idle", f(E11, "slider-hover-GREEN-idle.layout.json"),
      "--hover", f(E11, "slider-hover-GREEN-hover.layout.json"), "--plant"]),

    # issue 6: cursors really change on hover
    ("cursor_invariants", "every region resolves its cursor", 0,
     [f(T, "cursor_invariants.py"), f(E07, "CUR-GREEN.cursor.json")] + CURSOR_EXPECT),
    ("cursor_invariants", "known-bad fixture (crosshair everywhere)", 1,
     [f(T, "cursor_invariants.py"), f(E07, "CUR-RED.cursor.json")] + CURSOR_EXPECT),
    ("cursor_invariants", "plant: wrong cursor", 1,
     [f(T, "cursor_invariants.py"), f(E07, "CUR-GREEN.cursor.json")]
     + CURSOR_EXPECT + ["--plant", "wrong-cursor"]),

    # issue 1/3: a drag pointer sample must not enqueue a React commit.
    # This one reads the checked-in materialized artifact, not a fixture.
    ("drag_hover_no_react_commit", "shipping artifact guards the setter", 0,
     [f(D, "drag_hover_no_react_commit.py")]),
    ("drag_hover_no_react_commit", "plant: strip the drag guard", 1,
     [f(D, "drag_hover_no_react_commit.py"), "--plant", "unguard"]),

    # --- layout truth ---------------------------------------------------
    ("appearance_invariants", "clean shipping settings dump", 0,
     [f(T, "appearance_invariants.py"), f(E07, "SET-2-shipping-scrolled.layout.json")]),
    ("appearance_invariants", "plant: overlap two painted runs", 1,
     [f(T, "appearance_invariants.py"), f(E07, "SET-2-shipping-scrolled.layout.json"),
      "--plant-overlap"]),
    ("appearance_invariants", "plant: overflow a run past its box", 1,
     [f(T, "appearance_invariants.py"), f(E07, "SET-2-shipping-scrolled.layout.json"),
      "--plant-overflow"]),

    ("centering_invariant", "owner/text pairs stay centred", 0,
     [f(T, "centering_invariant.py"), f(E07, "SLIDER-GREEN-thumb.layout.json")]),
    ("centering_invariant", "plant: shift a run off centre", 1,
     [f(T, "centering_invariant.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--plant"]),

    ("box_intersection", "no two painted runs collide", 0,
     [f(D, "box_intersection.py"), f(E07, "SLIDER-GREEN-thumb.layout.json")]),
    ("box_intersection", "built-in positive control fires", 0,
     [f(D, "box_intersection.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--self-test", os.path.join(os.environ.get("TMPDIR", "/tmp"), "spectr-selftest-bi")]),

    ("painted_vs_measured_width", "built-in positive control fires", 0,
     [f(D, "painted_vs_measured_width.py"), f(E07, "SLIDER-GREEN-thumb.layout.json"),
      "--self-test", os.path.join(os.environ.get("TMPDIR", "/tmp"), "spectr-selftest-pvm")]),

    ("reachability_census", "every adjudicable control is on screen", 0,
     [f(T, "reachability_census.py"), f(E07, "SET-837-settings-unscrolled")]),
    ("reachability_census", "plant: push one control off screen", 1,
     [f(T, "reachability_census.py"), f(E07, "SET-837-settings-unscrolled"),
      "--plant", "offscreen"]),
    ("reachability_census", "plant: collapse one control to zero", 1,
     [f(T, "reachability_census.py"), f(E07, "SET-837-settings-unscrolled"),
      "--plant", "zero"]),

    # Scoped to the settings panel on purpose: text behind the settings scrim
    # is dimmed BY DESIGN, and measuring it reddens this for the wrong reason.
    ("text_contrast", "settings panel clears its contrast floor", 0,
     [f(T, "text_contrast.py"), f(E07, "SET-837-settings-unscrolled.layout.json"),
      f(E07, "SET-837-settings-unscrolled.png"), "--scale", "1.5",
      "--within", "400,90.5,520,679"]),
    ("text_contrast", "plant: dim the highest-contrast label", 1,
     [f(T, "text_contrast.py"), f(E07, "SET-837-settings-unscrolled.layout.json"),
      f(E07, "SET-837-settings-unscrolled.png"), "--scale", "1.5",
      "--within", "400,90.5,520,679", "--plant"]),
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--only", help="run only cases whose detector contains this")
    ap.add_argument("--verbose", action="store_true",
                    help="print each case's output, not only its verdict")
    ap.add_argument("--plant", action="store_true",
                    help="invert one expectation, so this harness MUST fail")
    args = ap.parse_args()

    cases = [c for c in CASES if not args.only or args.only in c[0]]
    if not cases:
        print(f"no verdict: --only {args.only!r} selected no case", file=sys.stderr)
        return 2

    if args.plant:
        det, label, want, argv = cases[0]
        cases = [(det, label + " [EXPECTATION INVERTED BY --plant]",
                  1 if want == 0 else 0, argv)] + cases[1:]
        print("CONTROL: inverted the expected exit of the first case\n")

    # Prove the fixtures are present before reporting on any of them. A missing
    # fixture makes every detector "fail" for a reason that is about this
    # checkout, not about the product.
    missing = []
    for _, _, _, argv in cases:
        for a in argv[1:]:
            if a.startswith("-") or "=" in a:
                continue
            p = os.path.join(REPO, a)
            if a.endswith((".json", ".png")) and not os.path.exists(p):
                missing.append(a)
    if missing:
        print("no verdict: missing fixture(s): " + ", ".join(sorted(set(missing))),
              file=sys.stderr)
        return 2

    failures = []
    by_detector: dict[str, list[int]] = {}
    for det, label, want, argv in cases:
        proc = subprocess.run([sys.executable] + argv, cwd=REPO,
                              capture_output=True, text=True)
        got = proc.returncode
        ok = got == want
        by_detector.setdefault(det, []).append(1 if ok else 0)
        print(f"  {'ok  ' if ok else 'FAIL'}  {det:<32} {label}"
              f"  (want exit {want}, got {got})")
        if args.verbose or not ok:
            tail = (proc.stdout + proc.stderr).strip().splitlines()[-12:]
            for line in tail:
                print(f"        | {line}")
        if not ok:
            failures.append((det, label, want, got))

    dets = len(by_detector)
    print(f"\n{len(cases)} case(s) across {dets} detector(s); "
          f"{len(cases) - len(failures)} as expected, {len(failures)} not")
    if failures:
        print("\nA detector whose clean case fails is broken or the product "
              "regressed; a detector whose PLANTED case passes can no longer "
              "fail and its green runs prove nothing.", file=sys.stderr)
        return 1
    if args.plant:
        print("BROKEN: the inverted expectation was not reported -- this "
              "harness cannot fail", file=sys.stderr)
        return 4
    print("OK: every detector was shown both passing and failing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
