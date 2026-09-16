#!/usr/bin/env python3
"""Drive the band context menu in the SHIPPING STANDALONE and judge every row
on the state it changes.

Why the standalone and not a rig. Every gate this repo has ever had over this
menu drives it by CSS selector (`SPECTR_CLICK`, `rig.activate`), and a selector
never consults `hit_test` -- it happily "presses" a row no pointer can reach,
which is exactly this menu's failure mode. An offscreen `View::simulate_click`
is not the answer either: it hit-tests and delivers with bubble=TRUE, while a
real macOS host routes a press inside an active overlay through
`route_press_to_active_overlay` and delivers it with bubble=FALSE. Driving the
real binary through the host's own sequence is the only reading that is about
the product.

Each row is pressed at the painted centre of its OWN LABEL -- where the words
the user is reading actually are -- and two things are asserted separately:

  * the EFFECT the row promises (band gains, mute flags, edit-mode parameter,
    viewport), read from the processor's own state; and
  * whether the menu CLOSED.

They are separate because they fail separately. A press that closes the menu
while firing a different row's action, and a press that fires nothing and
leaves the menu up, are different bugs, and collapsing them into one pass is
how this surface stayed green while being unusable.

Exit: 0 every assertion holds, 1 one did not, 3 the run could not be made.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

# One press per row, each preceded by its own reopen because a working row
# closes the menu. Band levels are arranged with a REAL DRAG: a host-parameter
# write reaches the band field through apply_parameters on the audio thread,
# and a headless run opens no audio device, so process() never runs and the
# write would be invisible.
SCENARIO = ";".join([
    "d_band8=drag:378,400>378,330@12",
    "d_band12=drag:520,400>520,350@12",
    "o_mute=rpress:378,400",       "mute=row:Mute / Unmute",
    "o_unmute=rpress:378,400",     "unmute=row:Mute / Unmute",
    "o_reset=rpress:378,400",      "reset=row:Reset to 0 dB",
    "d_again=drag:378,400>378,330@12",
    "o_solo=rpress:378,400",       "solo=row:Solo",
    "o_seln=rpress:378,400",       "selnone_disabled=row:Select none",
    "selall=row:Select all",
    "o_zero=rpress:378,400",       "zerosel=row:Zero selection",
    "d_band8b=drag:378,400>378,330@12",
    "o_msel=rpress:378,400",       "mutesel=row:Mute / Unmute selection",
    "o_msel2=rpress:378,400",      "mutesel2=row:Mute / Unmute selection",
    "o_seln2=rpress:378,400",      "selnone=row:Select none",
    "o_after_seln=rpress:378,400",
    "glide=row:Glide",
    "o_sculpt=rpress:378,400",     "sculpt=row:Sculpt",
    "o_level=rpress:378,400",      "level=row:Level",
    "o_boost=rpress:378,400",      "boost=row:Boost",
    "o_flare=rpress:378,400",      "flare=row:Flare",
    "o_glide2=rpress:378,400",     "glide2=row:Glide",
    "zoom=wheel:378,400,-240,12",
    "o_fit=rpress:378,400",        "fit=row:Fit full range",
    "settle_fit=wait",
    # Escape and outside-click, with a selection live, where no DAW can be
    # the confound.
    "o_esc0=rpress:378,400",       "selall2=row:Select all",
    "o_esc=rpress:378,400",        "pre_esc=wait", "esc=escape",
    "o_out=rpress:378,400",        "pre_out=wait", "outside=outside:60,60",
    # The reopen sequence the user described as "gets better once closed and
    # reopened". Four opens with the selection live, measured.
    "o_re1=rpress:378,400",        "esc_re1=escape",
    "o_re2=rpress:378,400",        "esc_re2=escape",
    "o_re3=rpress:378,400",        "esc_re3=escape",
    "o_re4=rpress:378,400",        "esc_re4=escape",
    # And the other half of the same field report -- "i can't tell if it's
    # fixed after resizing explicitly or not". A host resize re-solves the
    # whole tree, so whether it repairs the menu's stale box is measurable.
    "grow=resize:1500,980",        "o_re5=rpress:378,400", "esc_re5=escape",
    "shrink=resize:1320,860",      "o_re6=rpress:378,400",
])

# What each row is aimed at, and what a press aimed at it ACTUALLY reaches
# while a selection is live. Pinned exactly: the rows whose box is stale sit
# under a row from the overflowing tail, so the press fires that row instead.
KNOWN_MISAIM_WITH_SELECTION = {
    "Mute / Unmute": "Boost (edit mode)",
    "Reset to 0 dB": "Glide (edit mode)",
    "Solo": "the VIEW divider — nothing at all, and the menu stays open",
    "mute others": "the VIEW divider — nothing at all, and the menu stays open",
    "Select all": "Fit full range (the viewport)",
}


def step(steps, name):
    for s in steps:
        if s["step"] == name:
            return s
    return None


def muted_set(s):
    return {i for i, v in enumerate(s["muted"]) if v}


def gain(s, i):
    return s["gain_db"][i] if i < len(s["gain_db"]) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--plant-no-press", action="store_true",
                    help="negative control: every measured press becomes a "
                         "no-op wait. Every row assertion must then fail.")
    args = ap.parse_args()

    scenario = SCENARIO
    if args.plant_no_press:
        scenario = ";".join(
            (part.split("=")[0] + "=wait") if "=row:" in part else part
            for part in SCENARIO.split(";"))

    os.makedirs(args.out, exist_ok=True)
    json_path = os.path.join(args.out, "menu-scenario.json")
    if os.path.exists(json_path):
        os.remove(json_path)
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1",
        "PULP_TEST_MODE": "1",
        "SPECTR_MENU_SCENARIO": scenario,
        "SPECTR_MENU_SCENARIO_OUT": json_path,
        "SPECTR_MENU_SCENARIO_DELAY": "10",
        "PULP_SCREENSHOT": os.path.join(args.out, "menu-scenario.png"),
        "PULP_FRAMES": "1400",
    })
    log_path = os.path.join(args.out, "menu-scenario.log")
    with open(log_path, "w") as log:
        proc = subprocess.run([args.binary], env=env, stdout=log,
                              stderr=subprocess.STDOUT, timeout=900)
    if not os.path.exists(json_path):
        print("UNPROVEN: the standalone produced no scenario record (rc=%d); "
              "see %s" % (proc.returncode, log_path), file=sys.stderr)
        return 3
    # The standalone must not have opened an audio device.
    with open(log_path) as log:
        text = log.read()
    if "no audio device created, opened, or started" not in text:
        print("UNPROVEN: the run did not report that it opened no audio "
              "device, so it cannot be said to have been silent.",
              file=sys.stderr)
        return 3

    steps = json.load(open(json_path))["steps"]
    if len(steps) != len(scenario.split(";")):
        print("UNPROVEN: %d of %d steps ran; the frame budget is too small."
              % (len(steps), len(scenario.split(";"))), file=sys.stderr)
        return 3

    rows = []

    def record(item, state, asserted, ok, reading):
        rows.append((item, state, asserted, ok, reading))

    def closed(name):
        s = step(steps, name)
        return s is not None and not s["menu_mounted"]

    # ── premise: the menu opens, and the arrangement drag moved a band ──
    o = step(steps, "o_mute")
    d = step(steps, "d_band8")
    if o is None or not o["menu_mounted"]:
        print("UNPROVEN: the context press did not open the menu.",
              file=sys.stderr)
        return 3
    if d is None or abs(gain(d, 8) or 0.0) < 1.0:
        print("UNPROVEN: the arrangement drag did not move band 8 "
              "(read %.3f dB), so nothing below is measuring a level."
              % (gain(d, 8) or 0.0), file=sys.stderr)
        return 3
    level = gain(d, 8)

    m = step(steps, "mute")
    record("Mute / Unmute", "no selection", "band 8 muted, menu closed",
           8 in muted_set(m) and closed("mute"),
           "muted=%s closed=%s" % (8 in muted_set(m), closed("mute")))

    u = step(steps, "unmute")
    # The premise matters: a band that was never muted is already "unmuted at
    # its old level", so without it this assertion is true of a row that did
    # nothing. It is also what the no-press control has to be able to fail.
    was_muted = 8 in muted_set(m)
    record("Mute / Unmute (2nd)", "no selection",
           "band 8 unmuted at its old level, menu closed",
           was_muted and 8 not in muted_set(u)
           and abs(gain(u, 8) - level) < 0.5 and closed("unmute"),
           "was muted first=%s -> muted=%s level %.2f (was %.2f) closed=%s"
           % (was_muted, 8 in muted_set(u), gain(u, 8), level,
              closed("unmute")))

    r = step(steps, "reset")
    record("Reset to 0 dB", "no selection", "band 8 at 0 dB, menu closed",
           abs(gain(r, 8)) < 0.5 and 8 not in muted_set(r) and closed("reset"),
           "band 8 = %.2f dB muted=%s closed=%s"
           % (gain(r, 8), 8 in muted_set(r), closed("reset")))

    s_solo = step(steps, "solo")
    others = muted_set(s_solo) - {8}
    n = s_solo["n_visible"]
    record("Solo (mute others)", "no selection",
           "every other band muted, soloed band audible AND its level kept",
           len(others) == n - 1 and 8 not in muted_set(s_solo)
           and abs(gain(s_solo, 8) - gain(step(steps, "d_again"), 8)) < 0.5
           and closed("solo"),
           "%d of %d others muted, band 8 muted=%s level %.2f (was %.2f) "
           "closed=%s" % (len(others), n - 1, 8 in muted_set(s_solo),
                          gain(s_solo, 8), gain(step(steps, "d_again"), 8),
                          closed("solo")))

    dis = step(steps, "selnone_disabled")
    ctl = step(steps, "selall")
    record("Select none (disabled)", "no selection",
           "inert AND the menu stays, while a live row in the same menu works",
           dis["menu_mounted"] and muted_set(dis) == muted_set(s_solo)
           and closed("selall")
           and (step(steps, "o_zero") or {}).get("menu_children") == 17,
           "menu stayed=%s state unchanged=%s; live control (Select all) "
           "closed=%s and the reopened menu has %s children"
           % (dis["menu_mounted"], muted_set(dis) == muted_set(s_solo),
              closed("selall"),
              (step(steps, "o_zero") or {}).get("menu_children")))

    record("Select all", "no selection",
           "the reopened menu carries the selection rows",
           (step(steps, "o_zero") or {}).get("menu_children") == 17,
           "menu children after reopen = %s (17 = selection rows present)"
           % (step(steps, "o_zero") or {}).get("menu_children"))

    z = step(steps, "zerosel")
    record("Zero selection", "all selected",
           "every band at 0 dB, menu closed",
           all(abs(g) < 0.5 for g in z["gain_db"]) and closed("zerosel"),
           "max |gain| = %.2f dB closed=%s"
           % (max(abs(g) for g in z["gain_db"]), closed("zerosel")))

    ms = step(steps, "mutesel")
    record("Mute / Unmute selection", "all selected",
           "every band muted, menu closed",
           len(muted_set(ms)) == ms["n_visible"] and closed("mutesel"),
           "%d of %d muted closed=%s"
           % (len(muted_set(ms)), ms["n_visible"], closed("mutesel")))

    ms2 = step(steps, "mutesel2")
    before = gain(step(steps, "d_band8b"), 8)
    all_were_muted = len(muted_set(ms)) == ms["n_visible"]
    record("Mute / Unmute selection (2nd)", "all selected",
           "every band unmuted AND band 8's level restored, menu closed",
           all_were_muted and not muted_set(ms2)
           and abs(gain(ms2, 8) - before) < 0.5 and closed("mutesel2"),
           "all were muted first=%s -> %d muted, band 8 = %.2f dB (was %.2f) "
           "closed=%s" % (all_were_muted, len(muted_set(ms2)), gain(ms2, 8),
                          before, closed("mutesel2")))

    record("Select none", "all selected",
           "the reopened menu has lost the selection rows",
           (step(steps, "o_after_seln") or {}).get("menu_children") == 14
           and closed("selnone"),
           "menu children after reopen = %s (14 = selection rows gone) "
           "closed=%s" % ((step(steps, "o_after_seln") or {}).get(
               "menu_children"), closed("selnone")))

    for name, value in (("sculpt", 0.0), ("level", 1.0), ("boost", 2.0),
                        ("flare", 3.0), ("glide2", 4.0)):
        s_mode = step(steps, name)
        prior = steps[steps.index(s_mode) - 2]
        record(name.rstrip("2").capitalize(), "no selection",
               "the edit-mode parameter moves to this mode, menu closed",
               s_mode["edit_mode"] == value and prior["edit_mode"] != value
               and closed(name),
               "edit_mode %.1f -> %.1f (want %.1f) closed=%s"
               % (prior["edit_mode"], s_mode["edit_mode"], value,
                  closed(name)))

    zoomed = step(steps, "zoom")
    after_fit = step(steps, "settle_fit")
    record("Fit full range", "no selection",
           "the processor's viewport returns to 20 Hz - 20 kHz, menu closed",
           zoomed["max_hz"] < 1000.0
           and abs(after_fit["min_hz"] - 20.0) < 1.0
           and abs(after_fit["max_hz"] - 20000.0) < 200.0 and closed("fit"),
           "zoomed to %.0f..%.0f Hz -> %.0f..%.0f Hz closed=%s"
           % (zoomed["min_hz"], zoomed["max_hz"], after_fit["min_hz"],
              after_fit["max_hz"], closed("fit")))

    # Each carries its own control in the step immediately before it: the menu
    # has to be mounted with nothing happening, so the unmount that follows is
    # attributable to the dismissal and not to the reopen or to a settle.
    e = step(steps, "esc")
    pre_e = step(steps, "pre_esc")
    record("Escape", "all selected",
           "menu up beforehand, then the policy dismisses AND it unmounts",
           pre_e["menu_mounted"] and e["result"] == "overlay"
           and not e["menu_mounted"],
           "mounted before=%s; policy answered '%s'; mounted after=%s"
           % (pre_e["menu_mounted"], e["result"], e["menu_mounted"]))

    out = step(steps, "outside")
    pre_o = step(steps, "pre_out")
    record("Press outside", "all selected",
           "menu up beforehand, then the policy dismisses AND it unmounts",
           pre_o["menu_mounted"] and out["result"] == "dismissed"
           and not out["menu_mounted"],
           "mounted before=%s; policy answered '%s'; mounted after=%s"
           % (pre_o["menu_mounted"], out["result"], out["menu_mounted"]))

    # ── the layout defect this lane does not own, pinned exactly ──
    def unreachable(name):
        s = step(steps, name)
        if s is None or not s.get("rows"):
            return None
        return sorted(r["label"] for r in s["rows"]
                      if r["pressable"] and not r["owns_own_centre"])

    reopens = ["o_re1", "o_re2", "o_re3", "o_re4", "o_re5", "o_re6"]
    heights = [(step(steps, k) or {}).get("menu_rect", [0, 0, 0, 0])[3]
               for k in reopens]
    sets = [unreachable(k) for k in reopens]
    expected = sorted(KNOWN_MISAIM_WITH_SELECTION)

    print("\n── the band menu with a selection live ──")
    print("Neither closing and reopening NOR a host resize changes it. "
          "Container height at six opens (the last two straddling a resize to "
          "1500x980 and back): %s" % heights)
    for k, s in zip(reopens, sets):
        print("  %-6s rows a pointer cannot reach: %s" % (k, s))
    print("\nWhat a press aimed at each of those rows ACTUALLY reaches:")
    for label in expected:
        print("  %-16s -> %s" % (label, KNOWN_MISAIM_WITH_SELECTION[label]))

    stale = [h for h in heights if h != heights[0]]
    layout_ok = (not stale) and all(s == expected for s in sets)
    record("(upstream) menu container height", "all selected",
           "unchanged across six reopens and a resize; same rows unreachable",
           layout_ok,
           "heights %s; unreachable sets identical=%s, expected=%s"
           % (heights, all(s == sets[0] for s in sets), sets[0] == expected))

    print("\n%-32s %-14s %-8s %s" % ("ITEM", "STATE", "VERDICT", "READING"))
    failures = 0
    for item, state, asserted, ok, reading in rows:
        if not ok:
            failures += 1
        print("%-32s %-14s %-8s %s" % (item, state, "PASS" if ok else "FAIL",
                                       reading))
    print("\n%d checks, %d failure(s)" % (len(rows), failures))

    if args.plant_no_press:
        # The layout row is not press-driven, so it is excluded from the
        # control's population: it reads the same either way by design.
        # Escape and the outside press are not driven by a `row:` step, so
        # this control cannot neuter them and must not score them; they carry
        # their own before/after control inside the main run instead. The
        # layout row is a geometry reading and is press-independent by design.
        not_press_driven = ("(upstream)", "Escape", "Press outside")
        press_driven = [r for r in rows
                        if not r[0].startswith(not_press_driven)]
        still_passing = [r[0] for r in press_driven if r[3]]
        if still_passing:
            print("NEGATIVE CONTROL FAILED: %d assertion(s) still pass with "
                  "every measured press skipped: %s"
                  % (len(still_passing), still_passing), file=sys.stderr)
            return 1
        print("negative control: every press-driven assertion went red with "
              "the presses skipped, as it must")
        return 0

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
