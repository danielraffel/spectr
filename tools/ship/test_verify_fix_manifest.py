#!/usr/bin/env python3
"""Negative controls for verify_fix_manifest.py.

The verifier's whole job is to distinguish "this fix is not in the package" from
"I cannot read this package". Those two produce identical-looking output unless
something forces them apart, so each case below drives the verifier into a state
where a naive implementation would report a confident wrong answer.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
VERIFY = HERE / "verify_fix_manifest.py"
MANIFEST = HERE / "fix-manifest.json"


def run(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(VERIFY), *args],
                          capture_output=True, text=True)


def with_manifest(mutate) -> str:
    doc = json.loads(MANIFEST.read_text())
    mutate(doc)
    handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
    json.dump(doc, handle)
    handle.close()
    return handle.name


def main() -> int:
    failures: list[str] = []

    def check(name: str, ok: bool, detail: str = "") -> None:
        print(f"  {'ok  ' if ok else 'FAIL'}  {name}" + (f"   ({detail})" if detail and not ok else ""))
        if not ok:
            failures.append(name)

    # A binary that is not Spectr at all. Every token is absent, which a naive
    # verifier reports as "no fixes shipped" - a confident finding about a file
    # it never understood. The control token must turn that into UNMEASURED.
    res = run(["/bin/ls"])
    check("a non-Spectr binary is UNMEASURED, not 'all fixes absent'",
          res.returncode == 2 and "UNMEASURED" in res.stdout,
          f"exit={res.returncode}")
    check("and it names the control token that failed",
          "control token absent" in res.stdout)

    # A path that does not exist at all.
    res = run(["/nonexistent/Spectr.app"])
    check("a missing artifact is UNMEASURED", res.returncode == 2,
          f"exit={res.returncode}")

    # A token carrying a literal quote can never match the embedded JSON, so it
    # would silently read ABSENT forever. It must be refused as a manifest bug.
    path = with_manifest(lambda d: d["fixes"][0].update(tokens=['"quoted"']))
    res = run(["/bin/ls", "--manifest", path])
    check("a token with a literal quote is refused as a manifest error",
          res.returncode == 2 and "MANIFEST ERROR" in res.stdout,
          f"exit={res.returncode}")

    # A manifest with no control tokens cannot prove its own instrument works.
    path = with_manifest(lambda d: d.update(control_tokens=[]))
    res = run(["/bin/ls", "--manifest", path])
    check("a manifest with no control tokens is UNMEASURED",
          res.returncode == 2 and "no control tokens" in res.stdout,
          f"exit={res.returncode}")

    # Inversion: prove the pass path is reachable, so a green run means
    # something. A synthetic file containing every control token and every fix
    # token must return 0 - otherwise the verifier can only ever fail.
    doc = json.loads(MANIFEST.read_text())
    blob = " ".join(doc["control_tokens"] + [t for f in doc["fixes"] for t in f["tokens"]])
    synthetic = Path(tempfile.mkdtemp()) / "synthetic-binary"
    synthetic.write_text(blob)
    res = run([str(synthetic)])
    check("an artifact carrying every token passes",
          res.returncode == 0 and "OK:" in res.stdout,
          f"exit={res.returncode}")

    # And removing one token from that same artifact must fail, which is what
    # proves the pass above was not vacuous.
    dropped = doc["fixes"][0]["tokens"][0]
    partial = Path(tempfile.mkdtemp()) / "synthetic-binary"
    partial.write_text(blob.replace(dropped, "x" * len(dropped)))
    res = run([str(partial)])
    check("removing one token from that same artifact fails",
          res.returncode == 1 and "ABSENT" in res.stdout,
          f"exit={res.returncode}")

    print()
    if failures:
        print(f"FAIL: {len(failures)} control(s) did not hold: {failures}")
        return 1
    print("OK: every control held.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
