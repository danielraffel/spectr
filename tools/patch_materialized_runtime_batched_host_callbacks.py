#!/usr/bin/env python3
"""Transplant @pulp/react runtime revision 3 (batched host callbacks).

WHY THIS EXISTS

    `native-ui/materialized/runtime.js` is a CHECKED-IN @pulp/react bundle.
    Its reconciler renders through a LegacyRoot, so every setState made
    outside a React event handler commits synchronously, and every commit in
    a materialized import re-applies captured import metadata. Animation-frame
    callbacks and native-message listeners are outside a React handler. Measured
    on the AU editor-open probe: the post-mount state hydrate, delivered from
    one requestAnimationFrame, made 13 commits and cost 449 ms of the open;
    batched, one commit and 70 ms.

    Pulp fixed this generically (runtime-fingerprint revision 3,
    `batched-host-callbacks`): @pulp/react installs
    `globalThis.__pulpBatchUpdates__` (React's batchedUpdates) and
    WidgetBridge's frame/timer pump runs each callback through it. This bundle
    predates that, so it never installs the hook.

WHAT IT CHANGES

    One line after the reconciler is created: install the hook, exactly as
    @pulp/react's index.ts now does. Pulp SDK 0.895.1's WidgetBridge frame and
    timer pump calls it, so with this line rAF and timer callbacks commit once;
    Spectr's native-message `emit` (spectr-native-services.js) calls it
    directly as well.

    Kept on the 0.895.1 bump rather than regenerating runtime.js: the vendored
    bundle also carries Spectr-only runtime transplants (hit slop, insert
    index, overlay parent, pattern menu footer, ...) that a regenerated
    @pulp/react bundle would not, so regeneration is a full re-import.

DELETE WHEN

    runtime.js is regenerated from a Pulp whose runtime-fingerprint.json is at
    revision 3 or later (the regenerated bundle carries the hook itself).

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 the patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

MARKER = "globalThis.__pulpBatchUpdates__ ="
ANCHOR = "  var reconciler = (0, import_react_reconciler.default)(PulpHostConfig);\n"
INSERT = (
    "  // Transplanted @pulp/react fix batched-host-callbacks (fingerprint rev. 3):\n"
    "  // host-driven callbacks commit their state updates once.\n"
    "  globalThis.__pulpBatchUpdates__ = (fn, arg) => reconciler.batchedUpdates(fn, arg);\n"
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    if MARKER in raw:
        print("already applied  batched host callbacks")
        return 0
    count = raw.count(ANCHOR)
    if count != 1:
        sys.exit("FAIL: reconciler creation occurs %d times, expected 1" % count)
    raw = raw.replace(ANCHOR, ANCHOR + INSERT, 1)
    with open(PATH, "w", encoding="utf-8") as stream:
        stream.write(raw)
    print("applied  batched host callbacks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
