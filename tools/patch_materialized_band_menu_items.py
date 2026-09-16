#!/usr/bin/env python3
"""Make every row of the band context menu do what the row says under a REAL
press, and say that it did it.

Neither generator on `main` can rebuild the materialized runtime document, so
this is a surgical, idempotent patch against the checked-in artifact. Every
anchor is required to occur exactly once before anything is written, and the
marker makes a second run a no-op.

RETRACTED, AND WHY IT MATTERS -- "a press on a row never reaches the row".
An earlier revision of this file claimed that, and added a stopPropagation
guard to the menu root to fix it. It was wrong, and the way it was wrong is
the point: the reading came from `View::simulate_click`, which hit-tests and
then delivers the press with bubble=TRUE, so the press bubbled into the
spectrum surface the menu is mounted inside and the surface claimed the
pointer. A real macOS host does not do that. `window_host_mac.mm -mouseDown:`
consults `route_press_to_active_overlay` FIRST and, for a press inside the
active overlay, delivers it to the overlay target with **bubble=false** and
returns -- the ancestors never see it. Driven through the host's own sequence
in the shipping standalone, every row activates and the menu closes, with and
without that guard, byte-identically. The guard was removed; what replaced it
is a probe that drives the real binary through the host's real press path
(SPECTR_MENU_SCENARIO, tools/menu_scenario_check.py).

DEFECT 2 -- SOLO CHANGED THE LEVEL OF THE BAND IT SOLOED. The row says "Solo /
mute others" and it also raised the soloed band's own gain to `max(0, gain)`,
so soloing a band sitting at -6 dB silently moved it to 0 dB and the level the
user set was gone with no way back. Muting the others is the whole promise;
the only thing the soloed band needs is to not be muted itself, which is what
unmuting it to its stored pre-mute level does.

DEFECT 3 -- "ZERO SELECTION" READ THE SELECTION FROM A RENDER CLOSURE. Every
other selection-wide action in this file reads `selectionRef.current`, and the
comment above that ref explains why: a handler that closes over the `selection`
STATE goes stale the moment its component re-renders for another reason and
React.memo keeps the old props. This makes the one straggler read the mirror.

DEFECT 4 -- THREE ROWS DID THEIR WORK SILENTLY. Mute, Reset, Select all,
Select none and Mute selection all publish a status line; Solo, Zero selection
and Fit full range published nothing, so "it worked" and "the row is dead"
looked identical to the user. Each now says what it did.

MEASURED AND NOT CHANGED -- the viewport publication. "Fit full range" uses
`setView`, which (unlike the live wheel/minimap path) issues no publication of
its own, and that looked like a second defect: zoom and pan are sound-defining
here, so a window the processor never hears about would mean the picture read
20 Hz - 20 kHz while the audio kept the zoomed band mapping. It was planted --
the publication deleted -- and the gate still read the processor's viewport
back at 20 Hz - 20 kHz, because a separate effect recomputes the publication
signature after the render `setView` causes and publishes the difference. So
the row already reaches the processor and nothing here touches that path.
"""

import json
import os
import sys

PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                    "native-ui", "materialized", "materialized-document.runtime.json")

MARKER = "__spectrBandMenuItems"

# ── 2. solo mutes the others and leaves the soloed band's level alone ──────
_SOLO_OLD = '''        onSoloBand: (b) => {
          const map = /* @__PURE__ */ new Map();
          for (let i = 0; i < N; i++) map.set(i, i === b ? Math.max(0, targetGainsRef.current[b]) : -Infinity);
          commitMany(map);
        },'''
_SOLO_NEW = '''        onSoloBand: (b) => {
          const map = /* @__PURE__ */ new Map();
          // "mute others" is the whole promise. The soloed band only has to
          // not be muted itself; raising it to max(0, gain) also DESTROYED a
          // level the user set -- a band at -6 dB came back at 0 dB with no
          // way to recover it. Unmuting restores the level it carried when it
          // was muted, which is what every other unmute in this editor does.
          const current = targetGainsRef.current[b];
          map.set(b, isMuted(current) ? restoreMutedGain(b) : current);
          for (let i = 0; i < N; i++) if (i !== b) map.set(i, -Infinity);
          commitMany(map);
          if (onStatus) onStatus(`BAND ${b + 1} SOLO`);
        },'''

# ── 3. the selection mirror, not a render closure ──────────────────────────
_ZEROSEL_OLD = '''        onZeroSel: () => {
          const map = /* @__PURE__ */ new Map();
          for (const i of selection) map.set(i, 0);
          commitMany(map);
        },'''
_ZEROSEL_NEW = '''        onZeroSel: () => {
          // The mirror, for the reason recorded where it is declared: a
          // handler closing over the `selection` STATE goes stale as soon as
          // its component re-renders for some other reason and React.memo
          // hands the menu back the props it already had.
          // Named `selected` rather than `sel`: the group-mute gate plants
          // its stale-closure control by rewriting the one line that reads
          // `const sel = selectionRef.current;`, and a second copy of that
          // exact text anywhere in the document breaks its exactly-once
          // anchor and disarms the control.
          const selected = selectionRef.current;
          const map = /* @__PURE__ */ new Map();
          for (const i of selected) map.set(i, 0);
          if (map.size === 0) { if (onStatus) onStatus("NO SELECTION"); return; }
          commitMany(map);
          if (onStatus) onStatus(`${map.size} BAND${map.size === 1 ? "" : "S"} ZEROED`);
        },'''

# ── 4. fit full range says what it did ────────────────────────────────────
_FIT_OLD = '''        onFitView: () => setView({ lmin: Math.log10(20), lmax: Math.log10(2e4) })'''
_FIT_NEW = '''        onFitView: () => {
          setView({ lmin: Math.log10(20), lmax: Math.log10(2e4) });
          if (onStatus) onStatus("VIEW \\u2192 20 Hz \\u2013 20 kHz");
        }'''

# The publication helper has to read the LIVE viewport. `setView` replaces the
# object `view` was bound to when the component rendered (the live path mutates
# it in place), so a publication issued between a setView and the next render
# was sending the previous window.
# The marker, so a second run is a no-op and the build can prove which
# document it is holding.
_MARKER_OLD = '''function ContextMenu({ x, y, band, N, selection, editMode, onClose,'''
_MARKER_NEW = '''const __spectrBandMenuItems = true;
function ContextMenu({ x, y, band, N, selection, editMode, onClose,'''

PATCHES = [
    ("solo mutes the others without moving the soloed band's level",
     _SOLO_OLD, _SOLO_NEW),
    ("zero selection reads the selection mirror", _ZEROSEL_OLD, _ZEROSEL_NEW),
    ("fit full range reports what it did", _FIT_OLD, _FIT_NEW),
    ("declare the marker", _MARKER_OLD, _MARKER_NEW),
]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]

    if MARKER in html:
        print("patch_materialized_band_menu_items: already applied")
        return 0

    for name, old, _new in PATCHES:
        count = html.count(old)
        if count != 1:
            print("patch_materialized_band_menu_items: %s: anchor occurs %d "
                  "times (want exactly 1)" % (name, count), file=sys.stderr)
            return 1

    for _name, old, new in PATCHES:
        html = html.replace(old, new, 1)

    for name, _old, new in PATCHES:
        if html.count(new) != 1:
            print("patch_materialized_band_menu_items: %s did not apply" % name,
                  file=sys.stderr)
            return 1
    if MARKER not in html:
        print("patch_materialized_band_menu_items: marker missing",
              file=sys.stderr)
        return 1

    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("patch_materialized_band_menu_items: applied %d patches" % len(PATCHES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
