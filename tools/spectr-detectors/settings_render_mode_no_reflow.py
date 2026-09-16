#!/usr/bin/env python3
"""Switching the Latency mode must not move one pixel of the Settings panel.

THE REPORT. "The settings jump when switching between metering or tracking."
Selecting Tracking after Mixing shifted APPEARANCE, STRUCTURE and every group
below LATENCY up by 9px, and selecting Mixing shifted them back.

WHY THIS FILE EXISTS RATHER THAN ONE MORE ARTIFACT ASSERTION. The reserve that
was supposed to fix this already had a test. That test read the DECLARED
constant -- `style.minHeight` -- out of the document and required the two
selections to declare the same one. They always did: it is one literal. It
would have read `52` and `52`, and PASSED, while the panel moved 9px; it would
read `52` and `52` and pass if the panel moved 200px. A reserve is a claim
about RENDERED height, and only the renderer can settle it.

So this drives the shipping standalone, opens Settings, presses each chip, and
compares the two settled layouts box by box. The verdict is geometry, not
presence and not a declaration.

WHAT IT ASSERTS
  1. The switch actually happened -- the visible guidance line differs between
     the two captures. Without this the gate would compare Mixing to Mixing
     whenever the press silently missed, which is a vacuous pass that looks
     exactly like a clean one.
  2. Every box in the Settings subtree has the same x, y and w in both modes,
     and the same h -- with ONE exception, the out-of-flow line of guidance
     text itself, whose own height follows its own string. That exception is
     safe only because its PARENT's height is asserted identical: a box that
     escaped the reserve and pushed the row would move the parent and fail.
  3. Each mode's guidance text FITS the reserved box. The reserve is sized from
     the longest option's string, which is a proxy for the tallest rendering;
     if that proxy ever picks wrong the text would overflow the box instead of
     growing it, and the rule above could not see it. This one can.
  4. The reserve is a real height -- non-zero and at least as tall as the
     taller of the two measured strings. Two equal zeros satisfy rule 2 while
     reserving nothing.

Usage:
  settings_render_mode_no_reflow.py --app <Spectr binary> [--out-dir DIR]
                                    [--emit-receipt PATH]
  settings_render_mode_no_reflow.py --receipt <receipt.json> [--plant NAME]

Exit codes: 0 pass, 1 fail, 2 harness/instrument error.

**EXIT 2 IS NOT A PASS.** It means no verdict was reached -- the panel never
opened, the press never landed, or the two captures are not comparable. A run
that ends in 2 has adjudicated nothing.
"""

import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout_common  # noqa: E402

# The two guidance lines, by their opening words. These are the anchor for
# "which mode is this capture showing", and they are deliberately read from the
# RENDER rather than from the document: a capture whose press missed shows the
# other one, and that is the only way to tell a real comparison from a vacuous
# one.
MIXING_MARK = "Deepest cuts"
TRACKING_MARK = "Plays in time"

CHIP_SELECTOR = '[data-spectr-setting-option="%s"]'
EPS = 0.01
# A line of guidance may sit up to this far outside its reserved box before it
# is called an overflow. Half a pixel of rounding in the text shaper is not a
# clipped sentence.
FIT_SLACK = 0.5


def capture(app, name, out_dir, clicks):
    png = os.path.join(out_dir, name + ".png")
    dump = os.path.join(out_dir, name + ".layout.json")
    depths = os.path.join(out_dir, name + ".depths.json")
    # Remove any artifact from a previous run BEFORE launching. Without this a
    # launch that produces nothing silently adjudicates the PREVIOUS run's
    # capture and reports a confident verdict about code no longer under test.
    for stale in (png, dump, depths):
        if os.path.exists(stale):
            os.remove(stale)
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1",
        "PULP_SCREENSHOT": png,
        "PULP_FRAMES": "90",
        "SPECTR_OPEN_SETTINGS": "1",
        "SPECTR_LAYOUT_DUMP": dump,
    })
    if clicks:
        env["SPECTR_CLICK"] = clicks
    else:
        env.pop("SPECTR_CLICK", None)
    log = os.path.join(out_dir, name + ".log")
    with open(log, "wb") as fh:
        subprocess.run([app], env=env, stdout=fh, stderr=subprocess.STDOUT,
                       timeout=300)
    if not os.path.exists(dump):
        raise SystemExit("no layout dump produced for '%s' (see %s)"
                         % (name, log))
    return dump


def texts(node):
    return [b.get("text", "") for b in (node.get("measured_text_boxes") or [])]


def settings_subtree(dump):
    """Indices of the Settings panel's subtree, and the panel's own index.

    The panel is found from its scroll viewport, which is the only scrolling
    node on this surface -- the same landmark the scroll-track detector uses.
    Ancestry comes from the depths sidecar; rect containment cannot be used,
    because a scrolled row routinely escapes its parent's bounds.
    """
    scrollers = [i for i, n in enumerate(dump.nodes)
                 if (n.get("overflow") or "") in layout_common.SCROLL_OVERFLOWS]
    if len(scrollers) != 1:
        raise SystemExit("expected exactly one scrolling node, found %d -- "
                         "the Settings panel is probably not open"
                         % len(scrollers))
    root = scrollers[0]
    idx = [root]
    for i in range(len(dump.nodes)):
        if i != root and root in dump.ancestors(i):
            idx.append(i)
    return idx, root


def compact(dump_path):
    """Reduce one capture to the Settings subtree, as a committable receipt."""
    d = layout_common.Dump(dump_path)
    if d.parent is None:
        raise SystemExit("no depths sidecar beside %s; ancestry is "
                         "unrecoverable and no verdict is possible" % dump_path)
    idx, _root = settings_subtree(d)
    nodes = []
    for i in idx:
        n = d.nodes[i]
        r = n.get("rect") or {}
        row = {
            "i": i,
            "id": n.get("id") or "",
            "type": n.get("type") or "",
            "parent": d.parent[i],
            "rect": {k: round(float(r.get(k, 0.0)), 4) for k in ("x", "y", "w", "h")},
        }
        # Most boxes carry no text. Omitting the empty ones keeps the committed
        # receipt readable and roughly a third the size; every reader below
        # goes through text_of()/rect_of(), which supply the default.
        t = " | ".join(texts(n))
        if t:
            row["text"] = t
        nodes.append(row)
    return {"viewport": d.viewport, "nodes": nodes}


def mode_of(state):
    """Which mode a capture is SHOWING, read from the rendered guidance line.

    The sizer -- the transparent copy that reserves the height -- always
    carries the longest option's string, so "the capture contains the Mixing
    sentence" is true in both modes and cannot be the signal. The signal is the
    node whose text differs between captures, which is the visible line.
    """
    seen = set()
    for n in state["nodes"]:
        if MIXING_MARK in n.get("text", ""):
            seen.add("mixing")
        if TRACKING_MARK in n.get("text", ""):
            seen.add("tracking")
    return seen


def hint_nodes(state):
    """(visible-line node, its parent node) for the guidance line on show.

    The visible line is the guidance node that is positioned out of flow; it is
    identified as the node carrying a guidance sentence whose parent ALSO
    carries one (the sizer's twin). Returns the deepest such pair.
    """
    by_i = {n["i"]: n for n in state["nodes"]}
    cands = [n for n in state["nodes"]
             if MIXING_MARK in n.get("text", "")
             or TRACKING_MARK in n.get("text", "")]
    out = []
    for n in cands:
        p = by_i.get(n["parent"])
        if p is not None:
            out.append((n, p))
    return out


def adjudicate(mixing, tracking, note):
    failures = []

    def check(name, ok, detail=""):
        if ok:
            print("  PASS %s" % name)
        else:
            print("  FAIL %s  %s" % (name, detail))
            failures.append(name)

    # ---- INSTRUMENT: the two captures must be comparable at all -----------
    if len(mixing["nodes"]) != len(tracking["nodes"]):
        print("INSTRUMENT: the two captures describe different trees "
              "(%d vs %d Settings nodes); no verdict"
              % (len(mixing["nodes"]), len(tracking["nodes"])))
        return 2
    if not mixing["nodes"]:
        print("INSTRUMENT: the Settings subtree is empty; no verdict")
        return 2

    # ---- 1. THE SWITCH ACTUALLY HAPPENED ---------------------------------
    m_seen, t_seen = mode_of(mixing), mode_of(tracking)
    m_hints = hint_nodes(mixing)
    t_hints = hint_nodes(tracking)
    if not m_hints or not t_hints:
        print("INSTRUMENT: no guidance line found in one of the captures "
              "(mixing=%d tracking=%d); the Latency control did not render, "
              "so nothing was measured" % (len(m_hints), len(t_hints)))
        return 2
    m_text = " ".join(n.get("text", "") for n, _ in m_hints)
    t_text = " ".join(n.get("text", "") for n, _ in t_hints)
    switched = (m_text != t_text)
    if not switched:
        print("INSTRUMENT: both captures show the same guidance text, so the "
              "Tracking press never landed. Comparing a capture with itself "
              "would pass whatever the panel does; no verdict.")
        print("  text = %r" % m_text[:120])
        return 2
    check("the captures show different modes (the press landed)",
          "mixing" in m_seen and "tracking" in t_seen,
          "mixing-capture=%s tracking-capture=%s" % (sorted(m_seen), sorted(t_seen)))

    # ---- 2. THE GATE: no box moves ---------------------------------------
    # The visible guidance line is out of flow, so its OWN height follows its
    # own string. Everything else -- including that node's parent -- must be
    # identical, which is what makes the exception safe.
    exempt_h = {n["i"] for n, _ in t_hints
                if n.get("text") != next((x.get("text") for x, _ in m_hints
                                          if x["i"] == n["i"]), None)}
    # The exemption is only safe while the exempt node's PARENT is itself
    # under the height rule -- that is what stops a box from escaping the
    # reserve and pushing the row while hiding inside the exception. Prove the
    # parent is actually in the compared set rather than assuming it.
    compared = {n["i"] for n in tracking["nodes"]}
    for n, parent in t_hints:
        if n["i"] in exempt_h and parent["i"] not in compared:
            print("INSTRUMENT: the out-of-flow guidance line #%d is exempt "
                  "from the height rule but its parent #%d is not compared, "
                  "so the exemption guards nothing; no verdict"
                  % (n["i"], parent["i"]))
            return 2

    moved = []
    resized = []
    for a, b in zip(mixing["nodes"], tracking["nodes"]):
        if a["i"] != b["i"]:
            print("INSTRUMENT: node order differs at %d/%d; no verdict"
                  % (a["i"], b["i"]))
            return 2
        for k in ("x", "y", "w"):
            if abs(a["rect"][k] - b["rect"][k]) > EPS:
                moved.append((a, b, k))
        if abs(a["rect"]["h"] - b["rect"]["h"]) > EPS and a["i"] not in exempt_h:
            resized.append((a, b))
    check("no Settings box moves when the mode changes", not moved,
          "; ".join("#%d %s %s: %.2f -> %.2f" % (a["i"], a["id"], k,
                                                 a["rect"][k], b["rect"][k])
                    for a, b, k in moved[:6])
          + (" (+%d more)" % (len(moved) - 6) if len(moved) > 6 else ""))
    check("no Settings box changes height when the mode changes", not resized,
          "; ".join("#%d %s h: %.2f -> %.2f" % (a["i"], a["id"],
                                                a["rect"]["h"], b["rect"]["h"])
                    for a, b in resized[:6])
          + (" (+%d more)" % (len(resized) - 6) if len(resized) > 6 else ""))

    # ---- 3. THE TEXT FITS THE RESERVE IN EACH MODE ------------------------
    # Guards the proxy: the reserve is sized from the LONGEST string, which is
    # a stand-in for the TALLEST rendering. If that ever picks wrong, the
    # out-of-flow text overflows its box instead of growing it, and rule 2
    # above stays green while the sentence runs into the row below.
    for label, state in (("Mixing", mixing), ("Tracking", tracking)):
        worst = None
        for n, p in hint_nodes(state):
            over = n["rect"]["h"] - p["rect"]["h"]
            if worst is None or over > worst[0]:
                worst = (over, n, p)
        if worst is None:
            check("%s: a reserved box encloses the guidance line" % label, False,
                  "no guidance line found")
            continue
        over, n, p = worst
        check("%s: the guidance line fits its reserved box" % label,
              over <= FIT_SLACK,
              "line h=%.2f exceeds reserve h=%.2f by %.2f (#%d in #%d)"
              % (n["rect"]["h"], p["rect"]["h"], over, n["i"], p["i"]))

    # ---- 4. THE RESERVE IS A REAL HEIGHT ----------------------------------
    # Two equal zeros would satisfy rule 2 and reserve nothing at all.
    reserves = [p["rect"]["h"] for _, p in hint_nodes(mixing)]
    tallest_text = max([n["rect"]["h"] for n, _ in hint_nodes(mixing)]
                       + [n["rect"]["h"] for n, _ in hint_nodes(tracking)])
    check("the reserve is a real height, not zero",
          bool(reserves) and max(reserves) > 0, str(reserves))
    check("the reserve is at least as tall as the taller guidance line",
          bool(reserves) and max(reserves) + FIT_SLACK >= tallest_text,
          "reserve=%s tallest line=%.2f" % (reserves, tallest_text))

    print("")
    if note:
        print(note)
    if failures:
        print("%d check(s) failed: %s" % (len(failures), ", ".join(failures)))
        return 1
    print("all checks passed")
    return 0


PLANTS = {
    # The shipped defect, reproduced from the receipt: the reserve was a typed
    # constant 9px shy of what the taller option renders, so the row -- and
    # every group below it -- was as tall as whatever happened to be selected.
    "fixed-reserve-too-small": "reinstate the pre-fix geometry",
}


def plant_receipt(receipt, name):
    """Rebuild a receipt so it describes the defect, for a negative control."""
    if name != "fixed-reserve-too-small":
        raise SystemExit("unknown plant %r; known: %s"
                         % (name, ", ".join(sorted(PLANTS))))
    pre = receipt.get("pre_fix")
    if not pre:
        raise SystemExit("this receipt carries no 'pre_fix' pair, so the "
                         "control cannot reinstate the defect it names")
    return pre["mixing"], pre["tracking"]


def verdict(rc, expect_fail):
    if not expect_fail:
        return rc
    if rc == 1:
        print("CONTROL OK: the planted defect was rejected")
        return 0
    if rc == 0:
        print("CONTROL FAILED: the suite ACCEPTED the planted capture pair, "
              "so the rules it was meant to exercise prove nothing")
        return 1
    print("CONTROL INCONCLUSIVE: the run reached no verdict (exit %d), which "
          "is not the same as rejecting the defect" % rc)
    return 1


def write_receipt(path, payload):
    """One node per line: compact enough to commit, still diffable by eye."""
    def state(st):
        rows = ",\n   ".join(json.dumps(n, separators=(",", ":"))
                             for n in st["nodes"])
        return ('{"viewport": %s,\n  "nodes": [\n   %s\n  ]}'
                % (json.dumps(st["viewport"], separators=(",", ":")), rows))
    parts = []
    for key in ("mixing", "tracking", "pre_fix"):
        if key not in payload:
            continue
        if key == "pre_fix":
            inner = ",\n ".join('"%s": %s' % (k, state(payload[key][k]))
                                for k in ("mixing", "tracking"))
            parts.append(' "pre_fix": {\n %s\n }' % inner)
        else:
            parts.append(' "%s": %s' % (key, state(payload[key])))
    with open(path, "w") as fh:
        fh.write("{\n" + ",\n".join(parts) + "\n}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app")
    ap.add_argument("--out-dir", default="/tmp/spectr-render-mode-reflow")
    ap.add_argument("--receipt")
    ap.add_argument("--emit-receipt")
    ap.add_argument("--plant", choices=sorted(PLANTS))
    # Inverts the verdict for a control row. This lives here rather than in
    # CTest's WILL_FAIL because WILL_FAIL accepts ANY non-zero exit, so a
    # missing receipt or a usage error would read green while proving nothing
    # -- and a control that cannot tell "rejected the defect" from "never ran"
    # is not a control. Only an exact verdict of 1 counts.
    ap.add_argument("--expect-fail", action="store_true")
    args = ap.parse_args()

    if args.receipt:
        with open(args.receipt) as fh:
            receipt = json.load(fh)
        if args.plant:
            mixing, tracking = plant_receipt(receipt, args.plant)
            note = ("CONTROL: adjudicated the pre-fix capture pair recorded in "
                    "%s" % os.path.basename(args.receipt))
        else:
            mixing, tracking = receipt["mixing"], receipt["tracking"]
            note = "receipt: %s" % os.path.basename(args.receipt)
        return verdict(adjudicate(mixing, tracking, note), args.expect_fail)

    if not args.app:
        ap.error("one of --app or --receipt is required")
    os.makedirs(args.out_dir, exist_ok=True)
    mixing_dump = capture(args.app, "mixing", args.out_dir, None)
    tracking_dump = capture(args.app, "tracking", args.out_dir,
                            CHIP_SELECTOR % "zero_latency")
    mixing = compact(mixing_dump)
    tracking = compact(tracking_dump)
    if args.emit_receipt:
        write_receipt(args.emit_receipt, {"mixing": mixing, "tracking": tracking})
        print("wrote %s" % args.emit_receipt)
    return verdict(adjudicate(mixing, tracking, "live: %s" % args.app),
                   args.expect_fail)


if __name__ == "__main__":
    sys.exit(main())
