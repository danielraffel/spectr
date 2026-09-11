#!/usr/bin/env python3
"""Assert the pattern dropdown answers Escape, Up/Down + Return, and tap-outside.

The three affordances are asserted directly, each paired with a control that
must return non-zero on the same instrument and the same target, so a silent
harness cannot read as a passing dropdown.

Exit 0 pass, 1 fail (the affordance is missing), 2 inconclusive (a control
came back empty, so the measurement proves nothing either way).
"""

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys

APP = "build-now/Spectr.app/Contents/MacOS/Spectr"
TRIGGER = '[data-spectr-menu-root="pattern"] button'
# FACTORY_PATTERNS[2] is ALTERNATING; three ArrowDowns land on it.
THIRD_ITEM_CAPTION = "ALTERN… ▾"

DISMISS_PROBE = r"""
(function(){
  var L=function(s){ console.log("[d2probe] " + s); };
  var conts = document.querySelectorAll("[data-spectr-menu-options]") || [];
  L("containers=" + conts.length);
  var c = conts[0];
  var id = c && (c.__pulpId || c._id || c.id);
  L("container_id=" + String(id));
  var cbs = globalThis.__pulpReactEventCallbacks__;
  L("callbacks_size=" + (cbs ? cbs.size : -1));
  var dismissTotal = 0;
  if (cbs && cbs.forEach) cbs.forEach(function(v,k){
    if (String(k).indexOf(":dismiss") >= 0) dismissTotal++; });
  L("CONTROL_dismiss_callbacks_total=" + dismissTotal);
  var own = (cbs && id) ? cbs.get(String(id)+":dismiss") : null;
  L("menu_has_dismiss=" + (typeof own === "function"));
  if (typeof __dispatch__ === "function" && id) {
    __dispatch__(String(id), "dismiss", 0);
    L("dispatched=yes");
  } else { L("dispatched=no"); }
  if (typeof __pulpRuntimeSettle__ === "function") __pulpRuntimeSettle__(8);
  var after = document.querySelectorAll("[data-spectr-menu-options]") || [];
  L("containers_after=" + after.length);
})();
"""


KEY_SEQUENCE_PROBE = r"""
(function(){
  var L=function(s){ console.log("[d2keys] " + s); };
  var targets = [];
  if (typeof document !== 'undefined') targets.push(document);
  if (typeof window !== 'undefined' && window !== document) targets.push(window);
  var fired = 0;
  for (var i = 0; i < targets.length; i++)
    if (typeof targets[i].addEventListener === 'function')
      targets[i].addEventListener('keydown', function(){ fired++; }, true);
  var keys = ["ArrowDown","ArrowDown","ArrowDown","Enter"];
  var sent = 0;
  for (var k = 0; k < keys.length; k++) {
    var ev = { type:'keydown', key:keys[k], code:keys[k], bubbles:true,
               cancelable:true,
               preventDefault:function(){ this.defaultPrevented = true; },
               stopPropagation:function(){} };
    for (var j = 0; j < targets.length; j++)
      if (typeof targets[j].dispatchEvent === 'function') targets[j].dispatchEvent(ev);
    sent++;
    if (typeof __pulpRuntimeSettle__ === 'function') __pulpRuntimeSettle__(4);
  }
  L("CONTROL_listeners_fired=" + fired + " sent=" + sent);
})();
"""


def run(root, out_dir, name, env_extra, app=APP):
    out_dir.mkdir(parents=True, exist_ok=True)
    layout = out_dir / f"{name}.layout.json"
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1",
        "PULP_SCREENSHOT": str(out_dir / f"{name}.png"),
        "PULP_FRAMES": "90",
        "SPECTR_LAYOUT_DUMP": str(layout),
    })
    env.update(env_extra)
    proc = subprocess.run([str(root / app)], env=env, cwd=str(root),
                          capture_output=True, text=True, timeout=300)
    log = proc.stdout + proc.stderr
    (out_dir / f"{name}.log").write_text(log, encoding="utf-8")
    nodes = None
    if layout.exists():
        nodes = len(json.loads(layout.read_text(encoding="utf-8"))["nodes"])
    return nodes, log, layout


def probe_values(log):
    out = {}
    for m in re.finditer(r"\[d2probe\] (\w+)=(\S+)", log):
        out[m.group(1)] = m.group(2)
    return out


def key_listeners(log):
    m = re.search(r"\[key-control\][^\n]*listeners_fired=(\d+)", log)
    return int(m.group(1)) if m else None


def seq_listeners(log):
    m = re.search(r"\[d2keys\] CONTROL_listeners_fired=(\d+) sent=(\d+)", log)
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def caption_texts(layout_path):
    doc = json.loads(pathlib.Path(layout_path).read_text(encoding="utf-8"))
    texts = []
    for node in doc["nodes"]:
        for box in node.get("measured_text_boxes") or []:
            t = box.get("text") or ""
            if "▾" in t:
                texts.append(t)
    return texts


# The tree this detector measures defaults to the checkout it is part of.
# It must never default to some other checkout: a bare run would then report a
# verdict about code that is not the code under test, and a PASS describing an
# unlanded tree reads exactly like a PASS describing this one.
REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

# Files that exist only in a Spectr checkout. A root without them is not a tree
# this detector can say anything about, so refuse rather than measure it.
ROOT_MARKERS = (
    "CMakeLists.txt",
    "native-ui/materialized/materialized-document.runtime.json",
)


def self_test():
    """Prove the default root is this checkout, not a foreign one."""
    failures = []
    default = pathlib.Path(
        _parser().get_default("root")).resolve()
    if default != REPO_ROOT:
        failures.append(f"--root defaults to {default}, not this checkout "
                        f"({REPO_ROOT}).")
    if not str(REPO_ROOT / "tools" / "spectr-detectors") == str(
            pathlib.Path(__file__).resolve().parent):
        failures.append("REPO_ROOT is not two levels above this file.")
    for marker in ROOT_MARKERS:
        if not (REPO_ROOT / marker).exists():
            failures.append(f"this checkout is missing {marker!r}, so the "
                            "marker set cannot gate a foreign root.")
    # Control: the marker gate must actually reject something. A gate that
    # accepts every path would pass the checks above while guarding nothing.
    bogus = pathlib.Path("/")
    if all((bogus / m).exists() for m in ROOT_MARKERS):
        failures.append("the marker set accepts '/', so it gates nothing.")
    for line in failures:
        print("FAIL: " + line)
    if failures:
        return 1
    print(f"PASS: --root defaults to this checkout ({REPO_ROOT}).")
    print(f"PASS: the marker set {ROOT_MARKERS} rejects a foreign root.")
    return 0


def _parser():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(REPO_ROOT),
                    help="Spectr checkout to measure (default: this one).")
    ap.add_argument("--out", default="/tmp/uxfix/proof/d2")
    # No build script produces build-now/, so the default is a convention a
    # caller may not share. Let them name the binary rather than silently
    # measuring nothing.
    ap.add_argument("--app", default=APP,
                    help=f"Spectr binary, relative to --root (default: {APP}).")
    ap.add_argument("--self-test", action="store_true",
                    help="check the default root, then exit.")
    return ap


def main():
    args = _parser().parse_args()
    if args.self_test:
        return self_test()
    root = pathlib.Path(args.root).resolve()
    out = pathlib.Path(args.out)

    missing = [m for m in ROOT_MARKERS if not (root / m).exists()]
    if missing:
        print(f"INCONCLUSIVE: {root} is not a Spectr checkout "
              f"(missing {missing}); refusing to report a verdict about it.")
        return 2
    binary = root / args.app
    if not binary.exists():
        print(f"INCONCLUSIVE: no Spectr binary at {binary}; build one or pass "
              "--app. A detector that cannot launch the app proves nothing.")
        return 2
    print(f"measuring {root} via {args.app}")

    failures = []
    inconclusive = []

    closed, _, _ = run(root, out, "closed", {}, args.app)
    opened, _, open_layout = run(root, out, "open", {"SPECTR_CLICK": TRIGGER}, args.app)
    if closed is None or opened is None:
        print("INCONCLUSIVE: the app produced no layout dump.")
        return 2
    # Control: opening the menu must add nodes, or the driver never opened it.
    print(f"control  menu closed = {closed} nodes, menu open = {opened} nodes")
    if opened <= closed:
        print("INCONCLUSIVE: opening the dropdown changed nothing; the click "
              "driver did not reach the trigger.")
        return 2

    # 1. Escape dismisses.
    esc_nodes, esc_log, _ = run(root, out, "escape",
                                {"SPECTR_CLICK": TRIGGER, "SPECTR_KEY_JS": "Escape"}, args.app)
    fired = key_listeners(esc_log)
    if not fired:
        inconclusive.append("the key harness reported no keydown listeners; "
                            "Escape never reached the document.")
    elif esc_nodes != closed:
        failures.append(f"Escape left the dropdown open ({esc_nodes} nodes, "
                        f"closed is {closed}).")
    else:
        print(f"PASS: Escape dismisses the dropdown "
              f"({opened} -> {esc_nodes} nodes, {fired} listeners fired).")

    # 2. ArrowDown x3 + Return commits the third item.
    arrow_nodes, arrow_log, arrow_layout = run(
        root, out, "arrow_enter",
        {"SPECTR_CLICK": TRIGGER, "SPECTR_EVAL": KEY_SEQUENCE_PROBE}, args.app)
    fired, sent = seq_listeners(arrow_log)
    if sent is not None and sent != 4:
        inconclusive.append(f"the key driver sent {sent} keys, not 4.")
    captions = caption_texts(arrow_layout) if arrow_layout.exists() else []
    if not fired:
        inconclusive.append("the key harness reported no keydown listeners for "
                            "the arrow sequence.")
    elif not captions:
        inconclusive.append("no caret captions were measured; the layout dump "
                            "cannot say which preset is selected.")
    elif arrow_nodes != closed:
        failures.append(f"Return did not close the dropdown ({arrow_nodes} nodes).")
    elif THIRD_ITEM_CAPTION not in captions:
        failures.append("Return committed the wrong item: the trigger reads "
                        f"{captions!r}, expected {THIRD_ITEM_CAPTION!r}.")
    else:
        print(f"PASS: ArrowDown x3 + Return commits the third preset "
              f"(trigger reads {THIRD_ITEM_CAPTION!r}).")

    # 3. Tap-outside: the open dropdown claims the native overlay and answers
    #    the dismiss the platform host fires on an outside press.
    dis_nodes, dis_log, _ = run(root, out, "dismiss",
                                {"SPECTR_CLICK": TRIGGER, "SPECTR_EVAL": DISMISS_PROBE}, args.app)
    p = probe_values(dis_log)
    total = int(p.get("CONTROL_dismiss_callbacks_total", "0"))
    if p.get("containers") in (None, "0"):
        inconclusive.append("the probe found no options container to measure.")
    elif total == 0:
        inconclusive.append("no dismiss callbacks exist anywhere in the "
                            "document; the callback registry is not readable.")
    elif p.get("menu_has_dismiss") != "true":
        failures.append(
            "the open dropdown registers no dismiss handler, so an outside "
            f"press cannot close it (id {p.get('container_id')}, "
            f"{total} dismiss handlers exist elsewhere).")
    elif p.get("containers_after") != "0" or dis_nodes != closed:
        failures.append(
            "the dropdown survived the overlay dismiss "
            f"(containers_after={p.get('containers_after')}, {dis_nodes} nodes).")
    else:
        print(f"PASS: the dropdown claims the overlay and closes on the "
              f"platform dismiss ({total} dismiss handlers document-wide).")

    for line in inconclusive:
        print("INCONCLUSIVE: " + line)
    for line in failures:
        print("FAIL: " + line)
    if inconclusive:
        return 2
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
