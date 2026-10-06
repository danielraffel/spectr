#!/usr/bin/env python3
"""Run a third-party plugin validator against a built Spectr bundle.

    run_plugin_validator.py clap  <Spectr.clap>  [--log FILE]
    run_plugin_validator.py vst3  <Spectr.vst3>  [--log FILE]
    run_plugin_validator.py --self-test

`clap` runs clap-validator (`validate --in-process`, the mode the 1.0.7 RC1
verification failed in); `vst3` runs pluginval at strictness 5 with GUI tests
skipped, so neither opens a window or an audio device.

Exit codes: 0 every test passed; 1 the validator reported a failure, exited
non-zero, or printed no verdict this script can read; 77 the validator is not
installed here (CTest reports SKIP via SKIP_RETURN_CODE, never PASS); 2 usage.

A validator that prints nothing parseable is a failure, not a pass: a summary
line this script cannot find is a broken instrument, and reading it as "no
failures" is exactly the vacuous zero this gate exists to refuse.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SKIP = 77

CLAP_SUMMARY = re.compile(
    r"^(\d+) tests run, (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) warnings",
    re.MULTILINE)


def find_clap_validator() -> str | None:
    found = shutil.which("clap-validator")
    if found:
        return found
    cargo = Path.home() / ".cargo" / "bin" / "clap-validator"
    return str(cargo) if cargo.is_file() else None


def find_pluginval() -> str | None:
    found = shutil.which("pluginval")
    if found:
        return found
    for candidate in (Path("/Applications/pluginval.app/Contents/MacOS/pluginval"),
                      Path.home() / "Applications/pluginval.app/Contents/MacOS/pluginval"):
        if candidate.is_file():
            return str(candidate)
    return None


def clap_verdict(output: str, returncode: int) -> tuple[bool, str]:
    summaries = CLAP_SUMMARY.findall(output)
    if not summaries:
        return False, "clap-validator printed no summary line (no verdict)"
    run = failed = 0
    for total, _passed, fails, _skipped, _warn in summaries:
        run += int(total)
        failed += int(fails)
    if run == 0:
        return False, "clap-validator ran zero tests (no verdict)"
    if failed:
        return False, f"clap-validator: {failed} failed of {run}"
    if returncode != 0:
        return False, f"clap-validator exited {returncode} with 0 failures reported"
    return True, f"clap-validator: {run} tests run, 0 failed"


def pluginval_verdict(output: str, returncode: int) -> tuple[bool, str]:
    started = output.count("Starting tests in:")
    if started == 0:
        return False, "pluginval ran no test groups (no verdict)"
    if returncode != 0 or "FAILED" in output or "SUCCESS" not in output:
        return False, f"pluginval failed (exit {returncode}) across {started} test groups"
    return True, f"pluginval: SUCCESS across {started} test groups"


def run(kind: str, bundle: Path, log: Path | None) -> int:
    if not bundle.exists():
        print(f"no such bundle: {bundle}", file=sys.stderr)
        return 2
    if kind == "clap":
        tool = find_clap_validator()
        if not tool:
            print("SKIP: clap-validator is not installed (cargo install --git "
                  "https://github.com/free-audio/clap-validator); nothing was validated")
            return SKIP
        cmd = [tool, "validate", "--in-process", str(bundle)]
        verdict = clap_verdict
    else:
        tool = find_pluginval()
        if not tool:
            print("SKIP: pluginval is not installed (PATH or /Applications/pluginval.app); "
                  "nothing was validated")
            return SKIP
        cmd = [tool, "--strictness-level", "5", "--skip-gui-tests",
               "--validate", str(bundle)]
        verdict = pluginval_verdict
    env = dict(os.environ)
    env.setdefault("PULP_AUDIO_DEVICE", "null")
    print("+ " + " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors="replace", env=env)
    output = proc.stdout
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(output)
    ok, message = verdict(output, proc.returncode)
    # The useful part of a failure is the failing test's own text.
    if not ok:
        for block in re.findall(r"^\s+- [^\n]+\n(?:\s{5,}[^\n]*\n)*?\s+FAILED:[^\n]*(?:\n\s{6,}[^\n]*)*",
                                output, re.MULTILINE):
            print(block)
        if kind == "vst3":
            print("\n".join(line for line in output.splitlines()
                            if "FAILED" in line or "!!!" in line))
    print(("PASS: " if ok else "FAIL: ") + message + (f" (log: {log})" if log else ""))
    return 0 if ok else 1


def self_test() -> int:
    """Each parser must reject a planted failure and an empty transcript."""
    bad = 0

    def expect(name: str, got: bool, want: bool) -> None:
        nonlocal bad
        if got != want:
            bad += 1
            print(f"self-test {name}: expected {want}, got {got}")

    passing = "Plugin tests:\n\n21 tests run, 16 passed, 0 failed, 5 skipped, 0 warnings\n"
    failing = "   - state-reproducibility-flush: ...\n     FAILED: x\n\n" \
              "21 tests run, 15 passed, 1 failed, 5 skipped, 0 warnings\n"
    expect("clap pass", clap_verdict(passing, 0)[0], True)
    expect("clap planted failure", clap_verdict(failing, 0)[0], False)
    expect("clap empty transcript", clap_verdict("", 0)[0], False)
    expect("clap zero tests", clap_verdict(
        "0 tests run, 0 passed, 0 failed, 0 skipped, 0 warnings\n", 0)[0], False)
    expect("clap nonzero exit", clap_verdict(passing, 1)[0], False)

    pv_ok = "Starting tests in: pluginval / Plugin info...\nSUCCESS\n"
    pv_bad = "Starting tests in: pluginval / Plugin state...\n!!! Test 1 failed\nFAILED\n"
    expect("pluginval pass", pluginval_verdict(pv_ok, 0)[0], True)
    expect("pluginval planted failure", pluginval_verdict(pv_bad, 1)[0], False)
    expect("pluginval failure exit 0", pluginval_verdict(pv_bad, 0)[0], False)
    expect("pluginval empty transcript", pluginval_verdict("", 0)[0], False)
    print("self-test: " + ("ok" if bad == 0 else f"{bad} control(s) misjudged"))
    return 0 if bad == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kind", nargs="?", choices=["clap", "vst3"])
    parser.add_argument("bundle", nargs="?", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.kind or not args.bundle:
        parser.error("kind and bundle are required")
    return run(args.kind, args.bundle, args.log)


if __name__ == "__main__":
    sys.exit(main())
