#!/usr/bin/env python3
"""OVERLAY: the live status banner, judged on the surface a user looks at.

Every check here is deliberately anchored to painted pixels or to the string
the native Label actually carries, never to a value read back out of the DOM
shim. The shim reports what JS wrote; it cannot report what a viewer saw. That
gap is exactly how a "reaches the runtime" pass once stood in for "a user sees
it", and none of these properties survive that substitution:

  CENTER-V   text is vertically centered in the banner. The measured text box
             can be centered while the glyph INK sits high inside it, so the
             box comparison is the wrong instrument and this reads rows of ink.
  RULER      the banner clears the graph's top ruler line. The ruler is painted,
             not laid out -- there is no node for it -- so its y comes from the
             render.
  LIVE       the painted string changes across drag stages BEFORE the release.
             A screenshot cannot be taken mid-gesture, so the evidence is the
             sequence of laid-out trees captured between the pointer verbs.
  HOLD       the newest status stays up a bounded while after its update, then
             goes. Both halves are required: "still up later" alone is also
             what a banner that never hides looks like.
  CLEAN      once hidden, no empty box is left behind. Judged by lit pixels in
             the banner's own region, with the lit frame as the mandatory
             positive control on that identical region -- an unlit reading from
             a mis-aimed window is otherwise indistinguishable from success.
  CENTER-H   the banner is horizontally centered in the viewport, judged
             on the painted border row -- a transform moves paint without
             moving the layout rect, so the rect cannot answer this.

Exit 0 all hold, 1 any violation, 2 the run cannot be judged.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

from PIL import Image

SHELL_ID = "__behavior_pr_z"
TEXT_ID = "__behavior_pr_y"
DEVIATION = 24


def load_nodes(path: str) -> dict:
    doc = json.load(open(path))
    return doc, {n["id"]: n for n in doc["nodes"]}


def banner_text(by_id: dict) -> str:
    node = by_id.get(TEXT_ID)
    if node is None:
        return ""
    boxes = node.get("measured_text_boxes") or []
    return " ".join(b["text"] for b in boxes).strip()


def shell_rect(by_id: dict):
    node = by_id.get(SHELL_ID)
    return None if node is None else node["rect"]


def band_background(values: list[int]) -> int:
    return sorted(values)[len(values) // 10]


def ink_rows(px, x0: int, x1: int, y0: int, y1: int) -> list[int]:
    """Rows in [y0,y1) whose pixels deviate from the row-band background."""
    highs, lows = [], []
    for y in range(y0, y1):
        row = [px[x, y] for x in range(x0, x1)]
        highs.append(max(row))
        lows.append(min(row))
    if not highs:
        return []
    bg = band_background(highs)
    return [
        y0 + i
        for i in range(len(highs))
        if (highs[i] - bg) > DEVIATION or (bg - lows[i]) > DEVIATION
    ]


def lit_pixels(px, x0: int, x1: int, y0: int, y1: int, floor: int):
    count, peak = 0, 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            v = px[x, y]
            if v > floor:
                count += 1
            peak = max(peak, v)
    return count, peak


def ruler_y(px, width: int, x0: int, x1: int, y0: int, y1: int, lit: int):
    """The topmost near-full-width horizontal rule in the search band."""
    best = None
    span = x1 - x0
    for y in range(y0, y1):
        run = sum(1 for x in range(x0, x1) if px[x, y] > lit)
        if run > span * 0.8:
            best = y
            break
    return best


def painted_span(px, img_w, img_h, rect, scale, floor):
    """Horizontal extent of the banner as PAINTED, from its own border rule.

    The banner's border is a solid horizontal rule one pixel tall, so it shows
    up as an unbroken run of lit pixels on some row of the banner's vertical
    band. Two things make picking that run harder than it looks. The run has to
    be contiguous -- the first and last lit columns of the whole row belong to
    unrelated chrome further out in the window, and reading those reports the
    centre of the window's furniture rather than of the banner. And the widest
    run in the band is not necessarily the banner: if the banner is painted
    over another rule, that rule is wider and wins.

    So the candidate is chosen by WIDTH agreement with the banner's own layout
    rect, which is what identifies a run as this view rather than a neighbour,
    and its POSITION -- the thing actually under test -- is left free to be
    wrong. When no run's width comes close, the banner cannot be identified and
    nothing is reported rather than something being measured.
    """
    MIN_EDGE_PX = 50
    want = rect["w"] * scale
    y_lo = max(0, int(rect["y"] * scale) - 3)
    y_hi = min(img_h, int((rect["y"] + rect["h"]) * scale) + 4)
    best = None
    for y in range(y_lo, y_hi):
        start = None
        for x in range(img_w + 1):
            lit = x < img_w and px[x, y] > floor
            if lit:
                if start is None:
                    start = x
            elif start is not None:
                run = x - start
                if run >= MIN_EDGE_PX:
                    err = abs(run - want)
                    if best is None or err < best[4]:
                        best = (start, x - 1, y, run, err)
                start = None
    if best is None or best[4] > want * 0.5:
        return None
    return best[:4]


def check_center_v(px, rect, scale, tol, inset, out):
    """Vertical ink centring, measured inside the banner's own border.

    The scan window is inset because the border is drawn in the same bright
    ink colour as the glyphs: scanning the full rect makes the top and bottom
    border rows read as ink, which pins the measured centre to the box centre
    and turns this check into an unfailable tautology.
    """
    x0 = int((rect["x"] + inset) * scale)
    x1 = int((rect["x"] + rect["w"] - inset) * scale)
    y0 = int((rect["y"] + inset) * scale)
    y1 = int((rect["y"] + rect["h"] - inset) * scale)
    rows = ink_rows(px, x0, x1, y0, y1)
    if not rows:
        out.append(("CENTER-V", None, "no ink in the banner rect -- cannot judge"))
        return None
    ink_mid = (min(rows) + max(rows) + 1) / 2.0
    box_mid = (rect["y"] + rect["h"] / 2.0) * scale
    delta = (ink_mid - box_mid) / scale
    detail = (
        f"ink rows {min(rows)}..{max(rows)} in box {y0}..{y1}; "
        f"offset {delta:+.1f} design px (tol {tol})"
    )
    out.append(("CENTER-V", abs(delta) <= tol, detail))
    return delta


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stages", required=True,
                    help="dump prefix of the LIVE drag run (…/g)")
    ap.add_argument("--lit", required=True, help="PNG captured with the banner up")
    ap.add_argument("--lit-snapshot", required=True,
                    help="layout dump describing that same frame")
    ap.add_argument("--hold-stages", required=True,
                    help="dump prefix of the timed-hold run (…/s)")
    ap.add_argument("--gone", required=True, help="PNG captured after the hold expired")
    ap.add_argument("--scale", type=float, default=1.5,
                    help="design px -> image px")
    ap.add_argument("--center-tol", type=float, default=1.5,
                    help="allowed centering error, design px")
    ap.add_argument("--hold-min-ms", type=float, default=1500.0,
                    help="the banner must still be up at or past this offset")
    ap.add_argument("--hold-max-ms", type=float, default=3300.0,
                    help="the banner must be gone by this offset")
    ap.add_argument("--ink-inset", type=float, default=2.0,
                    help="design px trimmed off every side before scanning for "
                         "glyph ink, so the banner border is not read as ink")
    ap.add_argument("--lit-floor", type=int, default=26,
                    help="luminance above which a pixel counts as lit")
    args = ap.parse_args()

    results: list[tuple[str, bool | None, str]] = []

    lit_img = Image.open(args.lit).convert("L")
    lit_px = lit_img.load()
    _, lit_by_id = load_nodes(args.lit_snapshot)
    rect = shell_rect(lit_by_id)
    if rect is None:
        print("cannot judge: no status shell in the lit snapshot", file=sys.stderr)
        return 2
    if not banner_text(lit_by_id):
        print("cannot judge: the lit snapshot carries no banner text", file=sys.stderr)
        return 2

    # Both pixel checks below are only meaningful while the banner is actually
    # painted. The shell keeps its layout rect when it has nothing to say, so a
    # frame with the banner hidden still has a rect to scan -- and scanning it
    # measures whatever sits BEHIND the transparent shell, which reads as ink
    # and can score as perfectly centred. Identifying the banner's own painted
    # border first is what separates "centred" from "not there".
    span = painted_span(lit_px, lit_img.size[0], lit_img.size[1], rect,
                        args.scale, args.lit_floor)

    if span is None:
        results.append(("CENTER-V", None,
                        "the banner is not painted in this frame -- cannot judge"))
    else:
        check_center_v(lit_px, rect, args.scale, args.center_tol, args.ink_inset,
                       results)

    # CENTER-H, measured on the PAINTED banner rather than on its layout rect.
    # The two are not interchangeable: a CSS transform moves a view at paint
    # time and leaves the rect where it was, so a rect-based reading would call
    # a visibly centred banner broken -- and would equally miss a rect that is
    # centred while the paint is not. The border row is the thing a viewer sees,
    # so it is the thing measured. The rect is printed beside it, unjudged, so a
    # divergence between layout and paint is visible instead of silent.
    doc, _ = load_nodes(args.lit_snapshot)
    view_w = float(doc["viewport"]["w"])
    if span is None:
        results.append(("CENTER-H", None,
                        "no painted banner edge found -- cannot judge"))
    else:
        x0, x1, row, count = span
        paint_mid = (x0 + x1 + 1) / 2.0 / args.scale
        dx = paint_mid - view_w / 2.0
        rect_mid = rect["x"] + rect["w"] / 2.0
        results.append(("CENTER-H", abs(dx) <= args.center_tol,
                        f"painted centre x={paint_mid:.1f} vs viewport centre "
                        f"{view_w / 2:.1f}; offset {dx:+.1f} design px "
                        f"(edge row y={row}, {count} px wide; layout rect "
                        f"centre {rect_mid:.1f})"))

    # RULER, measured on the image where the banner is GONE. Two earlier forms
    # of this check could not fail. Searching only above the banner and then
    # asserting the rule was found above it is true by construction. Searching
    # the lit image instead trades that for a blind spot: the banner paints an
    # opaque background, so a banner dropped onto the ruler covers enough of it
    # to put the rule under the full-width threshold, and the collision reports
    # as "no rule found" rather than as the overlap it is. The ruler is static
    # chrome in the same window, so the frame where the banner has cleared
    # shows it unoccluded, and the banner's rect can then be compared against
    # it honestly.
    top_px = int(rect["y"] * args.scale)
    bottom_px = int((rect["y"] + rect["h"]) * args.scale)
    ruler_img = Image.open(args.gone).convert("L")
    ruler_src = ruler_img.load()
    ry = ruler_y(ruler_src, ruler_img.size[0], 0, ruler_img.size[0], 0,
                 bottom_px, args.lit_floor)
    if ry is None:
        results.append(("RULER", None,
                        "no full-width rule found above the banner's bottom"))
    else:
        results.append(("RULER", ry < top_px,
                        f"ruler at image y={ry} (design {ry / args.scale:.1f}), "
                        f"banner top at image y={top_px} "
                        f"(design {rect['y']:.1f})"))

    # LIVE. Only stages strictly before the release count: a difference that
    # appears only at release is precisely the defect this row names.
    seen: list[str] = []
    for i in range(1, 32):
        path = f"{args.stages}.move{i}.layout.json"
        if not os.path.exists(path):
            break
        _, by_id = load_nodes(path)
        seen.append(banner_text(by_id))
    if len(seen) < 2:
        results.append(("LIVE", None,
                        f"only {len(seen)} pre-release move stage(s) -- cannot judge"))
    else:
        distinct = len(set(seen))
        results.append(("LIVE", distinct >= 2,
                        f"{distinct} distinct painted strings across "
                        f"{len(seen)} pre-release moves"))

    # HOLD. Both halves, from the timed probes of the second run.
    probes: list[tuple[float, str]] = []
    prefix_dir = os.path.dirname(args.hold_stages) or "."
    base = os.path.basename(args.hold_stages)
    for name in os.listdir(prefix_dir):
        if not name.startswith(base + ".t") or not name.endswith(".layout.json"):
            continue
        tag = name[len(base) + 2:-len(".layout.json")]
        try:
            offset = float(tag)
        except ValueError:
            continue
        _, by_id = load_nodes(os.path.join(prefix_dir, name))
        probes.append((offset, banner_text(by_id)))
    probes.sort()
    if not probes:
        results.append(("HOLD", None, "no timed probes found -- cannot judge"))
    else:
        late_up = [t for t, s in probes if s and t >= args.hold_min_ms]
        gone = [t for t, s in probes if not s and t <= args.hold_max_ms]
        last_up = max([t for t, s in probes if s], default=None)
        first_gone = min([t for t, s in probes if not s], default=None)
        ok = bool(late_up) and bool(gone)
        results.append(("HOLD", ok,
                        f"populated through t={last_up}ms, empty from "
                        f"t={first_gone}ms (need up at >= {args.hold_min_ms:.0f} "
                        f"and gone by <= {args.hold_max_ms:.0f})"))

    # CLEAN, with its positive control. The control is not decoration: an
    # unlit reading from a window aimed at the wrong rows looks identical to a
    # banner that vanished properly.
    gone_img = Image.open(args.gone).convert("L")
    gone_px = gone_img.load()
    x0 = int(rect["x"] * args.scale)
    x1 = int((rect["x"] + rect["w"]) * args.scale)
    y0 = int(rect["y"] * args.scale)
    y1 = int((rect["y"] + rect["h"]) * args.scale)
    ctl_count, ctl_peak = lit_pixels(lit_px, x0, x1, y0, y1, args.lit_floor)
    count, peak = lit_pixels(gone_px, x0, x1, y0, y1, args.lit_floor)
    if ctl_count < 200:
        results.append(("CLEAN", None,
                        f"positive control is dark too ({ctl_count} lit px) -- "
                        "the window is mis-aimed, reporting nothing"))
    else:
        results.append(("CLEAN", count * 4 < ctl_count,
                        f"{count} lit px (peak {peak}) vs control "
                        f"{ctl_count} (peak {ctl_peak}) on the same region"))

    width = max(len(name) for name, _, _ in results)
    failed = 0
    unjudged = 0
    for name, ok, detail in results:
        if ok is None:
            verdict, unjudged = "SKIP", unjudged + 1
        elif ok:
            verdict = "PASS"
        else:
            verdict, failed = "FAIL", failed + 1
        print(f"{verdict}  {name.ljust(width)}  {detail}")

    if unjudged:
        print(f"\n{unjudged} check(s) could not be judged.", file=sys.stderr)
    if failed:
        print(f"\n{failed} violation(s).", file=sys.stderr)
        return 1
    return 2 if unjudged else 0


if __name__ == "__main__":
    raise SystemExit(main())
