#!/usr/bin/env python3
"""Give every audible RUN of bands its own response curve, spanning its own edges.

Two rules, one mechanism:

  * A muted band carries no response, so the curve BREAKS there rather than
    plunging across it. Each contiguous audible run is its own subpath.
  * Each run is held FLAT from its own first band's left edge to its own last
    band's right edge, so it covers exactly the bands it represents.

The second rule subsumes the earlier plot-extreme fix: a run that begins at
band 0 or ends at band N-1 still reaches the outer edge of the plot, so the
first and last band stop looking half-drawn. Plotted through band CENTRES
alone the line begins and ends halfway across those two bands.

The `fft` stair-step overlay already draws exactly this -- one flat step per
band, no segment over a muted band -- and the user confirmed it reads
correctly. It is the reference; the response line was the outlier, and in the
default `both` visualization the two are drawn over each other, so the
response line bridging a mute that the stair-step leaves empty is what the
user sees as a line "sticking out past the edge" before dropping to 0.

The endpoint rule is a FLAT HOLD, not an extrapolation: the run's outer band's
painted value is carried to that band's own edge. A linear extrapolation off
the last two centres would invent a value no band has and could leave the plot
vertically on a steep edge.

Every edge is derived from the document's own band geometry -- `bandLeftX(i, g)`
and `bandLeftX(i, g) + g.bandW` -- never from a nudged constant, so band count
(32/64) and window size cannot break the alignment.

Four painters were examined:

  * `drawMaskResponse`  -- the RESPONSE line (the user's screenshots). PATCHED
    for both rules.
  * `drawBands`, the `iir`/`hybrid` bezier STROKE -- already breaks into runs
    and already holds each run to its own band edges. Unchanged.
  * `drawBands`, the `iir` FILL under that stroke -- broke at neither, so it
    slid under muted bands its own stroke skipped. PATCHED to emit one closed
    polygon per run. (Chrome renders no `dspMode` control, so this path is
    unreachable from the shipping UI today; the executed-painter suite is what
    covers it.)
  * `drawBands`, the `fft` stair-step -- the reference. NOT patched.
  * `drawSpectrum` -- the PEAK/AVG analyzer, sampled across `inner.x ..
    inner.x + inner.w` rather than per band, so bands are not its domain and a
    muted band does not describe a gap in it. NOT patched.

Only the shipping document is patched. `native-ui/materialized/materialized-document.json`
is an intermediate no build consumes, and `resources/editor.html` is dead code
whose bytes are digest-locked by test/test_import_fidelity.cpp.

Idempotent, and replayable from EITHER of the two earlier forms of the
document: each edit accepts several `before` variants (the original
centres-only painter, and the plot-extreme-only painter) and converges on one
`after`. Re-running after a successful pass reports "already applied" and
exits 0.
"""
import json
import os
import sys

PATH = 'native-ui/materialized/materialized-document.runtime.json'

# (label, accepted `before` variants, after)
#
# Several variants per edit so the script replays from any earlier form of the
# checked-in artifact. Only one variant may be present, or the document is
# ambiguous and the patch refuses to write.
EDITS = [
    # --- 1. the RESPONSE line ------------------------------------------
    ('response line breaks at mutes and spans each run\'s band edges',
     (
         # v1: the original painter -- centres only, straight through mutes.
         '    for (let i = 0; i < N; ++i) {\n'
         '      const rendered = Number.isFinite(rg[i]) ? clamp(rg[i], -1, 1) : 0;\n'
         '      const y = isMuted(tg[i]) ? g.zeroY : g.zeroY - rendered * g.halfH;\n'
         '      const x = bandCenterX(i, g);\n'
         '      if (i === 0) ctx.moveTo(x, y);\n'
         '      else ctx.lineTo(x, y);\n'
         '    }\n',
         # v2: plot extremes held to their edges, still straight through mutes.
         '    for (let i = 0; i < N; ++i) {\n'
         '      const rendered = Number.isFinite(rg[i]) ? clamp(rg[i], -1, 1) : 0;\n'
         '      const y = isMuted(tg[i]) ? g.zeroY : g.zeroY - rendered * g.halfH;\n'
         '      const x = bandCenterX(i, g);\n'
         '      // Hold the outer bands\' own values flat out to their own edges.\n'
         '      // Plotted through centres alone the line starts and ends halfway\n'
         '      // across the first and last band, leaving a half-covered band at\n'
         '      // each end. A flat hold paints only what that band already says;\n'
         '      // extrapolating off the last two centres would invent a value.\n'
         '      if (i === 0) {\n'
         '        ctx.moveTo(bandLeftX(0, g), y);\n'
         '        ctx.lineTo(x, y);\n'
         '      } else ctx.lineTo(x, y);\n'
         '      if (i === N - 1) ctx.lineTo(bandLeftX(i, g) + g.bandW, y);\n'
         '    }\n',
     ),
     '    // A muted band carries no response, so the line BREAKS there instead\n'
     '    // of plunging across it: every contiguous audible run is its own\n'
     '    // subpath, held flat from its own first band\'s left edge to its own\n'
     '    // last band\'s right edge. That is exactly what the fft stair-step\n'
     '    // already draws, and it generalises the plot-extreme hold -- a run\n'
     '    // that begins at band 0 or ends at band N - 1 still reaches the outer\n'
     '    // edge of the plot, so neither end looks half drawn.\n'
     '    let inRun = false;\n'
     '    for (let i = 0; i < N; ++i) {\n'
     '      if (isMuted(tg[i])) {\n'
     '        inRun = false;\n'
     '        continue;\n'
     '      }\n'
     '      const rendered = Number.isFinite(rg[i]) ? clamp(rg[i], -1, 1) : 0;\n'
     '      const y = g.zeroY - rendered * g.halfH;\n'
     '      const x = bandCenterX(i, g);\n'
     '      if (!inRun) {\n'
     '        ctx.moveTo(bandLeftX(i, g), y);\n'
     '        inRun = true;\n'
     '      }\n'
     '      ctx.lineTo(x, y);\n'
     '      if (i === N - 1 || isMuted(tg[i + 1]))\n'
     '        ctx.lineTo(bandLeftX(i, g) + g.bandW, y);\n'
     '    }\n'),

    # --- 2. the DSP overlay carries its own band right edge -------------
    # `xR` deliberately includes the inter-band gap so the fft steps join;
    # that overshoots the plot by one gap on the last band (harmless, it is
    # clipped). The curve endpoints need the band's TRUE right edge instead.
    ('band points carry their own right edge',
     (
         '      pts.push({\n'
         '        cx: bandCenterX(i, g),\n'
         '        xL: bandLeftX(i, g),\n'
         '        xR: bandLeftX(i, g) + bandW + bandGap,\n'
         '        y: zeroY - effectiveGains[i] * halfH\n'
         '      });\n',
     ),
     '      pts.push({\n'
     '        cx: bandCenterX(i, g),\n'
     '        xL: bandLeftX(i, g),\n'
     '        xR: bandLeftX(i, g) + bandW + bandGap,\n'
     '        xE: bandLeftX(i, g) + bandW,\n'
     '        y: zeroY - effectiveGains[i] * halfH\n'
     '      });\n'),

    # --- 3. the iir fill under the curve -------------------------------
    # The fill broke at neither rule, so it slid under muted bands that its
    # own stroke skips. One closed polygon per run instead.
    ('iir fill breaks at mutes and spans each run\'s band edges',
     (
         # v1: one polygon, centre to centre, straight through mutes.
         '        ctx.fillStyle = "rgba(180,210,255,0.05)";\n'
         '        ctx.beginPath();\n'
         '        let filling = false;\n'
         '        for (let i = 0; i < N; i++) {\n'
         '          const p = pts[i];\n'
         '          if (!p) continue;\n'
         '          if (!filling) {\n'
         '            ctx.moveTo(p.cx, zeroY);\n'
         '            filling = true;\n'
         '          }\n'
         '          ctx.lineTo(p.cx, p.y);\n'
         '        }\n'
         '        if (filling) {\n'
         '          for (let i = N - 1; i >= 0; i--) {\n'
         '            const p = pts[i];\n'
         '            if (!p) continue;\n'
         '            ctx.lineTo(p.cx, zeroY);\n'
         '            break;\n'
         '          }\n'
         '          ctx.closePath();\n'
         '          ctx.fill();\n'
         '        }\n',
         # v2: one polygon held to the PLOT's outer edges, still through mutes.
         '        ctx.fillStyle = "rgba(180,210,255,0.05)";\n'
         '        ctx.beginPath();\n'
         '        let filling = false;\n'
         '        for (let i = 0; i < N; i++) {\n'
         '          const p = pts[i];\n'
         '          if (!p) continue;\n'
         '          if (!filling) {\n'
         '            ctx.moveTo(p.xL, zeroY);\n'
         '            ctx.lineTo(p.xL, p.y);\n'
         '            filling = true;\n'
         '          }\n'
         '          ctx.lineTo(p.cx, p.y);\n'
         '        }\n'
         '        if (filling) {\n'
         '          for (let i = N - 1; i >= 0; i--) {\n'
         '            const p = pts[i];\n'
         '            if (!p) continue;\n'
         '            ctx.lineTo(p.xE, p.y);\n'
         '            ctx.lineTo(p.xE, zeroY);\n'
         '            break;\n'
         '          }\n'
         '          ctx.closePath();\n'
         '          ctx.fill();\n'
         '        }\n',
     ),
     '        ctx.fillStyle = "rgba(180,210,255,0.05)";\n'
     '        ctx.beginPath();\n'
     '        // One closed polygon per audible run, so the fill stops at a\n'
     '        // muted band exactly where its own stroke does instead of\n'
     '        // sliding underneath it.\n'
     '        let filling = false;\n'
     '        let lastFilled = null;\n'
     '        const closeFill = () => {\n'
     '          if (filling && lastFilled) {\n'
     '            ctx.lineTo(lastFilled.xE, lastFilled.y);\n'
     '            ctx.lineTo(lastFilled.xE, zeroY);\n'
     '            ctx.closePath();\n'
     '          }\n'
     '          filling = false;\n'
     '          lastFilled = null;\n'
     '        };\n'
     '        for (let i = 0; i < N; i++) {\n'
     '          const p = pts[i];\n'
     '          if (!p) {\n'
     '            closeFill();\n'
     '            continue;\n'
     '          }\n'
     '          if (!filling) {\n'
     '            ctx.moveTo(p.xL, zeroY);\n'
     '            ctx.lineTo(p.xL, p.y);\n'
     '            filling = true;\n'
     '          }\n'
     '          ctx.lineTo(p.cx, p.y);\n'
     '          lastFilled = p;\n'
     '        }\n'
     '        closeFill();\n'
     '        ctx.fill();\n'),

    # --- 4. the iir/hybrid bezier stroke -------------------------------
    ('dsp curve spans its run\'s outer band edges',
     (
         '      let started = false;\n'
         '      let prevP = null, prevPrevP = null;\n'
         '      for (let i = 0; i < N; i++) {\n'
         '        const p = pts[i];\n'
         '        if (!p) {\n'
         '          started = false;\n'
         '          prevP = null;\n'
         '          prevPrevP = null;\n'
         '          continue;\n'
         '        }\n'
         '        if (!started) {\n'
         '          ctx.moveTo(p.cx, p.y);\n'
         '          started = true;\n'
         '          prevPrevP = p;\n'
         '          prevP = p;\n'
         '          continue;\n'
         '        }\n',
     ),
     '      let started = false;\n'
     '      let prevP = null, prevPrevP = null;\n'
     '      // A muted band breaks the curve into runs. Each run is held flat\n'
     '      // out to the edges of its own first and last band, so it covers\n'
     '      // exactly the bands it represents -- the same span the fft mode\n'
     '      // already steps across, and the fix for the half-covered first\n'
     '      // and last band at the plot extremes.\n'
     '      const closeRun = () => {\n'
     '        if (started && prevP) ctx.lineTo(prevP.xE, prevP.y);\n'
     '      };\n'
     '      for (let i = 0; i < N; i++) {\n'
     '        const p = pts[i];\n'
     '        if (!p) {\n'
     '          closeRun();\n'
     '          started = false;\n'
     '          prevP = null;\n'
     '          prevPrevP = null;\n'
     '          continue;\n'
     '        }\n'
     '        if (!started) {\n'
     '          ctx.moveTo(p.xL, p.y);\n'
     '          ctx.lineTo(p.cx, p.y);\n'
     '          started = true;\n'
     '          prevPrevP = p;\n'
     '          prevP = p;\n'
     '          continue;\n'
     '        }\n'),

    ('dsp curve closes its final run',
     (
         '        ctx.bezierCurveTo(c1x, c1y, c2x, c2y, p.cx, p.y);\n'
         '        prevPrevP = prevP;\n'
         '        prevP = p;\n'
         '      }\n'
         '      ctx.stroke();\n'
         '      if (dspM === "hybrid") {\n',
     ),
     '        ctx.bezierCurveTo(c1x, c1y, c2x, c2y, p.cx, p.y);\n'
     '        prevPrevP = prevP;\n'
     '        prevP = p;\n'
     '      }\n'
     '      closeRun();\n'
     '      ctx.stroke();\n'
     '      if (dspM === "hybrid") {\n'),
]

def main() -> int:
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(repo, PATH)
    document = json.load(open(path, encoding='utf-8'))
    html = document['html']

    applied, already, missing = [], [], []
    for label, befores, after in EDITS:
        if html.count(after) == 1:
            already.append(label)
            continue
        matched = [b for b in befores if html.count(b) == 1]
        if len(matched) == 1:
            html = html.replace(matched[0], after, 1)
            applied.append(label)
        else:
            missing.append((label, [html.count(b) for b in befores],
                            html.count(after)))

    for label in already:
        print(f'already applied  {label}')
    for label in applied:
        print(f'applied          {label}')
    for label, hits, post in missing:
        print(f'MISSING          {label} (before variants x{hits}, '
              f'after x{post})', file=sys.stderr)

    if missing:
        print('refusing to write a partial patch', file=sys.stderr)
        return 1

    if not applied:
        return 0

    document['html'] = html
    # Byte-for-byte the serialization the checked-in artifact already uses:
    # compact separators, no ASCII escaping, no trailing newline. Anything else
    # reformats 775 KB and buries the five-hunk diff this script exists to keep
    # reviewable through the merge conflicts parallel lanes guarantee.
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write(json.dumps(document, ensure_ascii=False,
                                separators=(',', ':')))
    print(f'wrote {PATH}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
