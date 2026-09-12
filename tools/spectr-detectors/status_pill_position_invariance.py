#!/usr/bin/env python3
"""STATUS-PILL-POSITION: the status pill does not MOVE when its content changes.

WHAT IT ASSERTS
    Given dumps captured on both sides of a content change -- typically a
    CLEAR, which publishes `CLEARED GAINS` and then lets a hover reading
    replace it -- the pill above the plot must:

      1. sit centred on the viewport in every dump, and
      2. sit at the SAME top in every dump.

    (1) is the property a viewer sees.  (2) is the half that (1) cannot
    catch and that the shipped defect actually needed: the pill moved by
    (-120, -44), and a rule that only looked at x would have called the
    vertical half of that clean.

WHY THIS EXISTS ALONGSIDE status_pill_width_invariance.py
    That detector already asserts "pill centre == viewport centre", and it
    did NOT catch this.  The rule was right; nothing could aim it.  Its
    `--app` driver presses a band, and across that whole population the
    pill measures exactly 660.0 in every dump -- which is the truth about
    the states it captured and says nothing about the one it could not
    reach, because no fixture could press CLEAR.  A rule that cannot be
    pointed at a defect is not a rule that failed.

    So this one is defined by its POPULATION, not by a new rule: it exists
    to be handed the two sides of a CLEAR, and its `--app` driver presses
    CLEAR through `[data-spectr-rail-action="clear"]` -- a stable hook,
    unlike "the first `button` in the document", which happens to be CLEAR
    today and would rot silently the first time a button lands above it.

WHAT THIS CANNOT SEE
    Whether the position is the RIGHT one in an absolute sense: it checks
    centring against the viewport and self-consistency of the top, so a
    pill authored at the wrong top but *consistently* at the wrong top
    passes.  It also cannot see a state nobody captured -- a dump per state
    is the whole population -- and a dismissed pill (no text child) carries
    no reading, so a run of only-dismissed dumps adjudicates nothing and
    says so rather than passing.

    It is deliberately blind to width.  Width is
    status_pill_width_invariance.py's job, and a pill that is too wide but
    still centred is a cosmetic residual, not this defect.

USAGE
    status_pill_position_invariance.py DUMP [DUMP ...]
    status_pill_position_invariance.py --app build/Spectr.app/Contents/MacOS/Spectr
    status_pill_position_invariance.py DUMP... --plant shift   # negative control
    status_pill_position_invariance.py DUMP... --plant raise   # negative control

Exit codes: 0 pass, 1 fail, 2 the probe could not measure (never a pass).
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout_common as lc  # noqa: E402

TOL = 0.5
# The pill is the only node the materialized document lifts to z-index 6.
# Its generated id (`__behavior_pr_z`) renumbers whenever the document is
# regenerated, so keying on it would rot silently; the stacking level is
# authored.
PILL_Z_INDEX = 6

# The exact displacement the shipped defect produced, in the authored
# 1320x860 design space.  The plants reproduce it rather than inventing a
# round number, so a plant that stops firing means the rule rotted against
# the real shape of the bug and not against a strawman.
PLANT_DX = -120.0
PLANT_DY = -44.0


def find_pill(dump):
    hits = [i for i, n in enumerate(dump.nodes)
            if (n.get("z_order") or {}).get("z_index") == PILL_Z_INDEX]
    if not hits:
        raise SystemExit(
            "%s: no node at z-index %d. The status pill is the only thing "
            "authored at that level, so this is a dump of some other surface "
            "-- or the pill never mounted. Either way nothing was measured."
            % (dump.path, PILL_Z_INDEX))
    if len(hits) > 1:
        raise SystemExit(
            "%s: %d nodes sit at z-index %d; the detector cannot tell which "
            "is the status pill." % (dump.path, len(hits), PILL_Z_INDEX))
    pill = hits[0]
    if dump.parent is None:
        raise SystemExit("%s: no depths sidecar, ancestry is unavailable"
                         % dump.path)
    labels = [i for i in range(len(dump.nodes))
              if dump.parent[i] == pill and n_text(dump.nodes[i])]
    if len(labels) > 1:
        raise SystemExit("%s: the status pill owns %d text children"
                         % (dump.path, len(labels)))
    return pill, (labels[0] if labels else None)


def n_text(node):
    return node.get("measured_text_boxes")


def collect(dumps, plant):
    rows, empty = [], []
    for index, dump in enumerate(dumps):
        pill_i, label_i = find_pill(dump)
        px, py, pw, ph = lc.r(dump.nodes[pill_i]["rect"])
        if label_i is None:
            empty.append((dump.path, py))
            continue
        box = dump.nodes[label_i]["measured_text_boxes"][0]
        text = box.get("text") or ""
        # Plant on every dump but the first: the rule is about a CHANGE
        # between states, so mutating all of them together would leave the
        # relationship intact and the rule would correctly stay silent.
        if index > 0:
            if plant == "shift":
                px += PLANT_DX
            elif plant == "raise":
                py += PLANT_DY
        rows.append(dict(path=dump.path, text=text, px=px, py=py, pw=pw,
                         vw=float(dump.viewport.get("w", 0.0))))
    return rows, empty


def check(rows):
    violations = []
    for r in rows:
        if r["vw"] <= 0:
            violations.append("%s: dump carries no viewport width"
                              % os.path.basename(r["path"]))
            continue
        centre = r["vw"] / 2.0
        pill_centre = r["px"] + r["pw"] / 2.0
        if abs(pill_centre - centre) > TOL:
            violations.append(
                "PILL OFF CENTRE: centre %.1f, viewport centre %.1f, off by "
                "%.1f (%s) painting %r"
                % (pill_centre, centre, pill_centre - centre,
                   os.path.basename(r["path"]), r["text"]))

    tops = sorted({round(r["py"], 2) for r in rows})
    if len(tops) > 1:
        by_top = {}
        for r in rows:
            by_top.setdefault(round(r["py"], 2), []).append(
                os.path.basename(r["path"]))
        violations.append(
            "PILL MOVED VERTICALLY BETWEEN STATES: tops %s -- %s. The pill's "
            "place on screen is not allowed to depend on which message it is "
            "carrying."
            % (", ".join("%.1f" % t for t in tops),
               "; ".join("%.1f: %s" % (t, ", ".join(p))
                         for t, p in sorted(by_top.items()))))
    return violations


# The band the hover gesture lands on, in the authored 1320x860 design space.
BAND_POINT = (400, 300, 520, 300, 6)
PROBE_MS = (200, 600)


def drive(app, out_dir, press_clear):
    """Capture one pass, with or without a CLEAR before the hover."""
    prefix = os.path.join(out_dir, "clear" if press_clear else "baseline")
    env = dict(os.environ)
    env.update(
        PULP_HEADLESS="1",
        PULP_FRAMES="200",
        PULP_SCREENSHOT=prefix + ".png",
        SPECTR_DRAG="%d,%d,%d,%d,%d" % BAND_POINT,
        SPECTR_DRAG_DUMP_PREFIX=prefix,
        SPECTR_STATUS_PROBE_MS=",".join(str(v) for v in PROBE_MS),
    )
    if press_clear:
        env["SPECTR_CLICK"] = '[data-spectr-rail-action="clear"]'
    else:
        env.pop("SPECTR_CLICK", None)
    log = prefix + ".log"
    with open(log, "wb") as fh:
        subprocess.run([app], env=env, stdout=fh, stderr=subprocess.STDOUT,
                       timeout=300)
    # POSITIVE CONTROL on the driver, not decoration. A selector that
    # matches nothing makes the fixture throw before the gesture runs, so
    # the pass produces NO dumps at all -- which the missing-file check
    # below turns into a loud failure rather than a quiet "nothing moved".
    # Verified both ways: `[data-spectr-rail-action="nope"]` yields 0 dumps
    # and one `no element` line; the real selector yields the dumps.
    with open(log, "rb") as fh:
        transcript = fh.read().decode("utf-8", "replace")
    if press_clear and "no element" in transcript:
        raise SystemExit(
            "the CLEAR fixture selector matched nothing (see %s) -- the "
            "gesture under test never happened. Not a pass." % log)
    wanted = [prefix + ".t%d.layout.json" % PROBE_MS[-1]]
    missing = [p for p in wanted if not os.path.exists(p)]
    if missing:
        raise SystemExit(
            "the %s pass produced no %s -- the gesture never reached the "
            "plot, so nothing was measured (see %s). Not a pass."
            % ("clear" if press_clear else "baseline",
               ", ".join(os.path.basename(m) for m in missing), log))
    return wanted


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumps", nargs="*")
    ap.add_argument("--app", default=None,
                    help="drive both passes from this Spectr binary")
    ap.add_argument("--out-dir", default=None)
    ap.add_argument("--plant", choices=("shift", "raise"), default=None,
                    help="mutate every dump after the first so the rule fires")
    a = ap.parse_args()

    if bool(a.app) == bool(a.dumps):
        raise SystemExit("pass DUMPs or --app, not both and not neither")

    tmp = None
    dumps = a.dumps
    if a.app:
        if not os.path.exists(a.app):
            print("INCONCLUSIVE: no binary at %s" % a.app)
            print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. "
                  "Not a pass.")
            return 2
        out_dir = a.out_dir
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        else:
            tmp = tempfile.TemporaryDirectory()
            out_dir = tmp.name
        dumps = drive(a.app, out_dir, False) + drive(a.app, out_dir, True)

    if len(dumps) < 2:
        print("INCONCLUSIVE: %d dump(s). This rule compares states; one state "
              "cannot move." % len(dumps))
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. "
              "Not a pass.")
        return 2

    rows, empty = collect([lc.Dump(p) for p in dumps], a.plant)

    print("dumps with a painted pill : %d" % len(rows))
    for path, py in empty:
        print("  SKIPPED (pill carries no text, y=%.1f): %s"
              % (py, os.path.basename(path)))
    if len(rows) < 2:
        print("RESULT: UNMEASURED -- fewer than two dumps show a painted "
              "pill, so no change of state was compared. Not a pass.")
        return 2

    for r in rows:
        print("  %-46s x=%7.1f y=%7.1f w=%6.1f centre=%7.1f  %r"
              % (os.path.basename(r["path"]), r["px"], r["py"], r["pw"],
                 r["px"] + r["pw"] / 2.0, r["text"]))

    violations = check(rows)
    if violations:
        print("\nFAIL (%d):" % len(violations))
        for v in violations:
            print("  - %s" % v)
        return 1
    print("\nOK: the pill stays centred and keeps the same top across every "
          "state captured.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
