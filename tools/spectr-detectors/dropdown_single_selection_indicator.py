#!/usr/bin/env python3
"""Assert an opened dropdown never shows a keyboard cursor on a row other than
the one it has selected.

WHY THIS EXISTS

    Opening EDIT MODE with LEVEL selected lit TWO rows: Spectr's own selected
    treatment on LEVEL, and a second highlight on SCULPT.  The second one is
    Pulp's popup owner painting its keyboard cursor, which it seeds from the
    list's ARIA selection -- and these rows advertised none, so it fell back to
    index 0.  A user who has touched neither the mouse nor the keyboard sees
    two indicators and cannot tell which value is current.

    The Settings `Select` already carried `aria-selected` and was already
    correct, which is why this only ever showed on the toolbar menus.

HOW IT MEASURES

    The invariant is deliberately an INCLUSION, not an equality, because the
    correct rendering differs by SDK and both are acceptable: Pulp's owner
    currently seeds a visible cursor on open, and a later SDK defers painting
    one until the user hovers or presses an arrow.  Either way the set of rows
    carrying `data-pulp-popup-active="true"` on a freshly opened menu must be a
    SUBSET of the selected row.  Two lit rows, or one lit row that is not the
    selection, is the defect.

    LEVEL is selected first precisely so "the cursor is on the selection" and
    "the cursor defaulted to index 0" are different readings; with the default
    SCULPT selected the two are indistinguishable and the check is vacuous.

    Pulp's owner claims a menu from the POINTERDOWN branch, and `SPECTR_CLICK`
    dispatches a click without one -- so the probe issues the pointerdown
    itself.  `claimed=` is the control for that: an unclaimed popup paints no
    cursor at all and would pass this check while proving nothing.

    The arrow step is the second control.  An empty `lit` reading means either
    "nothing is highlighted" or "this probe cannot see a highlight", and only a
    reading that MUST be non-empty separates them: after an ArrowDown some row
    is highlighted under every SDK.

    Two ways in. Given the app it drives it and captures its own reading.
    Given `--log` it adjudicates a committed one instead, which is what lets
    the selftest prove this rule still fires without a build; `--plant` there
    moves the cursor onto a row that is not the selection, which is the defect.

Exit codes: 0 pass, 1 fail, 2 inconclusive (the probe could not measure).
"""
import argparse
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(REPO, "build-now", "Spectr.app", "Contents", "MacOS", "Spectr")

TRIGGER = '[data-spectr-menu-root="edit"] [data-spectr-menu-trigger]'
ROW = '[data-spectr-edit-mode="level"]'
# Open, choose LEVEL (which closes the menu), reopen. The menu is then open
# with a selection that is NOT the list's first row.
CLICKS = ",".join((TRIGGER, ROW, TRIGGER))

EVAL = r'''(function(){
  function rows(){return document.querySelectorAll("[data-spectr-edit-mode]");}
  function pick(attr,want){var r=rows(),o=[];for(var i=0;i<r.length;i++)
    if(r[i].getAttribute(attr)===want)o.push(r[i].getAttribute("data-spectr-edit-mode"));
    return o.join(",")||"(none)";}
  function ev(type,extra){var e={type:type,bubbles:true,cancelable:true,
    preventDefault:function(){},stopPropagation:function(){},
    stopImmediatePropagation:function(){}};
    if(extra)for(var k in extra)e[k]=extra[k];return e;}
  var t=document.querySelector('SELECTOR');
  if(!t){console.log("[one] trigger=missing");return;}
  var down=ev("pointerdown");down.target=t;t.dispatchEvent(down);
  globalThis.__pulpRuntimeSettle__(24);
  console.log("[one] rows="+rows().length
    +" selected="+pick("aria-selected","true")
    +" lit_on_open="+pick("data-pulp-popup-active","true")
    +" claimed="+!!globalThis.__pulpPopupDefaultState__);
  var k=ev("keydown",{key:"ArrowDown",code:"ArrowDown"});k.target=document.body;
  document.dispatchEvent(k);
  if(window!==document)window.dispatchEvent(ev("keydown",{key:"ArrowDown",code:"ArrowDown"}));
  globalThis.__pulpRuntimeSettle__(24);
  console.log("[one] lit_after_arrow="+pick("data-pulp-popup-active","true"));
})();'''.replace("SELECTOR", TRIGGER.replace('"', '\\"'))


def parse(text):
    fields = {}
    for line in text.splitlines():
        if "[one]" not in line:
            continue
        for token in line.split("[one]", 1)[1].split():
            if "=" in token:
                key, value = token.split("=", 1)
                fields[key] = value
    return fields


def main():
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("app", nargs="?", default=APP)
    parser.add_argument("--log", help="adjudicate a committed probe reading")
    parser.add_argument("--plant", action="store_true")
    args = parser.parse_args()
    if args.log:
        with open(args.log) as fh:
            text = fh.read()
        if args.plant:
            # The defect: the popup owner's cursor sits on a row the app has
            # not selected, so two rows read as current at once.
            text = text.replace("lit_on_open=level", "lit_on_open=sculpt")
        return adjudicate(text)

    app = args.app
    if not os.path.exists(app):
        print("INCONCLUSIVE: app not built at %s" % app)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ)
        env.update(PULP_HEADLESS="1", PULP_FRAMES="90",
                   PULP_SCREENSHOT=os.path.join(tmp, "one.png"),
                   SPECTR_CLICK=CLICKS, SPECTR_EVAL=EVAL)
        out = subprocess.run([app], env=env, capture_output=True, timeout=240)
        text = (out.stdout or b"").decode("utf-8", "replace") \
            + (out.stderr or b"").decode("utf-8", "replace")
        with open(os.path.join(tmp, "one.log"), "w") as fh:
            fh.write(text)
    return adjudicate(text)


def adjudicate(text):
    fields = parse(text)
    print("READ %r" % (fields,))
    if not fields or "lit_on_open" not in fields:
        print("INCONCLUSIVE: the probe printed no reading")
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2
    if fields.get("rows") != "5":
        print("INCONCLUSIVE: found %r mode rows, expected 5 -- the menu is not "
              "open, so 'nothing is highlighted' is vacuous" % fields.get("rows"))
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2
    if fields.get("claimed") != "true":
        print("INCONCLUSIVE: Pulp's popup owner did not claim the menu, so it "
              "would paint no cursor whatever the rows say")
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2
    if fields.get("lit_after_arrow", "(none)") == "(none)":
        print("INCONCLUSIVE: no row is highlighted even after an arrow key -- "
              "this probe cannot see a highlight at all, so the open-state "
              "reading proves nothing")
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    selected = fields.get("selected", "(none)")
    if selected != "level":
        print("FAIL: the reopened menu reports %r as selected, expected 'level'; "
              "without a selection the popup owner has nothing to seed its "
              "cursor from and falls back to the first row" % selected)
        return 1

    lit = fields.get("lit_on_open", "(none)")
    allowed = set() if lit == "(none)" else set(lit.split(","))
    if not allowed <= {selected}:
        print("FAIL: a freshly opened menu highlights %r while %r is selected -- "
              "two indicators, and the user cannot tell which is current"
              % (sorted(allowed), selected))
        return 1

    print("PASS: a freshly opened menu shows no indicator other than its "
          "selection (selected=%s lit_on_open=%s), and the highlight is live "
          "(after ArrowDown: %s)"
          % (selected, lit, fields.get("lit_after_arrow")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
