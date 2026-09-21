#!/usr/bin/env python3
"""Assert the pattern dropdown answers Escape, Up/Down + Return, and tap-outside.

The three affordances are asserted directly, each paired with a control that
must return non-zero on the same instrument and the same target, so a silent
harness cannot read as a passing dropdown.

TWO READINGS THAT ARE NOT THE SAME READING. Pulp's popup owner writes
`data-pulp-popup-active` on every option of a popup it holds and removes the
attribute when it lets go, so PRESENCE is ownership. The value `"true"` marks
only the row the keyboard cursor is painted on, and a popup opened with the
POINTER paints no cursor at all until the user asks for one -- an arrow, a row
hover, or an arrow that opened the menu. Conflating the two is not academic: it
is the defect the `stale-ownership-sentinel` control below reproduces.

The first arrow follows from the same rule. On a popup whose cursor is hidden
and whose seed came from an edge because the author marked no selection, that
press reveals the cursor ON the edge rather than stepping past the row the user
was aiming at; seeded from a marked selection it steps off that row as a combo
box does. The probe decides which case it is from the markup, never from the
owner's own seeding flag.

Two negative controls, each required to make the assertion it targets go red:
  * `second-keyboard-owner` (the default) applies one arrow press twice, the
    user-reported "arrow keys skip a row";
  * `stale-ownership-sentinel` restores the document keydown guard that keyed
    on the painted cursor instead of ownership, which consumed Escape before
    the popup owner was ever offered it.

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
MENU_ROOT = "pattern"
ARROW_PRESSES = 3

# Keyboard affordances belong to Pulp's popup default handler, and that handler
# keeps no state until a `pointerdown` reaches it and the animation frame it
# schedules has run `activate()`. Dispatching keys without that opens nothing to
# steer, so every key is correctly ignored and the detector reads a healthy
# product as a broken one. Both probes below therefore deliver the pointerdown
# the native pointer path delivers, and wait a frame before asserting anything.
ACTIVATE = r"""
  var trigger = document.querySelector('__TRIGGER__');
  if (!trigger) { L("RESULT inconclusive reason=no_trigger"); return; }
  var popupState = function(){ return globalThis.__pulpPopupDefaultState__; };
  var idx = function(){ var s = popupState(); return s ? s.activeIndex : -999; };
  var containers = function(){
    return document.querySelectorAll("[data-spectr-menu-options]").length; };
  // Ownership is the ATTRIBUTE -- the owner writes it on every row of a popup
  // it holds and removes it when it lets go. Its VALUE "true" is the painted
  // keyboard cursor, which a pointer-opened popup does not show until the user
  // asks for one. Two readings, two selectors.
  var owned = function(){
    return document.querySelectorAll('[data-pulp-popup-active]').length; };
  var highlighted = function(){
    return document.querySelectorAll('[data-pulp-popup-active="true"]').length; };
  // Answered from the markup, never from the popup's own seeding flag: reading
  // the owner's bookkeeping to build the expectation would let a seeding bug
  // agree with itself.
  var authorMarkedSelection = function(popup, options){
    var holders = [trigger, popup];
    for (var h = 0; h < holders.length; ++h) {
      var pointer = holders[h] && holders[h].getAttribute
        ? holders[h].getAttribute("aria-activedescendant") : null;
      if (pointer) {
        for (var p = 0; p < options.length; ++p)
          if (options[p].id === pointer) return true;
      }
    }
    for (var i = 0; i < options.length; ++i) {
      var o = options[i];
      if (!o.getAttribute) continue;
      if (o.getAttribute("aria-selected") === "true") return true;
      if (o.getAttribute("aria-checked") === "true") return true;
      if (o.checked === true) return true;
      var current = o.getAttribute("aria-current");
      if (current && current !== "false") return true;
    }
    return false;
  };
  var sendKey = function(key){
    document.dispatchEvent({ type:"keydown", key:key, code:key,
      bubbles:true, cancelable:true,
      preventDefault:function(){ this.defaultPrevented = true; },
      stopPropagation:function(){}, stopImmediatePropagation:function(){} });
  };
"""

# The negative control. A second keyboard owner re-enters the popup handler with
# a fresh, unprevented event, so every press is applied twice and the cursor
# advances two rows -- the user-reported "arrow keys skip a row" symptom exactly.
# A repaired detector that stays green against this is not measuring.
SECOND_OWNER = r"""
  var reentry = false;
  document.addEventListener("keydown", function(e){
    if (reentry) return; reentry = true;
    globalThis.__pulpPopupDefaultHandle__({ type:"keydown", key:e.key, code:e.key,
      preventDefault:function(){ this.defaultPrevented = true; },
      stopPropagation:function(){}, stopImmediatePropagation:function(){} });
    reentry = false;
  });
  L("second_keyboard_owner=installed");
"""

# The second plant, for the Escape assertion. This is the defect that shipped,
# reproduced exactly: a document-level capture listener that answers Escape
# itself unless it can see `[data-pulp-popup-active="true"]`. That selector is
# the PAINTED keyboard cursor, not popup ownership, and a pointer-opened popup
# paints no cursor -- so the guard misses an open dropdown, consumes the key,
# and `document.dispatchEvent` then never offers the event to the popup owner
# (it offers only while `!event.defaultPrevented`). The Escape assertion must
# go red against this or it is not watching anything.
STALE_OWNERSHIP_SENTINEL = r"""
  document.addEventListener("keydown", function(e){
    if (!e || e.key !== "Escape") return;
    if (document.querySelector('[data-pulp-popup-active="true"]')) return;
    if (typeof e.preventDefault === "function") e.preventDefault();
    if (typeof e.stopPropagation === "function") e.stopPropagation();
  }, true);
  L("stale_ownership_sentinel=installed");
"""

CONTROLS = ("second-keyboard-owner", "stale-ownership-sentinel")

ESCAPE_PROBE = r"""
(function(){
  var L=function(s){ console.log("[d2esc] " + s); };
__ACTIVATE__
__SECOND_OWNER__
  var phase = 0, opened = -1;
  var step = function(){
    phase++;
    if (phase === 1) {
      globalThis.__pulpPopupDefaultHandle__({ type:"pointerdown", target: trigger });
    } else if (phase === 2) {
      if (!popupState()) { L("RESULT inconclusive reason=no_popup_state"); return; }
      opened = containers();
      L("CONTROL opened_containers=" + opened + " options=" +
        popupState().options.length + " highlighted=" + highlighted());
      if (!opened) { L("RESULT inconclusive reason=no_container_opened"); return; }
      sendKey("Escape");
    } else if (phase === 3) {
      var after = containers();
      var cleared = popupState() ? "present" : "null";
      L("RESULT " + (after === 0 && cleared === "null" ? "pass" : "fail") +
        " containers=" + opened + "->" + after + " state=" + cleared);
      return;
    }
    requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
})();
"""

KEY_SEQUENCE_PROBE = r"""
(function(){
  var L=function(s){ console.log("[d2keys] " + s); };
__ACTIVATE__
__SECOND_OWNER__
  var phase = 0, count = 0, bad = 0, badHighlight = 0, seen = [], committed = "";
  var marked = false, revealed = false;
  var step = function(){
    phase++;
    if (phase === 1) {
      globalThis.__pulpPopupDefaultHandle__({ type:"pointerdown", target: trigger });
    } else if (phase === 2) {
      var s = popupState();
      if (!s) { L("RESULT inconclusive reason=no_popup_state"); return; }
      count = s.options.length;
      if (!count) { L("RESULT inconclusive reason=zero_options"); return; }
      marked = authorMarkedSelection(s.popup, s.options);
      L("CONTROL options=" + count + " activeIndex=" + idx() +
        " owned=" + owned() + " highlighted=" + highlighted() +
        " marked=" + marked);
      if (owned() !== count) badHighlight++;
      // A pointer open reveals no cursor, so nothing is painted yet.
      if (highlighted() !== 0) badHighlight++;
      seen.push(idx());
    } else if (phase - 2 <= __PRESSES__) {
      var before = idx();
      // Stepping is circular. `(i + 1) % count` is a correct wrap, not a skip;
      // asserting a raw delta of 1 would score the wrap as the very defect this
      // detector exists to catch.
      //
      // And the FIRST arrow is not always a step. On a popup whose cursor is
      // still hidden and whose seed came from an edge because the author marked
      // no selection, that press REVEALS the cursor on the edge instead of
      // moving it -- otherwise it would skip the row the user was aiming at.
      // Seeded from a marked selection, it steps off that row as a combo box
      // does.
      var landOnSeed = !revealed && !marked;
      var want = landOnSeed ? 0 : (before + 1) % count;
      sendKey("ArrowDown");
      revealed = true;
      var after = idx();
      seen.push(after);
      if (after !== want) bad++;
      if (highlighted() !== 1 || owned() !== count) badHighlight++;
      L("PRESS ArrowDown " + before + " -> " + after + " want=" + want +
        (landOnSeed ? " (reveal)" : "") +
        (after === want ? "" : " MISSTEP"));
    } else if (phase - 2 === __PRESSES__ + 1) {
      var s2 = popupState();
      var active = s2 && s2.options ? s2.options[s2.activeIndex] : null;
      // Not every property survives this runtime's element shim -- textContent
      // reads empty here -- so try each and let the caller distinguish "the
      // commit named the wrong item" from "the item's name was unreadable".
      committed = "";
      if (active) {
        var reads = [active.textContent, active.innerText,
                     active.getAttribute && active.getAttribute("data-spectr-pattern-id"),
                     active.getAttribute && active.getAttribute("aria-label")];
        for (var r = 0; r < reads.length; r++)
          if (reads[r] && String(reads[r]).trim()) { committed = String(reads[r]).trim(); break; }
      }
      L("COMMITTING index=" + idx() + " text=" + JSON.stringify(committed));
      sendKey("Enter");
    } else {
      L("TRACE " + JSON.stringify(seen));
      L("RESULT " + (bad === 0 && badHighlight === 0 ? "pass" : "fail") +
        " missteps=" + bad + " bad_highlight=" + badHighlight +
        " containers_after=" + containers() +
        " committed=" + JSON.stringify(committed));
      return;
    }
    requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
})();
"""


def build_probe(template, negative_control):
    plants = {"second-keyboard-owner": SECOND_OWNER,
              "stale-ownership-sentinel": STALE_OWNERSHIP_SENTINEL}
    return (template
            .replace("__ACTIVATE__", ACTIVATE.replace("__TRIGGER__", TRIGGER))
            .replace("__SECOND_OWNER__", plants.get(negative_control, ""))
            .replace("__PRESSES__", str(ARROW_PRESSES)))


def probe_result(log, tag):
    """(verdict, detail) from the probe's single RESULT line."""
    m = re.search(r"\[" + tag + r"\] RESULT (\w+)([^\n]*)", log)
    if not m:
        return None, "the probe produced no RESULT line"
    return m.group(1), m.group(2).strip()

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
    # Each plant must actually change the probe, or its required failure could
    # never be produced, and the two must differ from each other or one of the
    # registered controls is measuring the other's defect.
    plain = build_probe(ESCAPE_PROBE, None)
    for control in CONTROLS:
        if build_probe(ESCAPE_PROBE, control) == plain:
            failures.append(f"--negative-control {control} does not change "
                            "the probe.")
    if (build_probe(ESCAPE_PROBE, CONTROLS[0])
            == build_probe(ESCAPE_PROBE, CONTROLS[1])):
        failures.append("the two negative controls plant the same thing.")
    # The ownership and paint readings must be different selectors. One probe
    # using the value-matching selector for both would read an owned but
    # unrevealed popup as unowned -- the confusion the shipped guard made.
    keys = build_probe(KEY_SEQUENCE_PROBE, None)
    for token in ("landOnSeed", "authorMarkedSelection",
                  "'[data-pulp-popup-active]'",
                  "'[data-pulp-popup-active=\"true\"]'"):
        if token not in keys:
            failures.append(f"the arrow probe no longer reads {token!r}.")
    for line in failures:
        print("FAIL: " + line)
    if failures:
        return 1
    print(f"PASS: --root defaults to this checkout ({REPO_ROOT}).")
    print(f"PASS: the marker set {ROOT_MARKERS} rejects a foreign root.")
    print(f"PASS: each of --negative-control {CONTROLS} produces a different "
          "probe.")
    print("PASS: the arrow probe derives the reveal-vs-step expectation from "
          "the markup and reads ownership and paint through different "
          "selectors.")
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
    ap.add_argument("--negative-control", nargs="?", const=CONTROLS[0],
                    choices=CONTROLS, default=None,
                    help="plant a defect; the assertion it targets MUST then "
                         f"fail. {CONTROLS[0]} (the default) applies one arrow "
                         f"press twice; {CONTROLS[1]} restores the guard that "
                         "kept Escape from reaching the popup owner.")
    ap.add_argument("--self-test", action="store_true",
                    help="check the default root, then exit.")
    return ap


def _caption_matches(option_text, captions):
    """A trigger caption is the committed option, possibly ellipsized."""
    want = option_text.strip().casefold()
    for caption in captions:
        got = caption.replace("\u25be", "").strip().casefold()
        head = got.rstrip("\u2026 ").strip()
        if got == want or (head and want.startswith(head)):
            return True
    return False


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

    # 1. Escape dismisses. Driven through the popup handler's own activation
    #    path, so the handler has the state Escape acts on.
    # SPECTR_CLICK renders the popup; the probe's pointerdown is what makes
    # Pulp's handler adopt it. Neither alone is enough: with no popup in the DOM
    # `popupFor` finds nothing and activate() stores no state, and with no
    # pointerdown the handler never activates at all.
    _, esc_log, _ = run(root, out, "escape",
                        {"SPECTR_CLICK": TRIGGER,
                         "SPECTR_EVAL": build_probe(ESCAPE_PROBE, args.negative_control)},
                        args.app)
    verdict, detail = probe_result(esc_log, "d2esc")
    if args.negative_control == "stale-ownership-sentinel":
        # Inverted: the restored guard MUST keep Escape from reaching the popup
        # owner. A green Escape assertion here means this detector cannot see
        # the defect that shipped, so every green run of it proves nothing.
        if verdict == "fail":
            print("PASS (negative control stale-ownership-sentinel): the "
                  f"restored guard was caught ({detail}).")
        elif verdict == "inconclusive":
            inconclusive.append(f"negative control proved nothing ({detail}).")
        else:
            failures.append(
                "negative control PASSED; the Escape assertion cannot see a "
                "document listener consuming the key before the popup owner "
                "is offered it, so a green run of this detector proves "
                "nothing.")
    elif args.negative_control:
        # The injected owner re-enters with Escape too, and Escape is
        # idempotent, so this run says nothing about it either way.
        print(f"(negative control) Escape: {verdict} {detail}")
    elif verdict is None or verdict == "inconclusive":
        inconclusive.append(f"the Escape probe proved nothing ({detail}).")
    elif verdict != "pass":
        failures.append(f"Escape left the dropdown open ({detail}).")
    else:
        print(f"PASS: Escape dismisses the dropdown ({detail}).")

    # 2. Each ArrowDown moves exactly one row, then Return commits it and
    #    closes. Stepping is circular, so the expectation is (i + 1) % count --
    #    a naive delta of 1 scores a correct wrap as the skip we are hunting.
    arrow_nodes, arrow_log, arrow_layout = run(
        root, out, "arrow_enter",
        {"SPECTR_CLICK": TRIGGER,
         "SPECTR_EVAL": build_probe(KEY_SEQUENCE_PROBE, args.negative_control)},
        args.app)
    verdict, detail = probe_result(arrow_log, "d2keys")
    committed = re.search(r'committed="([^"]*)"', detail or "")
    captions = caption_texts(arrow_layout) if arrow_layout.exists() else []
    if args.negative_control == "stale-ownership-sentinel":
        # That plant touches Escape only, so the arrow reading is not evidence
        # about it either way.
        print(f"(negative control stale-ownership-sentinel) arrows: {verdict} "
              f"{detail}")
    elif args.negative_control:
        # Inverted: the injected double-stepping owner MUST be caught. A green
        # arrow assertion here means the measurement is blind, and every green
        # run of this detector would then prove nothing.
        if verdict == "fail":
            print(f"PASS (negative control): the second keyboard owner was "
                  f"caught ({detail}).")
        elif verdict == "inconclusive":
            inconclusive.append(f"negative control proved nothing ({detail}).")
        else:
            failures.append(
                "negative control PASSED; the arrow assertion cannot see a "
                "double-stepping keyboard owner, so a green run of this "
                "detector proves nothing.")
    elif verdict is None or verdict == "inconclusive":
        inconclusive.append(f"the arrow-step probe proved nothing ({detail}).")
    elif verdict != "pass":
        failures.append(f"arrow stepping did not move exactly one row per "
                        f"press ({detail}).")
    elif "containers_after=0" not in (detail or ""):
        failures.append(f"Return did not close the dropdown ({detail}).")
    elif not captions:
        inconclusive.append("no caret captions were measured; the layout dump "
                            "cannot say which preset the commit selected.")
    elif not (committed and committed.group(1)):
        # The stepping and close claims above are proven; only the item's NAME
        # was unreadable. Reporting that as a failure would manufacture the
        # false verdict this detector exists to avoid, so say what went
        # unchecked instead.
        print(f"PASS: each ArrowDown moves exactly one row and Return closes "
              f"the dropdown ({detail}).")
        print("NOTE: the committed item's name was unreadable in this runtime, "
              "so WHICH item Return selected is unverified here; "
              "menu_arrow_step_exactly_one.py covers per-press stepping "
              "across all four dropdowns.")
    elif not _caption_matches(committed.group(1), captions):
        # Derived from the option the probe actually highlighted, not a pinned
        # string: a hardcoded caption silently rots when the preset list moves.
        failures.append(f"Return committed the wrong item: the trigger reads "
                        f"{captions!r}, expected {committed.group(1)!r}.")
    else:
        print(f"PASS: each ArrowDown moves exactly one row and Return commits "
              f"it ({detail}).")

    # 3. Tap-outside: the open dropdown claims the native overlay and answers
    #    the dismiss the platform host fires on an outside press.
    dis_nodes, dis_log = None, ""
    if not args.negative_control:
        dis_nodes, dis_log, _ = run(root, out, "dismiss",
                                    {"SPECTR_CLICK": TRIGGER,
                                     "SPECTR_EVAL": DISMISS_PROBE}, args.app)
    if args.negative_control:
        print("(negative control) skipping the overlay-dismiss assertion; the "
              "injected owner does not touch it.")
        for line in inconclusive:
            print("INCONCLUSIVE: " + line)
        for line in failures:
            print("FAIL: " + line)
        return 2 if inconclusive else (1 if failures else 0)

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
