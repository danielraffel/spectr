#!/usr/bin/env python3
import json
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
data = json.loads(path.read_text())
html = data["html"]
anchor = '''    if (typeof globalThis.spectrModulationExtraRows === "function")
      rows.push(...globalThis.spectrModulationExtraRows({ target, key, lfo, Item, on,
        modulation, publishModulation, ready: modulationReady }));'''
insert = '''    if (target === 0 && selection && selection.size > 0) {
      rows.push(Item({
        key: "assign-band-group-" + lfo,
        action: "assign-band-group-" + lfo,
        label: "Assign selected bands",
        sub: Math.round(amount * 100) + "%",
        disabled: !modulationReady,
        keepOpen: true,
        onClick: () => {
          let lo = 0, hi = 0;
          selection.forEach((index) => {
            if (index < 32) lo += Math.pow(2, index);
            else hi += Math.pow(2, index - 32);
          });
          if (window.pulp && typeof window.pulp.postMessage === "function")
            Promise.resolve(window.pulp.postMessage("band_modulation_group_set", {
              members_lo: lo, members_hi: hi, lfo: lfo - 1, depth: amount
            }, "spectr-band-modulation-group")).catch(() => {});
        }
      }));
    }
''' + anchor
if anchor not in html:
    raise SystemExit("anchor missing or already patched")
html = html.replace(anchor, insert, 1)
data["html"] = html
path.write_text(json.dumps(data, separators=(",", ":"), ensure_ascii=False))
print("applied")
