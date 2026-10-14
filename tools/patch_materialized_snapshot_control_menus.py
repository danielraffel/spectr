#!/usr/bin/env python3
"""The SNAPSHOT A / B capture buttons open a control menu on right-click.

Snapshot A and Snapshot B are modulation targets, and they were the two
targets with a header control but no control menu: every other target's
control (LIVE / FROZEN, LENGTH, MIX, INTENSITY, OUTPUT, BANDS, MORPH, PRESET)
opens its own menu on right-click, with the LFO switches and that control's
route rows. The capture buttons (A, B) now do the same; the recall buttons
keep their existing right-click, which empties a filled slot.

Idempotent raw-text surgery on the materialized document; re-running is a
no-op once applied.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DOC = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"

def encode(text):
    return json.dumps(text)[1:-1]

EDITS = [
    ("""  morph: { title: "MORPH", target: 3, reset: "Reset to A" },
  preset: { title: "PRESET", target: 12 }
};""",
     """  morph: { title: "MORPH", target: 3, reset: "Reset to A" },
  preset: { title: "PRESET", target: 12 },
  // The snapshot capture buttons: the slot is the target.
  snapshotA: { title: "SNAPSHOT A", target: 1 },
  snapshotB: { title: "SNAPSHOT B", target: 2 }
};"""),
    ("""React.createElement(SnapBtn, { id: "spectr-snapshot-capture-a", action: "capture", slot: "A", filled: snapshotStatus.A, onClick: act((b) => b.snapshot("A")), capture: true, label: "A" }""",
     """React.createElement(SnapBtn, { id: "spectr-snapshot-capture-a", action: "capture", slot: "A", filled: snapshotStatus.A, onClick: act((b) => b.snapshot("A")), capture: true, label: "A", onContextMenu: (e) => { if (e && typeof e.preventDefault === "function") e.preventDefault(); spectrOpenControlMenu("snapshotA", e); } }"""),
    ("""React.createElement(SnapBtn, { id: "spectr-snapshot-capture-b", action: "capture", slot: "B", filled: snapshotStatus.B, onClick: act((b) => b.snapshot("B")), capture: true, label: "B" }""",
     """React.createElement(SnapBtn, { id: "spectr-snapshot-capture-b", action: "capture", slot: "B", filled: snapshotStatus.B, onClick: act((b) => b.snapshot("B")), capture: true, label: "B", onContextMenu: (e) => { if (e && typeof e.preventDefault === "function") e.preventDefault(); spectrOpenControlMenu("snapshotB", e); } }"""),
    ("""function SnapBtn({ id, action, slot, filled, onClick, onClear, capture, label }) {""",
     """function SnapBtn({ id, action, slot, filled, onClick, onClear, capture, label, onContextMenu }) {"""),
    ("""      onContextMenu: onClear && !isCapture && filled ? (event) => {
        if (event && event.preventDefault) event.preventDefault();
        onClear(event);
      } : void 0,""",
     """      onContextMenu: onContextMenu ? onContextMenu : onClear && !isCapture && filled ? (event) => {
        if (event && event.preventDefault) event.preventDefault();
        onClear(event);
      } : void 0,"""),
]

def main():
    raw = DOC.read_text(encoding="utf-8")
    applied = 0
    for old, new in EDITS:
        o, n = encode(old), encode(new)
        if raw.count(n) == 1:
            continue
        if raw.count(o) != 1:
            print(f"snapshot control menus: anchor not unique ({raw.count(o)}): {old[:70]!r}", file=sys.stderr)
            return 1
        raw = raw.replace(o, n)
        applied += 1
    if applied:
        DOC.write_text(raw, encoding="utf-8")
        print(f"snapshot control menus: applied {applied} edit(s)")
    else:
        print("snapshot control menus: already applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
