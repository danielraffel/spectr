#!/usr/bin/env python3
"""Read the root's viewport metrics and the root's REAL box in the same frame.

WHY THIS EXISTS

    The band context menu clamps itself against `root.clientHeight`:

        const root = document.getElementById("root");
        const vh = root ? root.clientHeight : window.innerHeight;
        const top = Math.max(8, Math.min(y, vh - H - 8));

    Solving that clamp against six recorded (height, top) pairs from the
    shipping standalone gives vh = 575 -- while the menu's own rows lay out in
    the 1320x860 design space and the tallest panel measures 811. If an
    860-tall root really does report 575, the mismatch is a core Pulp bridge
    defect and belongs fixed there, inherited here, not papered over app-side.

    But 575 is DERIVED, not observed. It is six consistent data points fitted
    to an assumed formula, which is good evidence and not a reading. This
    turns it into a reading.

WHY IT READS SEVERAL METRICS, NOT ONE

    The element-metrics path has a history of answering with a number the
    caller did not assume -- `offsetWidth`/local-coordinate style values where
    a client box was expected -- and of doing it silently. A probe that read
    `clientHeight` alone could not tell "the root is 575 tall" from
    "clientHeight means something else here". So it reads clientHeight,
    offsetHeight, getBoundingClientRect and window.innerHeight together, and
    prints the native root's own bounds beside them from the same run.

    Disagreement between them IS the finding. Agreement at 860 would mean the
    derivation was wrong and the menu clamp is fine.

Usage:
    probe_root_viewport.py --binary path/to/Spectr.app/Contents/MacOS/Spectr
                           [--out DIR]

Runs headless and asserts the run opened no audio device. Exit: 0 a reading
was obtained, 3 it could not be.
"""

import argparse
import json
import os
import re
import subprocess
import sys

EVAL = r'''
(function () {
  var r = document.getElementById("root");
  var rect = null;
  try { rect = r && r.getBoundingClientRect ? r.getBoundingClientRect() : null; }
  catch (e) { rect = "threw:" + e; }
  var out = {
    found: !!r,
    clientHeight: r ? r.clientHeight : null,
    clientWidth: r ? r.clientWidth : null,
    offsetHeight: r ? r.offsetHeight : null,
    offsetWidth: r ? r.offsetWidth : null,
    innerHeight: (typeof window !== "undefined") ? window.innerHeight : null,
    innerWidth: (typeof window !== "undefined") ? window.innerWidth : null,
    rect: rect && typeof rect === "object"
      ? { x: rect.x, y: rect.y, width: rect.width, height: rect.height }
      : rect
  };
  console.log("[vh-probe] " + JSON.stringify(out));
})();
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--out", default="/tmp/spectr-vh-probe")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    layout = os.path.join(args.out, "root.layout.json")
    log_path = os.path.join(args.out, "probe.log")
    for stale in (layout, log_path):
        if os.path.exists(stale):
            os.remove(stale)

    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1",
        "PULP_TEST_MODE": "1",
        "PULP_FRAMES": "240",
        "PULP_SCREENSHOT": os.path.join(args.out, "probe.png"),
        "SPECTR_EVAL": EVAL,
        # The native side's own answer for the same root, same run.
        "SPECTR_LAYOUT_DUMP": layout,
    })
    if not os.path.exists(args.binary):
        print("UNPROVEN: no binary at %s; build the standalone first."
              % args.binary, file=sys.stderr)
        return 3
    with open(log_path, "w") as log:
        proc = subprocess.run([args.binary], env=env, stdout=log,
                              stderr=subprocess.STDOUT, timeout=300)

    text = open(log_path).read()
    if "no audio device created, opened, or started" not in text:
        print("UNPROVEN: the run did not report that it opened no audio "
              "device, so it cannot be said to have been silent.",
              file=sys.stderr)
        return 3

    m = re.search(r"\[vh-probe\] (\{.*\})", text)
    if not m:
        print("UNPROVEN: the probe produced no reading (rc=%d); see %s"
              % (proc.returncode, log_path), file=sys.stderr)
        return 3
    js = json.loads(m.group(1))

    native_h = native_w = None
    if os.path.exists(layout):
        try:
            dump = json.load(open(layout))
            node = dump if isinstance(dump, dict) else {}
            for key in ("root", "bounds", "rect"):
                if isinstance(node.get(key), dict):
                    node = node[key]
                    break
            native_w = node.get("width")
            native_h = node.get("height")
        except (ValueError, AttributeError):
            pass

    print("== the JS side's view of the root ==")
    for k in ("found", "clientWidth", "clientHeight", "offsetWidth",
              "offsetHeight", "innerWidth", "innerHeight", "rect"):
        print("  %-14s %s" % (k, js.get(k)))
    print("\n== the native side's own root bounds, same run ==")
    print("  %-14s %s x %s   (%s)"
          % ("root bounds", native_w, native_h,
             layout if os.path.exists(layout) else "no layout dump"))

    ch = js.get("clientHeight")
    print("\n== verdict ==")
    if ch is None:
        print("  no clientHeight: the root was not found from JS")
        return 3
    if native_h is None:
        print("  clientHeight = %s; the native root height could not be read "
              "from the layout dump, so this is only half the comparison."
              % ch)
        return 0
    if abs(float(ch) - float(native_h)) <= 1.0:
        print("  clientHeight (%s) AGREES with the native root height (%s). "
              "The derived 575 was wrong, and the menu's clamp reads a "
              "correct viewport." % (ch, native_h))
    else:
        print("  clientHeight (%s) DISAGREES with the native root height "
              "(%s). An element reporting a viewport it does not have is a "
              "core Pulp bridge defect: fix it there and let Spectr inherit."
              % (ch, native_h))
    return 0


if __name__ == "__main__":
    sys.exit(main())
