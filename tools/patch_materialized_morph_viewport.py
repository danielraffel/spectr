#!/usr/bin/env python3
"""Let the morph slider move the viewport in the editor, not just the bands.

A snapshot has always captured the window it was taken under -- C++ has stored
`FieldSnapshot::viewport` since the bank was written -- but nothing downstream
used it.  The processor half of that is fixed in C++ (`morph_viewports`).  This
script fixes the three places the EDITOR had to change to match:

  1. The snapshot projection now carries `min_hz` / `max_hz`, so the editor can
     interpolate the window itself during a drag instead of waiting for the
     processor to answer on every pointer sample.
  2. The morph paths -- both the native optimistic one and the browser-only
     fallback -- interpolate that window and write it to the live viewport.
  3. Settings > MODULATION grows a "Viewport" switch for the playback flag.

Two things this deliberately does NOT do, both learned expensively on this
document:

NO REACT COMMIT PER POINTER SAMPLE.  The viewport publication added by
`patch_materialized_zoom_readout.py` carries a `live` flag precisely because
coalescing a moving value "to one commit per frame" measured WORSE than the
150ms poll it replaced -- a commit re-applies the captured import metadata
across the whole document, about 22ms, which is 1.5 vsync intervals.  So the
morph path mutates `viewRef.current` IN PLACE (the shape `hydrateProcessingState`
already established) and publishes with `live = deferReact`.  The canvas redraw
reads `view.lmin` off that same object every rAF, so the window moves on screen
every frame while React commits nothing until the gesture settles.

NO SEPARATE CAPTURE SWITCH.  The flag governs whether morph APPLIES the
viewport, never whether capture records it.  A capture-time switch would
discard information the user cannot get back and would leave the ambiguous
case where A and B were captured under different settings.

The switch value reaches the bank through a small global rather than props: the
Settings modal and the filter bank are in different subtrees, and threading a
value between them would mean re-rendering the document's root -- the exact
commit this file exists to avoid.  `globalThis.__spectr*` is the established
shape here.

Every read and write of that holder SELF-HEALS -- `(g.__spectrMorphViewport ||
(g.__spectrMorphViewport = { enabled: true }))` -- rather than assuming the
head prelude ran.  In the shipping document it always has, but several test
harnesses extract one function out of this file and evaluate it in a bare vm
with no prelude at all, and an unguarded read there is a TypeError thrown from
inside an unrelated code path.  The first version of this patch did exactly
that and broke mount hydration in two existing suites: the panel's state read
threw before it could apply, which surfaced as "hydration did not land" with
nothing pointing at the viewport switch.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.

resources/editor.html is deliberately NOT mirrored -- it is the browser
bootstrap, not the shipping surface, and test_import_fidelity.cpp pins its
pre-patch shape on purpose.  The durable record of this change is this file.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# Presence of this marker means the document is already patched.
MARKER = "__spectrMorphViewport"

PATCHES = [
    (
        "the playback switch has one holder both subtrees can read",
        "  globalThis.__spectrNativeDispatchTrace = [];\n",
        "  globalThis.__spectrNativeDispatchTrace = [];\n"
        "  // Whether a morph also moves the viewport. The processor owns the\n"
        "  // value; this is the editor's cached copy, written by the hydration\n"
        "  // parse below and optimistically by the Settings switch, and read by\n"
        "  // the morph path. A global rather than props because the Settings\n"
        "  // modal and the filter bank are different subtrees: threading it\n"
        "  // would re-render the document root, which is the commit the whole\n"
        "  // viewport publication exists to avoid.\n"
        "  globalThis.__spectrMorphViewport = globalThis.__spectrMorphViewport\n"
        "    || { enabled: true };\n",
    ),
    (
        "the snapshot projection carries the window it was captured under",
        "      return {\n"
        "        gainDb: projection.gain_db.slice(),\n"
        "        muted: projection.muted.slice(),\n"
        "        values: projection.gain_db.map((db, index) => projection.muted[index]\n"
        "          ? -Infinity : Math.max(-1, Math.min(1, db / 24))),\n"
        "      };",
        "      // The captured window, in log10 Hz -- the space the viewport is\n"
        "      // stored and drawn in, so morphing it is a plain lerp of these.\n"
        "      // Absent or unusable on a processor that predates the projection:\n"
        "      // the morph path then leaves the viewport alone rather than\n"
        "      // guessing a window the slot never had.\n"
        "      const snapMin = Number(projection.min_hz);\n"
        "      const snapMax = Number(projection.max_hz);\n"
        "      const snapView = Number.isFinite(snapMin) && Number.isFinite(snapMax)\n"
        "        && snapMin > 0 && snapMax > snapMin\n"
        "        ? { lmin: Math.log10(snapMin), lmax: Math.log10(snapMax) }\n"
        "        : null;\n"
        "      return {\n"
        "        gainDb: projection.gain_db.slice(),\n"
        "        muted: projection.muted.slice(),\n"
        "        view: snapView,\n"
        "        values: projection.gain_db.map((db, index) => projection.muted[index]\n"
        "          ? -Infinity : Math.max(-1, Math.min(1, db / 24))),\n"
        "      };",
    ),
    (
        "hydration refreshes the cached playback switch",
        "    const projections = payload.snapshots || {};",
        "    // The switch rides the HYDRATION payload only, never the live\n"
        "    // per-revision one, so this must update on presence rather than on\n"
        "    // truthiness -- `undefined !== false` would force it back on for\n"
        "    // every automation frame and make the switch unusable.\n"
        "    if (payload && payload.modulation\n"
        "        && typeof payload.modulation.morph_applies_viewport === 'boolean')\n"
        "      (globalThis.__spectrMorphViewport\n"
        "        || (globalThis.__spectrMorphViewport = { enabled: true }))\n"
        "          .enabled = payload.modulation.morph_applies_viewport;\n"
        "    const projections = payload.snapshots || {};",
    ),
    (
        "a locally captured snapshot records its window too",
        "    const localSnapshot = () => {\n"
        "      const values = targetGainsRef.current.slice();\n"
        "      const gainDb2 = values.map((value, i) => isMuted(value) ? mutedGainDbRef.current[i] ?? 0 : clamp(value, -1, 1) * 24);\n"
        "      return { values, gainDb: gainDb2, muted: values.map(isMuted) };\n"
        "    };",
        "    const localSnapshot = () => {\n"
        "      const values = targetGainsRef.current.slice();\n"
        "      const gainDb2 = values.map((value, i) => isMuted(value) ? mutedGainDbRef.current[i] ?? 0 : clamp(value, -1, 1) * 24);\n"
        "      // Same shape the native projection parses to, so the browser\n"
        "      // fallback and the processor agree on what a snapshot IS. Capture\n"
        "      // always records the window; the switch governs playback only.\n"
        "      return {\n"
        "        values,\n"
        "        gainDb: gainDb2,\n"
        "        muted: values.map(isMuted),\n"
        "        view: { lmin: viewRef.current.lmin, lmax: viewRef.current.lmax },\n"
        "      };\n"
        "    };\n"
        "    // The viewport morph, shared by the native and fallback paths.\n"
        "    //\n"
        "    // `lmin`/`lmax` are already log10 Hz, so a plain lerp of them IS the\n"
        "    // log-space interpolation: equal steps in t move the window by a\n"
        "    // constant RATIO. Interpolating min_hz/max_hz linearly instead would\n"
        "    // cross most of the visible range in the first fifth of the sweep and\n"
        "    // then crawl through the bottom decades, which is where the action is.\n"
        "    //\n"
        "    // Writes viewRef IN PLACE and publishes with `live = deferReact`, the\n"
        "    // shape hydrateProcessingState already uses: the canvas reads\n"
        "    // view.lmin off this object every rAF so the window moves every\n"
        "    // frame, while React commits nothing until the gesture settles. A\n"
        "    // per-sample commit here would re-apply the captured import metadata\n"
        "    // across the whole document -- roughly 1.5 vsync intervals each.\n"
        "    const morphViewport = (a, b, amount, deferReact) => {\n"
        "      if ((globalThis.__spectrMorphViewport\n"
        "           || (globalThis.__spectrMorphViewport = { enabled: true }))\n"
        "          .enabled === false) return;\n"
        "      const va = a && a.view, vb = b && b.view;\n"
        "      if (!va || !vb) return;\n"
        "      const lmin = lerp(va.lmin, vb.lmin, amount);\n"
        "      const lmax = lerp(va.lmax, vb.lmax, amount);\n"
        "      if (!Number.isFinite(lmin) || !Number.isFinite(lmax) || lmax <= lmin)\n"
        "        return;\n"
        "      viewRef.current.lmin = lmin;\n"
        "      viewRef.current.lmax = lmax;\n"
        "      notifyViewportListeners(deferReact === true);\n"
        "    };",
    ),
    (
        "the native optimistic morph moves the window with the bands",
        "      mutedGainDbRef.current = gainDb2;\n"
        "      targetGainsRef.current = values.slice();\n"
        "      if (!deferReact) {\n"
        "        nativeProjectionRef.current = true;\n"
        "        setGains(values);\n"
        "      }\n"
        "    };",
        "      mutedGainDbRef.current = gainDb2;\n"
        "      targetGainsRef.current = values.slice();\n"
        "      morphViewport(a, b, amount, deferReact);\n"
        "      if (!deferReact) {\n"
        "        nativeProjectionRef.current = true;\n"
        "        setGains(values);\n"
        "      }\n"
        "    };",
    ),
    (
        "the browser fallback morph moves the window too",
        "        const map = /* @__PURE__ */ new Map();\n"
        "        const bDominant = v >= 0.5;\n"
        "        for (let i = 0; i < N; i++) {\n"
        "          const db = lerp(s.A.gainDb[i] ?? 0, s.B.gainDb[i] ?? 0, v);\n"
        "          const muted = bDominant ? !!s.B.muted[i] : !!s.A.muted[i];\n"
        "          map.set(i, muted ? -Infinity : clamp(db / 24, -1, 1));\n"
        "        }\n"
        "        commitMany(map, deferReact);",
        "        const map = /* @__PURE__ */ new Map();\n"
        "        const bDominant = v >= 0.5;\n"
        "        for (let i = 0; i < N; i++) {\n"
        "          const db = lerp(s.A.gainDb[i] ?? 0, s.B.gainDb[i] ?? 0, v);\n"
        "          const muted = bDominant ? !!s.B.muted[i] : !!s.A.muted[i];\n"
        "          map.set(i, muted ? -Infinity : clamp(db / 24, -1, 1));\n"
        "        }\n"
        "        morphViewport(s.A, s.B, v, deferReact);\n"
        "        commitMany(map, deferReact);",
    ),
    (
        "the settings panel reads the playback switch",
        "      targetMask: Number.isFinite(Number(modulation.target_mask))",
        "      morphViewport: (globalThis.__spectrMorphViewport\n"
        "        || (globalThis.__spectrMorphViewport = { enabled: true })).enabled,\n"
        "      targetMask: Number.isFinite(Number(modulation.target_mask))",
    ),
    (
        "a live frame never forces the playback switch back on",
        "    const pending = pendingWritesRef.current;\n"
        "    Object.keys(pending).forEach((key) => {",
        "    // Presence, not truthiness. The switch is not a host parameter and\n"
        "    // rides the hydration payload only, so a live automation frame omits\n"
        "    // it -- reading `undefined` as `false` (or as `!== false`) would make\n"
        "    // the switch flip itself on the next automation write.\n"
        "    if (typeof modulation.morph_applies_viewport === 'boolean') {\n"
        "      next.morphViewport = modulation.morph_applies_viewport;\n"
        "      (globalThis.__spectrMorphViewport\n"
        "        || (globalThis.__spectrMorphViewport = { enabled: true }))\n"
        "          .enabled = modulation.morph_applies_viewport;\n"
        "    }\n"
        "    const pending = pendingWritesRef.current;\n"
        "    Object.keys(pending).forEach((key) => {",
    ),
    (
        "the settings panel writes the playback switch",
        "  const publishTargets = (selection) => publishTargetMask(selection === \"all\" ? 15 : 0);",
        "  // Updates the cached copy first so a morph already in flight picks the\n"
        "  // new answer up on its very next sample, then tells the processor,\n"
        "  // which owns the value and persists it.\n"
        "  const publishMorphViewport = (enabled) => {\n"
        "    (globalThis.__spectrMorphViewport\n"
        "      || (globalThis.__spectrMorphViewport = { enabled: true }))\n"
        "        .enabled = enabled;\n"
        "    pendingWritesRef.current.morphViewport = enabled;\n"
        "    setValue((current) => ({ ...current, morphViewport: enabled }));\n"
        "    if (!spectrModulationBridge) return;\n"
        "    Promise.resolve(window.pulp.postMessage(\"morph_viewport_set\", { enabled }, \"spectr-morph-viewport\")).catch((error) => console.error(\"[Spectr] morph viewport write failed\", error));\n"
        "  };\n"
        "  const publishTargets = (selection) => publishTargetMask(selection === \"all\" ? 15 : 0);",
    ),
    (
        # LAST in the group, after the two shared destination rows, and always
        # visible. The group's other rows all belong to LFO 1 or LFO 2 and hide
        # with their owner; this one belongs to the MORPH SLIDER, which works
        # with both LFOs off. Placing it anywhere inside the LFO rows would both
        # read as an LFO setting and break the fixed row order that
        # test_materialized_modulation_grouping.mjs pins -- an order that exists
        # because the native bridge appends a late-mounting row and cannot move
        # it afterwards. Morph is already a citizen of this group: it is one of
        # the four Destinations.
        "settings shows the viewport switch after the shared destination rows",
        ", \"NONE\")]))))\n  );\n}",
        # The tail is `] + 4 parens`: the array, .concat, the row's inner div,
        # the SettingsField, and then the SettingsGroup. The new row is a
        # SIBLING INSIDE THE GROUP, so it goes after the THIRD paren -- putting
        # it after the fourth makes it a sibling of the group instead, which
        # renders nothing at all and shows up as a row that simply never mounts.
        ", \"NONE\")]))),\n"
        "    /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: \"Viewport\", hint: \"Morph moves the zoom window too\" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: value.morphViewport !== false, onChange: (next) => publishMorphViewport(next) })))\n"
        "  );\n}",
    ),
]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]

    if MARKER in html:
        print("patch_materialized_morph_viewport: already applied")
        return 0

    failures = []
    for name, old, new in PATCHES:
        count = html.count(old)
        if count != 1:
            failures.append("%s: patch point occurs %d times" % (name, count))
    if failures:
        for line in failures:
            print("patch_materialized_morph_viewport: " + line, file=sys.stderr)
        return 1

    for name, old, new in PATCHES:
        html = html.replace(old, new, 1)

    # Re-check: every replacement landed, and the marker the idempotence guard
    # keys on is present. A patch that silently applied nothing would otherwise
    # write an unchanged document and report success.
    for name, old, new in PATCHES:
        if html.count(new) != 1:
            print("patch_materialized_morph_viewport: %s did not apply" % name,
                  file=sys.stderr)
            return 1
    if MARKER not in html:
        print("patch_materialized_morph_viewport: marker missing after patch",
              file=sys.stderr)
        return 1

    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("patch_materialized_morph_viewport: applied %d patches" % len(PATCHES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
