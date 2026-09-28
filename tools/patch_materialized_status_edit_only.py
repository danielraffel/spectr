#!/usr/bin/env python3
"""Show the band reading in the status pill only when a gesture changes a band.

The top-centre status pill ("12.3kHz   -1.3 dB   BAND 60/64") is feedback for an
EDIT. It was also being written when nothing had been edited:

  * PRESS. `onPointerDown` published the pressed band's reading at once, for
    the ordinary press and for the shift mute brush alike, so touching a band
    to look at it put the pill up.
  * LABEL CHANGES. `updateLiveHoverStatus` runs on every pointer move and on
    every frame of the draw loop while a gain or mute-brush gesture is held,
    and wrote whenever the reading under the pointer changed. The reading was
    built from `renderGainsRef`, the painted value, which moves by itself:
    under a running LFO the painted gain changes every frame, and the render
    smoother keeps moving after an edit has stopped. Crossing bands without
    changing any of them changed the label too. So a held pointer under an LFO,
    or a drag that altered nothing, kept rewriting the pill.
  * RELEASE. `onPointerUp` published a reading for every gain or mute-brush
    gesture, whether or not it had changed anything.

The rule this patch installs: a reading is shown or updated only when the
gesture actually changed a band -- its target gain or its mute state -- or the
macro the gesture drives. The commit paths know that, so they say so:

  * `commitGain` and `commitMany` compare each band's target before and after
    (two muted values are equal whatever their stored level) and call
    `noteGestureEdit(band)` only for a band that changed. `driveMacro` does the
    same for a macro whose value moved, naming the pressed band.
  * `noteGestureEdit` records on the live pointer that the gesture changed
    something and which band to read out: the band under the pointer when it
    is one of the changed bands, otherwise the first band the gesture changed.
    Outside a drawing gesture it does nothing, so menus, keys and host
    automation are unaffected.
  * `updateLiveHoverStatus` reads out `pointer.editedBand`, not the hovered
    band, and does nothing until a band has been edited. Its reading is built
    from the band's TARGET, the value the user set, so neither modulation nor
    the render smoother can change the label; the label changes only when an
    edit changes it, and an unchanged label is still not rewritten.
  * The press publishes nothing. A shift press that mutes a band IS an edit,
    so it is read out -- through the same path, once the brush has painted.
  * The release re-publishes the edited band's reading only if the gesture
    changed something. A click that toggles mute still reports
    "BAND n MUTED" from its own path; that toggle is the edit.

Cost properties are unchanged: no React commit on move, press or release. The
first reading of a gesture puts the pill up with `spectrStatusBannerShow`,
which is a direct write; later readings take the existing direct-write path
while the pill is up. A pill already showing from a real edit finishes its own
fade.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once before
writing, refuses a half-patched document, re-checks the result, and reports
"already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

EDITS = [
    ('the reading is the band target, not the painted value',
     '    const rendered = macroAdjustedGain(renderGainsRef.current[band], band);\n'
     '    const gv = Number.isFinite(rendered) ? clamp(rendered, -1.02, 1.02) : 0;\n',
     '    // The TARGET the user set, not the painted value: modulation and the\n'
     '    // render smoother move the painted value with no edit behind them.\n'
     '    const edited = macroAdjustedGain(targetGainsRef.current[band], band);\n'
     '    const gv = Number.isFinite(edited) ? clamp(edited, -1.02, 1.02) : 0;\n'),

    ('a gesture records the bands it actually changed',
     '  const updateLiveHoverStatus = () => {\n'
     '    const current = hoverRef.current;\n'
     '    const pointer = pointerRef.current;\n'
     '    // The pill reads out a band only while it is being drawn; release shows\n'
     '    // the final value (onPointerUp) and the pill fades on its own.\n'
     '    if (!pointer || (pointer.mode !== "gain" && pointer.mode !== "mute-brush")) return;\n'
     '    if (hoverBandOf(current) < 0) return;\n'
     '    const label = liveHoverLabel(current);\n',
     '  // Called by the commit paths for a band whose target or mute state a\n'
     '  // drawing gesture actually changed. The pill reads out the band under the\n'
     '  // pointer when it is one of them, otherwise the first one changed.\n'
     '  const noteGestureEdit = (index) => {\n'
     '    const pointer = pointerRef.current;\n'
     '    if (!pointer || (pointer.mode !== "gain" && pointer.mode !== "mute-brush")) return;\n'
     '    if (!(index >= 0 && index < N)) return;\n'
     '    pointer.changed = true;\n'
     '    if (!(pointer.editedBand >= 0) || index === hoverBandOf(hoverRef.current))\n'
     '      pointer.editedBand = index;\n'
     '  };\n'
     '  const sameBandTarget = (a, b) => a === b || (isMuted(a) && isMuted(b));\n'
     '  const updateLiveHoverStatus = () => {\n'
     '    const pointer = pointerRef.current;\n'
     '    // The pill reads out a band only once the gesture has changed one: not\n'
     '    // on a press, not on a drag that alters nothing, and never because\n'
     '    // modulation moved what is painted.\n'
     '    if (!pointer || (pointer.mode !== "gain" && pointer.mode !== "mute-brush")) return;\n'
     '    const current = { band: pointer.editedBand, n: N };\n'
     '    if (hoverBandOf(current) < 0) return;\n'
     '    const label = liveHoverLabel(current);\n'),

    ('the first edit of a gesture puts the pill up',
     '      spectrPlaceStatusBanner(shown, spectrStatusBannerWidth(label));\n'
     '    }\n',
     '      spectrPlaceStatusBanner(shown, spectrStatusBannerWidth(label));\n'
     '    } else {\n'
     '      // No pill up: the first reading of a gesture, or one after the pill\n'
     '      // faded. The direct show is a write, not a render.\n'
     '      showStatus(label);\n'
     '    }\n'),

    ('commitGain notes a band it changed',
     '    targetGainsRef.current[idx] = value;\n'
     '    // The mirror is written FROM the ref.',
     '    const previousTarget = targetGainsRef.current[idx];\n'
     '    targetGainsRef.current[idx] = value;\n'
     '    if (!sameBandTarget(previousTarget, value)) noteGestureEdit(idx);\n'
     '    // The mirror is written FROM the ref.'),

    ('commitMany notes each band it changed',
     '      nextTarget[k] = v;\n'
     '    }\n'
     '    targetGainsRef.current = nextTarget;\n',
     '      nextTarget[k] = v;\n'
     '      if (!sameBandTarget(targetGainsRef.current[k], v)) noteGestureEdit(k);\n'
     '    }\n'
     '    targetGainsRef.current = nextTarget;\n'),

    ('a macro drag notes the pressed band when the macro moves',
     '      ? { valueDb: clamp(valueDb, -24, 24), slots: macro.slots } : macro);\n'
     '    setMacroState(next);\n',
     '      ? { valueDb: clamp(valueDb, -24, 24), slots: macro.slots } : macro);\n'
     '    if (macroValueOf(index) !== clamp(valueDb, -24, 24)) {\n'
     '      const pointer = pointerRef.current;\n'
     '      if (pointer && pointer.band >= 0) noteGestureEdit(pointer.band);\n'
     '    }\n'
     '    setMacroState(next);\n'),

    ('the mute-brush press reads out only the band it painted',
     '      brush.paint(band);\n'
     '      pointerRef.current = brush;\n'
     '      hoverRef.current = { band, x, y, n: N };\n'
     '      showStatus(liveHoverLabel(hoverRef.current));\n'
     '      return;\n',
     '      // Live before the first paint, so the paint is recorded as an edit.\n'
     '      brush.editedBand = -1;\n'
     '      brush.changed = false;\n'
     '      liveStatusLabelRef.current = "";\n'
     '      pointerRef.current = brush;\n'
     '      hoverRef.current = { band, x, y, n: N };\n'
     '      brush.paint(band);\n'
     '      updateLiveHoverStatus();\n'
     '      return;\n'),

    ('a gain press records no edit yet',
     '      startSnap,\n'
     '      downTime: performance.now(),\n'
     '      didDrag: false\n'
     '    };\n',
     '      startSnap,\n'
     '      downTime: performance.now(),\n'
     '      didDrag: false,\n'
     '      editedBand: -1,\n'
     '      changed: false\n'
     '    };\n'
     '    liveStatusLabelRef.current = "";\n'),

    ('a gain press publishes nothing',
     '      postNative("macro_drag_start", {});\n'
     '    hoverRef.current = { band, x, y, n: N };\n'
     '    showStatus(liveHoverLabel(hoverRef.current));\n'
     '  };\n',
     '      postNative("macro_drag_start", {});\n'
     '    hoverRef.current = { band, x, y, n: N };\n'
     '  };\n'),

    ('the release reads out only a gesture that changed something',
     '      nativeProjectionRef.current = false;\n'
     '      nativeEditPendingRef.current = false;\n'
     '      showStatus(liveHoverLabel(hoverRef.current));\n',
     '      nativeProjectionRef.current = false;\n'
     '      nativeEditPendingRef.current = false;\n'
     '      if (p.changed && p.editedBand >= 0)\n'
     '        showStatus(liveHoverLabel({ band: p.editedBand, n: N }));\n'),

    ('a move reads out its edit after the handler commits it',
     '      wrapRef.current.style.cursor = "default";\n'
     '    }\n'
     '    updateLiveHoverStatus();\n'
     '    const p = pointerRef.current;\n'
     '    if (!p || !p.mode) return;\n'
     '    if (p.mode === "minimap-resize") {',
     '      wrapRef.current.style.cursor = "default";\n'
     '    }\n'
     '    const p = pointerRef.current;\n'
     '    if (!p || !p.mode) return;\n'
     '    if (p.mode === "minimap-resize") {'),

    ('the move handler is wrapped by its status readout',
     '  const onPointerMove = (e) => {\n'
     '    const g = getGeom();\n',
     '  const onPointerMoveGesture = (e) => {\n'
     '    const g = getGeom();\n'),

    ('a move publishes once its gesture has committed',
     '        p.band = curBand;\n'
     '        return;\n'
     '      }\n'
     '    }\n'
     '  };\n'
     '  const onPointerUp = (e) => {\n',
     '        p.band = curBand;\n'
     '        return;\n'
     '      }\n'
     '    }\n'
     '  };\n'
     '  // A move reads out what it edited once its handler has committed it,\n'
     '  // not a frame later: the handler returns from many branches.\n'
     '  const onPointerMove = (e) => {\n'
     '    onPointerMoveGesture(e);\n'
     '    updateLiveHoverStatus();\n'
     '  };\n'
     '  const onPointerUp = (e) => {\n'),
]

# Present only after this patch, and absent before it. The first names the
# press readout this patch removes; none may survive.
FORBIDDEN_AFTER = (
    'showStatus(liveHoverLabel(hoverRef.current));',
    'macroAdjustedGain(renderGainsRef.current[band], band)',
)
REQUIRED_AFTER = {
    'const noteGestureEdit = (index) => {': 1,
    'noteGestureEdit(idx);': 1,
    'noteGestureEdit(k);': 1,
    'noteGestureEdit(pointer.band);': 1,
    'const current = { band: pointer.editedBand, n: N };': 1,
    'if (p.changed && p.editedBand >= 0)': 1,
    'updateLiveHoverStatus();': 3,
}


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for label, old, new in EDITS:
        if old in new:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)

    raw = open(PATH, encoding='utf-8').read()
    applied = 0
    already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(old_e) == 0 and raw.count(new_e) == 1:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        applied += 1
        print('applied         ', label)

    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')
    if not applied:
        return 0

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token, want in REQUIRED_AFTER.items():
        got = raw.count(escaped(token))
        if got != want:
            sys.exit('FAIL: %r appears %d times after patching, expected %d'
                     % (token, got, want))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
