#!/usr/bin/env python3
"""Invariants over a drag, sample by sample, and over a resize.

A screenshot is taken after the release, so it can only ever show where a
gesture ENDED. Every claim in the COR rows is about the path instead:

  * an edge drag must never move the opposite trim -- an excursion that comes
    back before the last frame is exactly the defect, and a before/after pair
    cannot see it;
  * a fast sweep must leave no band behind -- a skipped band is a HOLE in the
    set of touched bands, and the endpoint totals look the same either way;
  * a viewport pan must keep its span -- start and end can both be right while
    the span breathes in between.

So each check reads `spectr-gesture-probe-v1`, which records the plugin's own
processing state after every delivered pointer sample, and judges the whole
series.

Every check carries its own positive control, because the failure mode here is
not a wrong answer, it is a vacuous one. "The opposite trim never moved" is
trivially true of a gesture that never happened, and a probe that missed its
target produces exactly that. So a check that asserts something did NOT move
must also assert that the thing being dragged DID, and refuse to report a pass
when it cannot.

Exit codes: 0 green, 1 red, 3 inconclusive (nothing to measure), 4 broken (a
planted negative failed to fire).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

GREEN, RED, INCONCLUSIVE, BROKEN = 0, 1, 3, 4

PLANTS = [
    "opposite-moves",
    "frozen-drag",
    "band-hole",
    "band-shrink",
    "control-moves",
    "span-drift",
    "layout-drift",
    "one-size",
    "profile-flat",
    "profile-kink",
]


def load_probe(path: Path) -> dict:
    data = json.loads(path.read_text())
    if data.get("schema") != "spectr-gesture-probe-v1":
        raise SystemExit(f"{path}: not a spectr-gesture-probe-v1 file")
    return data


def gesture(data: dict, name: str) -> dict | None:
    for g in data.get("gestures", []):
        if g.get("name") == name:
            return g
    return None


def changed_bands(pre: list[float], now: list[float]) -> list[int]:
    return [i for i, (a, b) in enumerate(zip(pre, now)) if a != b]


# ── checks ───────────────────────────────────────────────────────────────────


def check_opposite_trim(g: dict, moving: str, plant: str | None) -> list[str]:
    """One minimap edge is dragged; the other must not move at any sample."""
    frozen = "max_hz" if moving == "min_hz" else "min_hz"
    samples = [s for s in g["samples"] if s["phase"] in ("down", "move", "up")]
    if plant == "opposite-moves":
        samples = [dict(s) for s in samples]
        samples[len(samples) // 2][frozen] *= 1.01
        print(f"  CONTROL: planted a 1% excursion on {frozen} mid-drag")
    if plant == "frozen-drag":
        samples = [dict(s) for s in samples]
        for s in samples:
            s[moving] = samples[0][moving]
        print(f"  CONTROL: planted a {moving} that never moves")

    if len(samples) < 3:
        return [f"INCONCLUSIVE only {len(samples)} delivered samples"]

    failures = []
    # Positive control FIRST. Without it "the other edge never moved" is a
    # statement about a gesture that may not have happened.
    moving_values = {round(s[moving], 6) for s in samples}
    if len(moving_values) < 2:
        failures.append(
            f"the dragged edge {moving} took only {len(moving_values)} distinct "
            f"value(s) across {len(samples)} samples — the gesture did not move "
            f"it, so 'the opposite trim never moved' is vacuous, not a pass"
        )

    frozen_values = {round(s[frozen], 6) for s in samples}
    if len(frozen_values) != 1:
        excursions = [
            (i, s[frozen]) for i, s in enumerate(samples)
            if round(s[frozen], 6) != round(samples[0][frozen], 6)
        ]
        failures.append(
            f"the opposite trim {frozen} moved during the drag: "
            f"{len(excursions)} of {len(samples)} samples differ from "
            f"{samples[0][frozen]} (first at sample {excursions[0][0]} = "
            f"{excursions[0][1]})"
        )
    if not failures:
        print(f"  {moving}: {samples[0][moving]:.4f} -> {samples[-1][moving]:.4f} "
              f"over {len(moving_values)} distinct values")
        print(f"  {frozen}: held at {samples[0][frozen]:.4f} across all "
              f"{len(samples)} delivered samples")
    return failures


def check_no_skipped_bands(g: dict, plant: str | None) -> list[str]:
    """A fast sweep must touch a contiguous, never-shrinking run of bands."""
    samples = g["samples"]
    if not samples:
        return ["INCONCLUSIVE no samples"]
    pre = samples[0]["gain_db"]
    sets = [changed_bands(pre, s["gain_db"]) for s in samples]

    if plant == "band-hole":
        sets = [list(s) for s in sets]
        victim = max(range(len(sets)), key=lambda i: len(sets[i]))
        if len(sets[victim]) >= 3:
            hole = sets[victim][len(sets[victim]) // 2]
            sets[victim] = [b for b in sets[victim] if b != hole]
            print(f"  CONTROL: planted a skipped band {hole} at sample {victim}")
    if plant == "band-shrink":
        sets = [list(s) for s in sets]
        victim = max(range(len(sets)), key=lambda i: len(sets[i]))
        sets[victim] = sets[victim][: max(1, len(sets[victim]) // 2)]
        print(f"  CONTROL: planted a shrinking painted set at sample {victim}")

    failures = []
    final = sets[-1]
    if not final:
        failures.append(
            "no band changed over the whole gesture — the drag did not reach "
            "the band field, so 'no band was skipped' is vacuous"
        )
        return failures

    for i, s in enumerate(sets):
        if not s:
            continue
        span = list(range(s[0], s[-1] + 1))
        if s != span:
            missing = sorted(set(span) - set(s))
            failures.append(
                f"sample {i} touched bands {s[0]}..{s[-1]} but skipped "
                f"{missing} — a fast drag left {len(missing)} band(s) behind"
            )
            break

    prev = 0
    for i, s in enumerate(sets):
        if len(s) < prev:
            failures.append(
                f"sample {i} touches {len(s)} bands after sample {i-1} touched "
                f"{prev} — the painted set shrank, so an earlier edit was "
                f"reverted mid-drag"
            )
            break
        prev = len(s)

    if not failures:
        print(f"  painted {len(final)} contiguous bands {final[0]}..{final[-1]} "
              f"over {len(sets)} delivered samples, never skipping and never "
              f"shrinking")
    return failures


def check_profile_tracks_drag(g: dict, plant: str | None) -> list[str]:
    """A monotone drag must draw a monotone curve.

    "No band was skipped" says every band was touched; it says nothing about
    what they were touched WITH. A drag whose pointer descends steadily across
    the field and leaves a flat shelf behind has painted every band and painted
    them all wrong — the gesture reached the field but stopped tracking. So the
    final profile over the painted run must be monotone in the same direction
    the pointer moved, with as many distinct values as the pointer had
    positions to hand it.
    """
    samples = g["samples"]
    if len(samples) < 3:
        return [f"INCONCLUSIVE only {len(samples)} delivered samples"]
    pre, final = samples[0]["gain_db"], samples[-1]["gain_db"]
    painted = changed_bands(pre, final)
    if not painted:
        return ["no band was painted, so there is no profile to judge"]

    profile = [final[i] for i in painted]
    if plant == "profile-flat":
        profile = [profile[0]] * len(profile)
        print("  CONTROL: planted a flat shelf where the drag descended")
    if plant == "profile-kink":
        profile = list(profile)
        mid = len(profile) // 2
        profile[mid] = profile[0]
        print(f"  CONTROL: planted a non-monotone kink at painted band "
              f"{painted[mid]}")

    dy = samples[-1]["y"] - samples[0]["y"]
    dx = samples[-1]["x"] - samples[0]["x"]
    if abs(dy) < 1.0 or abs(dx) < 1.0:
        return ["INCONCLUSIVE the drag did not move in both axes, so a "
                "profile slope is not defined"]
    # Screen y grows downward and gain grows upward, so a downward-right drag
    # must leave gains falling as the band index rises.
    rising = (dy < 0) == (dx > 0)
    # Non-strict on purpose. Each move repaints the whole span from the
    # previously painted band to the current one at the current value, so the
    # last band and the one before it legitimately share a value at every
    # release — and so does any pair the pointer crossed within one sample.
    # A strict test calls that a defect. What must never happen is a REVERSAL,
    # and a curve that stopped tracking is caught by the distinct-value clause
    # below rather than by pretending every step is a new value.
    ok = all((b >= a) if rising else (b <= a)
             for a, b in zip(profile, profile[1:]))

    failures = []
    if not ok:
        breaks = [painted[i + 1] for i, (a, b) in
                  enumerate(zip(profile, profile[1:]))
                  if not ((b >= a) if rising else (b <= a))]
        failures.append(
            f"the painted profile is not monotone over bands "
            f"{painted[0]}..{painted[-1]}: it reverses at band(s) "
            f"{breaks[:6]}{'...' if len(breaks) > 6 else ''}. The pointer moved "
            f"{dx:+.0f},{dy:+.0f}, so the curve should fall "
            f"{'up' if rising else 'down'} across the run"
        )
    distinct = len({round(v, 4) for v in profile})
    if distinct < max(2, len(profile) // 2):
        failures.append(
            f"only {distinct} distinct values across {len(profile)} painted "
            f"bands — the drag touched every band but stopped tracking the "
            f"pointer, which 'no band was skipped' alone would have passed"
        )
    if not failures:
        print(f"  {len(profile)} painted bands hold {distinct} distinct values, "
              f"monotone {'up' if rising else 'down'} from {profile[0]:.3f} dB "
              f"to {profile[-1]:.3f} dB, matching a pointer that moved "
              f"{dx:+.0f},{dy:+.0f}")
    return failures


def check_span_preserved(g: dict, tol: float, plant: str | None) -> list[str]:
    """A pan slides the window; it must not also zoom it."""
    samples = [s for s in g["samples"] if s["phase"] in ("down", "move", "up")]
    if plant == "span-drift":
        samples = [dict(s) for s in samples]
        samples[len(samples) // 2]["max_hz"] *= 1.10
        print("  CONTROL: planted a 10% span excursion mid-pan")
    if len(samples) < 3:
        return [f"INCONCLUSIVE only {len(samples)} delivered samples"]

    spans = [math.log10(s["max_hz"] / s["min_hz"]) for s in samples]
    failures = []
    if len({round(s["min_hz"], 6) for s in samples}) < 2:
        failures.append(
            "min_hz never moved, so this was not a pan and a preserved span "
            "proves nothing"
        )
    drift = max(spans) - min(spans)
    if drift > tol:
        failures.append(
            f"the viewport span drifted by {drift:.5f} decades during the pan "
            f"(tolerance {tol}); a pan must slide the window, not resize it"
        )
    if not failures:
        print(f"  span held at {spans[0]:.5f} decades (max drift {drift:.2e}) "
              f"while min_hz moved {samples[0]['min_hz']:.2f} -> "
              f"{samples[-1]['min_hz']:.2f}")
    return failures


def check_no_effect(g: dict, plant: str | None) -> list[str]:
    """The off-target control: a drag that misses must change nothing."""
    samples = g["samples"]
    if len(samples) < 3:
        return [f"INCONCLUSIVE only {len(samples)} delivered samples"]
    first, last = samples[0], samples[-1]
    if plant == "control-moves":
        last = dict(last)
        last["min_hz"] *= 1.5
        print("  CONTROL: planted a viewport change on the off-target drag")

    failures = []
    if round(first["min_hz"], 6) != round(last["min_hz"], 6) or \
       round(first["max_hz"], 6) != round(last["max_hz"], 6):
        failures.append(
            f"a drag delivered away from the minimap still moved the viewport "
            f"({first['min_hz']}..{first['max_hz']} -> "
            f"{last['min_hz']}..{last['max_hz']}). The on-target results are "
            f"then not evidence about the minimap — any drag moves it."
        )
    if changed_bands(first["gain_db"], last["gain_db"]):
        failures.append("a drag delivered outside the plot still changed bands")
    if not failures:
        print(f"  off-target drag on '{g['hit_id']}' changed neither the "
              f"viewport nor any band, so the on-target gestures are specific")
    return failures


def check_resize(paths: list[Path], plant: str | None) -> list[str]:
    """Layout and reachability must survive a real host-window resize."""
    rows = []
    for state_path in paths:
        state = json.loads(state_path.read_text())
        base = str(state_path)[: -len(".state.json")]
        layout = Path(base + ".layout.json").read_bytes()
        depths = Path(base + ".depths.json").read_bytes()
        rows.append({
            "name": Path(base).name,
            "host": (state["host"]["w"], state["host"]["h"]),
            "root": (state["root"]["w"], state["root"]["h"]),
            "hz": (round(state["min_hz"], 4), round(state["max_hz"], 4)),
            "layout": hashlib.sha256(layout).hexdigest(),
            "depths": hashlib.sha256(depths).hexdigest(),
        })

    if plant == "layout-drift":
        rows[-1]["layout"] = "planted-different-layout-digest"
        print("  CONTROL: planted a layout tree that differs at one size")
    if plant == "one-size":
        rows = rows[:1]
        print("  CONTROL: planted a single window size (nothing to compare)")

    if len(rows) < 2:
        return ["INCONCLUSIVE fewer than two window sizes — a resize check "
                "needs at least two, and one size cannot show invariance"]
    if len({r["host"] for r in rows}) < 2:
        return ["INCONCLUSIVE every run reports the same host size, so no "
                "resize actually happened"]

    failures = []
    for key, label in (("layout", "layout tree"), ("depths", "depth sidecar"),
                       ("root", "root bounds"), ("hz", "gesture outcome")):
        values = {r[key] for r in rows}
        if len(values) != 1:
            detail = ", ".join(f"{r['name']}={r[key]}" for r in rows)
            failures.append(
                f"the {label} is not identical across window sizes: {detail}"
            )
    if not failures:
        sizes = ", ".join(f"{r['host'][0]}x{r['host'][1]}" for r in rows)
        print(f"  {len(rows)} host sizes ({sizes}) share one layout digest "
              f"{rows[0]['layout'][:12]}, one root {rows[0]['root']}, and one "
              f"gesture outcome {rows[0]['hz']}")
    return failures


# ── driver ───────────────────────────────────────────────────────────────────


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--probe", type=Path,
                    help="a spectr-gesture-probe-v1 JSON file")
    ap.add_argument("--opposite-trim", metavar="GESTURE:min_hz|max_hz",
                    action="append", default=[],
                    help="edge drag GESTURE moves this edge and no other")
    ap.add_argument("--no-skipped-bands", metavar="GESTURE",
                    action="append", default=[])
    ap.add_argument("--span-preserved", metavar="GESTURE",
                    action="append", default=[])
    ap.add_argument("--profile-tracks-drag", metavar="GESTURE",
                    action="append", default=[],
                    help="the painted curve must follow the pointer path, not "
                         "merely cover it")
    ap.add_argument("--no-effect", metavar="GESTURE", action="append",
                    default=[], help="off-target control gesture")
    ap.add_argument("--span-tolerance", type=float, default=1e-3,
                    help="decades of span drift tolerated during a pan")
    ap.add_argument("--resize", type=Path, action="append", default=[],
                    metavar="STATE",
                    help="a spectr-state-v1 *.state.json; its sibling "
                         "*.layout.json and *.depths.json are read too. "
                         "Repeat for each window size.")
    ap.add_argument("--plant", choices=PLANTS,
                    help="force a failure to prove the check can fail")
    args = ap.parse_args()

    wanted = (args.opposite_trim or args.no_skipped_bands
              or args.span_preserved or args.no_effect or args.resize
              or args.profile_tracks_drag)
    if not wanted:
        print("INCONCLUSIVE no check requested", file=sys.stderr)
        return INCONCLUSIVE

    failures: list[str] = []
    inconclusive: list[str] = []

    def record(label: str, results: list[str]) -> None:
        for r in results:
            if r.startswith("INCONCLUSIVE"):
                inconclusive.append(f"{label}: {r}")
            else:
                failures.append(f"{label}: {r}")

    probe = load_probe(args.probe) if args.probe else None

    def need(name: str) -> dict | None:
        if probe is None:
            inconclusive.append(f"{name}: no --probe given")
            return None
        g = gesture(probe, name)
        if g is None:
            failures.append(f"{name}: gesture is absent from the probe file")
            return None
        if not g.get("hit"):
            failures.append(
                f"{name}: the press hit nothing (hit_id={g.get('hit_id')}), so "
                f"no sample in this gesture describes the control under test"
            )
            return None
        return g

    for spec in args.opposite_trim:
        name, _, edge = spec.partition(":")
        if edge not in ("min_hz", "max_hz"):
            print(f"--opposite-trim needs GESTURE:min_hz or GESTURE:max_hz, "
                  f"got {spec!r}", file=sys.stderr)
            return INCONCLUSIVE
        g = need(name)
        if g:
            print(f"[opposite-trim] {name} (dragging {edge})")
            record(f"opposite-trim {name}",
                   check_opposite_trim(g, edge, args.plant))

    for name in args.no_skipped_bands:
        g = need(name)
        if g:
            print(f"[no-skipped-bands] {name}")
            record(f"no-skipped-bands {name}",
                   check_no_skipped_bands(g, args.plant))

    for name in args.profile_tracks_drag:
        g = need(name)
        if g:
            print(f"[profile-tracks-drag] {name}")
            record(f"profile-tracks-drag {name}",
                   check_profile_tracks_drag(g, args.plant))

    for name in args.span_preserved:
        g = need(name)
        if g:
            print(f"[span-preserved] {name}")
            record(f"span-preserved {name}",
                   check_span_preserved(g, args.span_tolerance, args.plant))

    for name in args.no_effect:
        g = need(name)
        if g:
            print(f"[no-effect control] {name}")
            record(f"no-effect {name}", check_no_effect(g, args.plant))

    if args.resize:
        print(f"[resize] {len(args.resize)} window size(s)")
        record("resize", check_resize(args.resize, args.plant))

    for i in inconclusive:
        print(f"  ??   {i}")
    for f in failures:
        print(f"  RED  {f}")

    if failures:
        print(f"RED    {len(failures)} violation(s)")
        return RED
    if inconclusive:
        print(f"INCONCLUSIVE  {len(inconclusive)} check(s) had nothing to "
              f"measure — this is NOT a pass")
        return INCONCLUSIVE
    print("GREEN  all requested gesture invariants hold")
    if args.plant:
        print(f"BROKEN: the planted negative {args.plant!r} did not fail the "
              f"check", file=sys.stderr)
        return BROKEN
    return GREEN


if __name__ == "__main__":
    sys.exit(main())
