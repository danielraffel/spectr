#!/usr/bin/env python3
"""Count a token in the shipping editor document, without lying about zero.

USE THIS INSTEAD OF `grep` ON `materialized-document.runtime.json`.

That file is the most-measured artifact in this repository, and a raw `grep`
against it manufactures ABSENCE -- which is exactly the reading people act on.
Three distinct false zeros landed in one day, each of which looked like a
clean finding:

  1. NO `-a`. The document is one 800KB line, so grep classifies it as binary
     and prints nothing, exiting 0. The killer detail: the CONTROL token reads
     zero the same way, so the instrument looks healthy while reporting that
     everything is absent.

  2. SHELL-EATEN QUOTES. A token containing `"` loses them to an outer
     double-quoted string, so the pattern that actually runs is not the one
     that was written -- and it legitimately matches nothing.

  3. JSON ESCAPING, the subtlest. The page is stored as a JSON *string*, so
     the file holds `\\"` everywhere the source has `"`. A raw grep for
     `Hrow, { k: "M" }` reads 0 against a document that contains that row.
     Patch scripts handle this with `json.dumps(token)[1:-1]`; a person at a
     terminal does not.

All three have the same symptom and opposite fixes, and in two of them the
control failed identically and concealed it. So this decodes the payload and
searches the DECODED text -- the same string the browser and the native host
execute -- and refuses to report at all when a declared control reads zero.

    python3 tools/query_materialized.py 'clearSnap: (slot) => {'
    python3 tools/query_materialized.py --control 'commitMany' 'foo' 'bar'
    python3 tools/query_materialized.py --absent 'unmutePulseRef.current[k] = 1;'

A CONTROL IS ALWAYS APPLIED. With none declared, `React.createElement` is
used, because a document that does not contain it is not a document this tool
can say anything about. Declare your own with `--control` when the default is
too weak for the claim -- a control only proves the tool ran; if your token
lives in one region, control on something from that region.

`--absent` is the flag for an ABSENCE claim, and it is the whole point: it
turns "I grepped and saw nothing" into a verdict that cannot be reached while
the instrument is broken.

Exit codes: 0 counted (and every --absent token really is absent), 1 an
--absent token is present, 2 NO VERDICT -- a control read zero, the file is
unreadable, or it carries no html payload. 2 is never a pass.
"""

import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DOC = os.path.join(REPO, "native-ui", "materialized",
                           "materialized-document.runtime.json")
# Present ~450 times in every revision of this document. Weak on purpose: it
# proves the payload decoded and is searchable, nothing more.
DEFAULT_CONTROL = "React.createElement"


def load_payload(path):
    try:
        raw = open(path, encoding="utf-8").read()
    except OSError as error:
        print("no verdict: cannot read %s (%s)" % (path, error), file=sys.stderr)
        return None
    try:
        document = json.loads(raw)
    except ValueError as error:
        print("no verdict: %s is not JSON (%s)" % (path, error), file=sys.stderr)
        return None
    html = document.get("html")
    if not isinstance(html, str) or not html:
        print("no verdict: %s carries no html payload, so there is nothing to "
              "search" % path, file=sys.stderr)
        return None
    return html


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tokens", nargs="*", help="tokens to count")
    ap.add_argument("--document", default=DEFAULT_DOC)
    ap.add_argument("--control", action="append", default=[],
                    help="token that MUST be present; a zero here is a broken "
                         "instrument, not an absence")
    ap.add_argument("--absent", action="append", default=[],
                    help="assert this token is NOT present (exit 1 if it is)")
    ap.add_argument("--plant", choices=("dead-control",),
                    help="prove the instrument-broken path still fires")
    args = ap.parse_args()

    if not args.tokens and not args.absent:
        print("no verdict: nothing to look for", file=sys.stderr)
        return 2

    html = load_payload(args.document)
    if html is None:
        return 2

    controls = list(args.control) or [DEFAULT_CONTROL]
    if args.plant == "dead-control":
        # A token no revision of this document can hold. If this does NOT
        # produce a no-verdict, the guard below is decorative.
        controls = controls + ["zz-this-token-cannot-exist-zz"]

    dead = [c for c in controls if html.count(c) == 0]
    if dead:
        print("NO VERDICT: control token(s) %s read zero in %s.\n"
              "  The instrument is broken, NOT the token absent -- every count "
              "below would be a false zero.\n"
              "  (If you passed --control yourself, check the token; otherwise "
              "this document is not the one you think it is.)"
              % (", ".join(repr(c) for c in dead), args.document),
              file=sys.stderr)
        return 2
    for control in controls:
        print("control  %6d  %s" % (html.count(control), control))

    for token in args.tokens:
        print("         %6d  %s" % (html.count(token), token))

    rc = 0
    for token in args.absent:
        count = html.count(token)
        if count:
            print("PRESENT  %6d  %s  -- asserted absent" % (count, token),
                  file=sys.stderr)
            rc = 1
        else:
            print("absent        0  %s" % token)
    return rc


if __name__ == "__main__":
    sys.exit(main())
