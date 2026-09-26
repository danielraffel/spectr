#!/usr/bin/env python3
"""Each modulation look moves the painted bands the way it claims to.

Drives the real standalone: the look is chosen through its Settings chip
(`[data-spectr-setting-option=<look>]`), modulation frames are injected through
the same native->JS `modulation_frame` message the audio owner sends
(`modframe:` scenario steps), and the painted band heights are read back from
the editor (`rgprobe`). No audio device is opened.

Per look, the one property that defines it:

  classic  jumps EXACTLY to the frame's level, and snaps back on stop
  glide    eases in (strictly between start and target after one step),
           settles on the target, and eases back rather than snapping
  calm     settles at HALF the modulated excursion
  contour  keeps the bars at their authored level while modulation runs

Classic and Glide are required to DIFFER on the same input, so a picker that
never reached the editor (both would read as classic) fails rather than
passing twice.

Exit 0 pass, 1 fail, 2 inconclusive.
"""
import argparse
import json
import os
import subprocess
import sys

SCENARIO = ";".join([
    "close=key:escape", "close_settled=wait",
    "f1=modframe:12", "p1=rgprobe", "w1=wait", "p2=rgprobe",
    "f2=modframe:12", "w2=wait", "w3=wait", "p3=rgprobe",
    "stop=modstop", "p4=rgprobe", "w4=wait", "w5=wait", "p5=rgprobe",
])
TARGET = 12.0 / 24.0  # the injected 12 dB, as the editor's normalised gain


def run(app, look, out_dir):
    out = os.path.join(out_dir, look)
    os.makedirs(out, exist_ok=True)
    json_path = os.path.join(out, "s.json")
    if os.path.exists(json_path):
        os.remove(json_path)
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1", "PULP_TEST_MODE": "1",
        "SPECTR_OPEN_SETTINGS": "1",
        "SPECTR_CLICK": '[data-spectr-setting-option="%s"]' % look,
        "SPECTR_MENU_SCENARIO": SCENARIO,
        "SPECTR_MENU_SCENARIO_OUT": json_path,
        "SPECTR_MENU_SCENARIO_DELAY": "10",
        "PULP_SCREENSHOT": os.path.join(out, "s.png"),
        "PULP_FRAMES": "320",
    })
    with open(os.path.join(out, "log"), "w") as log:
        subprocess.run([app], env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=300)
    if not os.path.exists(json_path):
        return None
    steps = {s["step"]: s["result"] for s in json.load(open(json_path))["steps"]}
    readings = {}
    for key in ("p1", "p2", "p3", "p4", "p5"):
        value = steps.get(key, "")
        if not value.startswith("rg="):
            return None
        readings[key] = float(value[3:].split("|")[0])
    return readings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--out-dir", default="/tmp/spectr-modulation-looks")
    args = ap.parse_args()

    results = {look: run(args.app, look, args.out_dir)
               for look in ("classic", "glide", "calm", "contour")}
    dead = [look for look, r in results.items() if r is None]
    if dead:
        print("INCONCLUSIVE: no readable band heights for %s" % dead)
        return 2

    near = lambda a, b, tol=0.01: abs(a - b) <= tol
    c, g, m, t = (results[k] for k in ("classic", "glide", "calm", "contour"))
    checks = [
        ("classic jumps to the frame", near(c["p1"], TARGET, 1e-3)),
        ("classic snaps back on stop", near(c["p4"], 0.0, 1e-3)),
        ("glide eases in", 0.0 < g["p1"] < TARGET - 0.005),
        ("glide settles on the target", near(g["p3"], TARGET)),
        ("glide eases back instead of snapping", g["p4"] > 1e-3 and near(g["p5"], 0.0)),
        ("calm settles at half the excursion", near(m["p3"], TARGET / 2)),
        ("contour holds the bars at their authored level", near(t["p3"], 0.0, 1e-3)),
        ("classic and glide differ (the picker reached the editor)",
         abs(c["p1"] - g["p1"]) > 0.005),
    ]
    for name, ok in checks:
        print("%-4s %s" % ("PASS" if ok else "FAIL", name))
    print("readings: " + json.dumps(results))
    failures = [n for n, ok in checks if not ok]
    print("modulation looks: %d checks, %d failure(s)" % (len(checks), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
