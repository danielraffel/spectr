#!/usr/bin/env python3
"""Open the band menu's submenus BESIDE the menu, inside the viewport.

Measured on the shipping standalone at a 990x645 viewport: the `Macros`
panel computed itself for (122, 88) and painted at (501, 701) -- 56px below
the bottom of the window, across the transport rail. "Assign selection to
Macro 3" lost its own centre to the Snapshot `B` button and "Macro 4" was
entirely off-screen, so #165's fix was not reachable by a pointer.

Two independent causes, both fixed here:

1. SPACE. Each panel was nested inside its trigger row's
   `position: relative` wrapper. Yoga has no `fixed`, so Pulp resolves it
   like `absolute` -- against the nearest positioned ancestor, which was
   that wrapper. The panel's `left`/`top` were therefore measured from the
   trigger row rather than the viewport, and the error grew with how far
   down the menu the trigger sat. The panels now render as SIBLINGS of the
   menu container at the component's mount point, so neither they nor the
   menu sit inside a positioned ancestor and `fixed` resolves against the
   root for both. That is the same space the menu's own `left`/`top`
   already worked in -- not a compensating offset, which would only have
   been correct for one trigger position and one viewport size.

2. HEIGHT. The clamp reserved a CONSTANT `Math.min(440, menuMaxHeight)`.
   The macros panel is 186px, so the clamp held its top 254px higher than
   it needed to be, and a panel taller than 440 would have been clamped as
   if it fit. Each panel now measures itself, exactly as the menu already
   does for its own height, and the clamp uses that. The estimate stands
   for one frame and the layout effect corrects it.

The edge flip (`submenuOnLeft`) is unchanged and still decides which side
the panel opens on.

Attaching a `ref` to a div that already exists mounts no new node, which
matters: the document's bindings address nodes by positional DOM path and a
new child would re-point every later sibling.

Idempotent. Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = (Path(__file__).resolve().parents[1]
        / "native-ui/materialized/materialized-document.runtime.json")

SENTINEL = '"data-spectr-submenu-lifted": true'

GEOM_OLD = '''  const submenuOnLeft = left + W * 2 + 6 > vw - 8;
  const submenuHeight = Math.min(440, menuMaxHeight);
  const submenuTop = Math.max(8, Math.min(top + 108 + (hasSel ? 100 : 0), menuBottom - submenuHeight - 8));'''

GEOM_NEW = '''  const submenuOnLeft = left + W * 2 + 6 > vw - 8;
  const submenuLeft = submenuOnLeft
    ? Math.max(8, left - W - 6)
    : Math.min(vw - W - 8, left + W + 6);
  // Each panel measures itself, for the same reason the menu does: the
  // constant this replaces (440) reserved room the macros panel (186px) did
  // not need and would have under-reserved for a panel taller than it. A
  // clamp against a guess is a clamp that is wrong in one direction or the
  // other for every shape that is not the guess.
  const macrosRef = React.useRef(null);
  const modulationRef = React.useRef(null);
  const [macrosH, setMacrosH] = React.useState(null);
  const [modulationH, setModulationH] = React.useState(null);
  React.useLayoutEffect(() => {
    const read = (node, current, set) => {
      if (!node) { if (current !== null) set(null); return; }
      const h = node.offsetHeight || node.clientHeight || 0;
      if (Number.isFinite(h) && h > 0 && h !== current) set(h);
    };
    read(macrosRef.current, macrosH, setMacrosH);
    read(modulationRef.current, modulationH, setModulationH);
  });
  const submenuTopFor = (measured, estimate) => {
    const h = Math.min(measured === null ? estimate : measured, menuMaxHeight);
    return Math.max(8, Math.min(top + 108 + (hasSel ? 100 : 0),
                                menuBottom - h - 8));
  };
  const macrosTop = submenuTopFor(
    macrosH, 68 + (hasSel ? 116 : 0) + assigned.length * 29);
  const modulationTop = submenuTopFor(modulationH, 420);'''

RETURN_OLD = '''return /* @__PURE__ */ React.createElement(
    "div",
    {
      ref,
      "data-spectr-overlay": "true",'''

RETURN_NEW = '''return React.createElement(React.Fragment, null,
    /* @__PURE__ */ React.createElement(
    "div",
    {
      ref,
      "data-spectr-overlay": "true",'''

TAIL_OLD = '    )),\n  );\n}\nwindow.ContextMenu = ContextMenu;'
TAIL_NEW = ('    )),\n  ),\n  macrosPanel,\n  modulationPanel);\n}\n'
            'window.ContextMenu = ContextMenu;')

PANELS = [
    ("macros", 'macrosOpen && React.createElement("div", {',
     '"data-spectr-macros-panel": true,', 'macrosRef', 'macrosTop'),
    ("modulation", 'modulationOpen && React.createElement("div", {',
     '"data-spectr-modulation-panel": true,', 'modulationRef',
     'modulationTop'),
]


def balanced(html, marker, label):
    """The whole `<state> && React.createElement(...)` expression."""
    start = html.find(marker)
    if start < 0:
        sys.exit("FAIL %s: panel marker not found" % label)
    if html.count(marker) != 1:
        sys.exit("FAIL %s: panel marker occurs %d times, expected 1"
                 % (label, html.count(marker)))
    open_paren = html.find('(', html.find('React.createElement', start))
    depth = 0
    for i in range(open_paren, len(html)):
        if html[i] == '(':
            depth += 1
        elif html[i] == ')':
            depth -= 1
            if depth == 0:
                return start, i + 1
    sys.exit("FAIL %s: unbalanced panel expression" % label)


def main():
    document = json.loads(PATH.read_text())
    html = document["html"]

    if SENTINEL in html:
        if html.count(SENTINEL) != len(PANELS):
            sys.exit("FAIL: %d lifted panel(s), expected %d; refusing to write"
                     % (html.count(SENTINEL), len(PANELS)))
        print("submenu placement already applied")
        return 0

    for label, anchor in (("submenu geometry", GEOM_OLD),
                          ("component return", RETURN_OLD),
                          ("component tail", TAIL_OLD)):
        if html.count(anchor) != 1:
            sys.exit("FAIL %s: anchor occurs %d times, expected 1"
                     % (label, html.count(anchor)))

    # Lift each panel out of its trigger's positioned wrapper, giving it a
    # ref to measure itself and its own measured-height clamp.
    lifted = {}
    for label, marker, prop, ref, top_var in PANELS:
        a, b = balanced(html, marker, label)
        block = html[a:b]
        if block.count(prop) != 1 or block.count('top: submenuTop,') != 1:
            sys.exit("FAIL %s: panel body does not carry its expected "
                     "props exactly once" % label)
        block = block.replace(
            prop, '%s\n        %s,\n        ref: %s,' % (prop, SENTINEL, ref), 1)
        block = block.replace('top: submenuTop,', 'top: %s,' % top_var, 1)
        lifted[label] = block
        # The panel is its wrapper's last child, so drop the separator too.
        cut = '}),\n      ' + html[a:b]
        if html.count(cut) != 1:
            sys.exit("FAIL %s: wrapper separator occurs %d times, expected 1"
                     % (label, html.count(cut)))
        html = html.replace(cut, '})', 1)

    html = html.replace(GEOM_OLD, GEOM_NEW, 1)
    html = html.replace(
        RETURN_OLD,
        '  const macrosPanel = %s;\n  const modulationPanel = %s;\n  %s'
        % (lifted["macros"], lifted["modulation"], RETURN_NEW), 1)
    html = html.replace(TAIL_OLD, TAIL_NEW, 1)

    for token in (SENTINEL, 'const macrosPanel =', 'const modulationPanel =',
                  'React.createElement(React.Fragment, null,',
                  'macrosTop', 'modulationTop', 'submenuTopFor'):
        if token not in html:
            sys.exit("FAIL: %r absent after patching" % token)
    if 'top: submenuTop,' in html or 'const submenuHeight' in html:
        sys.exit("FAIL: the assumed-height clamp survived the patch")

    document["html"] = html
    PATH.write_text(json.dumps(document, separators=(",", ":"),
                               ensure_ascii=False) + "\n")
    print("submenus now open beside the menu, clamped to their measured height")
    return 0


if __name__ == "__main__":
    sys.exit(main())
