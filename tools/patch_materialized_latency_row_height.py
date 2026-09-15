#!/usr/bin/env python3
"""Reserve the Latency row's description height so switching does not reflow.

THE DEFECT. The two options' descriptions wrap to different line counts --
Mixing to four lines, Tracking to three -- so selecting one after the other
changed the row's height and shoved every group below it up or down by about
22px. APPEARANCE, STRUCTURE and everything under them jumped on every switch.

WHY A RESERVED HEIGHT RATHER THAN MATCHED COPY. Equalising the prose would
work until the first word anybody edits, and it puts a layout constraint on
copy that has to be free to say the true thing. One of these sentences is
already expected to change: "very narrow bands cut less deeply" describes a
defect being fixed, not a permanent property. Reserving the space the taller
option needs is indifferent to what either sentence says.

WHY ONLY THIS ROW, AND NOT THE SHARED FIELD COMPONENT. Reserving inside
SpectrSettingsField would fix the whole class -- Theme, Metaphor, Mute style
and Bands all carry per-option descriptions and are one edit away from the
same defect -- but it adds height to every row at once, which moves the
panel's total content extent well past the sanity window
test_native_state_parity.cpp pins around it. That is a deliberate,
re-measured change and belongs in its own commit; see the follow-up.

WHY THIS ONE IS FREE. Mixing is both the default and the taller option, so
reserving to its height leaves the extent at default exactly where it is and
the geometry window stays valid. The panel is fixed-width (width: 520, only
its height is viewport-derived), so a reserved height cannot be defeated by a
narrower panel producing more wraps.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous. Idempotent on the marker.
"""
import json
import os
import sys

PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrLatencyHintReserve"

# Sized to the taller option at the panel's fixed 520px width: four lines of
# the 9.5px hint type plus its 2px lead. Measured from the rendered row rather
# than derived from the font metrics, because the wrap point is what decides
# the line count and only the renderer knows it.
_OLD = ('  const hint = current\n'
        '    ? (current.description + " (" + millis(current.ms) + ")")\n'
        '    : "";\n')
_NEW = ('  const hintText = current\n'
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
        '    hintText);\n'
        '  const __spectrLatencyHintReserve = true;\n')

PATCHES = [("reserve the latency description height", _OLD, _NEW)]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]

    if MARKER in html:
        print("patch_materialized_latency_row_height: already applied")
        return 0

    for name, old, new in PATCHES:
        count = html.count(old)
        if count != 1:
            print("patch_materialized_latency_row_height: %s: patch point "
                  "occurs %d times" % (name, count), file=sys.stderr)
            return 1

    for name, old, new in PATCHES:
        html = html.replace(old, new, 1)

    for name, old, new in PATCHES:
        if html.count(new) != 1:
            print("patch_materialized_latency_row_height: %s did not apply"
                  % name, file=sys.stderr)
            return 1
    if MARKER not in html:
        print("patch_materialized_latency_row_height: marker missing",
              file=sys.stderr)
        return 1

    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("patch_materialized_latency_row_height: applied %d patches"
          % len(PATCHES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
