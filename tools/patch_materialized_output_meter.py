#!/usr/bin/env python3
"""Show the level Spectr is handing the host, and let it be trimmed there.

Everything this needs already existed and none of it was reachable:

  * `kOutputTrim` (param 2, "Output", +-24 dB) is applied post-render through a
    10 ms smoother and is proved exact by a unit test -- but neither "Output"
    nor "Mix" appeared anywhere in the shipping editor document or its help
    text. Both were host-only parameters, so a user in front of the plug-in
    had no way to reach either.
  * `bridge_.process()` is fed the POST-TRIM buffer, so Pulp's
    VisualizationBridge has been computing per-channel peak and a sample-level
    `clipped` verdict on exactly the signal leaving the plug-in the whole
    time. `Spectr::read_meter()` existed and its only caller was a test.

So this is a UI slice over data that already exists: no DSP, no latency, no
new audio-thread path. `Spectr::read_output_level()` reads the meter on the
UI thread and the existing analyzer tick publishes it as `output_meter`, which
this leaf subscribes to.

Labelled OVER, not CLIP, and it shows the NUMBER rather than a lamp. Spectr is
float end to end and clips nothing internally; what the readout reports is that
the signal handed to the host reached or passed full scale, which is the exact
condition under which anything fixed-point downstream distorts. A held dBFS
value says how far over, which a binary light cannot.

The hold only ever rises, and is cleared by clicking it -- the one event that
means "I have seen this". A decaying hold would let the loudest moment of a
session disappear while nobody was looking, which is the thing a person opens
a meter to find out. It also means the leaf commits only when the printed
value actually moves, which for a rising-only hold is rare: this readout is
not another per-frame React commit on the drag path.

Why a script and not a hand edit: the materialized generator is broken on main
(exits 1, writes 0 patches) and `tools/patch_materialized_editor.py` aborts on
a clean checkout at a stale needle, so the artifact cannot be regenerated. This
edits the `html` payload by exact-text substitution, asserts every patch point
is unique before writing, and re-checks the result, so the change is replayable
after a merge conflict rather than being an opaque one-line diff.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# spectr::Spectr::kOutputTrim. Sent as the flat id the existing `param_set`
# verb already accepts, so this control needs no new bridge command.
OUTPUT_TRIM_PARAM_ID = 2

COMPONENT = '''function SpectrOutputMeter() {
  // OVER, not CLIP. Spectr is float end to end and clips nothing internally;
  // this reports that the signal HANDED TO THE HOST reached full scale, which
  // is when anything fixed-point downstream distorts. The number is shown
  // rather than a lamp because how far over is the actionable part, and a
  // lamp cannot say it.
  const [reading, setReading] = React.useState({ peakDb: null, over: false });
  const [trim, setTrim] = React.useState(0);
  // The hold lives in a ref, not in state: it is updated on every published
  // frame and must not commit React on the frames where nothing printed
  // changes, which for a rising-only hold is almost all of them.
  const holdRef = React.useRef({ peakDb: null, over: false });
  const localTrimAtRef = React.useRef(0);
  React.useEffect(() => {
    if (!window.pulp || typeof window.pulp.on !== "function") {
      // Nothing to meter without the native bridge -- say so rather than
      // sitting at "--" forever, which is indistinguishable from silence.
      console.error("[Spectr] output meter found no native bridge");
      return;
    }
    const unsubscribe = window.pulp.on("output_meter", (message) => {
      const payload = message && message.payload;
      if (!payload) return;
      const raw = payload.peak_db;
      // Digital silence publishes null, never a number. A meter that reports
      // 0 for silence cannot be told apart from one reporting full scale.
      const peak = typeof raw === "number" && isFinite(raw) ? raw : null;
      const hold = holdRef.current;
      if (peak !== null && (hold.peakDb === null || peak > hold.peakDb))
        hold.peakDb = peak;
      if (payload.over === true) hold.over = true;
      setReading((previous) => previous.peakDb === hold.peakDb
        && previous.over === hold.over
        ? previous
        : { peakDb: hold.peakDb, over: hold.over });
      const published = payload.trim_db;
      // The published trim is what makes host automation move this control.
      // Ignore it briefly after a local write so the round trip cannot fight
      // a drag in progress with a value one frame stale.
      if (typeof published === "number" && isFinite(published)
          && Date.now() - localTrimAtRef.current > 400) {
        setTrim((previous) => Math.abs(previous - published) < 0.01
          ? previous : published);
      }
    });
    return unsubscribe;
  }, []);
  const writeTrim = (value) => {
    if (!isFinite(value)) return;
    const next = Math.max(-24, Math.min(24, value));
    localTrimAtRef.current = Date.now();
    setTrim(next);
    try {
      window.pulp.postMessage("param_set",
        { id: ''' + str(OUTPUT_TRIM_PARAM_ID) + ''', value: next },
        "spectr-output-trim");
    } catch (error) {
      console.error("[Spectr] output trim write failed", error);
    }
  };
  const resetHold = () => {
    holdRef.current = { peakDb: null, over: false };
    setReading({ peakDb: null, over: false });
  };
  const over = reading.over === true;
  const peakText = reading.peakDb === null
    ? "--"
    : (reading.peakDb > 0 ? "+" : "") + reading.peakDb.toFixed(1);
  const trimText = (trim > 0 ? "+" : "") + trim.toFixed(1);
  // ABSOLUTELY POSITIONED, and that is not a shortcut. The top bar's nodes
  // carry positions captured from the imported document, so a new flex child
  // does not make its siblings reflow: appended to the right-hand readout
  // cluster it ran off the 1320pt canvas to x=1414, and placed before the
  // spacer it was laid out from x=20, straight over the brand block. Both
  // were caught by `native buttons are tappable across their whole painted
  // bounds`, which is the gate for exactly this.
  //
  // The header's own flex spacer is empty from x=281.7 to x=669.5 (measured
  // from a SPECTR_LAYOUT_DUMP of the authored 1320x860 box), so this sits at
  // 282 and runs 96 peak + 14 + ~41 OUTPUT + 14 + 156 track + 14 + 34
  // readout, ending at ~651 -- inside that gap, with ~36pt of clear header
  // before the LIVE button at x=687.5, and out of the flow that cannot
  // absorb it. A longer track would eat that clearance.
  return /* @__PURE__ */ React.createElement("span", {
    "data-spectr-output-cluster": true,
    style: {
    position: "absolute",
    left: 282,
    top: 9,
    height: 26,
    // Above the top bar's own zIndex 5 and below the overlays at 50. As a
    // later sibling with no z of its own it painted under the bar, and
    // `hit_test` resolved every point inside this button to the bar instead --
    // a control that is visible and completely dead.
    zIndex: 6,
    display: "inline-flex",
    alignItems: "center",
    // 14, not the 6 a tight cluster would use: the runtime expands a
    // control's hit region past its painted bounds, and at 6 the trim's
    // slop reached back across the peak button's right edge.
    gap: 14,
    flexShrink: 0
  } }, /* @__PURE__ */ React.createElement("button", {
    "data-spectr-output-peak": true,
    "data-spectr-output-over": over ? "true" : "false",
    "aria-label": "Output peak level since reset, dBFS. Activate to reset.",
    title: over
      ? "The signal handed to the host reached full scale. Click to reset."
      : "Peak level handed to the host since reset, dBFS. Click to reset.",
    onClick: resetHold,
    style: {
      background: over ? "rgba(255,110,110,0.20)" : "rgba(255,255,255,0.03)",
      border: "1px solid " + (over ? "rgba(255,150,150,0.55)" : "rgba(255,255,255,0.10)"),
      color: over ? "rgb(255,196,196)" : "rgba(255,255,255,0.72)",
      padding: "5px 8px",
      width: 96,
      minWidth: 96,
      flexShrink: 0,
      minHeight: 22,
      boxSizing: "border-box",
      borderRadius: 3,
      fontFamily: "var(--mono)",
      fontSize: 10,
      letterSpacing: 0.8,
      cursor: "pointer",
      display: "inline-flex",
      alignItems: "center",
      justifyContent: "center",
      gap: 4,
      lineHeight: 1
    }
  }, /* @__PURE__ */ React.createElement("span", { className: "tnum", style: {
    lineHeight: 1, whiteSpace: "nowrap"
  } }, (over ? "OVER " : "PEAK ") + peakText)),
    /* @__PURE__ */ React.createElement("span", {
      "data-spectr-output-trim-label": true,
      // Declares its own face, size, tracking and colour. A control that
      // declares no type inherits the document body default, which is
      // how the readout beside it shipped 47% taller than every other
      // readout in this header.
      style: { fontFamily: "var(--mono)", fontSize: 10, letterSpacing: 0.8, color: "rgba(255,255,255,0.72)", lineHeight: 1, whiteSpace: "nowrap", flexShrink: 0 }
    }, "OUTPUT"),
    /* @__PURE__ */ React.createElement("input", {
      "data-spectr-output-trim": true,
      type: "range",
      min: -24,
      max: 24,
      step: 0.5,
      value: trim,
      "aria-label": "Output trim, decibels",
      title: "Output trim, dB",
      onChange: (event) => writeTrim(parseFloat(event.target.value)),
      style: { width: 156, flexShrink: 0, accentColor: "hsl(200,80%,60%)" }
    }),
    /* @__PURE__ */ React.createElement("span", {
      "data-spectr-output-trim-readout": true,
      className: "tnum",
      // Declares its own face, size and colour rather than inheriting the
      // document body default: unstated, this number rendered 47% taller and
      // far brighter than every other readout in the header, and overflowed
      // its own 34pt box at the trim extremes. Matches the PEAK button above.
      style: { width: 34, textAlign: "right", whiteSpace: "nowrap", flexShrink: 0, fontFamily: "var(--mono)", fontSize: 10, color: "rgba(255,255,255,0.72)" }
    }, trimText));
}
'''

ZOOM_HEADER = "function SpectrZoomReadout({ bankRef }) {\n"

# WHERE this goes is load bearing, and two earlier attempts got it wrong in
# ways worth recording, because the failure is silent in the document and loud
# somewhere else entirely.
#
# This document carries CAPTURED bindings -- `text_bindings`,
# `layout_bindings`, `paint_bindings` -- and each one addresses its node by a
# positional DOM path: `[{div,0},{div,1},{div,0},{span,1}]`. Inserting a child
# anywhere but the END of a parent therefore renumbers every later sibling and
# silently re-points those bindings at the wrong nodes. Appending the cluster
# inside the header's flex row did exactly that: the visualization buttons lost
# their captured text basis and remeasured at a fallback 20pt instead of 48pt
# ("BARS"), their hit boxes shrank to 20x18, and the band-count group was laid
# out twice. Nothing in the patched JS looks wrong; `native buttons are
# tappable across their whole painted bounds` is what says so.
#
# An earlier attempt failed differently and is worth keeping too: appended to
# the right-hand readout cluster, which already ends at the authored right
# padding (x=1300 of 1320), the controls ran off the canvas to x=1414 and their
# hit regions reached back over the neighbour's painted bounds.
#
# So: appended as the LAST child of Chrome's own fragment, which renumbers
# nothing, and positioned absolutely at x=282. That is the start of the
# header's empty flex gap, measured at x=281.7..669.5 from a
# SPECTR_LAYOUT_DUMP of the authored 1320x860 box; the cluster is 216pt wide
# and ends at 498, with ~170pt to spare before the LIVE/PRECISION control.
CHROME_TAIL = ('onLearnMore: () => { setHelpOpen(false); setHelpGuideOpen(true); }'
               ' }))));\n}')

# A fourth field, PROBE, is the STABLE needle that answers "is this edit
# already in the document". Keying that question on the whole replacement text
# was wrong and this is the incident: a later slice edited the component body,
# after which `COMPONENT + ZOOM_HEADER` no longer occurred, this script read
# that as "not applied", and a replay re-inserted a SECOND, older copy of the
# leaf. It failed closed on the duplicate-count assertion rather than writing
# it -- but the script had stopped being replayable, which is the only reason
# it exists. A probe that is a declaration rather than a body survives any
# later edit to that body, and is still absent from a clean pre-slice document.
EDITS = [
    ('the output meter leaf is declared beside the zoom readout',
     ZOOM_HEADER,
     COMPONENT + ZOOM_HEADER,
     'function SpectrOutputMeter() {'),

    ('the toolbar renders it without renumbering a captured sibling',
     CHROME_TAIL,
     'onLearnMore: () => { setHelpOpen(false); setHelpGuideOpen(true); } })))'
     ', /* @__PURE__ */ React.createElement(SpectrOutputMeter, null));\n}',
     'React.createElement(SpectrOutputMeter, null));'),
]

REQUIRED_AFTER = (
    'function SpectrOutputMeter() {',
    '"data-spectr-output-trim-label": true,',
    'React.createElement(SpectrOutputMeter, null));',
    'window.pulp.on("output_meter"',
    'window.pulp.postMessage("param_set",',
    '"data-spectr-output-over": over ? "true" : "false",',
    '(over ? "OVER " : "PEAK ") + peakText',
)

# The label must stay OVER. CLIP would claim Spectr clipped the signal, which
# it never does -- it is float end to end.
FORBIDDEN_AFTER = ('"CLIP "', 'CLIP ' + chr(34))

REQUIRED_COUNTS = {
    'function SpectrOutputMeter() {': 1,
    'React.createElement(SpectrOutputMeter, null)': 1,
}


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for label, old, new, probe in EDITS:
        old_e, new_e, probe_e = escaped(old), escaped(new), escaped(probe)
        if raw.count(probe_e):
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        applied += 1
        print('applied         ', label)

    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')

    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))
    for token in FORBIDDEN_AFTER:
        if raw.count(escaped(token)):
            sys.exit('FAIL: %r appears after patching; this readout is OVER, '
                     'never CLIP -- Spectr clips nothing internally' % (token,))
    for token, want in REQUIRED_COUNTS.items():
        got = raw.count(escaped(token))
        if got != want:
            sys.exit('FAIL: %r appears %d times after patching, expected %d'
                     % (token, got, want))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change needed')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
