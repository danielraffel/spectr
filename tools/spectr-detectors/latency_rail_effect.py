#!/usr/bin/env python3
"""The rail's Latency chip and the T key must move the PROCESSOR, not the chip.

WHY THIS IS NOT A TEXT ASSERTION. The chip writes the new mode into its own
store optimistically, before the processor has answered, so that a press does
not visibly lag the renderer rebuild (which spins up to 200ms proving the audio
thread has left the old renderer). That is good for the user and fatal for a
naive gate: after a press the chip reads "TRACKING . 1.3 ms" whether or not the
message ever reached C++. Ask the question that matters -- what would this read
if the control were inert? -- and the answer is "exactly the same", so the chip
text cannot be the evidence.

WHAT IS EVIDENCE. `render_mode_set` answers with the REHYDRATED payload, built
by make_editor_state_payload from the live plugin after Spectr::set_render_mode
has rebuilt the renderer and raised the host's latency-changed flag. The toggle
logs that answer. `latency_samples=64` is the zero-latency renderer's own render
block read back out of C++; it cannot be produced by a JS optimistic write, by a
chip that renders the right string, or by a key that is bound to nothing.

So this drives the real standalone and reads the processor's answer.

THE CONTROLS. Two halves, both required:
  * a press/key that SHOULD work must produce the confirmation, and
  * a press on another rail control, and a key that is bound to nothing, must
    NOT produce it.
Without the second half a gate that fired on every input would pass the first
half alone -- and that is precisely the shape of the bug this guards (an
ancestor handler swallowing the press, a key table matching too eagerly).

Exit codes
----------
0  every trial agreed with its expectation
1  a trial disagreed
2  usage / IO error
3  the instrument proved nothing (no launch, no hydration) -- reported as
   inconclusive rather than as a pass
"""

import argparse
import json
import os
import re
import subprocess
import sys

# The processor's own answer. `samples` is read back out of C++, so it is the
# field that cannot be forged by the optimistic JS write.
CONFIRM = re.compile(r"\[Spectr\] latency mode confirmed (\S+) latency_samples=(\d+)")
# The chip renders `null` until the hydration payload arrives, so its presence
# in the layout dump is the positive control: it proves the editor mounted AND
# the payload reached it. Without this a broken launch would read as "the key
# did nothing", which is the same shape as the defect being hunted.
CHIP_TEXT = re.compile(r"^(MIXING|TRACKING) \u00b7 [0-9.]+ ms$")

# The product's two modes and the sample counts each renderer declares.
# linear_phase is design_grid_size + analysis_hop (8192 + 2048); zero_latency
# is the fixed 64-sample render block. Stated here so a confirmation carrying
# the right token but a wrong number is still a failure.
SAMPLES = {"linear_phase": 10240, "zero_latency": 64}


def launch(app, out_dir, name, env_extra, timeout=300):
    """One headless run. Returns (returncode, log text)."""
    log = os.path.join(out_dir, name + ".log")
    # Remove the previous log BEFORE launching: a launch that produces nothing
    # would otherwise be adjudicated against the last run's output, which is a
    # confident verdict about code that is not under test.
    if os.path.exists(log):
        os.remove(log)
    env = dict(os.environ)
    env.update({"PULP_HEADLESS": "1", "PULP_FRAMES": "150",
                "PULP_SCREENSHOT": os.path.join(out_dir, name + ".png")})
    # Never inherit an input from the caller's environment: it would make two
    # trials identical and the comparison vacuous.
    for key in ("SPECTR_KEY_JS", "SPECTR_KEY", "SPECTR_CLICK", "SPECTR_EVAL", "SPECTR_MENU_SCENARIO", "SPECTR_MENU_SCENARIO_OUT"):
        env.pop(key, None)
    dump = os.path.join(out_dir, name + ".layout.json")
    if os.path.exists(dump):
        os.remove(dump)
    env["SPECTR_LAYOUT_DUMP"] = dump
    env.update(env_extra)
    try:
        with open(log, "wb") as handle:
            proc = subprocess.run([app], env=env, stdout=handle,
                                  stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "TIMEOUT", None
    except OSError as err:
        return None, "LAUNCH FAILED: %s" % err, None
    with open(log, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    return proc.returncode, text, (dump if os.path.exists(dump) else None)


def chip_box(dump):
    """The chip's rendered text and the BUTTON box that holds it.

    The button box is what must not move, not the text box: the two modes
    render different strings, so the text box is SUPPOSED to differ and
    comparing it would fail on a correct control. The button is found by
    containment rather than by a hardcoded rect, so moving the chip does not
    silently turn this into a check of nothing.
    """
    with open(dump, encoding="utf-8") as handle:
        document = json.load(handle)
    nodes = document.get("nodes") or []
    text_rect = text = None
    for node in nodes:
        for box in (node.get("measured_text_boxes") or []):
            if CHIP_TEXT.match(box.get("text", "")):
                text, text_rect = box.get("text"), node.get("rect")
                break
        if text is not None:
            break
    if text is None:
        return None
    cx = text_rect["x"] + text_rect["w"] / 2.0
    cy = text_rect["y"] + text_rect["h"] / 2.0
    best = None
    for node in nodes:
        r = node.get("rect") or {}
        if not r or r.get("h", 0) <= text_rect["h"]:
            continue
        if r["x"] <= cx <= r["x"] + r["w"] and r["y"] <= cy <= r["y"] + r["h"]:
            area = r["w"] * r["h"]
            if best is None or area < best[0]:
                best = (area, r)
    return text, (best[1] if best else text_rect)


def trial(app, out_dir, name, env_extra, expect):
    """Run one input and report what the processor confirmed."""
    code, text, dump = launch(app, out_dir, name, env_extra)
    if code is None:
        return dict(name=name, ok=False, inconclusive=True, detail=text, box=None)
    if code != 0:
        return dict(name=name, ok=False, inconclusive=True, box=None,
                    detail="standalone exited %d" % code)
    if dump is None:
        return dict(name=name, ok=False, inconclusive=True, box=None,
                    detail="no layout dump produced")
    rendered = chip_box(dump)
    if rendered is None:
        return dict(name=name, ok=False, inconclusive=True, box=None,
                    detail="the chip did not render, so this run says nothing "
                           "about the control")
    confirmations = list(CONFIRM.finditer(text))
    found = confirmations[-1] if confirmations else None
    got = None
    if found:
        got = (found.group(1), int(found.group(2)))
    if expect is None:
        return dict(name=name, ok=(got is None), inconclusive=False,
                    box=rendered,
                    detail="expected no mode change, got %r" % (got,))
    if got is None:
        return dict(name=name, ok=False, inconclusive=False,
                    box=rendered,
                    detail="expected the processor to confirm %s, but it "
                           "confirmed nothing -- the input reached no handler, "
                           "or the handler never told the processor" % expect)
    if got[0] != expect:
        return dict(name=name, ok=False, inconclusive=False,
                    box=rendered,
                    detail="expected %s, processor confirmed %s" % (expect, got[0]))
    if got[1] != SAMPLES[expect]:
        return dict(name=name, ok=False, inconclusive=False,
                    box=rendered,
                    detail="%s must report %d samples, processor said %d"
                           % (expect, SAMPLES[expect], got[1]))
    return dict(name=name, ok=True, inconclusive=False, box=rendered,
                detail="processor confirmed %s at %d samples" % got)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True, help="the standalone binary")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--expect-fail", action="store_true",
                    help="invert the verdict, for a negative control whose "
                         "plant is applied to the document by the caller")
    args = ap.parse_args()

    if not os.path.exists(args.app):
        print("FAIL usage: no such app: %s" % args.app, file=sys.stderr)
        return 2
    os.makedirs(args.out_dir, exist_ok=True)

    # A fresh instance starts in linear_phase, so every trial below toggles
    # TO zero_latency. That direction is deliberate: 64 samples is a number
    # only the zero-latency renderer produces, while 10240 is also what an
    # un-toggled instance would report.
    trials = [
        # The two real entry points.
        trial(args.app, args.out_dir, "key-t",
              {"SPECTR_KEY": "t"}, "zero_latency"),
        trial(args.app, args.out_dir, "press-chip",
              {"SPECTR_MENU_SCENARIO": "toggle=press:1137,832",
               "SPECTR_MENU_SCENARIO_OUT": os.path.join(args.out_dir, "press-chip.json")}, "zero_latency"),
        # CONTROL: an unbound key must change nothing. Without this, a handler
        # that fired on every keystroke would pass the trial above.
        trial(args.app, args.out_dir, "control-unbound-key",
              {"SPECTR_KEY": "y"}, None),
        # CONTROL: pressing a DIFFERENT rail control must change nothing.
        # Without this, an ancestor handler that toggled on any rail press
        # would pass the press trial above.
        trial(args.app, args.out_dir, "control-other-press",
              {"SPECTR_MENU_SCENARIO": "gap=press:1000,832",
               "SPECTR_MENU_SCENARIO_OUT": os.path.join(args.out_dir, "control-press.json")}, None),
        # CONTROL: no input at all must change nothing, which is what proves
        # the confirmation is caused by the input rather than emitted at mount.
        trial(args.app, args.out_dir, "control-no-input", {}, None),
        trial(args.app, args.out_dir, "roundtrip",
              {"SPECTR_MENU_SCENARIO": "track=press:1137,832;settle=wait;mix=press:1137,832;settle2=wait",
               "SPECTR_MENU_SCENARIO_OUT": os.path.join(args.out_dir, "roundtrip.json")},
              "linear_phase"),
    ]

    inconclusive = [t for t in trials if t["inconclusive"]]
    for t in trials:
        mark = "ok  " if t["ok"] else ("?   " if t["inconclusive"] else "FAIL")
        print("%s %-22s %s" % (mark, t["name"], t["detail"]))

    if inconclusive:
        print("INCONCLUSIVE: %d trial(s) did not produce a usable run; this "
              "detector reports nothing rather than a verdict"
              % len(inconclusive), file=sys.stderr)
        return 3

    # The chip carries a FIXED width so a toggle cannot resize it. Assert the
    # rendered BOX, not the declared constant: a declaration reads the same
    # whether or not the renderer honoured it, which is the exact way a reserve
    # has silently failed in this panel before. The two modes render different
    # strings, so if the box tracked its content this would differ.
    boxes = {}
    for t in trials:
        if t.get("box"):
            boxes.setdefault(json.dumps(t["box"][1], sort_keys=True), []).append(
                (t["name"], t["box"][0]))
    if len(boxes) > 1:
        print("FAIL chip-width-invariance: the chip box moved between modes; a "
              "control that resizes when you press it pushes nothing here (it "
              "is absolutely positioned) but reads as a glitch:")
        for box, who in boxes.items():
            print("       %s  <- %s" % (box, who))
        trials.append(dict(name="chip-width-invariance", ok=False,
                           inconclusive=False, box=None, detail="box varied"))
    else:
        only = list(boxes)[0] if boxes else "(none)"
        texts = sorted({t["box"][0] for t in trials if t.get("box")})
        print("ok   chip-width-invariance  one box %s across %d rendered "
              "strings %s" % (only, len(texts), texts))
        # A single box across ONE string proves nothing -- it would hold if
        # every trial rendered the same mode. Require both modes to have been
        # seen, or say the check was vacuous.
        if len(texts) < 2:
            print("       (vacuous: only one mode was rendered across all "
                  "trials, so invariance was not exercised)")

    failed = [t for t in trials if not t["ok"]]
    if args.expect_fail:
        if failed:
            print("negative control OK: %d trial(s) failed as intended"
                  % len(failed))
            return 0
        print("NEGATIVE CONTROL DID NOT FAIL: every trial passed with the "
              "plant applied, so this gate cannot see the defect it claims to",
              file=sys.stderr)
        return 1
    if failed:
        return 1
    print("all %d trials agreed" % len(trials))
    return 0


if __name__ == "__main__":
    sys.exit(main())
