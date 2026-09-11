#!/usr/bin/env python3
"""Assert a settings slider thumb grows while the pointer hovers its track.

Reads two `visual-layout-snapshot-v1` dumps from one `SPECTR_HOVER_PROBE` run --
the idle frame and the frame captured with the pointer over a settings slider --
and requires the thumb rect to be strictly larger in the hover frame.

The thumb is located structurally, never by generated node id: a slider track is
a node that owns a hit region, is 14-20px tall and at least 80px wide; its thumb
is the square node painted inside that track.

    slider_thumb_hover_growth.py --idle IDLE.layout.json --hover HOVER.layout.json

`--plant` feeds the idle dump in as the hover dump. A detector that still passes
under `--plant` is measuring nothing, so the plant run MUST fail.
"""
import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as handle:
        doc = json.load(handle)
    if doc.get("schema_version") != "visual-layout-snapshot-v1":
        sys.exit(f"{path}: unexpected schema {doc.get('schema_version')!r}")
    return doc["nodes"]


def tracks(nodes):
    return [n for n in nodes
            if n.get("hit_regions")
            and 14 <= n["rect"]["h"] <= 20
            and n["rect"]["w"] >= 80]


def thumb_in(nodes, track):
    tr = track["rect"]
    best = None
    for n in nodes:
        r = n["rect"]
        if n is track or r["w"] != r["h"] or not 8 <= r["w"] <= 32:
            continue
        if r["x"] < tr["x"] - 12 or r["x"] + r["w"] > tr["x"] + tr["w"] + 12:
            continue
        if r["y"] < tr["y"] - 12 or r["y"] + r["h"] > tr["y"] + tr["h"] + 12:
            continue
        if best is None or r["w"] > best["rect"]["w"]:
            best = n
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--idle", required=True)
    ap.add_argument("--hover", required=True)
    ap.add_argument("--plant", action="store_true",
                    help="negative control: compare the idle dump against itself")
    args = ap.parse_args()

    idle = load(args.idle)
    hover = load(args.idle if args.plant else args.hover)

    idle_tracks = tracks(idle)
    if not idle_tracks:
        sys.exit("no slider track found in the idle dump -- the detector is "
                 "measuring the wrong surface, not reporting an absence")
    print(f"control: {len(idle_tracks)} slider track(s) in the idle dump")

    hover_by_id = {n.get("id"): n for n in hover}
    grew = []
    for track in idle_tracks:
        idle_thumb = thumb_in(idle, track)
        if idle_thumb is None:
            continue
        hover_thumb = hover_by_id.get(idle_thumb.get("id"))
        if hover_thumb is None:
            continue
        a, b = idle_thumb["rect"], hover_thumb["rect"]
        bigger = b["w"] > a["w"] and b["h"] > a["h"]
        print(f"  {idle_thumb['id']}: idle {a['w']}x{a['h']} -> "
              f"hover {b['w']}x{b['h']} {'GREW' if bigger else 'unchanged'}")
        if bigger:
            grew.append(idle_thumb["id"])

    if not grew:
        sys.exit("FAIL: no settings slider thumb grew under hover")
    print(f"PASS: {len(grew)} slider thumb(s) grew under hover")


if __name__ == "__main__":
    main()
