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
    "d_history=drag:378,400>378,330@12",
    "o_undo=rpress:378,400", "undo=row:Undo", "after_undo=wait",
    "o_redo=rpress:378,400", "redo=row:Redo", "after_redo=wait",
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
    "assign1=row:Assign selection to Macro 1", "after_assign1=wait",
    "o_assign2=rpress:378,400", "assign2=row:Assign selection to Macro 2", "after_assign2=wait",
    "o_assign3=rpress:378,400", "assign3=row:Assign selection to Macro 3", "after_assign3=wait",
    "o_assign4=rpress:378,400", "assign4=row:Assign selection to Macro 4", "after_assign4=wait",
    "o_allmacros=rpress:378,400", "clear1=row:Clear Macro 1", "after_clear1=wait",
    "o_clear2=rpress:378,400", "clear2=row:Clear Macro 2", "after_clear2=wait",
    "o_clear3=rpress:378,400", "clear3=row:Clear Macro 3", "after_clear3=wait",
    "o_clear4=rpress:378,400", "clear4=row:Clear Macro 4", "after_clear4=wait",
    "o_final=rpress:378,400",
])



def step(steps, name):
    for s in steps:
        if s["step"] == name:
            return s
    return None


def muted_set(s):
    return {i for i, v in enumerate(s["muted"]) if v}


def gain(s, i):
    return s["gain_db"][i] if i < len(s["gain_db"]) else None


def labels(s):
    return {row['label'] for row in (s or {}).get('rows', [])}


def refuse_reason(text, steps):
    """Why this run cannot be reported on at all, or None if it can be.

    Three ways the INSTRUMENT dies while every row assertion still produces a
    confident-looking verdict. They are separated from the per-row checks
    because a dead instrument does not fail some rows -- it fails whichever
    rows the scenario happens to contain, and the count reads as a product
    finding. On 2026-09-19 a run of this scenario reported 42 failing
    assertions and every one of them was this function's first case.

    Pure, so `test_menu_scenario_check.py` can drive it from recorded runs.
    Being able to fire is not the same as firing on the real thing, so that
    test replays the actual dead run's artifacts rather than crafted ones.
    """
    # 1. A JS exception while the scenario was driving.
    #
    # The editor's rows, its menu and every handler behind them are JS. When a
    # throw kills the subtree owning the context handler, the native tree is
    # still there and still takes presses -- so `rpress` keeps answering
    # "handled", no menu ever mounts, and every row assertion fails for a
    # reason that has nothing to do with the row.
    #
    # Two `script-ui[error]` lines are emitted before the first frame on every
    # healthy run (the build-info probe), and the fatal one is byte-identical
    # to the benign one (`{}`), so the text cannot discriminate. The position
    # can: anything after the first frame fired while the scenario drove.
    first_frame = text.find("first frame")
    if first_frame >= 0 and "script-ui[error]" in text[first_frame:]:
        after = [ln.strip() for ln in text[first_frame:].splitlines()
                 if "script-ui[error]" in ln]
        return ("UNPROVEN: %d JS error(s) fired after the first frame, so the "
                "editor was broken while the scenario drove it and every "
                "assertion would be measuring the breakage, not the product:"
                "\n  %s" % (len(after), "\n  ".join(after)))

    # 2. A verb the standalone does not implement.
    #
    # It changes nothing and records `unknown-step`, so the scenario silently
    # measures less than it reads as measuring. `resize` was emitted by the
    # scenario in this file and unimplemented for its whole life: both steps
    # did nothing, and the two reopens after them were reported as
    # post-resize readings of a window that had never been resized.
    unknown = [s for s in steps if s["result"] == "unknown-step"]
    if unknown:
        return ("UNPROVEN: the standalone does not implement %d step(s) this "
                "scenario emits, so they changed nothing: %s"
                % (len(unknown),
                   ", ".join("%s=%s" % (s["step"], s["kind"])
                             for s in unknown)))

    # 3. A context press that did not open the menu.
    #
    # The per-row premise checks one press; this checks all of them, because a
    # scenario that opens the menu once and then stops is the same dead
    # instrument reported as a long list of product failures.
    presses = [s for s in steps if s["kind"] == "rpress"]
    dead = [s for s in presses if not s["menu_mounted"]]
    if dead:
        return ("UNPROVEN: %d of %d context presses did not open the menu "
                "(%s), so the rows behind them were never reachable and no "
                "assertion about them means anything."
                % (len(dead), len(presses),
                   ", ".join("%s:%s" % (s["step"], s["result"])
                             for s in dead[:6])))
    return None


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
        "PULP_FRAMES": "2100",
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

    # Is this run readable at all? See `refuse_reason`.
    reason = refuse_reason(text, steps)
    if reason is not None:
        print("%s\nSee %s" % (reason, log_path), file=sys.stderr)
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

    history = step(steps, "d_history")
    undo = step(steps, "after_undo")
    redo = step(steps, "after_redo")
    record("Undo", "one drag", "one undo reverses the entire gesture and closes the menu",
           abs(gain(history, 8)) > 1 and abs(gain(undo, 8)) < 0.1
           and history['undo_depth'] == undo['undo_depth'] + 1 and closed('undo'),
           f"gain {gain(history, 8)} -> {gain(undo, 8)}, depth {history['undo_depth']} -> {undo['undo_depth']}")
    record("Redo", "after undo", "redo restores the complete gesture and closes the menu",
           abs(gain(redo, 8) - gain(history, 8)) < 0.1
           and undo['redo_depth'] == redo['redo_depth'] + 1 and closed('redo'),
           f"gain {gain(undo, 8)} -> {gain(redo, 8)}")

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
           and 'Zero selection' in labels(step(steps, 'o_zero')),
           "menu stayed=%s state unchanged=%s; live control (Select all) "
           "closed=%s and the reopened menu has %s children"
           % (dis["menu_mounted"], muted_set(dis) == muted_set(s_solo),
              closed("selall"),
              (step(steps, "o_zero") or {}).get("menu_children")))

    record("Select all", "no selection",
           "the reopened menu carries the selection rows",
           'Zero selection' in labels(step(steps, "o_zero"))
           and 'Mute / Unmute selection' in labels(step(steps, "o_zero")),
           "selection actions = %s" % sorted(labels(step(steps, "o_zero"))))

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
           'Zero selection' not in labels(step(steps, "o_after_seln"))
           and closed("selnone"),
           "selection rows gone=%s closed=%s" % (
               'Zero selection' not in labels(step(steps, "o_after_seln")), closed("selnone")))

    for macro in range(4):
        number = macro + 1
        assigned = step(steps, f'after_assign{number}')
        cleared = step(steps, f'after_clear{number}')
        record(f'Assign Macro {number}', 'all selected', 'native membership is every visible band and menu closes',
               assigned['macros'][macro] == list(range(assigned['n_visible']))
               and closed(f'assign{number}'), str(assigned['macros'][macro]))
        record(f'Clear Macro {number}', 'assigned', 'native membership is empty and menu closes',
               bool(assigned['macros'][macro]) and cleared['macros'][macro] == []
               and closed(f'clear{number}'), str(cleared['macros'][macro]))

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

    # Every painted action must own its centre, including the largest menu.
    def unreachable(name):
        s = step(steps, name)
        if s is None or not s.get("rows"):
            return None
        return sorted(r["label"] for r in s["rows"]
                      if r["pressable"] and not r["owns_own_centre"])

    reopens = ["o_re1", "o_re2", "o_re3", "o_re4", "o_re5", "o_re6", "o_allmacros"]
    heights = [(step(steps, k) or {}).get("menu_rect", [0, 0, 0, 0])[3]
               for k in reopens]
    sets = [unreachable(k) for k in reopens]
    print("\n── band-menu reachability with a live selection ──")
    for name, missing in zip(reopens, sets):
        print("  %-6s unreachable rows: %s" % (name, missing))
    layout_ok = all(not missing for missing in sets)
    record("Menu row reachability", "all selected",
           "every enabled row owns its painted centre after reopen and resize",
           layout_ok, "heights %s; unreachable rows %s" % (heights, sets))

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
        not_press_driven = ("Menu row reachability", "Escape", "Press outside")
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
