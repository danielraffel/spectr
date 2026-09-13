#!/usr/bin/env python3
"""Assert every commit gesture the Preset Manager ADVERTISES reaches its owner.

WHY THIS EXISTS

    The manager's footer is the app's promise.  It read "DOUBLE-CLICK to
    apply" and double-clicking a preset row did nothing at all -- measured on
    the shipping standalone, one row, the same instrument three times:

        1 click  on factory:comb -> manager OPEN,   0/32 bands moved
        2 clicks on factory:comb -> manager OPEN,   0/32 bands moved
        APPLY button (control)   -> manager CLOSED, 32/32 bands moved

    The row carried `onDoubleClick: onDblClick`, which is why this read as
    wired on a source skim.  It was not.  `prop-applier`'s `eventNameFor`
    lowercases the prop name, so `onDoubleClick` registers its callback under
    the bridge event name `doubleclick` -- and nothing in Pulp dispatches
    that name (a sweep of core/ packages/ tools/ finds 0 files containing it,
    against controls `dblclick` 4, `mouseenter` 12, `panchange` 4).  The prop
    was a handler that could never run.

    Nothing else in the suite can see this.  Every screenshot, every layout
    assertion and every caption check passes on a dead gesture -- the footer
    renders identically whether the handler fires or not.

WHAT IT ADJUDICATES

    Not "does the gesture work" -- a static artifact read cannot know that,
    and this deliberately does not claim to.  It adjudicates the STRUCTURE
    that a working gesture requires, which is what silently rots:

      1. Every gesture the footer names has a live handler.  DOUBLE-CLICK
         requires both row lists to route through the click-pair detector.
         Return is required unconditionally: it is shipped behaviour that the
         footer deliberately does not name, exactly as the arrow keys are.
      2. No row handler is bound to a prop the native runtime cannot
         dispatch (`onDoubleClick` / `onDblClick`).
      3. There is exactly ONE commit owner, and the detail pane's APPLY
         reaches it rather than re-typing the commit body.  Two owners for
         one gesture is how the two drift apart.

    The gestures themselves are proved end-to-end against the built app by
    `preset_commit_gestures_app.py`, which asserts the END STATE -- bands
    moved and dialog closed -- rather than that a handler ran.

PLANTS

    Each re-introduces one half of the original defect into the extracted
    text and re-runs the same adjudication, which MUST then go red.

      --plant dead-prop    put `onDoubleClick: onDblClick` back on the row
      --plant no-enter     delete the Enter branch Return commits through
      --plant one-list     revert the user list to select-only

Exit codes: 0 pass, 1 fail, 2 no verdict (the instrument could not measure),
4 a plant could not be applied (the detector is broken, not the build).
"""

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARTIFACT = os.path.join(REPO, "native-ui", "materialized",
                        "materialized-document.runtime.json")

MANAGER = "PatternManager"
ROW = "PatternRow"
# Props that register under a bridge event name nothing in the runtime ever
# dispatches.  A handler on one of these is not a handler.
DEAD_PROPS = ("onDoubleClick", "onDblClick")


def load_payload(path):
    if not os.path.exists(path):
        sys.exit("no verdict: %s does not exist" % path)
    with open(path, encoding="utf-8") as fh:
        doc = json.load(fh)
    html = doc.get("html")
    if not isinstance(html, str) or not html:
        sys.exit("no verdict: %s has no 'html' payload" % path)
    return html


def extract_declared_function(payload, name):
    """Return the brace-balanced body of `function <name>(...) { ... }`.

    The parameter list is balanced FIRST.  Both of these components destructure
    their props, so the first `{` after the name opens the PARAMETER pattern,
    not the body -- taking it yields a 44-character "body" that contains none
    of the code being adjudicated and reads as a restructured runtime.
    """
    m = re.search(r"function\s+" + re.escape(name) + r"\s*\(", payload)
    if not m:
        return None
    paren = payload.index("(", m.start())
    depth = 0
    end_of_params = None
    for i in range(paren, len(payload)):
        if payload[i] == "(":
            depth += 1
        elif payload[i] == ")":
            depth -= 1
            if depth == 0:
                end_of_params = i
                break
    if end_of_params is None:
        return None
    start = payload.index("{", end_of_params)
    depth = 0
    for i in range(start, len(payload)):
        if payload[i] == "{":
            depth += 1
        elif payload[i] == "}":
            depth -= 1
            if depth == 0:
                return payload[start:i + 1]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--artifact", default=ARTIFACT)
    ap.add_argument("--plant", choices=["dead-prop", "no-enter", "one-list"],
                    help="re-introduce one half of the original defect, so the "
                         "detector MUST report the regression it exists for")
    args = ap.parse_args()

    payload = load_payload(args.artifact)

    # Positive controls.  A detector that cannot find its subject reports
    # every build as clean, so prove the payload is the one we think it is
    # before any verdict is rendered.
    controls = {
        "manager component": payload.count("function %s(" % MANAGER),
        "row component": payload.count("function %s(" % ROW),
        "manager footer": payload.count("to apply"),
        "row lists": payload.count("filteredFactory.map")
                     + payload.count("filteredUser.map"),
    }
    for label, count in sorted(controls.items()):
        print("control   %-22s %d" % (label, count))
    missing = [k for k, v in controls.items() if v == 0]
    if missing:
        sys.exit("no verdict: the payload has no %s -- this detector is "
                 "reading the wrong document or the manager was restructured"
                 % ", ".join(missing))

    manager = extract_declared_function(payload, MANAGER)
    row = extract_declared_function(payload, ROW)
    if manager is None or row is None:
        sys.exit("no verdict: could not extract %s/%s; they are no longer "
                 "plain function declarations and this detector needs updating"
                 % (MANAGER, ROW))
    print("subject   %-22s %d chars" % (MANAGER + "() body", len(manager)))
    print("subject   %-22s %d chars" % (ROW + "() body", len(row)))

    # Adjudicate CODE, never prose. The comment that records why a dead prop
    # could never fire necessarily names that prop, and a detector that cannot
    # tell the two apart reddens on its own documentation.
    def code_only(body):
        return "\n".join(line for line in body.splitlines()
                          if not line.lstrip().startswith("//"))

    # The footer text is the promise being adjudicated.  Read it off the
    # manager rather than assuming it, so changing the wording changes what
    # this detector requires instead of silently checking the wrong thing.
    footer = re.search(r'"([^"]*to apply)"', manager)
    if footer is None:
        sys.exit("no verdict: the manager names no '... to apply' footer, so "
                 "there is no advertised gesture to adjudicate")
    advertised = footer.group(1)
    print("subject   %-22s %r" % ("advertised", advertised))

    if args.plant == "dead-prop":
        needle = "      onClick,\n"
        if needle not in row:
            print("PLANT IMPOSSIBLE: the row has no onClick prop line to "
                  "attach a dead prop to", file=sys.stderr)
            return 4
        row = row.replace(needle, needle + "      onDoubleClick: onDblClick,\n", 1)
        print("CONTROL: re-attached onDoubleClick to the row")
    elif args.plant == "no-enter":
        if 'event.key === "Enter"' not in manager:
            print("PLANT IMPOSSIBLE: there is no Enter branch to delete",
                  file=sys.stderr)
            return 4
        manager = manager.replace('event.key === "Enter"',
                                  'event.key === "PlantedNeverPressed"', 1)
        print("CONTROL: deleted the Enter branch from the manager")
    elif args.plant == "one-list":
        m = re.search(r"filteredUser\.map.*?onClick: \(\) => activateRow\(p\),",
                      manager, re.S)
        if m is None:
            print("PLANT IMPOSSIBLE: the user list does not route through "
                  "activateRow, so there is nothing to revert", file=sys.stderr)
            return 4
        manager = manager[:m.start()] + m.group(0).replace(
            "onClick: () => activateRow(p),",
            "onClick: () => setSelectedId(p.id),") + manager[m.end():]
        print("CONTROL: reverted the user list to select-only")

    manager_code, row_code = code_only(manager), code_only(row)
    failures = []

    # 1. No gesture may be bound to a prop the runtime cannot dispatch.
    for prop in DEAD_PROPS:
        for where, code in ((ROW, row_code), (MANAGER, manager_code)):
            if prop not in code:
                continue
            failures.append(
                "%s is bound in %s: it registers under the bridge event name "
                "%r, which nothing in the runtime dispatches, so the handler "
                "can never run" % (prop, where, prop[2:].lower()))

    # 2. Every advertised gesture has a handler.
    if "DOUBLE-CLICK" in advertised.upper():
        routed = manager_code.count("onClick: () => activateRow(p),")
        if routed != 2:
            failures.append(
                "the footer advertises DOUBLE-CLICK but %d of the 2 preset "
                "row lists route through the click-pair detector; a row that "
                "does not cannot ever commit" % routed)
        if "const activateRow" not in manager_code:
            failures.append("the footer advertises DOUBLE-CLICK but the "
                            "manager declares no activateRow click-pair owner")
    # Return is SHIPPED behaviour, not something the footer gates. The caption
    # deliberately does not name it -- the row's sibling positions come from
    # the capture and do not reflow, so a longer caption wraps or collides --
    # exactly as the arrow keys have always worked here unadvertised. So this
    # is required unconditionally rather than keyed off the footer text.
    if 'event.key === "Enter"' not in manager_code:
        failures.append("the manager's keydown handler has no Enter branch, "
                        "so Return cannot commit the selected preset")

    # 3. Exactly one commit owner, reached by every gesture.
    owners = manager_code.count("const applyPattern = ")
    if owners != 1:
        failures.append("expected exactly 1 commit owner (applyPattern), "
                        "found %d" % owners)
    else:
        # The detail pane's APPLY must call the owner, not re-type its body.
        if "onApply: () => applyPattern(selected)" not in manager_code:
            failures.append("the detail pane's APPLY does not route through "
                            "applyPattern; a second commit body is how the "
                            "button and the gestures drift apart")
        if "applyPattern(pattern)" not in manager_code:
            failures.append("the click-pair path does not reach applyPattern")
        if "applyTargetRef.current" not in manager_code:
            failures.append("the Enter branch does not reach applyPattern "
                            "through applyTargetRef")

    if failures:
        print("FAIL:")
        for f in failures:
            print("  " + f)
        return 1
    print("PASS: %r is backed, Return reaches the same single commit owner, "
          "and no gesture is bound to an undispatchable prop" % advertised)
    return 0


if __name__ == "__main__":
    sys.exit(main())
