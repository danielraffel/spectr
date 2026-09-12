#!/usr/bin/env python3
"""Assert every acceptance test-name pattern actually matches a test.

A `ctest -R '(a|b|c)'` alternation is a silent false-pass generator: if `b`
matches nothing, ctest still runs `a` and `c` and exits 0. The tests behind `b`
simply never run, and no output anywhere says so. Three patterns in the M5
acceptance gate were dead this way -- 13 registered tests, including the whole
`#34` host-parameter surface and the standalone artifact test, had never run.

So the pattern list is data, and this gate measures it: every pattern must
match at least one registered test, or the acceptance job fails before it runs
anything. `--emit-regex` then hands the SAME list to ctest, so the thing
asserted and the thing executed cannot drift.

`--plant` appends a pattern that cannot match, proving this gate can fail.

Exit codes: 0 every pattern is live, 1 at least one is dead, 2 the census
itself could not run (no build dir, ctest missing -- an unmeasured run, never
a pass).
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

DEAD_PLANT = "^this-pattern-matches-no-test-by-construction$"


def load_patterns(path: str) -> list[str]:
    out = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line and not line.startswith("#"):
                out.append(line)
    if not out:
        raise SystemExit(f"no patterns in {path} -- refusing to assert nothing")
    return out


def count(build_dir: str, pattern: str) -> int:
    proc = subprocess.run(
        ["ctest", "--test-dir", build_dir, "-N", "-R", pattern],
        capture_output=True, text=True)
    m = re.search(r"^Total Tests:\s*(\d+)", proc.stdout, re.MULTILINE)
    if m is None:
        raise SystemExit(
            "no verdict: ctest -N printed no 'Total Tests:' line for %r.\n%s"
            % (pattern, (proc.stderr or proc.stdout)[-800:]))
    return int(m.group(1))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--patterns", required=True)
    ap.add_argument("--emit-regex", action="store_true",
                    help="print the combined alternation on stdout and exit")
    ap.add_argument("--plant", action="store_true",
                    help="add a pattern that cannot match, so the gate MUST fail")
    args = ap.parse_args()

    patterns = load_patterns(args.patterns)

    if args.emit_regex:
        print("(" + "|".join(patterns) + ")")
        return 0

    if not os.path.isdir(args.build_dir):
        print(f"no verdict: {args.build_dir} is not a directory", file=sys.stderr)
        return 2

    if args.plant:
        patterns = patterns + [DEAD_PLANT]
        print(f"CONTROL: planted a pattern that cannot match ({DEAD_PLANT})")

    total = count(args.build_dir, ".")
    print(f"CONTROL: {total} tests registered in {args.build_dir}")
    if total == 0:
        print("no verdict: the build dir registers no tests at all, so a "
              "zero-match pattern would prove nothing", file=sys.stderr)
        return 2

    dead, matched = [], 0
    for p in patterns:
        n = count(args.build_dir, p)
        matched += n
        flag = "  <<< MATCHES NOTHING" if n == 0 else ""
        print(f"  {n:>4}  {p}{flag}")
        if n == 0:
            dead.append(p)

    print(f"\n{len(patterns)} pattern(s), {matched} name match(es) "
          f"(patterns may overlap)")
    if dead:
        print(f"DEAD: {len(dead)} pattern(s) match no registered test. The "
              f"tests they were written for do not run, and the alternation "
              f"hides it:", file=sys.stderr)
        for p in dead:
            print(f"  {p}", file=sys.stderr)
        return 1
    if args.plant:
        print("BROKEN: the planted dead pattern was not reported -- this gate "
              "cannot fail and proves nothing", file=sys.stderr)
        return 4
    print("OK: every acceptance pattern matches at least one test.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
