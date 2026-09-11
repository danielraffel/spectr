#!/usr/bin/env python3
"""COPY-WIDTH: the settings copy build-info button holds ONE width in every
feedback state, and does not stretch to its panel.

WHAT IT ASSERTS
    Across every dump handed to it -- each captured in a different copy
    feedback state -- the button that owns the copy label must:

      1. have the SAME width in all of them (state-invariance), and
      2. be NARROWER than the panel that contains it, and
      3. fully contain its own label box.

WHY THOSE THREE
    The shipped defect was not centering: three commits fixed centering and the
    button still changed size, because its style was `alignSelf:"flex-start",
    minWidth:92` -- a width that is a function of the label.  The native
    lowering does not honour `alignSelf`, so in practice it stretched to the
    full 448px About-panel width, and any state whose label outgrew the box
    would have moved the box.  (1) is the property the user actually reports.
    (2) is the exact RED signature that shipped: button width == parent width.
    (3) catches the opposite failure, a fixed width too small for its label.

WHAT THIS CANNOT SEE
    Whether the GLYPHS fit.  The copy label is styled `width:"100%",
    height:"100%"` by the centering fix, which makes the emitter treat it as a
    multi-line label and report `intrinsic_width == 0` -- so it is one of the
    "zero-width/multi-line" runs that painted_vs_measured_width.py skips, and
    no dump-based check can measure its advance.  Ink fit is proven by raster
    instead; see docs/evidence for the COPY UNAVAILABLE capture.

USAGE
    copy_button_width_invariance.py DUMP [DUMP ...] [--expect 136]
    copy_button_width_invariance.py DUMP --plant      # negative control
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout_common as lc  # noqa: E402

COPY_STATES = ("COPY", "COPYING", "COPIED", "COPY UNAVAILABLE")


def find_copy_button(dump):
    """(state, label_index, button_index) for the copy build-info button."""
    hits = []
    for i, node in enumerate(dump.nodes):
        for box in node.get("measured_text_boxes") or []:
            text = (box.get("text") or "").strip()
            if text in COPY_STATES:
                hits.append((text, i))
    if not hits:
        raise SystemExit(
            "%s: no copy build-info label (looked for %s). The button is gated "
            "`info && ...`, so this also goes off when build info never "
            "resolved -- that is a broken instrument, not a pass."
            % (dump.path, "/".join(COPY_STATES))
        )
    if len(hits) > 1:
        raise SystemExit(
            "%s: %d nodes carry a copy-state label (%s); the detector cannot "
            "tell which is the button."
            % (dump.path, len(hits), ", ".join(t for t, _ in hits))
        )
    state, label_i = hits[0]
    if dump.parent is None:
        raise SystemExit("%s: no depths sidecar, ancestry is unavailable" % dump.path)
    button_i = dump.parent[label_i]
    if button_i is None:
        raise SystemExit("%s: copy label has no parent node" % dump.path)
    return state, label_i, button_i


def check(dumps, expect, plant):
    rows, violations = [], []
    for dump in dumps:
        state, label_i, button_i = find_copy_button(dump)
        bx, by, bw, bh = lc.r(dump.nodes[button_i]["rect"])
        lx, ly, lw, lh = lc.r(dump.nodes[label_i]["rect"])
        parent_i = dump.parent[button_i]
        pw = lc.r(dump.nodes[parent_i]["rect"])[2] if parent_i is not None else None
        if plant:
            bw = pw if pw is not None else bw * 2.0
        rows.append(
            dict(path=dump.path, state=state, bw=bw, bh=bh, bx=bx,
                 lw=lw, lx=lx, pw=pw,
                 button=dump.nodes[button_i].get("id"),
                 label=dump.nodes[label_i].get("id"))
        )

    widths = sorted({round(r["bw"], 2) for r in rows})
    if len(widths) > 1:
        violations.append(
            "STATE-DEPENDENT WIDTH: the button measures %s across states %s"
            % (", ".join("%.1f" % w for w in widths),
               ", ".join(r["state"] for r in rows))
        )
    for r_ in rows:
        if r_["pw"] is not None and abs(r_["bw"] - r_["pw"]) < 0.5:
            violations.append(
                "STRETCHED TO PANEL: %s is %.1f wide, the same as its parent "
                "(%.1f) -- alignSelf is not being honoured"
                % (r_["button"], r_["bw"], r_["pw"])
            )
        if r_["lx"] < r_["bx"] - 0.5 or (r_["lx"] + r_["lw"]) > (r_["bx"] + r_["bw"]) + 0.5:
            violations.append(
                "LABEL OVERFLOWS BUTTON: label box [%.1f..%.1f] escapes button "
                "box [%.1f..%.1f] in state %s"
                % (r_["lx"], r_["lx"] + r_["lw"], r_["bx"], r_["bx"] + r_["bw"],
                   r_["state"])
            )
        if expect is not None and abs(r_["bw"] - expect) > 0.5:
            violations.append(
                "WIDTH DRIFT: state %s measures %.1f, expected %.1f"
                % (r_["state"], r_["bw"], expect)
            )
    return rows, violations


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumps", nargs="+")
    ap.add_argument("--expect", type=float, default=None,
                    help="pin the width to a literal (e.g. 136)")
    ap.add_argument("--plant", action="store_true",
                    help="force the button to its parent width, to prove this "
                         "detector can go red")
    a = ap.parse_args()

    dumps = [lc.Dump(p) for p in a.dumps]
    rows, violations = check(dumps, a.expect, a.plant)

    print("dumps     : %d" % len(rows))
    for r_ in rows:
        print("  %-18s state=%-16s button=%s w=%.1f h=%.1f  parent_w=%s  label_w=%.1f"
              % (os.path.basename(r_["path"]), r_["state"], r_["button"],
                 r_["bw"], r_["bh"],
                 "n/a" if r_["pw"] is None else "%.1f" % r_["pw"], r_["lw"]))
    if a.expect is not None:
        print("expected  : %.1f" % a.expect)
    if len(rows) == 1:
        print("NOTE: one dump only -- state-invariance is NOT exercised; "
              "pass a dump per feedback state to assert it.")

    if violations:
        print("\nFAIL (%d):" % len(violations))
        for v in violations:
            print("  - %s" % v)
        return 1
    print("\nOK: copy button width is state-invariant, narrower than its panel, "
          "and contains its label.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
