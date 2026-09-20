#!/usr/bin/env python3
"""Move the macro rows under a single `Macros ›` entry (closes #165).

Eight flat rows -- `Assign selection to Macro 1-4` plus `Clear Macro N` --
put the band menu at 871px against a ~600px surface, so the last two fell
off the end and could not be pressed. #165 withdrew them originally and said
to restore them "once the menu container grows"; a submenu reaches the same
end by not needing the container to grow at all: 8 root rows become 1.

This mirrors the modulation submenu already in this document -- same anchor
wrapper, same fixed panel, same edge flip (`submenuOnLeft`), same
`‹ Back` row, same hover/ArrowRight/ArrowLeft behaviour -- rather than
inventing a second mechanism.

Idempotent. Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

STATE_OLD = '  const [modulationOpen, setModulationOpen] = React.useState(false);'
STATE_NEW = ('  const [modulationOpen, setModulationOpen] = React.useState(false);\n'
             '  const [macrosOpen, setMacrosOpen] = React.useState(false);')

ARROW_OLD = '''        if (modulationOpen && event.key === "ArrowLeft") {
          event.preventDefault();
          setModulationOpen(false);
        }'''
ARROW_NEW = '''        if ((modulationOpen || macrosOpen) && event.key === "ArrowLeft") {
          event.preventDefault();
          setModulationOpen(false);
          setMacrosOpen(false);
        }'''

ROWS_OLD = '''    /* @__PURE__ */ React.createElement(Divider, { label: "MACROS" }),
    hasSel && [0, 1, 2, 3].map(i => /* @__PURE__ */ React.createElement(Item, {
      key: "assign-" + i, action: "assign-macro-" + i,
      label: `Assign selection to Macro ${i + 1}`, onClick: () => onMacroMembers(i, false)
    })),
    assigned.map(i => /* @__PURE__ */ React.createElement(Item, {
      key: "clear-" + i, action: "clear-macro-" + i,
      label: `Clear Macro ${i + 1}`, onClick: () => onMacroMembers(i, true)
    }))'''

ROWS_NEW = '''    React.createElement("div", {
      "data-spectr-macros-anchor": true,
      style: { position: "relative", width: "100%" }
    },
      React.createElement(Item, {
        action: "macros-toggle", label: "Macros", sub: "\\u203A",
        keepOpen: true, expanded: macrosOpen, hasPopup: "menu",
        onHover: () => setMacrosOpen(true),
        onKeyDown: (event) => {
          if (event.key === "ArrowRight") {
            event.preventDefault();
            setMacrosOpen(true);
          }
        },
        onClick: () => setMacrosOpen(open => !open)
      }),
      macrosOpen && React.createElement("div", {
        "data-spectr-macros-panel": true,
        role: "menu",
        "aria-label": "Macros",
        style: {
          display: "flex", flexDirection: "column", position: "fixed",
          left: submenuOnLeft ? Math.max(8, left - W - 6) : Math.min(vw - W - 8, left + W + 6),
          top: submenuTop, width: W,
          background: "rgba(12,16,22,0.97)",
          border: "1px solid rgba(255,255,255,0.12)", borderRadius: 5,
          padding: "6px 0", boxShadow: "0 14px 40px rgba(0,0,0,0.6)",
          backdropFilter: "blur(12px)", zIndex: 2147483002,
          pointerEvents: "auto"
        }
      },
        React.createElement(Item, { action: "macros-back", label: "\\u2039 Back", keepOpen: true, onClick: () => setMacrosOpen(false) }),
        React.createElement(Divider, { label: "MACROS" }),
        hasSel && [0, 1, 2, 3].map(i => React.createElement(Item, {
          key: "assign-" + i, action: "assign-macro-" + i,
          label: `Assign selection to Macro ${i + 1}`, onClick: () => onMacroMembers(i, false)
        })),
        assigned.map(i => React.createElement(Item, {
          key: "clear-" + i, action: "clear-macro-" + i,
          label: `Clear Macro ${i + 1}`, onClick: () => onMacroMembers(i, true)
        }))
      )
    )'''

EDITS = [("macros submenu state", STATE_OLD, STATE_NEW),
         ("ArrowLeft closes whichever submenu is open", ARROW_OLD, ARROW_NEW),
         ("the eight macro rows move into a submenu", ROWS_OLD, ROWS_NEW)]


def main():
    document = json.loads(PATH.read_text())
    html = document["html"]
    done = sum(1 for _, _, new in EDITS if html.count(new) >= 1)
    if done == len(EDITS):
        print("macros submenu already applied")
        return 0
    if done:
        sys.exit("FAIL: document is half patched (%d/%d); refusing to write"
                 % (done, len(EDITS)))
    for label, old, _ in EDITS:
        if html.count(old) != 1:
            sys.exit("FAIL %s: anchor occurs %d times, expected 1"
                     % (label, html.count(old)))
    for label, old, new in EDITS:
        html = html.replace(old, new, 1)
    for token in ('data-spectr-macros-panel', 'action: "macros-toggle"',
                  'const [macrosOpen, setMacrosOpen]', 'Assign selection to Macro'):
        if token not in html:
            sys.exit("FAIL: %r absent after patching" % token)
    document["html"] = html
    PATH.write_text(json.dumps(document, separators=(",", ":"), ensure_ascii=False) + "\n")
    print("macros now live under one `Macros ›` entry")
    return 0


if __name__ == "__main__":
    sys.exit(main())
