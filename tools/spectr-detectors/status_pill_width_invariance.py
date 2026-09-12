#!/usr/bin/env python3
"""STATUS-PILL-WIDTH: the status pill is sized by the string it PAINTS.

WHAT IT ASSERTS
    Across every dump handed to it -- each captured in a different status
    state -- the pill above the plot (the readout carrying
    `281Hz   10.8 dB   BAND 25/64`, `CLEARED GAINS`, `SNAPSHOT A CAPTURED`)
    must:

      1. fully contain the ink it paints, and
      2. stay centred on the viewport, and
      3. have a width that is a PURE, NON-DECREASING function of the length
         of the string it is painting.

WHY THOSE THREE
    The shipped defect was not centring.  The pill has two writers: React
    commits a message, and `updateLiveHoverStatus` writes the live hover
    reading straight onto the DOM node every frame to avoid a whole-document
    React commit.  Only the first of those re-derived the width, and its
    publish is throttled to 700ms and skipped while the reading is unchanged
    -- so the pill kept the size of whatever string React last saw.  Press
    CLEAR (`CLEARED GAINS`) or click a band (`BAND n MUTED`) -- both 13
    characters, both 132px -- and the next hover reading was painted into
    that 132px box: 173px of ink in a 102px content box, overhanging 35px
    past each edge, for the whole 2.2s hold.

    (1) is the property the user actually reports.  (3) is the mechanism, and
    it catches the small version too -- an 8px lag as a reading's digit count
    changes mid-drag, where the ink still happens to fit.  (2) is the CONTROL
    that separates the two diagnoses: it measured EXACTLY 660.0 in both the
    healthy and the broken capture, which is how this was identified as a
    width defect rather than the centring defect it looks like on screen.
    The Settings copy button was "fixed" three times for centring before the
    same distinction was drawn there; if (2) ever fires, the diagnosis is
    genuinely different and this rule is the wrong one.

WHAT THIS CANNOT SEE
    Whether the pill is a SENSIBLE size -- only whether it agrees with its
    own text.  A rule that sized every pill to 40px would satisfy (3) and
    fail only (1).  It also cannot see a state nobody captured: a dump per
    status state is the whole population, and a dismissed pill (no text
    child) is skipped, so a run of only-dismissed dumps adjudicates nothing
    and says so rather than passing.

USAGE
    status_pill_width_invariance.py DUMP [DUMP ...]
    status_pill_width_invariance.py --app build/Spectr.app/Contents/MacOS/Spectr
    status_pill_width_invariance.py DUMP --plant shrink      # negative control
    status_pill_width_invariance.py DUMP --plant offset      # negative control

`--app` drives the capture itself: a press-and-release on a band, which IS the
mute, which is what publishes the 13-character message.  A buttonless
`simulate_hover` cannot be substituted -- neither mac host delivers a DOM
pointermove, so it never reaches the JS hover path at all.

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
# Its generated id (`__behavior_pr_z`) is an artifact of the lowering and
# renumbers whenever the document is regenerated, so keying on it would rot
# silently; the stacking level is authored.
PILL_Z_INDEX = 6


def find_pill(dump):
    """(pill_index, label_index or None) for the status pill in one dump."""
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
              if dump.parent[i] == pill and (dump.nodes[i].get("measured_text_boxes"))]
    if len(labels) > 1:
        raise SystemExit("%s: the status pill owns %d text children"
                         % (dump.path, len(labels)))
    return pill, (labels[0] if labels else None)


def collect(dumps, plant):
    rows, empty = [], []
    for dump in dumps:
        pill_i, label_i = find_pill(dump)
        px, py, pw, ph = lc.r(dump.nodes[pill_i]["rect"])
        if label_i is None:
            empty.append((dump.path, pw))
            continue
        lx, ly, lw, lh = lc.r(dump.nodes[label_i]["rect"])
        box = dump.nodes[label_i]["measured_text_boxes"][0]
        text = box.get("text") or ""
        ink = lc.r(box["rect"])[2]
        if plant == "shrink":
            # The exact shape that shipped: the pill keeps the width of a
            # 13-character message ("CLEARED GAINS" / "BAND n MUTED") while
            # painting a full hover reading.
            pw, lw = 132.0, 102.0
            px = dump.viewport.get("w", 1320) / 2.0 - pw / 2.0
            lx = px + 15.0
        elif plant == "offset":
            px += 40.0
            lx += 40.0
        rows.append(dict(path=dump.path, text=text, n=len(text),
                         px=px, pw=pw, lx=lx, lw=lw, ink=ink,
                         vw=float(dump.viewport.get("w", 0.0))))
    return rows, empty


def check(rows):
    violations = []
    for r in rows:
        if r["ink"] > r["lw"] + TOL:
            violations.append(
                "INK OVERFLOWS THE PILL: %r paints %.1fpx of ink in a %.1fpx "
                "content box (%.1fpx over, %.1fpx past each edge) -- pill is "
                "%.1f wide (%s)"
                % (r["text"], r["ink"], r["lw"], r["ink"] - r["lw"],
                   (r["ink"] - r["lw"]) / 2.0, r["pw"], os.path.basename(r["path"])))
        if r["vw"] > 0:
            centre = r["vw"] / 2.0
            if abs(r["px"] + r["pw"] / 2.0 - centre) > TOL:
                violations.append(
                    "PILL OFF CENTRE: centre %.1f, viewport centre %.1f (%s) "
                    "-- this is the CONTROL; if it fires the defect is "
                    "centring, not width"
                    % (r["px"] + r["pw"] / 2.0, centre, os.path.basename(r["path"])))
            if abs(r["lx"] + r["lw"] / 2.0 - centre) > TOL:
                violations.append(
                    "TEXT BOX OFF CENTRE: centre %.1f, viewport centre %.1f (%s)"
                    % (r["lx"] + r["lw"] / 2.0, centre, os.path.basename(r["path"])))

    by_len = {}
    for r in rows:
        by_len.setdefault(r["n"], []).append(r)
    for n in sorted(by_len):
        widths = sorted({round(r["pw"], 2) for r in by_len[n]})
        if len(widths) > 1:
            violations.append(
                "WIDTH IS NOT A FUNCTION OF THE PAINTED TEXT: %d-character "
                "readings measure %s -- so something other than the string on "
                "screen is sizing the box (%s)"
                % (n, ", ".join("%.1f" % w for w in widths),
                   ", ".join(sorted({os.path.basename(r["path"]) for r in by_len[n]}))))
    order = sorted(by_len)
    for a, b in zip(order, order[1:]):
        wa = max(r["pw"] for r in by_len[a])
        wb = min(r["pw"] for r in by_len[b])
        if wb < wa - TOL:
            violations.append(
                "WIDTH SHRINKS AS TEXT GROWS: %d characters get %.1f but %d "
                "characters get %.1f" % (a, wa, b, wb))
    return violations, by_len


# The band the gesture lands on, in the authored 1320x860 design space.
BAND_POINT = (400, 300)
# Read the pill 300ms after the release, off the fixture's own clock rather
# than off a frame count: the message holds for 2.2s, so this is inside the
# hold on any host, and it is the SAME instant on a fast box and a slow one.
PROBE_MS = 300


def drive(app, out_dir):
    """Capture the healthy reading and the post-short-message one."""
    prefix = os.path.join(out_dir, "run")
    env = dict(os.environ)
    env.update(
        PULP_HEADLESS="1",
        PULP_FRAMES="240",
        PULP_SCREENSHOT=os.path.join(out_dir, "run.png"),
        SPECTR_DRAG="%d,%d,%d,%d,1" % (BAND_POINT * 2),
        SPECTR_DRAG_DUMP_PREFIX=prefix,
        SPECTR_STATUS_PROBE_MS=str(PROBE_MS),
    )
    log = os.path.join(out_dir, "run.log")
    with open(log, "wb") as fh:
        subprocess.run([app], env=env, stdout=fh, stderr=subprocess.STDOUT,
                       timeout=300)
    before = prefix + ".move1.layout.json"
    after = prefix + ".t%d.layout.json" % PROBE_MS
    missing = [p for p in (before, after) if not os.path.exists(p)]
    if missing:
        raise SystemExit(
            "the probe produced no %s -- the gesture never reached the plot, "
            "so nothing was measured (see %s). Not a pass."
            % (", ".join(os.path.basename(m) for m in missing), log))
    return [before, after]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumps", nargs="*")
    ap.add_argument("--app", default=None,
                    help="drive the capture from this Spectr binary instead of "
                         "reading dumps")
    ap.add_argument("--out-dir", default=None)
    ap.add_argument("--plant", choices=("shrink", "offset"), default=None,
                    help="mutate a healthy input so the rule has to fire")
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
        dumps = drive(a.app, out_dir)

    rows, empty = collect([lc.Dump(p) for p in dumps], a.plant)

    print("dumps with a painted pill : %d" % len(rows))
    for path, pw in empty:
        print("  SKIPPED (pill carries no text, w=%.1f): %s"
              % (pw, os.path.basename(path)))
    if not rows:
        print("RESULT: UNMEASURED -- every dump shows a dismissed pill. "
              "Nothing was adjudicated. Not a pass.")
        return 2

    for r in rows:
        print("  %-44s chars=%3d pill_w=%6.1f pill_x=%6.1f box_w=%6.1f "
              "ink_w=%6.1f slack=%6.1f  %r"
              % (os.path.basename(r["path"]), r["n"], r["pw"], r["px"],
                 r["lw"], r["ink"], r["lw"] - r["ink"], r["text"]))

    violations, by_len = check(rows)
    if len(by_len) < 2:
        print("NOTE: every dump paints a %d-character reading -- the "
              "cross-length half of rule 3 (width must not shrink as text "
              "grows) is NOT exercised; pass dumps from states whose "
              "messages differ in length." % next(iter(by_len)))

    if violations:
        print("\nFAIL (%d):" % len(violations))
        for v in violations:
            print("  - %s" % v)
        return 1
    print("\nOK: the pill contains its ink, stays centred, and its width is a "
          "pure non-decreasing function of the string it paints.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
