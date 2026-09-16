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
     text itself, whose own height follows its own string. The exemption is
     safe because it is narrow in a specific way: x, y and w are never exempt
     for ANY node, and every other node's h is compared, so a box that escaped
     the reserve and pushed the row would move its neighbours and fail.
  3. Each mode's guidance text FITS the reserved box -- measured as the
     SHAPED TEXT height against the reserve, never box against box. That
     distinction is the whole rule: `measured_text_boxes[].rect.h` is a
     Label's `measured_height(width)`, computed without reference to its
     layout box, so it still reads 61 when the box has been pinned to 52 and
     the sentence is clipped. Comparing box against box reads 0 in exactly
     that case and sees nothing, which is how a reserve written as `height:`
     rather than `minHeight:` would sail through -- the same class of edit
     this fix is about.
  4. The reserve is a real height -- non-zero and at least as tall as the
     taller of the two measured strings, taken as the MINIMUM over reserves
     so one real reserve cannot mask a zero one.

WHAT IT STRUCTURALLY CANNOT SEE. Rects inside a scroll container are in
unscrolled content space (see layout_common), so a switch that changed the
panel's SCROLL OFFSET would be a visible jump with every rect identical. That
is a different mechanism from the reported defect and this instrument is blind
to it.

Usage:
  settings_render_mode_no_reflow.py --app <Spectr binary> [--out-dir DIR]
                                    [--emit-receipt PATH]
  settings_render_mode_no_reflow.py --receipt <receipt.json> [--plant NAME]
                                    [--expect-fail]

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
# A floor, not a pin. The Settings subtree measures ~315 boxes; this exists so
# that a refactor moving the panel's content out of the scroll viewport reports
# an unmeasured run instead of "all checks passed" over a handful of nodes that
# trivially agree with each other.
MIN_SETTINGS_NODES = 100


def fail_instrument(message):
    """Exit 2, not 1. `raise SystemExit("text")` exits 1, which would report an
    instrument failure as a product regression -- and the control rows here are
    justified on exactly the 1-vs-2 distinction."""
    print("INSTRUMENT: %s" % message, file=sys.stderr)
    raise SystemExit(2)


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
        # Never inherit a press from the caller's environment: it would make
        # the two captures identical and the comparison vacuous.
        env.pop("SPECTR_CLICK", None)
    log = os.path.join(out_dir, name + ".log")
    try:
        with open(log, "wb") as fh:
            proc = subprocess.run([app], env=env, stdout=fh,
                                  stderr=subprocess.STDOUT, timeout=300)
    except subprocess.TimeoutExpired:
        fail_instrument("'%s' did not exit within 300s (see %s)" % (name, log))
    except OSError as err:
        fail_instrument("could not launch %s: %s" % (app, err))
    if proc.returncode != 0:
        fail_instrument("'%s' exited %d; a dump written by a run that then "
                        "failed describes a state nobody should adjudicate "
                        "(see %s)" % (name, proc.returncode, log))
    if not os.path.exists(dump):
        fail_instrument("no layout dump produced for '%s' (see %s)" % (name, log))
    return dump


def texts(node):
    return [b.get("text", "") for b in (node.get("measured_text_boxes") or [])]


def shaped_height(node):
    """Tallest SHAPED-TEXT height on this node, independent of its layout box.

    For a Label the emitter writes `measured_height(width)`, which is computed
    from the string and the node's width and is NOT clamped to the node's
    height. That is what makes rule 3 able to see a clipped sentence.
    """
    return max([float((b.get("rect") or {}).get("h", 0.0))
                for b in (node.get("measured_text_boxes") or [])] or [0.0])


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
        fail_instrument("expected exactly one scrolling node, found %d -- the "
                        "Settings panel is probably not open" % len(scrollers))
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
        fail_instrument("no depths sidecar beside %s; ancestry is "
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
        # This is a GEOMETRY receipt, so it keeps only the text the adjudicator
        # actually reads -- the two guidance sentences, which are how a capture
        # says which mode it is showing. Everything else is dropped on purpose:
        # the panel also paints the build's own git SHA and a DIRTY/CLEAN flag,
        # so a receipt that kept all text would change on every single build
        # and churn against the other lanes editing this repo, for two labels
        # no rule here consults.
        #
        # `text_h` rides along for those same nodes and is load bearing: it is
        # the SHAPED height, which rule 3 needs and which the node's own rect
        # cannot supply once the box is height-constrained.
        t = " | ".join(texts(n))
        if t and (MIXING_MARK in t or TRACKING_MARK in t):
            row["text"] = t
            row["text_h"] = round(shaped_height(n), 4)
        nodes.append(row)
    return {"viewport": d.viewport, "nodes": nodes}


def mode_of(state):
    """Which guidance sentences a capture contains.

    NOTE the sizer -- the transparent copy that reserves the height -- always
    carries the longest option's string, so "this capture contains the Mixing
    sentence" is true in BOTH modes. That is why this is only a presence
    report; which mode is on show is decided by comparing the visible text
    across the two captures, not by this.
    """
    seen = set()
    for n in state["nodes"]:
        if MIXING_MARK in n.get("text", ""):
            seen.add("mixing")
        if TRACKING_MARK in n.get("text", ""):
            seen.add("tracking")
    return seen


def hint_nodes(state):
    """Every (guidance-text node, its parent) pair in this capture.

    There are two on the fixed surface -- the transparent sizer that reserves
    the height, and the visible line drawn over it -- and one on the pre-fix
    surface, which had no sizer. Both are returned deliberately rather than
    filtered down to "the visible one": the rules below want the worst case
    over all of them, and a filter that guessed which is which would be one
    more thing to get wrong.

    A guidance node whose parent is missing from the compared set is an
    INSTRUMENT failure rather than a silent drop. Dropping it quietly removes
    the node the reserve rules are about, and the gate then misreports the
    result as "the press never landed" -- a confidently wrong diagnosis.
    """
    by_i = {n["i"]: n for n in state["nodes"]}
    out = []
    for n in state["nodes"]:
        if MIXING_MARK not in n.get("text", "") \
                and TRACKING_MARK not in n.get("text", ""):
            continue
        p = by_i.get(n["parent"])
        if p is None:
            fail_instrument("guidance node #%d has no parent in the compared "
                            "set, so the rules about its reserve cannot run"
                            % n["i"])
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
    if len(mixing["nodes"]) < MIN_SETTINGS_NODES:
        print("INSTRUMENT: only %d Settings boxes were captured (floor %d). "
              "Two nearly-empty captures agree with each other trivially, so "
              "this is an unmeasured run, not a clean one."
              % (len(mixing["nodes"]), MIN_SETTINGS_NODES))
        return 2

    # ---- 1. THE SWITCH ACTUALLY HAPPENED ---------------------------------
    m_hints = hint_nodes(mixing)
    t_hints = hint_nodes(tracking)
    if not m_hints or not t_hints:
        print("INSTRUMENT: no guidance line found in one of the captures "
              "(mixing=%d tracking=%d); the Latency control did not render, "
              "so nothing was measured" % (len(m_hints), len(t_hints)))
        return 2
    m_text = " ".join(n.get("text", "") for n, _ in m_hints)
    t_text = " ".join(n.get("text", "") for n, _ in t_hints)
    if m_text == t_text:
        print("INSTRUMENT: both captures show the same guidance text, so the "
              "Tracking press never landed. Comparing a capture with itself "
              "would pass whatever the panel does; no verdict.")
        print("  text = %r" % m_text[:120])
        return 2
    # Each capture must contain the sentence its own mode is supposed to show.
    # This is weaker than it looks on the Mixing side -- the sizer carries that
    # string in both modes -- so it is a presence check backing the text
    # inequality above, not a substitute for it.
    if "tracking" not in mode_of(tracking) or "mixing" not in mode_of(mixing):
        print("INSTRUMENT: a capture does not contain its own mode's guidance "
              "sentence (mixing=%s tracking=%s); no verdict"
              % (sorted(mode_of(mixing)), sorted(mode_of(tracking))))
        return 2

    # ---- 2. THE GATE: no box moves ---------------------------------------
    # The visible guidance line is out of flow, so its OWN height follows its
    # own string. x/y/w are exempt for nothing, and every other node's h is
    # compared, which is what keeps the exception narrow.
    exempt_h = {n["i"] for n, _ in t_hints
                if n.get("text") != next((x.get("text") for x, _ in m_hints
                                          if x["i"] == n["i"]), None)}
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

    # ---- 3. THE SHAPED TEXT FITS THE RESERVE IN EACH MODE -----------------
    # Guards the proxy: the reserve is sized from the LONGEST string, which is
    # a stand-in for the TALLEST rendering. If that ever picks wrong, the
    # out-of-flow text overflows its box instead of growing it, and rule 2
    # above stays green. Measured as SHAPED height vs the reserve -- comparing
    # the node's box against its parent's reads 0 whenever the box is
    # height-constrained, which is precisely the regression worth catching.
    for label, state in (("Mixing", mixing), ("Tracking", tracking)):
        worst = None
        for n, p in hint_nodes(state):
            if "text_h" not in n:
                print("INSTRUMENT: guidance node #%d carries no shaped-text "
                      "height, so whether its line fits cannot be measured; "
                      "no verdict" % n["i"])
                return 2
            over = n["text_h"] - p["rect"]["h"]
            if worst is None or over > worst[0]:
                worst = (over, n, p)
        over, n, p = worst
        check("%s: the guidance line fits its reserved box" % label,
              over <= FIT_SLACK,
              "shaped text h=%.2f exceeds reserve h=%.2f by %.2f (#%d in #%d)"
              % (n["text_h"], p["rect"]["h"], over, n["i"], p["i"]))

    # ---- 4. THE RESERVE IS A REAL HEIGHT ----------------------------------
    # Two equal zeros would satisfy rule 2 and reserve nothing at all. Taken as
    # the MINIMUM over reserves, so one real reserve cannot mask a zero one.
    reserves = [p["rect"]["h"] for _, p in hint_nodes(mixing)] \
        + [p["rect"]["h"] for _, p in hint_nodes(tracking)]
    tallest_text = max([n.get("text_h", 0.0) for n, _ in hint_nodes(mixing)]
                       + [n.get("text_h", 0.0) for n, _ in hint_nodes(tracking)])
    check("the reserve is a real height, not zero", min(reserves) > 0,
          str(reserves))
    check("the reserve is at least as tall as the taller guidance line",
          min(reserves) + FIT_SLACK >= tallest_text,
          "reserves=%s tallest shaped line=%.2f" % (reserves, tallest_text))

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
    # Fails rules 2a and 2b.
    "fixed-reserve-too-small":
        "re-adjudicate the recorded pre-fix capture pair",
    # The OPPOSITE regression, and the reason rule 3 measures shaped text
    # rather than boxes: a reserve written as `height:` instead of `minHeight:`
    # pins the row in BOTH modes, so nothing moves and rule 2 stays green while
    # the taller sentence is silently clipped. Without this plant, rules 3 and
    # 4 would never have been observed failing.
    "clipped-reserve":
        "pin every reserve below the text it has to hold",
}


def plant_receipt(receipt, name):
    """Rebuild a receipt so it describes a defect, for a negative control."""
    if name == "fixed-reserve-too-small":
        pre = receipt.get("pre_fix")
        if not pre:
            fail_instrument("this receipt carries no 'pre_fix' pair, so the "
                            "control cannot reinstate the defect it names")
        if "mixing" not in pre or "tracking" not in pre:
            fail_instrument("the receipt's 'pre_fix' pair is incomplete")
        return pre["mixing"], pre["tracking"]

    if name == "clipped-reserve":
        import copy
        # ONE floor across BOTH captures -- the shorter option's shaped height.
        # It has to be shared, or the two captures get different pins, boxes
        # change height between them and rule 2 fires. Rule 2 firing would
        # defeat the point: this plant exists to show rule 3 catching a defect
        # that moves NOTHING, which is the case box-against-box comparison is
        # blind to.
        floor = min(n["text_h"]
                    for key in ("mixing", "tracking")
                    for n in receipt[key]["nodes"] if "text_h" in n)
        out = []
        for key in ("mixing", "tracking"):
            st = copy.deepcopy(receipt[key])
            by_i = {n["i"]: n for n in st["nodes"]}
            for n in st["nodes"]:
                if "text_h" in n:
                    n["rect"]["h"] = floor
                    parent = by_i.get(n["parent"])
                    if parent is not None:
                        parent["rect"]["h"] = floor
            out.append(st)
        return out[0], out[1]

    fail_instrument("unknown plant %r; known: %s"
                    % (name, ", ".join(sorted(PLANTS))))


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


def load_receipt(path):
    try:
        with open(path) as fh:
            receipt = json.load(fh)
    except (OSError, ValueError) as err:
        fail_instrument("could not read receipt %s: %s" % (path, err))
    for key in ("mixing", "tracking"):
        state = receipt.get(key)
        if not isinstance(state, dict) or not isinstance(state.get("nodes"), list):
            fail_instrument("receipt %s has no usable '%s' capture"
                            % (path, key))
        for n in state["nodes"]:
            if not isinstance(n.get("rect"), dict) or "i" not in n:
                fail_instrument("receipt %s has a malformed node in '%s'"
                                % (path, key))
    return receipt


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

    if args.app and args.receipt:
        ap.error("--app and --receipt name two different instruments; passing "
                 "both would silently adjudicate the frozen receipt and never "
                 "launch the binary")
    if not args.app and not args.receipt:
        ap.error("one of --app or --receipt is required")

    if args.receipt:
        receipt = load_receipt(args.receipt)
        if args.plant:
            mixing, tracking = plant_receipt(receipt, args.plant)
            note = ("CONTROL (%s): %s" % (args.plant, PLANTS[args.plant]))
        else:
            mixing, tracking = receipt["mixing"], receipt["tracking"]
            note = "receipt: %s" % os.path.basename(args.receipt)
        return verdict(adjudicate(mixing, tracking, note), args.expect_fail)

    os.makedirs(args.out_dir, exist_ok=True)
    mixing = compact(capture(args.app, "mixing", args.out_dir, None))
    tracking = compact(capture(args.app, "tracking", args.out_dir,
                               CHIP_SELECTOR % "zero_latency"))
    rc = adjudicate(mixing, tracking, "live: %s" % args.app)
    # Emit only AFTER a clean verdict, and never drop an existing pre_fix
    # pair: writing an unadjudicated capture over the committed evidence would
    # record a possibly-failing pair AND destroy the negative control's input.
    if args.emit_receipt:
        if rc != 0:
            print("not writing %s: the captures did not pass"
                  % args.emit_receipt, file=sys.stderr)
        else:
            payload = {"mixing": mixing, "tracking": tracking}
            if os.path.exists(args.emit_receipt):
                try:
                    with open(args.emit_receipt) as fh:
                        prior = json.load(fh)
                    if "pre_fix" in prior:
                        payload["pre_fix"] = prior["pre_fix"]
                except (OSError, ValueError):
                    pass
            write_receipt(args.emit_receipt, payload)
            print("wrote %s" % args.emit_receipt)
    return verdict(rc, args.expect_fail)


if __name__ == "__main__":
    sys.exit(main())
