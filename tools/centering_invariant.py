#!/usr/bin/env python3
"""CENTER: a text box must sit centered inside the element that owns it.

Ancestry is exact, not inferred: the layout snapshot names a text child
`<id>__text`, so `__behavior_pr_g8__text` is the text of `__behavior_pr_g8`.
No geometric guessing, so a nested button can never be mistaken for its row.

The planted negative is mandatory. A centering check that cannot be made to
go red is measuring nothing, and this file exists because a green run on the
wrong property is the failure mode being guarded against.
"""
import argparse, json, sys

def rect(r):
    return (float(r["x"]), float(r["y"]), float(r["w"]), float(r["h"]))

def load(path):
    doc = json.load(open(path))
    return doc, {n["id"]: n for n in doc["nodes"]}

def pairs(by_id):
    """(owner, text_node) for every exact `<id>__text` child."""
    out = []
    for nid, n in by_id.items():
        if not nid.endswith("__text"):
            continue
        owner = by_id.get(nid[: -len("__text")])
        if owner is None:
            continue
        boxes = n.get("measured_text_boxes") or []
        if not boxes:
            continue
        out.append((owner, n, boxes[0]))
    return sorted(out, key=lambda p: p[0]["id"])

def check(by_id, tol, only_visible=True):
    viol, seen = [], 0
    for owner, tnode, box in pairs(by_id):
        if only_visible and not owner.get("visible", True):
            continue
        ox, oy, ow, oh = rect(owner["rect"])
        tx, ty, tw, th = rect(tnode["rect"])
        if ow <= 0 or oh <= 0:
            continue
        seen += 1
        dx = (tx + tw / 2.0) - (ox + ow / 2.0)
        dy = (ty + th / 2.0) - (oy + oh / 2.0)
        text = (box.get("text") or "").strip()
        if abs(dx) > tol or abs(dy) > tol:
            viol.append((owner["id"], text, dx, dy, ow, oh))
    return viol, seen

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("snapshot")
    ap.add_argument("--tolerance", type=float, default=1.0)
    ap.add_argument("--plant", action="store_true",
                    help="offset one text box to prove the detector can fail")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    doc, by_id = load(a.snapshot)
    if a.plant:
        # Plant only into a pair the check will actually visit. Planting
        # into a node the checker skips (an invisible one, say) produces a
        # green run that looks like a working control and is not one.
        ps = [p for p in pairs(by_id)
              if p[0].get("visible", True)
              and float(p[0]["rect"]["w"]) > 0
              and float(p[0]["rect"]["h"]) > 0]
        if not ps:
            print("BROKEN: no visible pair to plant into", file=sys.stderr)
            return 4
        owner, tnode, _ = ps[0]
        tnode["rect"]["x"] = float(tnode["rect"]["x"]) + 25.0
        print(f"CONTROL: planted CENTER: shifted {tnode['id']} right by 25px")

    viol, seen = check(by_id, a.tolerance)
    name = a.snapshot.rsplit("/", 1)[-1].replace(".layout.json", "")
    print(f"surface={name}  owner/text pairs checked={seen}  tol={a.tolerance}px")
    if a.json:
        print(json.dumps([{"id": v[0], "text": v[1], "dx": v[2], "dy": v[3]}
                          for v in viol], indent=1))
    for oid, text, dx, dy, ow, oh in viol:
        axes = []
        if abs(dx) > a.tolerance: axes.append(f"dx={dx:+.2f}px")
        if abs(dy) > a.tolerance: axes.append(f"dy={dy:+.2f}px")
        print(f"  RED  CENTER: {oid} {text!r} off-center by "
              f"{', '.join(axes)} in a {ow:.1f}x{oh:.1f} box")
    if a.plant and not viol:
        print("BROKEN: the planted negative did not redden the detector")
        return 4
    if viol:
        print(f"RED    {len(viol)} violation(s)  [CENTER={len(viol)}]")
        return 1
    print("GREEN  every owned text box is centered in its element")
    return 0

sys.exit(main())
