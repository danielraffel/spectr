#!/usr/bin/env python3
"""Modulate a selection of bands: band groups in the band menu's Modulation
submenu.

Bank moves every band. With bands selected, the submenu lists the selection
as its own target above Bank -- "Band 12", "Bands 12-15" or "4 bands" -- with
a switch and a Depth per LFO like every other target. Switching it on makes a
band group natively (`band_group_set`); groups that exist but are not the
current selection are listed under it with their own switch and Depth, so
each group keeps its own Depth. The native modulation payload carries
`groups: [{bands: [...], lfo: [{on, amount}, {on, amount}]}]`, which
`spectrModulationFromNative` reads into `groups`.

Idempotent raw-text surgery on the materialized document; re-running is a
no-op once applied.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DOC = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"

def encode(text):
    return json.dumps(text)[1:-1]

EDITS = [
    # 1. read the groups from the native payload (presence, not truthiness:
    #    a payload without the key leaves the editor's copy alone)
    ("""  spectrReadModulationRoutes(modulation, next);
""",
     """  spectrReadModulationRoutes(modulation, next);
    if (Array.isArray(modulation.groups))
      next.groups = modulation.groups.map((g) => ({
        bands: Array.isArray(g && g.bands) ? g.bands.map(Number).filter(Number.isFinite) : [],
        lfo: [0, 1].map((i) => ({
          on: !!(g && g.lfo && g.lfo[i] && g.lfo[i].on === true),
          amount: Number.isFinite(Number(g && g.lfo && g.lfo[i] && g.lfo[i].amount))
            ? Math.max(0, Math.min(1, Number(g.lfo[i].amount))) : 0.5 }))
      })).filter((g) => g.bands.length > 0);
"""),
    # 2. the rows: the selection first, then the other groups
    ("""  const modulationWheel = (event) => {
    const delta = event && typeof event.deltaY === "number" ? event.deltaY : 0;
    if (!delta || !modulationGeomRef.current.scrolls) return;
""",
     """  // Band groups (tools/patch_materialized_band_group_rows.py): the current
  // selection as a target of its own, then every group that is not it.
  const spectrBandGroupName = (bands) => {
    const sorted = bands.slice().sort((a, b) => a - b);
    if (sorted.length === 1) return "Band " + (sorted[0] + 1);
    const run = sorted.every((b, i) => i === 0 || b === sorted[i - 1] + 1);
    return run ? "Bands " + (sorted[0] + 1) + "\\u2013" + (sorted[sorted.length - 1] + 1)
               : sorted.length + " bands";
  };
  const spectrBandGroupRows = () => {
    const lfo = modulationSource;
    const groups = Array.isArray(modulation.groups) ? modulation.groups : [];
    const sameBands = (a, b) => a.length === b.length
      && a.slice().sort((x, y) => x - y).every((v, i) => v === b.slice().sort((x, y) => x - y)[i]);
    const selected = selection && selection.size > 0 ? Array.from(selection).map(Number) : [];
    const post = (bands, on, amount) => {
      if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
      Promise.resolve(window.pulp.postMessage("band_group_set",
        { bands, lfo, on, amount }, "spectr-band-group")).catch((error) =>
          console.error("[Spectr] band group failed", error));
    };
    const rowsFor = (bands, key, group) => {
      const route = group ? group.lfo[lfo - 1] : null;
      const on = !!(route && route.on);
      const amount = route ? route.amount : 0.5;
      return [
        Item({ key, action: "modulation-group-" + key, label: spectrBandGroupName(bands),
          sub: on ? "On" : "Off", toggle: on, checked: on,
          disabled: !modulationReady, keepOpen: true,
          onClick: () => post(bands, !on, amount) }),
        SliderRow({ key: key + "-depth", action: "modulation-group-depth-" + key, label: "Depth",
          value: amount, min: 0, max: 1, step: 0.01, indent: true, hidden: !on,
          fmt: (v) => Math.round(v * 100) + "%",
          disabled: !modulationReady || !on,
          onChange: (v) => post(bands, true, Math.round(v * 100) / 100) })
      ];
    };
    const rows = [];
    if (selected.length) {
      const own = groups.find((g) => sameBands(g.bands, selected)) || null;
      rows.push(...rowsFor(selected, "selection", own));
    }
    groups.forEach((g, i) => {
      if (selected.length && sameBands(g.bands, selected)) return;
      rows.push(...rowsFor(g.bands, "g" + i, g));
    });
    return rows;
  };
  const modulationWheel = (event) => {
    const delta = event && typeof event.deltaY === "number" ? event.deltaY : 0;
    if (!delta || !modulationGeomRef.current.scrolls) return;
"""),
    ("""        spectrModulationRouteTargets.flatMap(([target, key, label]) =>
          spectrModulationTargetRows(target, key, label))),""",
     """        ...spectrBandGroupRows(),
        spectrModulationRouteTargets.flatMap(([target, key, label]) =>
          spectrModulationTargetRows(target, key, label))),"""),
]

def main():
    raw = DOC.read_text(encoding="utf-8")
    applied = 0
    for old, new in EDITS:
        o, n = encode(old), encode(new)
        if raw.count(n) == 1:
            continue
        if raw.count(o) != 1:
            print(f"band group rows: anchor not unique ({raw.count(o)}): {old[:70]!r}", file=sys.stderr)
            return 1
        raw = raw.replace(o, n)
        applied += 1
    if applied:
        DOC.write_text(raw, encoding="utf-8")
        print(f"band group rows: applied {applied} edit(s)")
    else:
        print("band group rows: already applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
