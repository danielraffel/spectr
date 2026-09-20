#!/usr/bin/env python3
"""Distinguish noninteractive section captions from disabled menu actions.

Replace one style anchor without inserting nodes or changing positional bindings.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "native-ui/materialized/materialized-document.runtime.json"
OLD = '''  const Divider = ({ label }) => /* @__PURE__ */ React.createElement("div", { style: {
    fontSize: 8.5,
    letterSpacing: 2,
    opacity: 0.4,
    padding: "8px 12px 4px",
    textTransform: "uppercase"
  } }, label);'''
NEW = '''  const Divider = ({ label, rule = true }) => /* @__PURE__ */ React.createElement("div", {
    "data-spectr-menu-section": "true",
    style: {
      fontSize: 8.5,
      fontWeight: 600,
      letterSpacing: 2,
      color: "rgba(178,200,224,0.85)",
      background: "rgba(120,160,200,0.08)",
      // A rule marks a section BOUNDARY, so the first header must not carry
      // one -- it would sit directly under the panel's own border and read as
      // a doubled edge rather than a divider.
      borderTop: rule ? "1px solid rgba(178,200,224,0.18)" : "none",
      padding: "8px 12px 4px",
      textTransform: "uppercase",
      cursor: "default"
    }
  }, label);'''

BAND_OLD = 'React.createElement(Divider, { label: `BAND ${band + 1}` })'
BAND_NEW = 'React.createElement(Divider, { label: `BAND ${band + 1}`, rule: false })'


def main():
    original = PATH.read_text()
    document = json.loads(original)
    html = document["html"]
    if html.count(NEW) == 1 and OLD not in html and BAND_NEW in html:
        print("Menu section styling already applied")
        return
    assert html.count(OLD) == 1 and NEW not in html, "menu section anchor drift"
    html = html.replace(OLD, NEW, 1)
    assert html.count(BAND_OLD) == 1 and BAND_NEW not in html, "band header anchor drift"
    html = html.replace(BAND_OLD, BAND_NEW, 1)
    document["html"] = html
    PATH.write_text(json.dumps(document, separators=(",", ":"), ensure_ascii=False) + "\n")
    print("Updated menu section styling; node order and bindings preserved")

if __name__ == "__main__":
    main()
