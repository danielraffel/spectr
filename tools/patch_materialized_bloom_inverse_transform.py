#!/usr/bin/env python3
"""Undo each bloom glow's transform explicitly instead of save()/restore().

Every lit band paints an elliptical glow: a circle drawn under
translate(cx, topY) and scale(glowW / glowH, 1). The painter bracketed each one
with ctx.save() / ctx.restore(). On Pulp's canvas those are not free: save()
clones the JS-mirrored path, and both of them invalidate the shim's sent-state
caches (composite operation, alpha, font, line cap and join, shadows), so the
next draw re-sends that state to the bridge. The bloom loop runs with the
"lighter" composite operation, so every glow paid for its own bracket and
then for the state the bracket threw away, per lit band, per frame.

Nothing inside the bracket changes any state but the transform: the gradient
fill style is set before it and stays set after it. So the bracket is replaced
by the exact inverse of the two transform calls, applied in reverse order --
scale(glowH / glowW, 1), then translate(-cx, -topY) -- which returns the
transform to where the glow found it and leaves every cache intact.
setTransform() is deliberately not used: restoring an absolute matrix has to
know the device scale, and the shim reads the canvas size over the bridge to
find it.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts the patch point occurs exactly once, and
reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 the patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

OLD = ('      ctx.fillStyle = rg2;\n'
       '      ctx.save();\n'
       '      ctx.translate(G.cx, G.topY);\n'
       '      ctx.scale(glowW / glowH, 1);\n'
       '      ctx.beginPath();\n'
       '      ctx.arc(0, 0, glowH, 0, Math.PI * 2);\n'
       '      ctx.fill();\n'
       '      ctx.restore();\n')
NEW = ('      ctx.fillStyle = rg2;\n'
       '      // The transform is the only state this changes, so undo exactly\n'
       '      // that: save()/restore() would also drop the canvas shim\'s\n'
       '      // sent-state caches and make the next draw re-send them.\n'
       '      ctx.translate(G.cx, G.topY);\n'
       '      ctx.scale(glowW / glowH, 1);\n'
       '      ctx.beginPath();\n'
       '      ctx.arc(0, 0, glowH, 0, Math.PI * 2);\n'
       '      ctx.fill();\n'
       '      ctx.scale(glowH / glowW, 1);\n'
       '      ctx.translate(-G.cx, -G.topY);\n')


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()
    if raw.count(escaped(NEW)) == 1 and raw.count(escaped(OLD)) == 0:
        print("already applied  bloom glows undo their own transform")
        return 0
    count = raw.count(escaped(OLD))
    if count != 1:
        sys.exit("FAIL: the bloom glow bracket occurs %d times, expected 1" % count)
    raw = raw.replace(escaped(OLD), escaped(NEW), 1)
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")
    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied          bloom glows undo their own transform")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
