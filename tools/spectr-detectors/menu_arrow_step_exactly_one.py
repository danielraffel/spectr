#!/usr/bin/env python3
"""Assert every Arrow press in a dropdown lands where the popup contract says.

Why this exists alongside `menu_dropdown_affordances.py`: that detector asserts
a *destination* (ArrowDown x3 then Return lands on a named caption), which is a
step-count only in aggregate and is entangled with Return closing the menu. A
double-owner that steps two rows and a handler that steps one can both be made
to land on the same caption, so the destination assertion cannot see the defect
the user reported. This one reads the highlight index after *each* press.

The keyboard owner under test is Pulp's own popup default
(`__pulpPopupDefaultHandle__` in `core/view/js/web-compat-document.js`), which a
popup opts into by ARIA shape. Its state only exists after a `pointerdown`
reaches that handler and the following animation frame runs `activate()`. A
driver that opens the menu with a JS `click` alone never creates that state, so
the arrows land on a popup nobody owns and every affordance reads as missing
while the product is fine. This probe therefore delivers the pointerdown the way
the native layer does and waits a frame before pressing anything.

THE CONTRACT, WHICH IS NOT "EVERY PRESS MOVES ONE ROW". A popup opened with the
POINTER is owned immediately but its keyboard cursor is not PAINTED until the
user asks for one, because an app that paints its own selected row plus a
framework cursor on a different row reads as two selections. Where the first
arrow lands then depends on whether that hidden cursor had a home:

  * the author marked a selection (`aria-activedescendant`, `aria-selected`,
    `aria-checked`, a `checked` property, or `aria-current`) -- the cursor was
    seeded ON it, so the first arrow STEPS OFF it, the way a platform combo box
    steps from its current value;
  * nothing was marked -- the cursor defaulted to an edge with nothing to step
    away from, so the first arrow lands ON that edge (row 0 for ArrowDown, the
    last row for ArrowUp) rather than skipping the row the user was aiming at.

Every press after the first, and every press on a popup whose cursor is already
revealed, steps circularly: `(i + 1) % count` forward, `(i - 1 + count) % count`
back. A naive `delta == 1` check reports a correct wrap as a defect.

The seeding question is answered from the DOM here, never from the popup's own
`seededFromSelection` flag. Reading the owner's own bookkeeping to build the
expectation would make this the owner grading its own homework: any seeding bug
would agree with itself and pass.

Controls, because a silent harness reads exactly like a working dropdown:
  * the popup must resolve (`state` present) and expose a non-zero option count;
  * `data-pulp-popup-active` must be present on EVERY option throughout -- that
    attribute is how the owner marks a popup it holds, and presence is the
    ownership signal. Its VALUE is the painted cursor and is a separate reading:
    zero rows painted at a pointer open, exactly one from the first arrow on;
  * `--negative-control` installs a second keyboard owner -- the shape that was
    deliberately removed from this app -- and REQUIRES the assertion to fail.
    A pass there means the instrument cannot see a skip and proves nothing.

Exit 0 pass, 1 fail, 2 inconclusive.
"""

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys

APP = "build/Spectr.app/Contents/MacOS/Spectr"
MENU_ROOTS = ("pattern", "bands", "edit", "analyzer")
PRESSES = 6

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ROOT_MARKERS = (
    "CMakeLists.txt",
    "native-ui/materialized/materialized-document.runtime.json",
)

# A second keyboard owner: re-enters the popup default with a fresh, unprevented
# event so one physical press is applied twice. This is the failure mode the
# user reported as "skipping a row", reproduced on purpose.
SECOND_OWNER = """
  var reentry = false;
  document.addEventListener("keydown", function(e){
    if (reentry) return; reentry = true;
    globalThis.__pulpPopupDefaultHandle__({ type:"keydown", key:e.key, code:e.key,
      preventDefault:function(){ this.defaultPrevented = true; },
      stopPropagation:function(){}, stopImmediatePropagation:function(){} });
    reentry = false;
  });
  L("second_keyboard_owner=installed");
"""

# The OTHER wrong answer, and the one the paint readings exist for: an owner
# that paints its cursor the moment the popup opens. That is what this app's
# dropdowns used to do, and while they did, "Pulp owns this popup" and "a row
# is highlighted" were the same reading -- which is how three ownership guards
# in the shipping document came to be keyed on the highlight. Planted here so
# the CONTROL line's `painted == 0` is a gate somebody has watched fail rather
# than a sentence.
EAGER_CURSOR = """
  var _pulpOwner = globalThis.__pulpPopupDefaultHandle__;
  globalThis.__pulpPopupDefaultHandle__ = function(e){
    var r = _pulpOwner(e);
    if (e && e.type === "pointerdown") {
      var s = globalThis.__pulpPopupDefaultState__;
      if (s && s.options && s.options[s.activeIndex]) {
        s.activeVisible = true;
        s.options[s.activeIndex].setAttribute("data-pulp-popup-active", "true");
      }
    }
    return r;
  };
  L("eager_cursor=installed");
"""

CONTROLS = ("second-keyboard-owner", "eager-cursor")

PROBE = """
(function(){
  var L = function(s){ console.log("[arrowstep] " + s); };
  var trigger = document.querySelector('[data-spectr-menu-root="__ROOT__"] button');
  if (!trigger) { L("RESULT inconclusive reason=no_trigger"); return; }
  var idx = function(){
    var s = globalThis.__pulpPopupDefaultState__;
    return s ? s.activeIndex : -999;
  };
  // Ownership is the ATTRIBUTE; the painted cursor is its value.
  var owned = function(){
    return document.querySelectorAll('[data-pulp-popup-active]').length;
  };
  var painted = function(){
    return document.querySelectorAll('[data-pulp-popup-active="true"]').length;
  };
  // Derived from the markup, not from the popup's own seeding flag. These are
  // the signals an author uses to say "this row is the current value"; any one
  // of them means the cursor had a home to step off.
  var authorMarkedSelection = function(popup, options){
    var holders = [trigger, popup];
    for (var h = 0; h < holders.length; ++h) {
      var pointer = holders[h] && holders[h].getAttribute
        ? holders[h].getAttribute("aria-activedescendant") : null;
      if (pointer) {
        for (var p = 0; p < options.length; ++p)
          if (options[p].id === pointer) return true;
      }
    }
    for (var i = 0; i < options.length; ++i) {
      var o = options[i];
      if (!o.getAttribute) continue;
      if (o.getAttribute("aria-selected") === "true") return true;
      if (o.getAttribute("aria-checked") === "true") return true;
      if (o.checked === true) return true;
      var current = o.getAttribute("aria-current");
      if (current && current !== "false") return true;
    }
    return false;
  };
__SECOND_OWNER__
  var phase = 0, seen = [], count = 0, bad = 0, badHighlight = 0;
  var marked = false, revealed = false;
  var keys = [];
  for (var a = 0; a < __PRESSES__; a++) keys.push("ArrowDown");
  for (var b = 0; b < __PRESSES__; b++) keys.push("ArrowUp");

  var step = function(){
    phase++;
    if (phase === 1) {
      // The exact call the native pointer path makes. activate() runs in the
      // animation frame this handler schedules, so nothing is asserted yet.
      globalThis.__pulpPopupDefaultHandle__({ type:"pointerdown", target: trigger });
    } else if (phase === 2) {
      var s = globalThis.__pulpPopupDefaultState__;
      if (!s) { L("RESULT inconclusive reason=no_popup_state"); return; }
      count = s.options.length;
      if (!count) { L("RESULT inconclusive reason=zero_options"); return; }
      marked = authorMarkedSelection(s.popup, s.options);
      L("CONTROL options=" + count + " owned=" + owned() +
        " painted=" + painted() + " marked=" + marked +
        " activeIndex=" + idx());
      // Owned means every row carries the attribute. A pointer open paints no
      // cursor, so the painted count must be zero here -- the user has not
      // asked for one yet.
      if (owned() !== count) badHighlight++;
      if (painted() !== 0) badHighlight++;
      seen.push(idx());
    } else if (phase - 2 <= keys.length) {
      var key = keys[phase - 3];
      var before = idx();
      // The first arrow on an unrevealed, unmarked popup REVEALS the cursor on
      // its edge instead of moving it. Every other press steps circularly.
      var landOnSeed = !revealed && !marked;
      var want = landOnSeed ? (key === "ArrowUp" ? count - 1 : 0)
               : key === "ArrowDown" ? (before + 1) % count
                                     : (before - 1 + count) % count;
      var ev = { type:"keydown", key:key, code:key,
                 preventDefault:function(){ this.defaultPrevented = true; },
                 stopPropagation:function(){},
                 stopImmediatePropagation:function(){} };
      document.dispatchEvent(ev);
      revealed = true;
      var after = idx();
      seen.push(after);
      if (after !== want) bad++;
      // From the first arrow on, the cursor is revealed: exactly one row
      // painted, and the whole popup still owned.
      if (painted() !== 1 || owned() !== count) badHighlight++;
      L("PRESS " + key + " " + before + " -> " + after + " want=" + want +
        (landOnSeed ? " (reveal)" : "") +
        " owned=" + owned() + " painted=" + painted() +
        (after === want ? "" : " MISSTEP"));
    } else {
      L("TRACE " + JSON.stringify(seen));
      L("RESULT " + (bad === 0 && badHighlight === 0 ? "pass" : "fail") +
        " missteps=" + bad + " bad_highlight=" + badHighlight +
        " presses=" + keys.length + " options=" + count +
        " marked=" + marked);
      return;
    }
    requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
})();
"""


def build_probe(menu_root, negative_control):
    plants = {"second-keyboard-owner": SECOND_OWNER, "eager-cursor": EAGER_CURSOR}
    return (PROBE
            .replace("__ROOT__", menu_root)
            .replace("__PRESSES__", str(PRESSES))
            .replace("__SECOND_OWNER__", plants.get(negative_control, "")))


def drive(root, app, out_dir, menu_root, negative_control):
    out_dir.mkdir(parents=True, exist_ok=True)
    tag = menu_root + ("-negctl-" + negative_control if negative_control else "")
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1",
        # Bounded: a headless run must not hold an audio device open.
        "PULP_FRAMES": "150",
        "PULP_SCREENSHOT": str(out_dir / f"{tag}.png"),
        "SPECTR_CLICK": f'[data-spectr-menu-root="{menu_root}"] button',
        "SPECTR_EVAL": build_probe(menu_root, negative_control),
    })
    proc = subprocess.run([str(root / app)], env=env, cwd=str(root),
                          capture_output=True, text=True, timeout=300)
    log = proc.stdout + proc.stderr
    (out_dir / f"{tag}.log").write_text(log, encoding="utf-8")
    m = re.search(r"\[arrowstep\] RESULT (\w+)([^\n]*)", log)
    if not m:
        return "inconclusive", "the probe produced no RESULT line", log
    detail = m.group(2).strip()
    return m.group(1), detail, log


def self_test():
    failures = []
    default = pathlib.Path(_parser().get_default("root")).resolve()
    if default != REPO_ROOT:
        failures.append(f"--root defaults to {default}, not this checkout.")
    for marker in ROOT_MARKERS:
        if not (REPO_ROOT / marker).exists():
            failures.append(f"this checkout is missing {marker!r}.")
    if all((pathlib.Path("/") / m).exists() for m in ROOT_MARKERS):
        failures.append("the marker set accepts '/', so it gates nothing.")
    # The probe must actually differ when the negative control is requested,
    # or `--negative-control` would re-run the same measurement and its
    # required failure could never be produced.
    for control in CONTROLS:
        if build_probe("pattern", control) == build_probe("pattern", None):
            failures.append(f"--negative-control {control} does not change "
                            "the probe.")
    if build_probe("pattern", CONTROLS[0]) == build_probe("pattern", CONTROLS[1]):
        failures.append("the two negative controls plant the same thing.")
    if "__ROOT__" in build_probe("pattern", False):
        failures.append("the probe template left a placeholder unsubstituted.")
    # The reveal-vs-move rule has to be in the probe at all. Asserting the
    # tokens is weak on its own, so the ownership and paint readings are
    # checked to be DIFFERENT selectors: a probe that used the value-matching
    # selector for both would report an owned-but-unrevealed popup as
    # unowned, which is the exact confusion this file exists to keep straight.
    probe = build_probe("pattern", False)
    for token in ("landOnSeed", "authorMarkedSelection",
                  "'[data-pulp-popup-active]'",
                  "'[data-pulp-popup-active=\"true\"]'"):
        if token not in probe:
            failures.append(f"the probe no longer reads {token!r}.")
    if "seededFromSelection" in probe:
        failures.append("the probe reads the owner's own seeding flag; the "
                        "expectation must be derived from the markup.")
    for line in failures:
        print("FAIL: " + line)
    if failures:
        return 1
    print(f"PASS: --root defaults to this checkout ({REPO_ROOT}).")
    print("PASS: the marker set rejects a foreign root.")
    print(f"PASS: each of --negative-control {CONTROLS} produces a "
          "different probe.")
    print("PASS: the probe derives the reveal-vs-move expectation from the "
          "markup and reads ownership and paint through different selectors.")
    return 0


def _parser():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(REPO_ROOT),
                    help="Spectr checkout to measure (default: this one).")
    ap.add_argument("--app", default=APP,
                    help=f"Spectr binary, relative to --root (default: {APP}).")
    ap.add_argument("--out", default="/tmp/spectr-arrowstep")
    ap.add_argument("--menu-root", action="append", choices=MENU_ROOTS,
                    help="limit to these dropdowns (default: all).")
    ap.add_argument("--negative-control", nargs="?", const=CONTROLS[0],
                    choices=CONTROLS, default=None,
                    help="plant a defect; the run must then FAIL. "
                         f"{CONTROLS[0]} (the default) applies one press "
                         f"twice; {CONTROLS[1]} paints the keyboard cursor at "
                         "open, before the user has asked for one.")
    ap.add_argument("--self-test", action="store_true")
    return ap


def main():
    args = _parser().parse_args()
    if args.self_test:
        return self_test()

    root = pathlib.Path(args.root).resolve()
    missing = [m for m in ROOT_MARKERS if not (root / m).exists()]
    if missing:
        print(f"INCONCLUSIVE: {root} is not a Spectr checkout (missing "
              f"{missing}); refusing to report a verdict about it.")
        return 2
    binary = root / args.app
    if not binary.exists():
        print(f"INCONCLUSIVE: no Spectr binary at {binary}; build one or pass "
              "--app. A detector that cannot launch the app proves nothing.")
        return 2

    out = pathlib.Path(args.out)
    roots = tuple(args.menu_root) if args.menu_root else MENU_ROOTS
    print(f"measuring {root} via {args.app}")

    failures, inconclusive, marks = [], [], []
    for menu_root in roots:
        verdict, detail, _ = drive(root, args.app, out, menu_root,
                                   args.negative_control)
        line = f"{menu_root}: {verdict} {detail}".rstrip()
        marks.append("marked=true" in detail)
        if args.negative_control:
            # Inverted: the injected second owner MUST be caught.
            if verdict == "fail":
                print(f"PASS (negative control {args.negative_control}): the "
                      f"planted defect was caught on {line}")
            elif verdict == "inconclusive":
                inconclusive.append(f"negative control on {line}")
            else:
                failures.append(
                    f"negative control {args.negative_control} on {menu_root} "
                    "PASSED; the assertion cannot see the planted defect, so "
                    "a green run of this detector proves nothing.")
            continue
        if verdict == "pass":
            print(f"PASS: every Arrow press landed where the popup contract "
                  f"says ({line})")
        elif verdict == "inconclusive":
            inconclusive.append(line)
        else:
            failures.append(line)

    # Control on the COVERAGE of the run, not on any one dropdown. The two
    # branches of the first-arrow rule are exercised only if the measured set
    # contains a dropdown that marks its current row and one that does not, so
    # a green run over four dropdowns that all landed on the same branch has
    # left the other branch untested and must say so.
    if not args.negative_control and len(roots) > 1 and len(set(marks)) < 2:
        inconclusive.append(
            "every measured dropdown took the same branch of the first-arrow "
            f"rule (marked={marks[0]}); the other branch went untested, so "
            "this run does not cover the reveal-vs-step distinction.")

    for line in inconclusive:
        print("INCONCLUSIVE: " + line)
    for line in failures:
        print("FAIL: " + line)
    if inconclusive:
        return 2
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
