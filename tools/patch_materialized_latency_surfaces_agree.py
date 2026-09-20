#!/usr/bin/env python3
"""Changing the Latency mode in Settings moves the bottom-bar chip with it.

THE DEFECT

    Open Settings, switch Latency from Mixing to Tracking, close Settings. The
    chip in the bottom rail still reads `MIXING - 171 ms`. The processor did
    switch -- the message was sent and the host's delay compensation moved --
    so the one surface a user glances at to see which mode they are in is the
    one surface still naming the old one, with the old millisecond figure
    beside it.

WHY ONLY THE RAIL LOSES

    Both surfaces read one store, `globalThis.__spectrLatency`, and neither
    polls it. `SpectrLatencySettings` keeps a local `pending` state, so its own
    chips repaint from the setState it already does. `SpectrLatencyRail` has no
    such state: it re-renders ONLY when something calls the store's listener
    list. That list is exactly what the rail subscribes to on mount, and what
    hydration and `spectrToggleLatencyMode` both call.

    `publish` -- the Settings writer -- was the one writer that skipped it. It
    wrote `store.state` and posted `render_mode_set`, and woke nobody. So the
    rail kept the last state it had rendered.

    The reverse direction was already right, which is why this survived: press
    the rail chip or the `T` key and `spectrToggleLatencyMode` wakes the list,
    so the Settings panel follows. Only Settings -> rail was one-way, and the
    rail is usually covered by the open modal at the moment of the change.

THE FIX

    `publish` wakes the listener list, in the same shape and with the same
    per-listener try/catch `spectrToggleLatencyMode` already uses -- one
    misbehaving subscriber must not stop the others, and must not throw out of
    a click handler. Deliberately NOT a second mechanism: the store and its
    listener list are the one channel the two surfaces share, and the rail is
    already subscribed to it.

    Placed immediately after the optimistic `store.state` write and BEFORE the
    `window.pulp` guard, so the surfaces agree even in a build with no bridge
    attached -- otherwise the panel and the chip would disagree exactly when
    nothing can correct them.

WHAT THIS DOES NOT CHANGE

    The wire contract: `publish` still posts the same `render_mode_set` with
    the same stable token, and the processor remains the owner of the value.
    The figures are still read out of `options` by mode, so the millisecond
    reading beside the label cannot drift from it. Nothing here touches the
    reserved-height hint, which the no-reflow detector measures.
"""
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def enc(text):
    """The page is stored as a JSON *string*, so every `"` is `\\"` on disk."""
    return json.dumps(text)[1:-1]


WAKE = """    // Wake BOTH surfaces, the same way spectrToggleLatencyMode does. The
    // bottom-bar chip re-renders ONLY when woken -- it keeps no state of its
    // own -- so without this it goes on showing the mode, and the millisecond
    // figure, it used to be in while the panel beside it already reads the
    // new one. `pending` above covers this component's own chips and reaches
    // nothing else; the listener list is the only channel the two surfaces
    // share. Each listener is isolated: one bad subscriber must not stop the
    // rest, and must not throw out of a click handler.
    (store.listeners || []).forEach(function (fn) {
      try { fn(); } catch (error) {
        console.error("[Spectr] latency listener failed", error);
      }
    });
"""

EDITS = [
    (
        "the Settings writer wakes the rail chip",
        # find -- the optimistic write, then the bridge guard that follows it.
        ("    store.state = Object.assign({}, store.state, { mode: next });\n"
         "    if (!window.pulp || !window.pulp.postMessage) return;\n"),
        # replace -- the wake lands between them, so the surfaces agree even
        # when there is no bridge to post to.
        ("    store.state = Object.assign({}, store.state, { mode: next });\n"
         + WAKE
         + "    if (!window.pulp || !window.pulp.postMessage) return;\n"),
        # done -- the marker that makes a rerun a no-op.
        ("    store.state = Object.assign({}, store.state, { mode: next });\n"
         "    // Wake BOTH surfaces, the same way spectrToggleLatencyMode does."),
    ),
]

REQUIRED_AFTER = (
    # The fix itself.
    "    store.state = Object.assign({}, store.state, { mode: next });\n"
    + WAKE,
    # The wire contract, unchanged: still a token, still one message type.
    'window.pulp.postMessage("render_mode_set", { mode: next }, "spectr-render-mode")',
    # The reverse direction, which was already right and must stay so.
    "function spectrToggleLatencyMode() {",
    # The rail's subscription, which is what the wake reaches.
    "function SpectrLatencyRail() {",
    # The hint reserve the no-reflow detector measures, untouched.
    "data-spectr-latency-hint-sizer",
)

COUNTS_AFTER = {
    # Three writers now wake the list: hydration, the shared toggle, and
    # Settings. Hydration uses its own `latencyStore` name, so this counts the
    # two that share `store`.
    "(store.listeners || []).forEach(function (fn) {": 2,
    # One optimistic Settings write, and still exactly one message from it.
    "    store.state = Object.assign({}, store.state, { mode: next });": 1,
    'window.pulp.postMessage("render_mode_set", { mode: next }, "spectr-render-mode")': 1,
}


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. Both surfaces and the shared
    # toggle must be present; a document missing any of them is one this script
    # must refuse rather than no-op into "already applied".
    control = (raw.count(enc("function SpectrLatencySettings() {"))
               + raw.count(enc("function SpectrLatencyRail() {"))
               + raw.count(enc("function spectrToggleLatencyMode() {"))
               + raw.count(enc("  const publish = function (next) {")))
    print("control: %d latency anchors (settings + rail + toggle + publish)"
          % control)
    if control != 4:
        print("FAIL: expected 4 anchors, found %d -- wrong document" % control,
              file=sys.stderr)
        return 1

    applied, already = [], []
    for label, find, replace, done in EDITS:
        # `done` alone decides. The replacement CONTAINS its own needle (the
        # wake is an insertion between two lines that both survive), so a rule
        # that also required `find == 0` would re-apply it on every run.
        if raw.count(enc(done)) >= 1:
            already.append(label)
            continue
        found = raw.count(enc(find))
        if found != 1:
            print("FAIL: %r matched %d times, expected exactly 1"
                  % (label, found), file=sys.stderr)
            return 1
        raw = raw.replace(enc(find), enc(replace), 1)
        applied.append(label)

    for token in REQUIRED_AFTER:
        if raw.count(enc(token)) == 0:
            print("FAIL: %r is absent after patching" % (token[:60],),
                  file=sys.stderr)
            return 1
    for token, want in COUNTS_AFTER.items():
        got = raw.count(enc(token))
        if got != want:
            print("FAIL: %r occurs %d times after patching, expected %d"
                  % (token[:60], got, want), file=sys.stderr)
            return 1

    # Parse to adjudicate, never to write: a broken payload here is an editor
    # that does not load at all, and the artifact is one logical line so a human
    # diff will not catch it.
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        print("FAIL: the patched document no longer carries an html payload",
              file=sys.stderr)
        return 1

    if not applied:
        print("already applied: %d edit(s), nothing written" % len(already))
        return 0

    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied %d edit(s), %d already present; %d -> %d bytes"
          % (len(applied), len(already), before, len(raw)))
    for label in applied:
        print("  + " + label)
    return 0


if __name__ == "__main__":
    sys.exit(main())
