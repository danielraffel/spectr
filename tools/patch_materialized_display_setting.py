#!/usr/bin/env python3
"""BARS / RESPONSE / BOTH moves from the header into Settings as "Display".

The header needed room for three level knobs (MIX, INTENSITY, OUTPUT + AUTO;
tools/patch_materialized_level_controls.py). The mask visualization switch is
a viewing preference set once and rarely touched, so it becomes a Settings row
in the APPEARANCE group, drawn with the same chips as Theme:

    Display    How the mask is drawn       [Bars] [Response] [Both]

It is the same control underneath: the Visualization host parameter (3103),
set through Chrome's own `setVisualizationMode`, which publishes the mode and
reports the change -- so existing sessions keep their saved choice, automation
still lands, and the default stays Both.

The header group is HIDDEN (display: none), not deleted, along with the
divider in front of it. Captured text/layout bindings address header nodes by
positional DOM path; removing a child re-points every later sibling (the
band-count menu and the zoom readout), so the nodes stay in the tree.

Idempotent: exact substitutions, each asserted unique.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def escaped(value):
    # ensure_ascii=False: the artifact stores non-ASCII literally (see
    # tools/git/merge_materialized_runtime.py), so must every edit.
    return json.dumps(value, ensure_ascii=False)[1:-1]


EDITS = [
    ("the header switch is hidden, not removed",
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-visualization": true, "aria-label": "Mask visualization" }, ',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-visualization": true, "aria-label": "Mask visualization", style: { display: "none" } }, '),
    ("and so is the divider in front of it (the one after it now leads the band-count menu)",
     '/* @__PURE__ */ React.createElement("div", { style: { width: 1, height: 20, background: "rgba(255,255,255,0.08)" } }), /* @__PURE__ */ React.createElement("div", { "data-spectr-visualization": true,',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-visualization-divider": true, style: { width: 1, height: 20, background: "rgba(255,255,255,0.08)", display: "none" } }), /* @__PURE__ */ React.createElement("div", { "data-spectr-visualization": true,'),
    # Display (here) and Range (patch_materialized_range.py) add two rows, so
    # the panel's tall-window cap grows to keep the release layout unscrolled
    # where it was before (test/test_materialized_ux_polish_browser.mjs).
    ("the Settings panel's tall-window cap fits the two new rows",
     'height: "min(92vh, 1500px)",',
     'height: "min(92vh, 1700px)",'),
    ("Settings receives the visualization mode",
     '''    SettingsModal,
    {
      settings,
      setSettings,
      open: settingsOpen,''',
     '''    SettingsModal,
    {
      settings,
      setSettings,
      visualizationMode,
      setVisualizationMode,
      open: settingsOpen,'''),
    ("Settings declares it",
     'function SettingsModal({ settings, setSettings, onClose, open = true }) {',
     'function SettingsModal({ settings, setSettings, onClose, open = true, visualizationMode, setVisualizationMode }) {'),
    ("APPEARANCE gains a Display row",
     '/* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: "general", title: "APPEARANCE", subtitle: "How bands and colors render." }, ',
     '/* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: "general", title: "APPEARANCE", subtitle: "How bands and colors render." }, '
     '/* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Display", hint: "Draw the bands, the response curve, or both" }, '
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-display-setting": visualizationMode || "both" }, '
     '/* @__PURE__ */ React.createElement(SpectrSettingsChips, { value: visualizationMode || "both", '
     'onChange: (v) => typeof setVisualizationMode === "function" && setVisualizationMode(v), '
     'opts: [["bars", "Bars"], ["response", "Response"], ["both", "Both"]] }))), '),
]


def main():
    raw = open(PATH, encoding="utf-8").read()
    changed = False
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1:
            print("already applied ", label)
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        print("applied         ", label)
    html = json.loads(raw)["html"]
    if html.count('label: "Display"') != 1:
        sys.exit("FAIL: the Display row is not present exactly once")
    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
