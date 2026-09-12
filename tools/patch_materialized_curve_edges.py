#!/usr/bin/env python3
"""Extend the mask-response curve to the outer edges of the first and last band.

The response polyline is plotted through band CENTRES. Between bands that is
right -- each segment spans centre-to-centre and the bands tile continuously --
but at the two extremes the line begins and ends halfway across the first and
last band, leaving a visibly half-covered band at each end of the plot.

The endpoint rule here is a FLAT HOLD, not an extrapolation: the first band's
painted value is carried left to that band's own left edge, and the last band's
value is carried right to its own right edge. A linear extrapolation off the
last two centres would invent a value the band does not have and could even
leave the plot vertically; a flat hold paints only what the extreme band
already says, over the width that band already owns.

Both edges are derived from the document's own band geometry --
`bandLeftX(0, g)` and `bandLeftX(N - 1, g) + g.bandW` -- never from a nudged
constant, so band count (32/64) and window size cannot break the alignment.

Three painters were examined; two share the defect and are patched here:

  * `drawMaskResponse`  -- the RESPONSE line (the user's screenshots). PATCHED.
  * `drawBands`, the `iir`/`hybrid` DSP overlay -- a bezier through `p.cx`,
    plus the `iir` fill under it. Same defect at every run boundary, which
    includes the plot extremes. PATCHED (stroke and fill together, so the fill
    cannot sit inset from its own stroke).
  * `drawBands`, the `fft` DSP overlay -- already steps `p.xL` -> `p.xR` per
    band, so it already reaches both edges. NOT patched.
  * `drawSpectrum` -- the PEAK/AVG analyzer, already sampled across
    `inner.x .. inner.x + inner.w`. NOT patched.

A run of the bezier overlay is broken by muted bands, and each run is held out
to the edges of its OWN first and last band -- the run covers exactly the bands
it represents, which is what the `fft` mode already does.

Only the shipping document is patched. `native-ui/materialized/materialized-document.json`
is an intermediate no build consumes, and `resources/editor.html` is dead code
whose bytes are digest-locked by test/test_import_fidelity.cpp.

Idempotent: re-running after a successful pass reports "already applied" and
exits 0.
"""
import json
import os
import sys

PATH = 'native-ui/materialized/materialized-document.runtime.json'

# (label, before, after)
EDITS = [
    # --- 1. the RESPONSE line ------------------------------------------
    ('response line spans the first and last band',
     '    for (let i = 0; i < N; ++i) {\n'
     '      const rendered = Number.isFinite(rg[i]) ? clamp(rg[i], -1, 1) : 0;\n'
     '      const y = isMuted(tg[i]) ? g.zeroY : g.zeroY - rendered * g.halfH;\n'
     '      const x = bandCenterX(i, g);\n'
     '      if (i === 0) ctx.moveTo(x, y);\n'
     '      else ctx.lineTo(x, y);\n'
     '    }\n',
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
     '    }\n'),

    # --- 2. the DSP overlay carries its own band right edge -------------
    # `xR` deliberately includes the inter-band gap so the fft steps join;
    # that overshoots the plot by one gap on the last band (harmless, it is
    # clipped). The curve endpoints need the band's TRUE right edge instead.
    ('band points carry their own right edge',
     '      pts.push({\n'
     '        cx: bandCenterX(i, g),\n'
     '        xL: bandLeftX(i, g),\n'
     '        xR: bandLeftX(i, g) + bandW + bandGap,\n'
     '        y: zeroY - effectiveGains[i] * halfH\n'
     '      });\n',
     '      pts.push({\n'
     '        cx: bandCenterX(i, g),\n'
     '        xL: bandLeftX(i, g),\n'
     '        xR: bandLeftX(i, g) + bandW + bandGap,\n'
     '        xE: bandLeftX(i, g) + bandW,\n'
     '        y: zeroY - effectiveGains[i] * halfH\n'
     '      });\n'),

    # --- 3. the iir fill under the curve -------------------------------
    ('iir fill spans its run\'s outer band edges',
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
     '          }\n',
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
     '          }\n'),

    # --- 4. the iir/hybrid bezier stroke -------------------------------
    ('dsp curve spans its run\'s outer band edges',
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
     '        ctx.bezierCurveTo(c1x, c1y, c2x, c2y, p.cx, p.y);\n'
     '        prevPrevP = prevP;\n'
     '        prevP = p;\n'
     '      }\n'
     '      ctx.stroke();\n'
     '      if (dspM === "hybrid") {\n',
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
    for label, before, after in EDITS:
        hits = html.count(before)
        if hits == 1:
            html = html.replace(before, after, 1)
            applied.append(label)
        elif html.count(after) == 1:
            already.append(label)
        else:
            missing.append((label, hits, html.count(after)))

    for label in already:
        print(f'already applied  {label}')
    for label in applied:
        print(f'applied          {label}')
    for label, hits, post in missing:
        print(f'MISSING          {label} (before x{hits}, after x{post})',
              file=sys.stderr)

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
