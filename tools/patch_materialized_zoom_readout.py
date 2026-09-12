#!/usr/bin/env python3
"""Move the zoom readout off an app-root poll and onto a viewport subscription.

The app root ran `setInterval(..., 150)` that read the live viewport off the
bank handle and wrote the result into root state.  A memo guard kept the write
out of unchanged frames, but zoom is exactly the value that DOES move while the
user is dragging the minimap, so during the one interaction that has to stay
cheap the root committed every 150ms.  A root commit re-applies the whole
captured import document, which is why band drawing and minimap interaction
felt heavy.

The fix is structural, not a throttle: the bank owns the viewport, so it
publishes viewport changes to subscribers, and the readout is a leaf component
that subscribes.  The interval is gone, and the leaf commits only when a
gesture SETTLES.

That last part is the whole fix and it was learned the expensive way.  Simply
moving the readout onto the publication and coalescing it "to one commit per
frame" is not an improvement -- it is a REGRESSION.  A resize gesture moves
the second decimal place on nearly every frame, so per-frame coalescing still
commits every frame, which roughly doubles the commit rate the 150ms poll had:
measured on the minimap workload, frame gaps >= 25ms went 16 -> 29 by Perfetto
and 17 -> 42 by cadence probe, with __flushFrames__ climbing 533ms -> 1265ms
even though __flushTimers__ correctly collapsed 397ms -> 0.28ms.  Killing the
timer is necessary and nowhere near sufficient.

So the publication carries a `live` flag: the per-pointer-sample path marks
itself live and the leaf ignores it outright, and the readout repaints when
the gesture settles (pointer-up, the deferred wheel commit, or any ordinary
setView).  The live number is not lost -- drawMinimap paints the same x-value
onto the minimap canvas every frame, off the React path entirely -- so the
React copy is redundant precisely while the drag is in flight.

Why one commit is worth this much care: the cost is not the readout, it is
what a commit triggers.  resetAfterCommit gates on a single
materializedTreeDirty boolean that commitTextUpdate sets unconditionally, so a
one-character text change in one leaf re-applies captured import metadata
across the whole document -- about 22ms, which is 1.5 vsync intervals, so
every one of them drops a frame.

Applied by hand because `tools/patch_materialized_editor.py` -- the mirror that
would normally carry an edit like this -- does not run on this checkout: it
aborts at `semantic popup surfaces delegate dismissal to Pulp: patch point
occurs 3 times` with seven further needles missing, and writes nothing.  This
script is deliberately narrow: it edits the `html` payload of the shipping
runtime document by exact-text substitution, asserts every patch point is
unique before writing, and re-checks the result, so it is replayable and
reviewable rather than an opaque artifact diff.

resources/editor.html is deliberately NOT mirrored. It is the browser
bootstrap, not the shipping surface -- the native editor loads the runtime
document -- and test_import_fidelity.cpp's kPatchNeedles pin its pre-patch
shape on purpose, so editing it would break the needles that exist to prove
the mirror's own patch points still match. The durable record of this change
is this file.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# The leaf.  It renders the identical span the app root used to render -- same
# className, same style object, same text -- so the captured layout and every
# text binding that addresses it are untouched.  Only the owner of the value
# changes.
READOUT_COMPONENT = '''function SpectrZoomReadout({ bankRef }) {
  const [zoom, setZoom] = React.useState("1.00");
  React.useEffect(() => {
    const full = Math.log10(2e4) - Math.log10(20);
    let disposed = false;
    let frame = 0;
    let unsubscribe = null;
    let attempts = 0;
    const apply = (view) => {
      if (disposed || !view) return;
      const span = view.lmax - view.lmin;
      if (!(span > 0)) return;
      const next = (full / span).toFixed(2);
      setZoom((previous) => previous === next ? previous : next);
    };
    // A gesture publishes per pointer sample, and a resize moves the second
    // decimal place on nearly every one of them -- so coalescing to one
    // commit per FRAME is still a commit per frame, and every commit
    // re-applies the captured import metadata across the whole document.
    // Cost nothing at all until the gesture settles. The live value is not
    // lost: drawMinimap paints the same x-number onto the minimap canvas each
    // frame, off the React path, so this copy is redundant exactly while a
    // drag is in flight.
    const onViewport = (view, live) => {
      if (live) return;
      apply(view);
    };
    // The bank publishes the viewport, and the bank handle is installed by an
    // effect. Sibling effect order already puts that before this one, but a
    // readout that silently never subscribes would look like a correct
    // "1.00" forever, so retry rather than assume, and say so if it never
    // arrives.
    const attach = () => {
      if (disposed) return;
      const bank = bankRef && bankRef.current;
      if (bank && typeof bank.subscribeViewport === "function") {
        unsubscribe = bank.subscribeViewport(onViewport);
        apply(bank.view);
        return;
      }
      if (++attempts > 600) {
        console.error("[Spectr] zoom readout found no viewport publisher");
        return;
      }
      frame = requestAnimationFrame(() => {
        frame = 0;
        attach();
      });
    };
    attach();
    return () => {
      disposed = true;
      if (frame) cancelAnimationFrame(frame);
      if (unsubscribe) unsubscribe();
    };
  }, [bankRef]);
  return /* @__PURE__ */ React.createElement("span", { className: "tnum", style: { whiteSpace: "nowrap", flexShrink: 0, minWidth: 84 } }, zoom, "\\xD7 zoom");
}
'''

EDITS = [
    ('bank publishes viewport changes to subscribers',
     '  const view = viewRef.current;\n'
     '  const setView = (next) => {\n'
     '    const resolved = typeof next === "function" ? next(viewRef.current) : next;\n'
     '    viewRef.current = { ...resolved };\n'
     '    setReactView({ ...resolved });\n'
     '  };\n',
     '  const viewportListenersRef = useRef(null);\n'
     '  if (!viewportListenersRef.current) viewportListenersRef.current = /* @__PURE__ */ new Set();\n'
     '  const notifyViewportListeners = (live) => {\n'
     '    for (const listener of Array.from(viewportListenersRef.current)) {\n'
     '      try {\n'
     '        listener(viewRef.current, live === true);\n'
     '      } catch (error) {\n'
     '        console.error("[Spectr] viewport listener failed", error);\n'
     '      }\n'
     '    }\n'
     '  };\n'
     '  const subscribeViewport = (listener) => {\n'
     '    if (typeof listener !== "function") return () => {\n'
     '    };\n'
     '    viewportListenersRef.current.add(listener);\n'
     '    return () => {\n'
     '      viewportListenersRef.current.delete(listener);\n'
     '    };\n'
     '  };\n'
     '  const view = viewRef.current;\n'
     '  const setView = (next) => {\n'
     '    const resolved = typeof next === "function" ? next(viewRef.current) : next;\n'
     '    viewRef.current = { ...resolved };\n'
     '    setReactView({ ...resolved });\n'
     '    notifyViewportListeners(false);\n'
     '  };\n'
     '  // The deferred wheel commit settles the viewport without going\n'
     '  // through setView, so it needs the same settle publication.\n'
     '  const settleViewport = () => {\n'
     '    setReactView({ ...viewRef.current });\n'
     '    notifyViewportListeners(false);\n'
     '  };\n'),

    ('live drag viewport commits notify subscribers',
     '  const commitLiveViewport = (next) => {\n'
     '    viewRef.current.lmin = next.lmin;\n'
     '    viewRef.current.lmax = next.lmax;\n'
     '    queueNativeProcessingStatePublication();\n'
     '  };\n',
     '  const commitLiveViewport = (next) => {\n'
     '    viewRef.current.lmin = next.lmin;\n'
     '    viewRef.current.lmax = next.lmax;\n'
     '    queueNativeProcessingStatePublication();\n'
     '    notifyViewportListeners(true);\n'
     '  };\n'),

    ('native state projection notifies subscribers',
     '        viewRef.current.lmin = Math.log10(state.minHz);\n'
     '        viewRef.current.lmax = Math.log10(state.maxHz);\n'
     '        return true;\n',
     '        viewRef.current.lmin = Math.log10(state.minHz);\n'
     '        viewRef.current.lmax = Math.log10(state.maxHz);\n'
     '        notifyViewportListeners(false);\n'
     '        return true;\n'),

    ('bank handle exposes the viewport subscription',
     '      getGains: () => Array.from(targetGainsRef.current),\n'
     '      view,\n'
     '      N\n'
     '    };\n',
     '      getGains: () => Array.from(targetGainsRef.current),\n'
     '      subscribeViewport,\n'
     '      view,\n'
     '      N\n'
     '    };\n'),

    # Two sites, both the deferred wheel commit. They settle the viewport by
    # calling setReactView directly rather than setView, so without this the
    # readout would never repaint after a wheel gesture.
    ('deferred wheel commits settle through the publisher',
     # The two sites differ in indentation, so the needle starts at the
     # statement rather than at the line.
     'wheelCommitRef.current = setTimeout(() => setReactView({ ...viewRef.current }), 80);',
     'wheelCommitRef.current = setTimeout(settleViewport, 80);', 2),

    ('zoom readout is a leaf that owns its own value',
     'function Chrome({ settings, setSettings, bankRef, info, status,',
     READOUT_COMPONENT
     + 'function Chrome({ settings, setSettings, bankRef, status,'),

    ('chrome renders the leaf readout',
     'React.createElement("span", { className: "tnum", style: { whiteSpace: "nowrap", flexShrink: 0, minWidth: 84 } }, info.zoom, "\\xD7 zoom")',
     'React.createElement(SpectrZoomReadout, { bankRef })'),

    # The polled N was always settings.bandCount one tick late: FilterBank
    # derives its N from that same setting. Read the setting directly so the
    # highlight stops depending on a poll, and stops lagging the label beside
    # it, which already read settings.bandCount.
    ('band-count highlight reads the setting it is set from',
     '        background: info.N === n ? "rgba(120,180,255,0.18)" : "rgba(255,255,255,0.03)",\n'
     '        border: "1px solid " + (info.N === n ? "rgba(180,210,255,0.4)" : "rgba(255,255,255,0.1)"),\n'
     '        color: info.N === n ? "#fff" : "rgba(255,255,255,0.7)",\n',
     '        background: settings.bandCount === n ? "rgba(120,180,255,0.18)" : "rgba(255,255,255,0.03)",\n'
     '        border: "1px solid " + (settings.bandCount === n ? "rgba(180,210,255,0.4)" : "rgba(255,255,255,0.1)"),\n'
     '        color: settings.bandCount === n ? "#fff" : "rgba(255,255,255,0.7)",\n'),

    ('app root no longer polls the viewport',
     '  useAppE(() => {\n'
     '    const iv = setInterval(() => {\n'
     '      const b = bankRef.current;\n'
     '      if (!b) return;\n'
     '      const v = b.view;\n'
     '      const full = Math.log10(2e4) - Math.log10(20);\n'
     '      const span = v.lmax - v.lmin;\n'
     '      const nextInfo = { N: b.N, zoom: (full / span).toFixed(2) };\n'
     '      setInfo((previous) => previous.N === nextInfo.N && previous.zoom === nextInfo.zoom ? previous : nextInfo);\n'
     '    }, 150);\n'
     '    return () => clearInterval(iv);\n'
     '  }, []);\n',
     ''),

    ('app root drops the polled viewport state',
     '  const [info, setInfo] = useAppS({ N: settings.bandCount, zoom: "1.00" });\n',
     ''),

    ('pattern manager reads the band count setting',
     '      N: info.N,\n',
     '      N: settings.bandCount,\n'),

    ('chrome no longer receives polled viewport state',
     '      settings,\n'
     '      setSettings,\n'
     '      bankRef,\n'
     '      info,\n'
     '      status,\n',
     '      settings,\n'
     '      setSettings,\n'
     '      bankRef,\n'
     '      status,\n'),
]

# After the edits, nothing in the app root or the chrome may still read the
# polled object. SpectrBuildInfo has its own unrelated local named `info`, so
# assert on the two properties the poller owned rather than on the identifier.
FORBIDDEN_AFTER = ('info.zoom', 'info.N', 'setInfo((previous)')
REQUIRED_AFTER = ('function SpectrZoomReadout(', 'subscribeViewport',
                  'React.createElement(SpectrZoomReadout, { bankRef })',
                  # The live/settle split IS the fix. Without the guard the
                  # leaf commits per frame, which measured worse than the poll
                  # it replaced.
                  'const onViewport = (view, live) => {',
                  'notifyViewportListeners(true);',
                  'setTimeout(settleViewport, 80)')


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for edit in EDITS:
        label, old, new = edit[:3]
        if old and old in new:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for edit in EDITS:
        label, old, new = edit[:3]
        expected = edit[3] if len(edit) == 4 else 1
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(old_e) == 0 and (new_e == '' or raw.count(new_e) >= expected):
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != expected:
            sys.exit('FAIL %s: patch point occurs %d times, expected %d'
                     % (label, count, expected))
        raw = raw.replace(old_e, new_e)
        changed = True
        applied += 1
        print('applied         ', label)

    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))

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
