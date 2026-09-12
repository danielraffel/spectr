#!/usr/bin/env python3
"""Assert every Arrow press in a dropdown moves exactly one row.

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

Controls, because a silent harness reads exactly like a working dropdown:
  * the popup must resolve (`state` present) and expose a non-zero option count;
  * exactly one element must carry `data-pulp-popup-active="true"` throughout;
  * `--negative-control` installs a second keyboard owner -- the shape that was
    deliberately removed from this app -- and REQUIRES the assertion to fail.
    A pass there means the instrument cannot see a skip and proves nothing.

Stepping is circular: `(index + 1) % count` forward, `(index - 1 + count) %
count` back. A naive `delta == 1` check reports a correct wrap as a defect.

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

PROBE = """
(function(){
  var L = function(s){ console.log("[arrowstep] " + s); };
  var trigger = document.querySelector('[data-spectr-menu-root="__ROOT__"] button');
  if (!trigger) { L("RESULT inconclusive reason=no_trigger"); return; }
  var idx = function(){
    var s = globalThis.__pulpPopupDefaultState__;
    return s ? s.activeIndex : -999;
  };
  var highlighted = function(){
    return document.querySelectorAll('[data-pulp-popup-active="true"]').length;
  };
__SECOND_OWNER__
  var phase = 0, seen = [], count = 0, bad = 0, badHighlight = 0;
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
      L("CONTROL options=" + count + " highlighted=" + highlighted() +
        " activeIndex=" + idx());
      if (highlighted() !== 1) badHighlight++;
      seen.push(idx());
    } else if (phase - 2 <= keys.length) {
      var key = keys[phase - 3];
      var before = idx();
      var want = key === "ArrowDown" ? (before + 1) % count
                                     : (before - 1 + count) % count;
      var ev = { type:"keydown", key:key, code:key,
                 preventDefault:function(){ this.defaultPrevented = true; },
                 stopPropagation:function(){},
                 stopImmediatePropagation:function(){} };
      document.dispatchEvent(ev);
      var after = idx();
      seen.push(after);
      if (after !== want) bad++;
      if (highlighted() !== 1) badHighlight++;
      L("PRESS " + key + " " + before + " -> " + after + " want=" + want +
        " highlighted=" + highlighted() + (after === want ? "" : " MISSTEP"));
    } else {
      L("TRACE " + JSON.stringify(seen));
      L("RESULT " + (bad === 0 && badHighlight === 0 ? "pass" : "fail") +
        " missteps=" + bad + " bad_highlight=" + badHighlight +
        " presses=" + keys.length + " options=" + count);
      return;
    }
    requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
})();
"""


def build_probe(menu_root, negative_control):
    return (PROBE
            .replace("__ROOT__", menu_root)
            .replace("__PRESSES__", str(PRESSES))
            .replace("__SECOND_OWNER__", SECOND_OWNER if negative_control else ""))


def drive(root, app, out_dir, menu_root, negative_control):
    out_dir.mkdir(parents=True, exist_ok=True)
    tag = menu_root + ("-negctl" if negative_control else "")
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
    if build_probe("pattern", True) == build_probe("pattern", False):
        failures.append("--negative-control does not change the probe.")
    if "__ROOT__" in build_probe("pattern", False):
        failures.append("the probe template left a placeholder unsubstituted.")
    for line in failures:
        print("FAIL: " + line)
    if failures:
        return 1
    print(f"PASS: --root defaults to this checkout ({REPO_ROOT}).")
    print("PASS: the marker set rejects a foreign root.")
    print("PASS: --negative-control produces a different probe.")
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
    ap.add_argument("--negative-control", action="store_true",
                    help="install a second keyboard owner; the run must FAIL.")
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

    failures, inconclusive = [], []
    for menu_root in roots:
        verdict, detail, _ = drive(root, args.app, out, menu_root,
                                   args.negative_control)
        line = f"{menu_root}: {verdict} {detail}".rstrip()
        if args.negative_control:
            # Inverted: the injected second owner MUST be caught.
            if verdict == "fail":
                print("PASS (negative control): the second keyboard owner was "
                      f"caught on {line}")
            elif verdict == "inconclusive":
                inconclusive.append(f"negative control on {line}")
            else:
                failures.append(
                    f"negative control on {menu_root} PASSED; the assertion "
                    "cannot see a double-stepping keyboard owner, so a green "
                    "run of this detector proves nothing.")
            continue
        if verdict == "pass":
            print(f"PASS: every Arrow press moved exactly one row ({line})")
        elif verdict == "inconclusive":
            inconclusive.append(line)
        else:
            failures.append(line)

    for line in inconclusive:
        print("INCONCLUSIVE: " + line)
    for line in failures:
        print("FAIL: " + line)
    if inconclusive:
        return 2
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
