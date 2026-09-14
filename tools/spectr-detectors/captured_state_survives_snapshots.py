#!/usr/bin/env python3
"""Assert an open dropdown still resolves to ITS OWN captured state after both
snapshot slots are filled.

WHY THIS EXISTS

    Spectr renders each dropdown's option rows by replaying a captured table of
    absolute layout boxes -- `edit.materialized.json` pins a row's label span at
    `top:0` and its description span at `top:19`, and that pinning is the only
    reason the two stack.  Live, they do not: `<span style={{flex:1}}>` and
    `<button style={{display:"block"}}>` are both created as ROWS, so an
    unpinned row puts the description beside a crushed label column.

    Which table is replayed comes from `resolveCapturedStateFromAtlas()`, which
    scans `capturedStates` from the end and returns the first match -- array
    order IS the precedence rule.  The atlas mixes two kinds of state under it:
    transient overlays, matched by a node that exists only while that surface is
    open, and LEVEL states, matched by something that stays true.
    `snapshots-morph` is the level one: its selector
    `[data-spectr-snapshots-ready="true"]` rides the permanent transport label
    and reads `snapshotStatus.A && snapshotStatus.B`, so it is true from the
    second capture until relaunch.  It sat at index 5, above `bands`(0),
    `overflow`(1), `edit`(2), `analyzer`(3) and `pattern`(4) -- so once both
    snapshots were captured it won every resolution and those five menus could
    never reach their own state again.  Quitting and relaunching fixed it; that
    is what the user reported.

    Nothing in the existing suite could see it.  The apply loop reports
    `layout_applied == layout_expected` and `layout_node_miss == 0` in the
    degraded state, because no binding went MISSING -- a different, smaller
    table was substituted wholesale.  A screenshot of a fresh launch passes, and
    so does every check that opens a menu without capturing snapshots first.

HOW IT MEASURES

    Both snapshots are captured, and the menu is opened on a real commit AFTER
    `[data-spectr-snapshots-ready="true"]` has appeared -- not before.  That
    ordering is the whole defect: applying a binding mutates the node
    (`setPosition absolute` + left/top/width/height) and nothing reverts it, so
    a popover that mounts while its own state is still active stays correctly
    pinned for as long as it lives.  Open the menu first and the bug hides.

    The verdict is the RESOLVED STATE ID while the menu is open, not the
    presence of a binding -- a count cannot see a wholesale table swap.

    Two controls, both required before any case is believed:

      * `snapshots_ready` must be true in every case.  If the fixture never got
        both slots filled, "the state resolved to `edit`" is vacuous -- it would
        have resolved to `edit` anyway -- so that is INCONCLUSIVE, never a pass.
      * the no-menu case must still resolve to a LEVEL state.  Otherwise a
        "fix" that simply stops `snapshots-morph` from ever matching would pass
        every menu case while silently deleting the morph surface's own
        captured layout.  That is the opposite defect and it must fail here.

Exit codes: 0 pass, 1 fail, 2 inconclusive (the probe could not measure).
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SCHEMA = "spectr-captured-state-receipt-v1"
MENUS = ("edit", "analyzer", "pattern")
LEVEL_STATES = ("snapshots-morph",)

# The menu is opened from inside the per-commit state refresh, on the first
# commit after the readiness marker appears, so the open happens strictly after
# the snapshot state has settled -- the order the user's report describes.
EVAL = r"""
var A=globalThis.__pulpActivateMaterializedElement__;
var F=globalThis.__pulpFindMaterializedElement__;
var READY=String.fromCharCode(91)+"data-spectr-snapshots-ready="+String.fromCharCode(34)+"true"+String.fromCharCode(34)+String.fromCharCode(93);
A('#spectr-snapshot-capture-a','click',null); A('#spectr-snapshot-capture-b','click',null);
var opened=false, n=0, logged=false;
var orig=globalThis.__pulpRefreshMaterializedState__;
globalThis.__pulpRefreshMaterializedState__=function(){
  var r=orig.apply(this,arguments);
  if(!opened && F(READY,"")){ opened=true; __OPEN__ }
  else if(opened && !logged && ++n>6){ logged=true;
    var d=globalThis.__pulpMaterializedMetadataDiagnostics__;
    var v=typeof d==="function"?d():d;
    console.log('[captured-state] ready='+(!!F(READY,""))+' state='+v.state_id
      +' applied='+v.layout_applied+' expected='+v.layout_expected
      +' miss='+v.layout_node_miss);
  }
  return r;
};
"""


def probe_app(app, menu, tmp):
    """Drive the app once and return one case dict, or None if it said nothing."""
    if menu:
        open_js = ("A('[data-spectr-menu-root=\"%s\"] [data-spectr-menu-trigger]',"
                   "'click',null);" % menu)
    else:
        open_js = ""
    env = dict(os.environ)
    env.update(PULP_HEADLESS="1", PULP_FRAMES="200",
               PULP_SCREENSHOT=os.path.join(tmp, (menu or "nomenu") + ".png"),
               SPECTR_EVAL=EVAL.replace("__OPEN__", open_js))
    out = subprocess.run([app], env=env, capture_output=True, timeout=600)
    blob = (out.stdout or b"") + (out.stderr or b"")
    line = None
    for raw in blob.decode("utf-8", "replace").splitlines():
        if "[captured-state]" in raw:
            line = raw[raw.index("[captured-state]"):]
    if line is None:
        return None
    fields = dict(part.split("=", 1) for part in line.split()[1:] if "=" in part)
    return {
        "menu": menu,
        "snapshots_ready": fields.get("ready") == "true",
        "state_id": fields.get("state", ""),
        "layout_applied": int(fields.get("applied", -1)),
        "layout_expected": int(fields.get("expected", -1)),
        "layout_node_miss": int(fields.get("miss", -1)),
    }


def adjudicate(cases):
    problems, notes = [], []

    by_menu = {c.get("menu", ""): c for c in cases}
    for menu in MENUS:
        if menu not in by_menu:
            return 2, ["receipt has no case for the %s menu" % menu], notes
    if "" not in by_menu:
        return 2, ["receipt has no no-menu case; the level-state control is "
                   "what separates this fix from deleting the morph state"], notes

    # CONTROL 1 -- the condition under test must actually have been reached.
    unready = [c.get("menu") or "no-menu" for c in cases if not c.get("snapshots_ready")]
    if unready:
        return 2, ["snapshots never reported ready for: %s; every verdict below "
                   "would be vacuous" % ", ".join(unready)], notes

    for menu in MENUS:
        case = by_menu[menu]
        got = case.get("state_id", "")
        if got != menu:
            if got in LEVEL_STATES:
                problems.append(
                    "%s menu open with both snapshots filled resolved to the LEVEL "
                    "state %r instead of %r -- its option rows lose the bindings "
                    "that stack them" % (menu, got, menu))
            else:
                problems.append("%s menu resolved to %r, expected %r"
                                % (menu, got, menu))
        else:
            notes.append("%s -> %s" % (menu, got))
        applied = case.get("layout_applied", -1)
        expected = case.get("layout_expected", -1)
        if applied != expected or applied < 0:
            problems.append("%s applied %s of %s bindings"
                            % (menu, applied, expected))
        if case.get("layout_node_miss", -1) != 0:
            problems.append("%s reported %s node misses"
                            % (menu, case.get("layout_node_miss")))

    # CONTROL 2 -- the level state must still be reachable when nothing is open.
    fallback = by_menu[""]
    if fallback.get("state_id") not in LEVEL_STATES:
        problems.append(
            "with no menu open and both snapshots filled the state resolved to "
            "%r, not a level state %s -- the morph surface lost its own captured "
            "layout" % (fallback.get("state_id"), list(LEVEL_STATES)))
    else:
        notes.append("no-menu -> %s" % fallback.get("state_id"))

    return (1 if problems else 0), problems, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("receipt", nargs="?", help="committed receipt JSON to adjudicate")
    ap.add_argument("--app", help="Spectr binary to drive instead of reading a receipt")
    ap.add_argument("--write-receipt", help="write the driven receipt here")
    ap.add_argument("--plant", choices=("shadowed", "level-lost"),
                    help="mutate a healthy receipt so the rule must fire")
    args = ap.parse_args()

    if args.app:
        with tempfile.TemporaryDirectory() as tmp:
            cases = []
            for menu in list(MENUS) + [""]:
                case = probe_app(args.app, menu, tmp)
                if case is None:
                    print("INCONCLUSIVE: the app printed no [captured-state] line "
                          "for %s; the probe never ran" % (menu or "no-menu"))
                    return 2
                cases.append(case)
        if args.write_receipt:
            with open(args.write_receipt, "w", encoding="utf-8") as handle:
                json.dump({"schema": SCHEMA, "cases": cases}, handle, indent=1)
                handle.write("\n")
    else:
        if not args.receipt:
            print("INCONCLUSIVE: give a receipt path or --app", file=sys.stderr)
            return 2
        with open(args.receipt, encoding="utf-8") as handle:
            cases = json.load(handle).get("cases", [])

    if args.plant == "shadowed":
        # exactly the defect: the level state shadows every open menu
        for case in cases:
            if case.get("menu"):
                case["state_id"] = "snapshots-morph"
                case["layout_applied"] = case["layout_expected"] = 68
    elif args.plant == "level-lost":
        # the opposite over-fix: snapshots-morph stops matching at all
        for case in cases:
            if not case.get("menu"):
                case["state_id"] = ""

    code, problems, notes = adjudicate(cases)
    for note in notes:
        print("  ok  %s" % note)
    for problem in problems:
        print("FAIL  %s" % problem)
    if code == 0:
        print("PASS: every dropdown keeps its own captured state with both "
              "snapshots filled, and the level state still resolves when none "
              "is open")
    elif code == 2:
        print("INCONCLUSIVE: %s" % ("; ".join(problems) or "nothing measured"))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
