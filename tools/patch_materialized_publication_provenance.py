#!/usr/bin/env python3
"""Every full-state publication says which state it was drawn from, and reads
the live viewport.

THE DEFECT

  The editor publishes its whole picture -- every band, the viewport and the
  band count -- on every edit (`processing_state_set`), a frame after the render
  it read. Host automation reaches the same state from the parameter-sync
  worker, a real thread, and is handed to the editor once per frame as a live
  projection. Native merges a publication against the state the editor was
  handed, treating a value equal to it as carried over rather than edited, so a
  stale publication cannot revert a host write.

  "The state the editor was handed" is not always the state it drew from. A
  band-count projection is not applied at once: the editor asks for a resync
  and keeps drawing the old count until the re-hydration lands. A publication
  built in that window carries the OLD count, and diffed against the newer
  projection it reads as a user edit -- so the host's band count is put back
  and written to the host as a parameter change.

  Separately, both publication sites read the viewport through `view`, the
  object `viewRef.current` was bound to when the component rendered. Host
  automation mutates that object in place, so it stays live -- but `setView`
  replaces it, and a publication issued between a `setView` and the next render
  sends the window the editor has already left.

THE FIX

  Both sites build their publication through one helper that reads the live
  viewport off `viewRef` and stamps `drawn_revision`: the revision of the last
  state the editor APPLIED (`nativeAppliedRevisionRef`, which hydration, every
  applied command response and every applied live projection advance, and
  nothing else). Native diffs the publication against exactly that state.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once, refuses a
half-patched document, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "spectrPublicationProvenance"

EDITS = [
    ('the direct publication reads the live viewport and names its base',
     '  const queueNativeProcessingStatePublication = () => {\n'
     '    if (!window.pulp || typeof window.pulp.postMessage !== "function") return;\n'
     '    const current = targetGainsRef.current;\n'
     '    const muted = current.map((value) => isMuted(value));\n'
     '    const gainDb2 = current.map((value, i) => isMuted(value) ? Number.isFinite(mutedGainDbRef.current[i]) ? mutedGainDbRef.current[i] : 0 : clamp(Number.isFinite(value) ? value : 0, -1, 1) * 24);\n'
     '    nativeDirectPublicationSignatureRef.current = JSON.stringify([\n'
     '      N, gainDb2, muted, view.lmin, view.lmax\n'
     '    ]);\n'
     '    ++nativeCommandSequenceRef.current;\n'
     '    Promise.resolve(window.pulp.postMessage("processing_state_set", {\n'
     '      n_visible: N,\n'
     '      gain_db: gainDb2,\n'
     '      muted,\n'
     '      min_hz: Math.pow(10, view.lmin),\n'
     '      max_hz: Math.pow(10, view.lmax)\n'
     '    }, "spectr-processing-state")).catch((error) => {\n',
     '  // What a full-state publication was drawn from, read when it is SENT.\n'
     '  // The viewport comes off viewRef, never the render-time `view`: setView\n'
     '  // replaces that object, so a publication issued before the next render\n'
     '  // would carry the window the editor already left. `drawn` is the\n'
     '  // revision of the last state this editor APPLIED -- hydration, applied\n'
     '  // command responses and applied live projections advance it, nothing\n'
     '  // else -- so native can tell a value the editor carried over from that\n'
     '  // state from one the user set, even when a newer projection has been\n'
     '  // handed over but not yet applied (a band-count change waits for a\n'
     '  // resync). Without it, the carried-over count reverted the host\'s.\n'
     '  const spectrPublicationProvenance = () => ({\n'
     '    lmin: viewRef.current.lmin,\n'
     '    lmax: viewRef.current.lmax,\n'
     '    drawn: nativeAppliedRevisionRef.current\n'
     '  });\n'
     '  const queueNativeProcessingStatePublication = () => {\n'
     '    if (!window.pulp || typeof window.pulp.postMessage !== "function") return;\n'
     '    const current = targetGainsRef.current;\n'
     '    const live = spectrPublicationProvenance();\n'
     '    const muted = current.map((value) => isMuted(value));\n'
     '    const gainDb2 = current.map((value, i) => isMuted(value) ? Number.isFinite(mutedGainDbRef.current[i]) ? mutedGainDbRef.current[i] : 0 : clamp(Number.isFinite(value) ? value : 0, -1, 1) * 24);\n'
     '    nativeDirectPublicationSignatureRef.current = JSON.stringify([\n'
     '      N, gainDb2, muted, live.lmin, live.lmax\n'
     '    ]);\n'
     '    ++nativeCommandSequenceRef.current;\n'
     '    Promise.resolve(window.pulp.postMessage("processing_state_set", {\n'
     '      n_visible: N,\n'
     '      gain_db: gainDb2,\n'
     '      muted,\n'
     '      min_hz: Math.pow(10, live.lmin),\n'
     '      max_hz: Math.pow(10, live.lmax),\n'
     '      drawn_revision: live.drawn\n'
     '    }, "spectr-processing-state")).catch((error) => {\n'),
    ('the render publication reads the live viewport and names its base',
     '      const current = targetGainsRef.current;\n'
     '      const muted = current.map((value) => isMuted(value));\n'
     '      const gainDb2 = current.map((value, i) => isMuted(value) ? Number.isFinite(mutedGainDbRef.current[i]) ? mutedGainDbRef.current[i] : 0 : clamp(Number.isFinite(value) ? value : 0, -1, 1) * 24);\n'
     '      const publicationSignature = JSON.stringify([\n'
     '        N, gainDb2, muted, view.lmin, view.lmax\n'
     '      ]);\n'
     '      if (publicationSignature === nativeDirectPublicationSignatureRef.current) return;\n'
     '      try {\n'
     '        ++nativeCommandSequenceRef.current;\n'
     '        window.pulp.postMessage("processing_state_set", {\n'
     '          n_visible: N,\n'
     '          gain_db: gainDb2,\n'
     '          muted,\n'
     '          min_hz: Math.pow(10, view.lmin),\n'
     '          max_hz: Math.pow(10, view.lmax)\n'
     '        }, "spectr-processing-state");\n',
     '      const current = targetGainsRef.current;\n'
     '      const live = spectrPublicationProvenance();\n'
     '      const muted = current.map((value) => isMuted(value));\n'
     '      const gainDb2 = current.map((value, i) => isMuted(value) ? Number.isFinite(mutedGainDbRef.current[i]) ? mutedGainDbRef.current[i] : 0 : clamp(Number.isFinite(value) ? value : 0, -1, 1) * 24);\n'
     '      const publicationSignature = JSON.stringify([\n'
     '        N, gainDb2, muted, live.lmin, live.lmax\n'
     '      ]);\n'
     '      if (publicationSignature === nativeDirectPublicationSignatureRef.current) return;\n'
     '      try {\n'
     '        ++nativeCommandSequenceRef.current;\n'
     '        window.pulp.postMessage("processing_state_set", {\n'
     '          n_visible: N,\n'
     '          gain_db: gainDb2,\n'
     '          muted,\n'
     '          min_hz: Math.pow(10, live.lmin),\n'
     '          max_hz: Math.pow(10, live.lmax),\n'
     '          drawn_revision: live.drawn\n'
     '        }, "spectr-processing-state");\n'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 1:
        print('already applied  publications name the state they were drawn from')
        return 0
    for label, old, new in EDITS:
        if raw.count(escaped(old)) != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, raw.count(escaped(old))))
    for label, old, new in EDITS:
        raw = raw.replace(escaped(old), escaped(new), 1)
        print('applied         ', label)
    document = json.loads(raw)
    html = document.get('html')
    if not isinstance(html, str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    for label, _old, new in EDITS:
        if html.count(new) != 1:
            sys.exit('FAIL %s: did not apply exactly once' % label)
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
