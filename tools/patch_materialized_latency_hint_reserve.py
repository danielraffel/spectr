#!/usr/bin/env python3
"""Reserve the Latency hint's height from the copy itself, not a typed number.

THE DEFECT THIS FIXES, AND WHY THE PREVIOUS FIX DID NOT. The reserve shipped
as a literal `minHeight: 52`, derived on paper as "four lines of the 9.5px hint
type plus its 2px lead". The renderer disagrees. Measured from the shipping
standalone with SPECTR_LAYOUT_DUMP: the "Mixing" description renders **61px**
tall in the hint column, and "Tracking" renders 46 and is floored to 52. So the
floor binds only the shorter option, the row is still 61 in one mode and 52 in
the other, and every group below LATENCY still moved **9px** on each switch --
which is what the owner reported after that fix shipped. The constant was not
slightly off; it was never compared against a render.

That is the real lesson, and it is about the mechanism rather than the number:
a typed pixel count is a prediction about wrapping, and only the renderer knows
where a line breaks. A corrected constant would be one copy edit away from
being wrong again, silently, in exactly the same way.

THE MECHANISM. The reserve is now a real string, laid out through the same
wrap at the same width and by the same renderer as the visible text -- the
longest option's description, painted at zero opacity. It IS the measurement,
so it cannot be a stale prediction and cannot fall behind an edit to the copy.
The visible text is lifted out of flow on top of it, so which option is
selected has no effect on the box at all.

WHY `opacity` AND NOT `visibility`. In this runtime `visibility` lowers to
`setVisible()`, which takes the node out of the layout -- it would reserve
nothing. `opacity` lowers to `setOpacity()`, a paint property: the box is laid
out and simply not drawn. That distinction is the whole trick.

WHY THE SIZER IS CHOSEN BY STRING LENGTH. The renderer owns the wrap, so
nothing here can compute a rendered height. Character count is a proxy. It is
gated rather than trusted: the live detector asserts BOTH that the two modes
produce an identical box AND that each mode's measured text fits inside it, so
a proxy that ever picks the shorter-rendering option is caught by the second
rule rather than silently clipping.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous. Idempotent on the marker.
"""
import json
import os
import sys

PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "data-spectr-latency-hint-sizer"

_OLD = ('  const hintText = current\n'
        '    ? (current.description + " (" + millis(current.ms) + ")")\n'
        '    : "";\n'
        '  // Reserve the taller option\'s height so switching does not reflow\n'
        '  // every group below this one. The number is the rendered height of\n'
        '  // the longer description at this panel\'s fixed width; the test pins\n'
        '  // that both selections produce the SAME row height, so a wrong value\n'
        '  // here fails rather than merely looking slightly off.\n'
        '  const hint = /* @__PURE__ */ React.createElement("div",\n'
        '    { "data-spectr-latency-hint": true,\n'
        '      style: { minHeight: 52 } },\n'
        '    hintText);\n')

_NEW = ('  const hintFor = function (option) {\n'
        '    return option\n'
        '      ? (option.description + " (" + millis(option.ms) + ")")\n'
        '      : "";\n'
        '  };\n'
        '  const hintText = hintFor(current);\n'
        '  // Reserve the height of the LONGEST option\'s description rather than\n'
        '  // a typed pixel count. A constant is a PREDICTION about where lines\n'
        '  // wrap, and the constant that stood here predicted 52px for a\n'
        '  // description the renderer lays out at 61 -- so the row still changed\n'
        '  // height on every switch. A real string, wrapped by the renderer at\n'
        '  // this same width, is the measurement rather than a guess at it.\n'
        '  const sizerText = options.reduce(function (longest, option) {\n'
        '    const text = hintFor(option);\n'
        '    return text.length > longest.length ? text : longest;\n'
        '  }, "");\n'
        '  // The sizer is painted at zero opacity, which is a PAINT property\n'
        '  // here. `visibility` lowers to setVisible() and would take the box\n'
        '  // out of the layout, reserving nothing at all.\n'
        '  const hint = /* @__PURE__ */ React.createElement("div",\n'
        '    { "data-spectr-latency-hint": true,\n'
        '      style: { position: "relative" } },\n'
        '    /* @__PURE__ */ React.createElement("div",\n'
        '      { "data-spectr-latency-hint-sizer": true,\n'
        '        style: { opacity: 0 } },\n'
        '      sizerText),\n'
        '    /* @__PURE__ */ React.createElement("div",\n'
        '      { "data-spectr-latency-hint-text": true,\n'
        '        style: { position: "absolute", left: 0, right: 0, top: 0 } },\n'
        '      hintText));\n')

PATCHES = [("derive the latency hint reserve from the longest option", _OLD, _NEW)]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]

    if MARKER in html:
        print("patch_materialized_latency_hint_reserve: already applied")
        return 0

    for name, old, new in PATCHES:
        count = html.count(old)
        if count != 1:
            print("patch_materialized_latency_hint_reserve: %s: patch point "
                  "occurs %d times" % (name, count), file=sys.stderr)
            return 1

    for name, old, new in PATCHES:
        html = html.replace(old, new, 1)

    for name, old, new in PATCHES:
        if html.count(new) != 1:
            print("patch_materialized_latency_hint_reserve: %s did not apply"
                  % name, file=sys.stderr)
            return 1
    if MARKER not in html:
        print("patch_materialized_latency_hint_reserve: marker missing",
              file=sys.stderr)
        return 1

    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("patch_materialized_latency_hint_reserve: applied %d patches"
          % len(PATCHES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
