#!/usr/bin/env python3
"""Cache the colour prefix specColor builds for each band position and theme.

`specColor(pos, alpha, theme)` turns a band's position into an hsla() string.
The painters call it for every band on every frame -- bars, caps, glow stops,
the unmute pulse -- about 450 strings a frame, and every call re-ran the theme
branch and formatted hue, saturation and lightness again, although for a given
band and theme those three never change; only alpha does.

So the hue/saturation/lightness half is computed once per (theme, pos) and kept
as the string prefix "hsla(h, s%, l%, ", and each call appends only the alpha.
The result is the same string, character for character: the prefix is built by
the same template the whole string was, and the alpha is formatted the same way.

The cache is bounded. Band positions are a small fixed set per band count, but
nothing stops a caller passing a continuous value, so the cache is cleared once
it holds more entries than any band layout needs.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once, and
reports "already applied" on a second run.

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
    ('specColor reads a cached prefix',
     'function specColor(pos, alpha = 1, theme = "spectral") {\n'
     '  let hue, sat = 80, light = 62;\n',
     '// hue/saturation/lightness depend only on (theme, pos), so they are\n'
     '// formatted once per pair and each call appends only its alpha.\n'
     'const specColorPrefixes = /* @__PURE__ */ new Map();\n'
     'function specColor(pos, alpha = 1, theme = "spectral") {\n'
     '  const key = theme + "|" + pos;\n'
     '  let prefix = specColorPrefixes.get(key);\n'
     '  if (prefix === void 0) {\n'
     '    if (specColorPrefixes.size >= 4096) specColorPrefixes.clear();\n'
     '    prefix = specColorPrefix(pos, theme);\n'
     '    specColorPrefixes.set(key, prefix);\n'
     '  }\n'
     '  return `${prefix}${alpha})`;\n'
     '}\n'
     'function specColorPrefix(pos, theme) {\n'
     '  let hue, sat = 80, light = 62;\n'),
    ('the prefix builder returns everything but the alpha',
     '  return `hsla(${hue}, ${sat}%, ${light}%, ${alpha})`;\n}\n',
     '  return `hsla(${hue}, ${sat}%, ${light}%, `;\n}\n'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for label, old, new in EDITS:
        if old in new:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)
    raw = open(PATH, encoding='utf-8').read()
    applied = already = 0
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
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
