#!/usr/bin/env python3
"""A running LFO does not move the band being drawn, and resumes after.

Drives the real standalone headlessly. A band is dragged with the button
held (`drag:...!hold`), modulation frames arrive through the same
native->JS `modulation_frame` message the audio owner sends (`modframe:`),
and the painted band height is read back from the editor (`rgprobe:<band>`).
No audio device is opened.

  hold on (the default)
    * mid-gesture, the painted band stays where it was drawn, although
      every frame asks for -12 dB;
    * after the button is released, the next frame is painted again.
  hold off (the negative control: Settings > Hold while editing)
    * mid-gesture, the frame's -12 dB is painted -- so the frames are known
      to reach the editor, and the held reading above is the setting's
      doing rather than frames that never arrived.

Exit 0 pass, 1 fail, 2 inconclusive.
"""
import argparse
import json
import os
import subprocess
import sys

BAND = 8  # the band under x=378 at the default 32 bands
SCENARIO = ";".join([
    "close=key:escape", "close_settled=wait",
    "d=drag:378,400>378,330@12!hold", "d_settled=wait",
    "f1=modframe:-12", "w1=wait", "f2=modframe:-12", "w2=wait",
    "p_held=rgprobe:%d" % BAND,
    "rel=release", "rel_settled=wait",
    "f3=modframe:-12", "w3=wait",
    "p_after=rgprobe:%d" % BAND,
    "stop=modstop", "stop_settled=wait",
])
FRAME = -12.0 / 24.0  # the injected -12 dB, as the editor's normalised gain


def run(app, label, hold, out_dir):
    out = os.path.join(out_dir, label)
    os.makedirs(out, exist_ok=True)
    json_path = os.path.join(out, "s.json")
    if os.path.exists(json_path):
        os.remove(json_path)
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1", "PULP_TEST_MODE": "1",
        "SPECTR_OPEN_SETTINGS": "1",
        "SPECTR_MENU_SCENARIO": SCENARIO,
        "SPECTR_MENU_SCENARIO_OUT": json_path,
        "SPECTR_MENU_SCENARIO_DELAY": "10",
        "PULP_SCREENSHOT": os.path.join(out, "s.png"),
        "PULP_FRAMES": "300",
    })
    env.pop("SPECTR_CLICK", None)
    if not hold:
        env["SPECTR_CLICK"] = '[data-spectr-hold-edit="on"] button'
    with open(os.path.join(out, "log"), "w") as log:
        subprocess.run([app], env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=300)
    if not os.path.exists(json_path):
        return None
    steps = {s["step"]: s["result"] for s in json.load(open(json_path))["steps"]}
    if steps.get("d") != "held" or steps.get("rel") != "released":
        return None
    readings = {}
    for key in ("p_held", "p_after"):
        value = steps.get(key, "")
        if not value.startswith("rg="):
            return None
        readings[key] = float(value[3:].split("|")[0])
    return readings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--out-dir", default="/tmp/spectr-modulation-hold")
    args = ap.parse_args()

    on = run(args.app, "hold-on", True, args.out_dir)
    off = run(args.app, "hold-off", False, args.out_dir)
    if on is None or off is None:
        print("INCONCLUSIVE: a run produced no readable gesture (on=%s off=%s)" % (on, off))
        return 2

    near = lambda a, b, tol=0.02: abs(a - b) <= tol
    checks = [
        ("held: the drawn band ignores the running LFO", on["p_held"] > 0.05),
        ("released: modulation is painted again", near(on["p_after"], FRAME)),
        ("control, hold off: the same frames move the band mid-gesture",
         near(off["p_held"], FRAME)),
    ]
    for name, ok in checks:
        print("%-4s %s" % ("PASS" if ok else "FAIL", name))
    print("readings: " + json.dumps({"on": on, "off": off}))
    failures = [n for n, ok in checks if not ok]
    print("modulation hold: %d checks, %d failure(s)" % (len(checks), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
