#!/usr/bin/env python3
"""A group gain drag moves every selected band's LEVEL and no band's MUTE.

WHAT THE USER REPORTED

    "if i select all, then shift+drag mute some, then raise all dragging up
    (since all still selected) the muted ones unmute and raise back to where
    they were."

WHY THIS IS A STATE ASSERTION AND NOT A PICTURE

    A band that has been silently unmuted at the level the drag moved it to
    draws EXACTLY the same bar as a band that is still muted underneath and
    whose stashed level moved with the group. Every screenshot, every pixel
    diff and every layout assertion passes on both. The difference is audible
    and nowhere else, so the only instrument that can see it is the processing
    field itself -- `muted[]` and `gain_db[]`, which the gesture probe reads
    out of `processing_state_snapshot()` after every delivered sample.

    Concretely, the reading this exists to reject:

        before the drag   muted [6, 7, 8]   gain_db 6..12 = +7.0
        after  the drag   muted []          gain_db 6..12 = +15.0   <- defect
        after  the drag   muted [6, 7, 8]   gain_db 6..12 = +15.0   <- correct

    The gain column is IDENTICAL in both. Only `muted[]` separates them.

HOW THE GESTURES ARE DRIVEN

    All four by real press, through `SPECTR_GESTURES` -> the same
    `deliver_mouse_down/drag/up` verbs PulpMetalView calls, including the
    modifiers: the mute brush is a genuine Shift-held press and the select-all
    is a genuine Command-held marquee. Nothing here sets state through a test
    hook, so a gesture that stopped reaching its branch of the pointer handler
    fails this rather than being papered over.

PREMISES, WHICH ARE REPORTED AS INCONCLUSIVE AND NEVER AS A PASS

    Each stage must be shown to have DONE something before the stage after it
    means anything: the paint must move bands off 0 dB, the brush must mute a
    non-empty set, and the marquee must actually have selected -- proved by
    bands far from the group drag's own x moving with it, which a sculpt
    (the no-selection fallback) cannot do.

Exit codes: 0 pass, 1 a finding, 3 the premise is unproven.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Calibrated against the authored 1320x860 editor box, measured from a
# SPECTR_LAYOUT_DUMP plus a probe drag: 32 bands, band 6 at x=300 and band 12
# at x=520, 0 dB at y~439 and ~0.0796 dB per pixel. Any other viewport makes
# these coordinates name different bands, so the viewport is asserted below
# rather than assumed.
VIEWPORT = (1320.0, 860.0)
GESTURES = ";".join((
    # Paint bands 6..12 up to a distinctive +7 dB, so a muted member's stashed
    # level is a number this can recognise rather than the 0 dB default.
    "paint=none:300,350>520,350@10",
    # Shift-held mute brush across bands 6..8.
    "mute=shift:300,430>375,430@6",
    # Command-held marquee across the whole plot: select every band.
    "selectall=cmd:70,600>1250,600@14",
    # The gesture under test: a plain drag UP on band 20, a member of the
    # selection, far from the muted bands so nothing about it is local to them.
    "group=none:813,438>813,338@10",
))
# One shared dB delta is what a group drag means, so every member must move by
# the same amount. Tolerance covers the float round trip through the field.
DELTA_TOL_DB = 0.35


PLANT_ENV = {
    "unmute": "SPECTR_GROUP_DRAG_UNMUTE_PLANT",
    "freeze": "SPECTR_GROUP_DRAG_FREEZE_PLANT",
}
PLANT_MARKER = {
    "unmute": "[plant] group drag routed back",
    "freeze": "[plant] group drag holds mute but discards",
}


def run(app, out_dir, plant):
    env = dict(os.environ)
    env.update(
        PULP_HEADLESS="1",
        PULP_FRAMES="400",
        PULP_SCREENSHOT=os.path.join(out_dir, "shot.png"),
        SPECTR_GESTURES=GESTURES,
        SPECTR_GESTURE_OUT=os.path.join(out_dir, "gestures.json"),
    )
    for var in PLANT_ENV.values():
        env.pop(var, None)
    if plant:
        env[PLANT_ENV[plant]] = "1"
    log = os.path.join(out_dir, "app.log")
    with open(log, "wb") as fh:
        proc = subprocess.run([app], env=env, stdout=fh, stderr=subprocess.STDOUT,
                              timeout=420)
    path = os.path.join(out_dir, "gestures.json")
    if proc.returncode != 0 or not os.path.exists(path):
        return None, log
    with open(path) as fh:
        return json.load(fh), log


def settled(gesture):
    """The post-gesture reading. The field is published through a queue, so the
    `up` sample is taken before the gesture's own commit lands; `settled` is
    the sample after the runtime was pumped."""
    for sample in reversed(gesture["samples"]):
        if sample["phase"] == "settled":
            return sample
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--out")
    ap.add_argument("--negative-control", choices=("unmute", "freeze"),
                    nargs="?", const="unmute",
                    help="plant a known-wrong group drag and REQUIRE this "
                         "measurement to catch it. `unmute` is the reported "
                         "defect; `freeze` holds the mute but discards the "
                         "offset, which no screenshot can see.")
    args = ap.parse_args()

    out_dir = args.out or tempfile.mkdtemp(prefix="spectr-group-drag-")
    os.makedirs(out_dir, exist_ok=True)

    doc, log = run(args.app, out_dir, plant=args.negative_control)
    if doc is None:
        print("INCONCLUSIVE: the app produced no gesture artifact; see", log)
        return 3

    if args.negative_control:
        with open(log, encoding="utf-8", errors="replace") as fh:
            body = fh.read()
        if PLANT_MARKER[args.negative_control] not in body:
            print("INCONCLUSIVE: the plant did not report applying itself, so "
                  "this control would be measuring a healthy app; see", log)
            return 3

    vp = doc.get("viewport") or {}
    if (round(vp.get("w", 0), 1), round(vp.get("h", 0), 1)) != VIEWPORT:
        print("INCONCLUSIVE: editor box is %sx%s, but the gesture coordinates "
              "are calibrated for %sx%s and would name different bands"
              % (vp.get("w"), vp.get("h"), *VIEWPORT))
        return 3

    stages = {g["name"]: g for g in doc.get("gestures", [])}
    for name in ("paint", "mute", "selectall", "group"):
        if name not in stages:
            print("INCONCLUSIVE: gesture %r never ran" % name)
            return 3
        if not stages[name].get("hit"):
            print("INCONCLUSIVE: gesture %r hit nothing; a press delivered to "
                  "nothing leaves a sample list that reads like a control"
                  % name)
            return 3

    painted = settled(stages["paint"])
    before = settled(stages["selectall"])
    after = settled(stages["group"])
    if painted is None or before is None or after is None:
        print("INCONCLUSIVE: a gesture produced no settled sample")
        return 3

    n = after["n_visible"]
    problems = []

    # ── Premises ─────────────────────────────────────────────────────────
    lifted = [i for i in range(n) if abs(painted["gain_db"][i]) > 1.0]
    if not lifted:
        print("INCONCLUSIVE: the paint moved no band off 0 dB, so no muted "
              "band would carry a distinctive stashed level")
        return 3

    muted_before = [i for i in range(n) if before["muted"][i]]
    if not muted_before:
        print("INCONCLUSIVE: the Shift-held brush muted nothing, so there is "
              "no mute for the drag to preserve or clear")
        return 3

    # Did the marquee select? With no selection a plain drag is a SCULPT: it
    # writes an absolute level onto the band(s) it sweeps and leaves the rest
    # alone. A group offset moves everything. Bands far from the drag's own x
    # moving is therefore the selection's signature, and the control that must
    # return non-zero.
    #
    # Counted over the UNMUTED bands only. Whether a muted member moves is the
    # question under test, so folding it into the premise would let a defect
    # that freezes muted members report itself as "inconclusive, no selection"
    # -- an unproven premise where there is a finding.
    unmuted = [i for i in range(n) if not before["muted"][i]]
    moved = [i for i in unmuted
             if abs(after["gain_db"][i] - before["gain_db"][i]) > 0.5]
    if len(moved) < len(unmuted) - 1:
        print("INCONCLUSIVE: only %d of %d unmuted bands moved, so the marquee "
              "did not select and this measured a sculpt, not a group drag"
              % (len(moved), len(unmuted)))
        return 3

    # ── Findings ─────────────────────────────────────────────────────────
    # The shared delta, taken from the UNMUTED members: they are the ones whose
    # behaviour is not in question, so they define what "moved with the group"
    # means for the ones that are.
    deltas = [after["gain_db"][i] - before["gain_db"][i] for i in unmuted]
    delta = sorted(deltas)[len(deltas) // 2]
    spread = max(deltas) - min(deltas)
    if spread > DELTA_TOL_DB:
        problems.append(
            "the unmuted members did not share one delta (spread %.2f dB over "
            "%d bands); a group drag applies ONE dB offset" % (spread, len(unmuted)))

    # 1. Mute is untouched. This is the user's report.
    muted_after = [i for i in range(n) if after["muted"][i]]
    cleared = [i for i in muted_before if i not in muted_after]
    gained = [i for i in muted_after if i not in muted_before]
    if cleared:
        problems.append(
            "the drag UNMUTED bands %s -- they were muted before it and are "
            "audible after it. Mute and gain are separate axes: a gain drag "
            "must not write the mute flag." % cleared)
    if gained:
        problems.append("the drag MUTED bands %s, which it never should" % gained)

    # 2. A muted member's level moved WITH the group. Without this the fix
    #    could be "hold the mute and freeze the band", which is the other wrong
    #    answer the draw path gives -- and which looks correct on screen.
    for i in muted_before:
        if i in cleared:
            continue
        got = after["gain_db"][i] - before["gain_db"][i]
        if abs(got - delta) > DELTA_TOL_DB:
            problems.append(
                "muted band %d's level moved %.2f dB while the selection moved "
                "%.2f dB; a muted member must take the same offset underneath "
                "its mute, so unmuting returns it to where the group moved it"
                % (i, got, delta))

    print("driven by real press: paint(mods=%d) mute(mods=%d) selectall(mods=%d) "
          "group(mods=%d)"
          % tuple(stages[k].get("mods", 0)
                  for k in ("paint", "mute", "selectall", "group")))
    print("muted before %s  after %s   shared delta %+.2f dB over %d bands"
          % (muted_before, muted_after, delta, len(moved)))

    if args.negative_control:
        if problems:
            print("CONTROL OK: the assertion caught the planted defect:")
            for p in problems:
                print("   -", p)
            return 0
        print("CONTROL FAILED: the planted defect went undetected, so this "
              "measurement cannot see the bug it claims to watch for")
        return 1

    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    print("PASS: the group drag preserved every mute and moved every selected "
          "band's level by one shared offset")
    return 0


if __name__ == "__main__":
    sys.exit(main())
