#!/usr/bin/env python3
"""Assert a pointer sample during a band drag cannot enqueue a React commit.

Band drawing dispatches a pointer sample per move.  If that path writes React
state, every sample re-renders the app root, and the materialized runtime
answers a re-render by re-applying the whole captured import document -- tens
of thousands of bridge calls and a Yoga pass over the imported tree, for a
readout the canvas already paints from a ref.  Measured on the shipping build,
that put the 95th-percentile pointer dispatch at 326ms against 1.6ms without
it.

The hover path is written correctly in both generator sources
(resources/editor.html and tools/patch_materialized_editor.py).  It regressed
because the CHECKED-IN ARTIFACT drifted away from them and nothing compared
the two.  This detector is that comparison, stated as the invariant rather
than as a diff, so an equivalent rewrite still passes and a re-drift does not.

Exit codes: 0 pass, 1 fail, 2 no verdict (the instrument could not measure).
"""

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARTIFACT = os.path.join(REPO, "native-ui", "materialized",
                        "materialized-document.runtime.json")

# The pointer-sample entry point, and the React state setters that must not be
# reachable from it unconditionally.  setHover is the one that regressed; the
# others are listed so a future rewrite that swaps the setter is still caught.
ENTRY = "updatePointerHover"
STATE_SETTERS = ("setHover", "setStatus", "setCtxMenu", "setSettingsOpen")
# The guard that makes a state write safe: it fires only when no drag is in
# progress.  Any expression naming the pointer mode ref qualifies.
GUARD_TOKENS = ("pointerRef.current", "pointerRef.current.mode")


def load_payload(path):
    if not os.path.exists(path):
        sys.exit("no verdict: %s does not exist" % path)
    with open(path) as fh:
        doc = json.load(fh)
    html = doc.get("html")
    if not isinstance(html, str) or not html:
        sys.exit("no verdict: %s has no 'html' payload" % path)
    return html


def extract_function(payload, name):
    """Return the brace-balanced body of `const <name> = (...) => { ... }`."""
    m = re.search(r"const\s+" + re.escape(name) + r"\s*=\s*\([^)]*\)\s*=>\s*\{",
                  payload)
    if not m:
        return None
    depth = 0
    start = payload.index("{", m.start())
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
    args = ap.parse_args()

    payload = load_payload(args.artifact)

    # Positive control.  A detector that cannot find its subject reports every
    # build as clean.  Prove the payload is the one we think it is before any
    # verdict: the drag path and the setter it must guard both have to be
    # present, or the instrument -- not the build -- is what changed.
    controls = {
        "pointer dispatch entry": payload.count(ENTRY),
        "hover state setter": payload.count("setHover("),
        "drag pointer ref": payload.count("pointerRef.current"),
    }
    for label, count in controls.items():
        print("control   %-24s %d" % (label, count))
    missing = [k for k, v in controls.items() if v == 0]
    if missing:
        sys.exit("no verdict: the payload has no %s -- this detector is "
                 "reading the wrong document or the runtime was restructured"
                 % ", ".join(missing))

    body = extract_function(payload, ENTRY)
    if body is None:
        sys.exit("no verdict: could not extract the body of %s(); it is no "
                 "longer an arrow function and this detector needs updating"
                 % ENTRY)
    print("subject   %-24s %d chars" % (ENTRY + "() body", len(body)))

    failures = []
    for setter in STATE_SETTERS:
        if setter + "(" not in body:
            continue
        # Every occurrence must sit on a line that also names the drag guard.
        for line in body.splitlines():
            if setter + "(" not in line:
                continue
            if any(tok in line for tok in GUARD_TOKENS):
                continue
            failures.append(
                "%s() reaches %s() without testing whether a drag is in "
                "progress, so every pointer sample enqueues a React commit: %s"
                % (ENTRY, setter, line.strip()))

    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print("PASS: a pointer sample during a drag writes no React state.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
