#!/usr/bin/env python3
"""Hide the LIVE / PRECISION motion control and ease the display at one rate.

Motion Mode (param 3100, `kParamMotionMode`) never reached the audio: the only
thing that read it was the editor's draw loop, which eased each painted column
toward its target at k = 22 (LIVE) or k = 6 (PRECISION). The control is hidden
from the header, the Settings MOTION group and the design-tool Tweaks panel,
and the draw loop always eases at the LIVE rate.

The parameter itself stays registered -- a ParamID is a permanent contract, and
removing it would orphan automation lanes -- and nothing rewrites its stored
value. A session saved with PRECISION loads with that value intact and simply
looks like LIVE, so bringing the control back round-trips.

The hidden JSX is COMMENTED OUT rather than deleted, with a note naming what a
restore also needs. `/* @__PURE__ */` annotations inside a commented region are
dropped, because a nested `*/` would end the comment early.

CAPTURED BINDINGS. The header's children are addressed by positional DOM path
(`div[0]/div[1]/div[N]`) in the document's layout/text/paint bindings and in
every captured state's metadata inside runtime.js. With the segmented control
gone from slot 2, every later sibling moves down one index, so this:

    drops every binding under div[0]/div[1]/div[2]   (the control, 2 buttons)
    renumbers div[0]/div[1]/div[3..6] -> div[2..5]   (divider, view, divider,
                                                      band/zoom group)

Left alone, the BARS/RESPONSE/BOTH group would inherit the LIVE/PRECISION box
and captions, and the header would fail the zero-miss metadata contract.

Why a script: the shipping document is one minified line and the materialized
generator cannot rebuild it, so every edit must be replayable. Each text edit
asserts its patch point occurs exactly once; the binding rewrite recognises its
own output. A second run reports "already applied" and writes nothing.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCUMENT = os.path.join(REPO, "native-ui", "materialized",
                        "materialized-document.runtime.json")
RUNTIME = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

RESTORE_NOTE = (
    "Motion mode (LIVE / PRECISION) is hidden and the editor always\n"
    "    eases at the LIVE rate. Restoring this control also means restoring\n"
    "    its help section (\"Live and Precision\" in help-content.js), the\n"
    "    Settings MOTION group, and the captured header bindings for this\n"
    "    slot (tools/patch_materialized_motion_mode_hidden.py removed them).\n"
)

HEADER_OLD = (
    'React.createElement("div", { style: { flex: 1 } }), '
    '/* @__PURE__ */ React.createElement(\n'
    '    Segmented,\n'
    '    {\n'
    '      value: settings.motionMode,\n'
    '      onChange: (v) => {\n'
    '        setSettings((s) => ({ ...s, motionMode: v }));\n'
    '        window.spectrPublishMode("motion", v);\n'
    '      },\n'
    '      options: [["live", "LIVE"], ["precision", "PRECISION"]]\n'
    '    }\n'
    '  ), '
)
HEADER_NEW = (
    'React.createElement("div", { style: { flex: 1 } }), '
    '/* ' + RESTORE_NOTE +
    '  React.createElement(\n'
    '    Segmented,\n'
    '    {\n'
    '      value: settings.motionMode,\n'
    '      onChange: (v) => {\n'
    '        setSettings((s) => ({ ...s, motionMode: v }));\n'
    '        window.spectrPublishMode("motion", v);\n'
    '      },\n'
    '      options: [["live", "LIVE"], ["precision", "PRECISION"]]\n'
    '    }\n'
    '  ), */ '
)

EASE_OLD = '      const k = motionMode === "precision" ? 6 : 22;\n'
EASE_NEW = (
    '      // One easing rate, the LIVE one. Motion Mode (param 3100) stays\n'
    '      // registered and keeps whatever value a session stored, but the\n'
    '      // display no longer reads it.\n'
    '      const k = 22;\n'
)

SETTINGS_OLD = (
    ', /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: '
    '"general", title: "MOTION", subtitle: "How gain changes smooth over '
    'time." }, /* @__PURE__ */ React.createElement(SpectrSettingsField, '
    '{ label: "Response", hint: "Live = snappy, Precision = eased" }, '
    '/* @__PURE__ */ React.createElement(\n'
    '    SpectrSettingsChips,\n'
    '    {\n'
    '      value: settings.motionMode,\n'
    '      onChange: (v) => persist({ motionMode: v }),\n'
    '      opts: [["live", "Live"], ["precision", "Precision"]]\n'
    '    }\n'
    '  )))'
)
SETTINGS_NEW = (
    ' /* Hidden with the header\'s LIVE / PRECISION control; restoring it\n'
    '    also means restoring that control and its help section.\n'
    '  , React.createElement(SpectrSettingsGroup, { marker: '
    '"general", title: "MOTION", subtitle: "How gain changes smooth over '
    'time." }, React.createElement(SpectrSettingsField, '
    '{ label: "Response", hint: "Live = snappy, Precision = eased" }, '
    'React.createElement(\n'
    '    SpectrSettingsChips,\n'
    '    {\n'
    '      value: settings.motionMode,\n'
    '      onChange: (v) => persist({ motionMode: v }),\n'
    '      opts: [["live", "Live"], ["precision", "Precision"]]\n'
    '    }\n'
    '  ))) */'
)

TWEAKS_OLD = (
    ', /* @__PURE__ */ React.createElement(Section, { label: "MOTION" }, '
    '/* @__PURE__ */ React.createElement(Row, { label: "Mode" }, '
    '/* @__PURE__ */ React.createElement(\n'
    '    Chips,\n'
    '    {\n'
    '      value: settings.motionMode,\n'
    '      onChange: (v) => persist({ motionMode: v }),\n'
    '      opts: [["live", "Live"], ["precision", "Precision"]]\n'
    '    }\n'
    '  )))'
)
TWEAKS_NEW = (
    ' /* Hidden with the header\'s LIVE / PRECISION control.\n'
    '  , React.createElement(Section, { label: "MOTION" }, '
    'React.createElement(Row, { label: "Mode" }, '
    'React.createElement(\n'
    '    Chips,\n'
    '    {\n'
    '      value: settings.motionMode,\n'
    '      onChange: (v) => persist({ motionMode: v }),\n'
    '      opts: [["live", "Live"], ["precision", "Precision"]]\n'
    '    }\n'
    '  ))) */'
)

HTML_EDITS = [
    ("header: the LIVE / PRECISION control is commented out",
     HEADER_OLD, HEADER_NEW),
    ("draw loop: one easing rate", EASE_OLD, EASE_NEW),
    ("settings: the MOTION group is commented out", SETTINGS_OLD, SETTINGS_NEW),
    ("tweaks panel: the MOTION section is commented out", TWEAKS_OLD, TWEAKS_NEW),
]

# The removed slot and the siblings after it, as the header's child index.
REMOVED_SLOT = 2
LAST_OLD_SLOT = 6

# The document is compact JSON; runtime.js is a JS literal with spaces.
DOCUMENT_BINDING = re.compile(
    r'\{"index":\d+,"anchor":"#root","path":\[\{"tag":"div","index":0\},'
    r'\{"tag":"div","index":1\},\{"tag":"div","index":(\d+)\}')
RUNTIME_BINDING = re.compile(
    r'\{ "anchor": "#root", "path": \[\{ "tag": "div", "index": 0 \}, '
    r'\{ "tag": "div", "index": 1 \}, \{ "tag": "div", "index": (\d+) \}')


def escaped(value):
    return json.dumps(value)[1:-1]


def object_end(text, start):
    """Index one past the `}` closing the object that opens at `start`."""
    depth = 0
    quote = None
    index = start
    while index < len(text):
        ch = text[index]
        if quote:
            if ch == "\\":
                index += 2
                continue
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise ValueError("unterminated object at %d" % start)


def rewrite_bindings(text, pattern, label):
    """Drop slot-2 header bindings and renumber the siblings after it.

    Returns (new_text, dropped, renumbered), or None when the text already has
    no binding in slot 6 -- the rewrite's own signature -- and so is patched.
    """
    slots = [int(m.group(1)) for m in pattern.finditer(text)]
    if not slots:
        sys.exit("FAIL %s: no header binding found; the header is not where "
                 "this script expects it" % label)
    if LAST_OLD_SLOT not in slots:
        if max(slots) != LAST_OLD_SLOT - 1:
            sys.exit("FAIL %s: header bindings reach slot %d; expected either "
                     "%d (unpatched) or %d (patched)"
                     % (label, max(slots), LAST_OLD_SLOT, LAST_OLD_SLOT - 1))
        return None
    out = []
    cursor = 0
    dropped = renumbered = 0
    for match in pattern.finditer(text):
        slot = int(match.group(1))
        if slot < REMOVED_SLOT:
            continue
        start = match.start()
        out.append(text[cursor:start])
        if slot == REMOVED_SLOT:
            end = object_end(text, start)
            # Take the separator with the object so the list stays well formed.
            if text.startswith(", ", end):
                end += 2
            elif text.startswith(",", end):
                end += 1
            elif out and out[-1].endswith(", "):
                out[-1] = out[-1][:-2]
            elif out and out[-1].endswith(","):
                out[-1] = out[-1][:-1]
            cursor = end
            dropped += 1
            continue
        index_at = match.start(1)
        out.append(text[start:index_at])
        out.append(str(slot - 1))
        cursor = match.end(1)
        renumbered += 1
    out.append(text[cursor:])
    return "".join(out), dropped, renumbered


def main():
    raw = open(DOCUMENT, encoding="utf-8").read()
    runtime = open(RUNTIME, encoding="utf-8").read()

    applied = already = 0
    for label, old, new in HTML_EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1 and raw.count(old_e) == 0:
            print("already applied ", label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        applied += 1
        print("applied         ", label)

    document_result = rewrite_bindings(raw, DOCUMENT_BINDING, "document")
    runtime_result = rewrite_bindings(runtime, RUNTIME_BINDING, "runtime.js")
    for label, result in (("document bindings", document_result),
                          ("runtime.js captured bindings", runtime_result)):
        if result is None:
            print("already applied ", label)
            already += 1
        else:
            print("applied          %s: dropped %d, renumbered %d"
                  % (label, result[1], result[2]))
            applied += 1

    if already and applied:
        sys.exit("FAIL: the editor is half patched; refusing to write")
    if not applied:
        print("no change needed")
        return 0

    if document_result is not None:
        raw = document_result[0]
    if runtime_result is not None:
        runtime = runtime_result[0]

    document = json.loads(raw)
    html = document.get("html")
    if not isinstance(html, str):
        sys.exit("FAIL: the patched document no longer carries an html payload")
    for binding_list in ("layout_bindings", "text_bindings", "paint_bindings"):
        for binding in document.get(binding_list, []):
            if binding.get("text") in ("LIVE", "PRECISION"):
                sys.exit("FAIL: a %s entry still captures %r"
                         % (binding_list, binding["text"]))
    open(DOCUMENT, "w", encoding="utf-8").write(raw)
    open(RUNTIME, "w", encoding="utf-8").write(runtime)
    print("written", DOCUMENT)
    print("written", RUNTIME)
    return 0


if __name__ == "__main__":
    sys.exit(main())
