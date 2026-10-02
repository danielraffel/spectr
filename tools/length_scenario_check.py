#!/usr/bin/env python3
"""Drive the Freeze LENGTH control in the SHIPPING STANDALONE and judge every
step on what a person would see and what the processor holds.

The standalone's own scenario engine (SPECTR_MENU_SCENARIO, see
native_editor.cpp) delivers every press, hover and key through the macOS
host's own routes: a click goes through the active-overlay routing and the
host's mouse-down/up with its click callback, a hover through -mouseMoved:,
a key in -keyDown:'s order (root hook, navigation claim, script, overlay
Escape). Presses are aimed at the painted centre of the element a selector
names, measured from the live layout at that moment. `lenprobe` reads the
control (collapsed label, menu rows with * checked and ^ highlighted, the
editor's fields and focus, the Fraction list) and the processor's length.
`shot` writes a Skia raster of the editor mid-run.

Headless (PULP_HEADLESS=1), no audio device, nothing installed.

Exit: 0 every assertion holds, 1 one did not, 3 the run could not be made.
"""

import argparse
import urllib.parse
import json
import os
import subprocess
import sys

TRIGGER = '[data-spectr-length-trigger]'


def opt(label):
    return '[data-spectr-length-option="%s"]' % label


def frac(text):
    return '[data-spectr-length-fraction-option="%s"]' % text


FRACTIONS = ["1/32", "1/16", "1/12", "1/8", "1/6", "3/16", "1/4", "1/3", "3/8", "1/2",
             "5/8", "2/3", "3/4", "5/6", "7/8", "15/16"]
BARS = ["1 bar", "2 bars", "4 bars", "8 bars"]
FRACTION_LIST = '[data-spectr-length-fraction-viewport]'


def menu_rows(checked=None, custom=False):
    """The LENGTH menu's rows as lenprobe reads them: every fraction of a
    bar, the whole bars, the checked custom row when one is in force, and
    Custom length...; * marks the checked row."""
    rows = [f + " bar" for f in FRACTIONS] + BARS + (["custom"] if custom else []) + ["custom-editor"]
    return [r + ("*" if r == checked else "") for r in rows]


def scenario(shots):
    s = []
    add = lambda name, step: s.append("%s=%s" % (name, step))
    shot = lambda name: add("shot_" + name, "shot:" + os.path.join(shots, name + ".png"))
    add("p_start", "lenprobe")
    shot("standalone-1-collapsed")
    # Open by click, a hover moves the highlight, arrows move it, Return
    # picks, the menu closes.
    add("open1", "psel:" + TRIGGER)
    add("p_open1", "lenprobe")
    add("hover4", "hsel:" + opt("4 bars"))
    add("p_hover4", "lenprobe")
    shot("standalone-2-menu-hover")
    add("down1", "key:down")
    add("p_down1", "lenprobe")
    add("up1", "key:up")
    add("p_up1", "lenprobe")
    add("enter1", "key:enter")
    add("p_enter1", "lenprobe")
    # A second press on the trigger closes it, and the next opens it again.
    # (The pointer moves onto the trigger first, as a mouse does: psel
    # presses without moving it.)
    add("onto_t", "hsel:" + TRIGGER)
    add("open_t", "psel:" + TRIGGER)
    add("p_open_t", "lenprobe")
    add("toggle_t", "psel:" + TRIGGER)
    add("p_toggle_t", "lenprobe")
    add("reopen_t", "psel:" + TRIGGER)
    add("p_reopen_t", "lenprobe")
    # A fraction row is a press away, and so is the bottom of the list.
    add("pick_316", "psel:" + opt("3/16 bar"))
    add("p_pick_316", "lenprobe")
    add("open_l", "psel:" + TRIGGER)
    add("pick_8", "psel:" + opt("8 bars"))
    add("p_pick_8", "lenprobe")
    add("open_4", "psel:" + TRIGGER)
    add("pick_4", "psel:" + opt("4 bars"))
    # Close by a press outside it (the host's outside-press route), and by
    # Escape: nothing changes.
    add("open2", "psel:" + TRIGGER)
    add("close2", "outside:660,430")
    add("p_close2", "lenprobe")
    add("open3", "psel:" + TRIGGER)
    add("esc3", "key:escape")
    add("p_esc3", "lenprobe")
    # Custom length...: Bars by its arrows, by keys, typed; invalid values.
    add("open4", "psel:" + TRIGGER)
    add("custom4", "psel:" + opt("custom-editor"))
    add("p_editor", "lenprobe")
    shot("standalone-3-editor")
    add("kup1", "key:up")
    add("kup2", "key:up")
    add("p_kup", "lenprobe")
    add("stepdown", "psel:[data-spectr-length-bars-step=\"down\"]")
    add("p_stepdown", "lenprobe")
    add("clear1", "key:delete")
    add("p_empty", "lenprobe")
    add("t1", "key:1")
    add("t2", "key:2")
    add("t9", "key:9")
    add("p_129", "lenprobe")
    shot("standalone-4-editor-129")
    add("clear2", "key:delete")
    add("tminus", "key:-")
    add("t1b", "key:1")
    add("p_minus1", "lenprobe")
    add("bs", "key:backspace")
    add("t0", "key:0")
    add("p_zero", "lenprobe")
    # Fraction: one scrolling column. It opens on "no fraction" at the top;
    # the wheel takes it to the bottom, where 7/8 and 15/16 are; a hover
    # and an arrow move the highlight; Return picks 15/16.
    add("tab1", "key:tab")
    add("p_tab1", "lenprobe")
    add("fopen", "psel:[data-spectr-length-fraction]")
    add("p_fopen", "lenprobe")
    shot("standalone-5-fraction-list")
    add("fwheel", "wsel:" + FRACTION_LIST + "|120|3")
    add("p_fwheel", "exists:[data-spectr-length-fraction-offset=\"288\"]")
    add("fhover", "hsel:" + frac("7/8"))
    add("p_fhover", "lenprobe")
    shot("standalone-5b-fraction-list-scrolled")
    add("fdown", "key:down")
    add("p_fdown", "lenprobe")
    add("fenter", "key:enter")
    add("p_fenter", "lenprobe")
    add("tab2", "key:tab")
    add("tab3", "key:tab")
    add("p_tab3", "lenprobe")
    add("stab", "key:shift+tab")
    add("p_stab", "lenprobe")
    add("cancel_enter", "key:enter")
    add("p_cancelled", "lenprobe")
    # Apply 0 + 15/16 by a press on its row, then Return: "15/16 bar" (the
    # preset of that name).
    add("open5", "psel:" + TRIGGER)
    add("custom5", "psel:" + opt("custom-editor"))
    add("clear5", "key:delete")
    add("t0b", "key:0")
    add("fopen5", "psel:[data-spectr-length-fraction]")
    add("fwheel5", "wsel:" + FRACTION_LIST + "|120|3")
    add("f1516", "psel:" + frac("15/16"))
    add("enter5", "key:enter")
    add("p_1516", "lenprobe")
    # Apply 1 + 1/8 with APPLY, then the menu shows it checked above Custom.
    add("open6", "psel:" + TRIGGER)
    add("custom6", "psel:" + opt("custom-editor"))
    add("p_editor6", "lenprobe")
    add("clear6", "key:delete")
    add("t1c", "key:1")
    add("tab6", "key:tab")
    add("fdown6", "key:down")
    add("p_fdown6", "lenprobe")
    add("fopen6", "psel:[data-spectr-length-fraction]")
    add("fwheel6", "wsel:" + FRACTION_LIST + "|-120|3")
    add("f18", "psel:" + frac("1/8"))
    add("p_118", "lenprobe")
    shot("standalone-6-editor-1-1-8")
    add("apply6", "psel:[data-spectr-length-apply]")
    add("p_applied", "lenprobe")
    shot("standalone-7-after-apply")
    add("open7", "psel:" + TRIGGER)
    add("p_menu7", "lenprobe")
    shot("standalone-8-after-apply-menu")
    add("esc7", "key:escape")
    # Cancel restores, by the CANCEL button.
    add("open8", "psel:" + TRIGGER)
    add("custom8", "psel:" + opt("custom-editor"))
    add("t3", "key:delete")
    add("t3b", "key:3")
    add("cancel8", "psel:[data-spectr-length-cancel]")
    add("p_cancel8", "lenprobe")
    # A common length again: 2 bars, plural; the custom row disappears.
    add("open9", "psel:" + TRIGGER)
    add("two", "psel:" + opt("2 bars"))
    add("open10", "psel:" + TRIGGER)
    add("p_menu10", "lenprobe")
    add("esc10", "key:escape")
    add("end", "wait")
    return ";".join(s)


def probe(steps, name):
    for st in steps:
        if st["step"] == name:
            text = st["result"]
            body, _, proc = text.partition("|processor=")
            try:
                value = json.loads(urllib.parse.unquote(body))
            except Exception:  # noqa: BLE001
                value = {"unreadable": text}
            value["processor"] = proc
            return value
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--shots", default=None, help="where the mid-run rasters go (default --out)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    shots = args.shots or args.out
    os.makedirs(shots, exist_ok=True)
    spec = scenario(shots)
    json_path = os.path.join(args.out, "length-scenario.json")
    if os.path.exists(json_path):
        os.remove(json_path)
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1", "PULP_TEST_MODE": "1", "PULP_AUDIO_DEVICE": "null",
        "SPECTR_MENU_SCENARIO": spec, "SPECTR_MENU_SCENARIO_OUT": json_path,
        "SPECTR_MENU_SCENARIO_DELAY": "10",
        "PULP_SCREENSHOT": os.path.join(args.out, "length-scenario-final.png"),
        "PULP_FRAMES": str(400 + 12 * len(spec.split(";"))),
    })
    env.pop("SPECTR_OPEN_SETTINGS", None)
    log_path = os.path.join(args.out, "length-scenario.log")
    with open(log_path, "w") as log:
        proc = subprocess.run([args.binary], env=env, stdout=log, stderr=subprocess.STDOUT,
                              timeout=900)
    if not os.path.exists(json_path):
        print("UNPROVEN: no scenario record (rc=%d); see %s" % (proc.returncode, log_path))
        return 3
    if "no audio device created, opened, or started" not in open(log_path).read():
        print("UNPROVEN: the run did not report that it opened no audio device")
        return 3
    steps = json.load(open(json_path))["steps"]
    if len(steps) != len(spec.split(";")):
        print("UNPROVEN: %d of %d steps ran" % (len(steps), len(spec.split(";"))))
        return 3
    bad = [s for s in steps if s["result"] in ("unknown-step", "bad-arg", "selector-absent",
                                                "probe-error", "not-written")]
    for s in bad:
        print("UNPROVEN: step %s -> %s" % (s["step"], s["result"]))
    if bad:
        return 3

    failures = []

    def check(ok, what, reading):
        print(("PASS  " if ok else "FAIL  ") + what + "   " + json.dumps(reading))
        if not ok:
            failures.append(what)

    P = lambda name: probe(steps, name)
    r = P("p_start"); check(r["label"] == "1 bar" and r["menu"] is None, "starts collapsed at 1 bar", r)
    r = P("p_open1"); check(r["menu"] == menu_rows("1 bar"),
                            "a click opens every fraction, 1/2/4/8 bars and Custom length..., 1 bar checked", r)
    r = P("p_hover4"); check("4 bars^" in r["menu"], "hover highlights 4 bars", r)
    r = P("p_down1"); check("8 bars^" in r["menu"], "Down moves the highlight to 8 bars", r)
    r = P("p_up1"); check("4 bars^" in r["menu"], "Up moves it back to 4 bars", r)
    r = P("p_enter1"); check(r["label"] == "4 bars" and r["menu"] is None
                             and r["processor"] == "4 bars", "Return picks 4 bars and closes", r)
    r = P("p_open_t"); check(r["menu"] is not None, "a press on the trigger opens it", r)
    r = P("p_toggle_t"); check(r["menu"] is None and r["label"] == "4 bars",
                               "a second press on the trigger closes it", r)
    r = P("p_reopen_t"); check(r["menu"] is not None, "and the next press opens it again", r)
    r = P("p_pick_316"); check(r["label"] == "3/16 bar" and r["processor"] == "3/16 bar" and r["menu"] is None,
                               "a fraction row: 3/16 bar, exactly", r)
    r = P("p_pick_8"); check(r["label"] == "8 bars" and r["processor"] == "8 bars",
                             "the last bars row: 8 bars", r)
    r = P("p_close2"); check(r["menu"] is None and r["label"] == "4 bars", "a press outside closes it", r)
    r = P("p_esc3"); check(r["menu"] is None and r["label"] == "4 bars", "Escape closes it, nothing changes", r)
    r = P("p_editor"); check(r["editor"] and r["bars"] == "1" and r["fraction"] == "0"
                             and r["focus"] == "bars", "Custom length... opens the editor on Bars", r)
    r = P("p_kup"); check(r["bars"] == "3", "Up steps Bars (1 -> 3)", r)
    r = P("p_stepdown"); check(r["bars"] == "2", "the down arrow steps Bars (3 -> 2)", r)
    r = P("p_empty"); check(r["bars"] == "" and r["valid"] == "false"
                            and r["message"] == "Enter a number of bars", "empty Bars cannot apply", r)
    r = P("p_129"); check(r["bars"] == "129" and r["valid"] == "false"
                          and r["message"] == "Up to 128 bars", "129 bars is refused, naming the limit", r)
    r = P("p_minus1"); check(r["bars"] == "1", "a minus sign cannot be typed (-1 reads 1)", r)
    r = P("p_zero"); check(r["bars"] == "0" and r["valid"] == "false"
                           and r["message"] == "Choose a length longer than 0", "0 + 0 is refused", r)
    r = P("p_tab1"); check(r["focus"] == "fraction", "Tab moves to Fraction", r)
    r = P("p_fopen"); check(r["fractions"] is not None and "0*" in r["fractions"],
                            "the Fraction list opens with no fraction checked", r)
    st = [x for x in steps if x["step"] == "p_fwheel"][0]
    check(st["result"] == "present", "the wheel scrolls the Fraction list to its end (288pt)", st["result"])
    r = P("p_fhover"); check(any(t.startswith("7/8") and t.endswith("^") for t in r["fractions"]),
                             "hover highlights 7/8 in the Fraction list", r)
    r = P("p_fdown"); check(any(t.startswith("15/16") and "^" in t for t in r["fractions"]),
                            "Down moves the Fraction highlight to 15/16", r)
    r = P("p_fenter"); check(r["fractions"] is None and r["fraction"] == "15/16"
                             and r["valid"] == "true", "Return picks 15/16 and closes the list", r)
    r = P("p_tab3"); check(r["focus"] == "apply", "Tab, Tab reaches APPLY", r)
    r = P("p_stab"); check(r["focus"] == "cancel", "Shift+Tab goes back to CANCEL", r)
    r = P("p_cancelled"); check(not r["editor"] and r["label"] == "4 bars",
                                "Return on CANCEL cancels; 4 bars stands", r)
    r = P("p_1516"); check(r["label"] == "15/16 bar" and r["processor"] == "15/16 bar",
                           "15/16, scrolled to and pressed, applies by Return: 15/16 bar", r)
    # 15/16 bar is a preset of its own, so the custom length is still the
    # default 1 bar the editor opens on.
    r = P("p_editor6"); check(r["bars"] == "1" and r["fraction"] == "0",
                              "the editor opens on the custom length (1 bar; 15/16 bar is a preset)", r)
    r = P("p_fdown6"); check(r["fraction"] == "0" and r["focus"] == "fraction",
                             "Down on Fraction stays on no fraction, the first", r)
    r = P("p_118"); check(r["bars"] == "1" and r["fraction"] == "1/8" and r["valid"] == "true",
                          "1/8 picked from the Fraction list", r)
    r = P("p_applied"); check(r["label"] == "1 1/8 bars" and not r["editor"]
                              and r["processor"] == "1 1/8 bars",
                              "APPLY: the control reads the resolved 1 1/8 bars", r)
    r = P("p_menu7"); check(r["menu"] == menu_rows("custom", custom=True),
                            "the menu shows 1 1/8 bars checked right above Custom length...", r)
    r = P("p_cancel8"); check(r["label"] == "1 1/8 bars" and r["processor"] == "1 1/8 bars",
                              "CANCEL restores 1 1/8 bars", r)
    r = P("p_menu10"); check(r["menu"] == menu_rows("2 bars") and r["label"] == "2 bars",
                             "2 bars: checked, plural, no custom row", r)
    print("%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
