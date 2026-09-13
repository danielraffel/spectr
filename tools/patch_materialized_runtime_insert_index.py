#!/usr/bin/env python3
"""Transplant Pulp #8272's renderer half into the vendored materialized runtime.

WHY THIS EXISTS

    `native-ui/materialized/runtime.js` is a CHECKED-IN copy of Pulp's React
    host config, and it is the renderer that actually ships in the editor.  The
    copy predates Pulp #8272 (`8042c3815`, "Give the widget bridge a way to
    position a child"), so only HALF of that fix reaches the user:

        native half   SDK v0.850.0 registers the `insertChild` bridge function
                      (core/view/src/widget_bridge/metadata_api.cpp) over
                      View::move_child_to_index.  PRESENT -- verified in the tag
                      and in the shipping binary.
        renderer half host-config threads attach()'s computed insert index into
                      materialize() -> materializeUnder() and emits
                      insertChild after the appending createWidget.  ABSENT from
                      this artifact.

    Every native createX APPENDS.  attach() computes the right insert index,
    keeps `childIds` and the DOM shim in that order -- and then drops the index
    on the floor at materialize().  So a subtree that mounts LATE into a parent
    that kept its other children lands last instead of in place.

    That is the user's report.  Re-opening a dropdown remounts its rows, so they
    come back after the siblings that stayed, and each row's caption separates
    from the description it labels -- captions and bodies interleave and overlap
    ("SCULPT"/"Free-", "LEVEL"/"Flat", "PEAK"/its description).  It is
    state-dependent, which is why the menu is correct until something remounts
    it: a preset apply or a snapshot recall re-renders the overlay subtree.

    The artifact DOES already contain an `insertChild` call, which makes it look
    transplanted.  It is not.  That call sits in insertBefore()'s same-parent
    REORDER branch, which predates #8272 (blame: 048d01a5e / 8e00c2be3) and was
    dead code until the SDK defined the function.  A reorder of children that
    are already on the bridge is a different path from a fresh MOUNT at an
    index, and only the reorder path was ever wired.  Presence of the identifier
    is not evidence the mount path is covered.

WHY A TRANSPLANT AND NOT A RE-COPY

    Re-copying runtime.js from the SDK would be a 1.6MB diff carrying every
    unrelated change between the vendored revision and today, on the file that
    is the shipping editor's entire behaviour.  These five edits are the whole
    of #8272's host-config change, written in this artifact's own dialect
    (`g4`/`call2` rather than `g`/`call`), and they are reviewable.

    `call2` no-ops when the named bridge function is absent and the emit is
    additionally guarded on `typeof g4.insertChild === "function"`, so the
    bundle still runs unchanged against an older SDK: ordering degrades to
    append exactly as it does today rather than throwing.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

# Each entry: (label, old, new).  Every `old` must occur EXACTLY once.
EDITS = [
    (
        "attach: thread the index through the recreate fallback",
        """        child.onBridge = false;
        if (parent.onBridge) materialize(parent, child);""",
        """        child.onBridge = false;
        if (parent.onBridge) materialize(parent, child, insertIdx);""",
    ),
    (
        "attach: thread the index through the live-parent path",
        """    if (parent.onBridge) {
      materialize(parent, child);
    } else {
      parent.pendingChildren.push({ child, index: insertIdx });""",
        """    if (parent.onBridge) {
      materialize(parent, child, insertIdx);
    } else {
      parent.pendingChildren.push({ child, index: insertIdx });""",
    ),
    (
        "materialize: accept an index and move a non-last child into place",
        """  function materialize(parent, child) {
    child.inheritedSvgViewBox = svgViewportFor(parent);
    materializeUnder(parent.id, child);
  }""",
        """  function materialize(parent, child, index) {
    child.inheritedSvgViewBox = svgViewportFor(parent);
    // Every createX call appends, so a child landing anywhere but last has to
    // be moved into place right after it is created -- otherwise a subtree that
    // mounts late (a re-opened dropdown remounting its rows) comes back in
    // mount order rather than authored order, and captions detach from their
    // bodies.
    //
    // Its authored index doubles as its native index: every earlier sibling in
    // childIds has already reached the bridge, because attach() materializes
    // eagerly under a live parent and materializeUnder drains a deferred
    // parent's queue in authored order.
    const appendsLast = index === void 0 || index >= parent.childIds.length - 1;
    materializeUnder(parent.id, child, appendsLast ? void 0 : index);
  }""",
    ),
    (
        "materializeUnder: emit insertChild after the appending createWidget",
        """  function materializeUnder(parentId, child) {
    if (child.onBridge) return;
    createWidget(child.type, child.id, parentId, child.props);""",
        """  function materializeUnder(parentId, child, index) {
    if (child.onBridge) return;
    createWidget(child.type, child.id, parentId, child.props);
    // An older native host has no indexed insert and can only append. Ordering
    // then degrades exactly as it did before this call existed, rather than
    // throwing, so one renderer bundle still runs on both.
    if (index !== void 0 && index >= 0 && typeof g4.insertChild === "function") {
      call2("insertChild", parentId, child.id, index);
    }""",
    ),
    (
        "materializeUnder: drain a deferred parent's queue in authored order",
        """      const drained = child.pendingChildren;
      child.pendingChildren = [];
      for (const { child: gc } of drained) {""",
        """      const drained = child.pendingChildren;
      child.pendingChildren = [];
      // Replay in authored order, not queue order. A deferred subtree can be
      // reordered (or inserted into) before its parent reaches the bridge, so
      // the queue records the order the attaches arrived in, not the order the
      // author wrote. Draining in childIds order makes each create an append
      // again, which is the one thing the native factory can always do.
      const authoredOrder = (entry) => {
        const at = child.childIds.indexOf(entry.child.id);
        return at < 0 ? Number.MAX_SAFE_INTEGER : at;
      };
      drained.sort((a, b) => authoredOrder(a) - authoredOrder(b));
      for (const { child: gc } of drained) {""",
    ),
]


def main() -> int:
    try:
        with open(PATH, "r", encoding="utf-8") as handle:
            source = handle.read()
    except OSError as error:
        print(f"error: cannot read {PATH}: {error}", file=sys.stderr)
        return 1

    applied = 0
    already = 0
    for label, old, new in EDITS:
        if new in source:
            already += 1
            continue
        count = source.count(old)
        if count != 1:
            print(
                f"error: patch point {count}x (want 1): {label}",
                file=sys.stderr,
            )
            return 1
        source = source.replace(old, new, 1)
        applied += 1

    if applied == 0:
        print(f"already applied: all {already} edits present")
        return 0

    if already:
        print(
            f"error: partially applied ({already} present, {applied} missing) -- "
            "refusing a half-written artifact",
            file=sys.stderr,
        )
        return 1

    with open(PATH, "w", encoding="utf-8") as handle:
        handle.write(source)
    print(f"applied {applied} edits to {os.path.relpath(PATH, REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
