#!/usr/bin/env python3
"""Static, non-dispatching checks for Spectr's local-first acceptance lane."""
from pathlib import Path
import json, re, tomllib
from validate_release_sdk import cmake_bool, feature_mismatches

ROOT = Path(__file__).resolve().parents[2]
workflow = (ROOT / ".github/workflows/m5-product-acceptance.yml").read_text()
# The focused-behavior step no longer carries its patterns inline. A
# `ctest -R '(a|b|c)'` alternation hides a dead alternative -- ctest runs the
# rest and exits 0 -- and three of them WERE dead, so 12 registered tests never
# ran. The list is data now (tools/ci/acceptance-ctest-patterns.txt), a gate
# asserts every entry matches a registered test, and the same file emits the
# regex ctest is handed. Read the patterns from the file, and require the
# workflow to consume them through that gate rather than by hand.
acceptance_patterns_path = ROOT / "tools/ci/acceptance-ctest-patterns.txt"
acceptance_patterns = [
    line.strip()
    for line in acceptance_patterns_path.read_text().splitlines()
    if line.strip() and not line.strip().startswith("#")
] if acceptance_patterns_path.exists() else []
headless_focused_regex = "|".join(acceptance_patterns)
editor_enabled_artifact = re.search(
    r"env CI=0 PULP_DISABLE_PLUGIN_EDITOR=0 PULP_HEADLESS=0 PULP_TEST_MODE=0[ \t]*\\\r?\n"
    r"[ \t]+ctest --test-dir[^\n]*[ \t]*\\\r?\n"
    r"[ \t]+-R '\^Pulp host loads'", workflow)
# Command lines only: the workflow's own comments discuss `--parallel` and
# `-j` in prose, and a check that cannot tell a comment from a command would
# fail on the sentence explaining why the bound exists.
workflow_commands = "\n".join(
    line for line in workflow.splitlines()
    if not line.lstrip().startswith("#"))

config_text = (ROOT / ".shipyard/config.toml").read_text()
config = tomllib.loads(config_text)
pin = json.loads((ROOT / "tools/ci/pulp-sdk-release.json").read_text())
cmake = (ROOT / "CMakeLists.txt").read_text()
package = (ROOT / "package.sh").read_text()
checks = {
    "exact name": "name: Spectr M5 Product Acceptance" in workflow,
    # The gate runs on demand and on a PR into main -- and on nothing else. A
    # push or schedule trigger would put an unattended 120-minute build on a
    # SHARED self-hosted Mac with no PR to bound it, which is what "manual
    # only" was originally protecting against; a path-filtered pull_request is
    # not that, and without it this repo's one automated check never fires.
    "dispatch and PR only": ("workflow_dispatch:" in workflow
                             and not re.search(r"(?m)^  (push|schedule):", workflow)),
    "PR trigger targets main": bool(re.search(
        r"(?m)^  pull_request:\n(?:.*\n)*?    branches: \[main\]", workflow)),
    "PR trigger skips docs-only": bool(re.search(
        r"(?m)^    paths-ignore:\n(?:      - .*\n)+", workflow)),
    "PR pushes collapse to one run": (
        "cancel-in-progress: ${{ github.event_name == 'pull_request' }}" in workflow),
    "exact labels": "runs-on: [self-hosted, macOS, ARM64, spectr-build, spectr-build-vm, spectr-gate-fast]" in workflow,
    "no hosted label": not re.search(r"runs-on:.*(macos-|ubuntu-|windows-)", workflow, re.I),
    "no selector input": "runner_selector" not in workflow and "runner_provider" not in workflow,
    "clean temp": "$RUNNER_TEMP/spectr-product-acceptance-" in workflow,
    "Release": "-DCMAKE_BUILD_TYPE=Release" in workflow,
    "provenance": ("validate_release_sdk.py" in workflow
                   and "SPECTR_EXPECTED_PULP_SDK_SHA" in workflow
                   and '-DSPECTR_EXPECTED_PRODUCT_GIT_SHA="$GITHUB_SHA"' in workflow
                   and '--product-sha "$GITHUB_SHA"' in workflow),
    "focused tests": ("ctest --test-dir" in workflow
                      and '-R "$SPECTR_ACCEPTANCE_CTEST_REGEX"' in workflow
                      and "^every native dropdown" in headless_focused_regex
                      and "^remaining native modal" in headless_focused_regex
                      and "^native settings" in headless_focused_regex
                      and "^editor resize" in headless_focused_regex),
    # The pattern list is only load-bearing if something proves each entry
    # still matches a test, and if the list that is asserted is the list that
    # runs. Both halves are required so neither can quietly drop out.
    "every focused pattern is proved live": (
        "tools/ci/ctest_pattern_gate.py" in workflow
        and "--patterns tools/ci/acceptance-ctest-patterns.txt" in workflow
        and "--emit-regex" in workflow
        and "SPECTR_ACCEPTANCE_CTEST_REGEX=$regex" in workflow),
    # The two detectors that shipped dead did so because nothing ran them. The
    # fixture self-test is cheap (seconds, no build) and must stay BLOCKING and
    # ahead of the build, so a rotted detector fails fast instead of passing
    # vacuously for months.
    "detector suite self-test is a blocking gate": (
        "python3 tools/ci/detector_selftest.py" in workflow
        and workflow.index("tools/ci/detector_selftest.py")
        < workflow.index("cmake -S ")),
    # The ctest pattern gate is the mirror case: it must run AFTER the build,
    # because catch_discover_tests registers Catch2 cases in a POST_BUILD step.
    # Run earlier, the ctest list holds only statically-registered tests and
    # nearly every pattern reports zero matches -- indistinguishable from a
    # genuinely rotted pattern, which is exactly what the gate exists to catch.
    "ctest pattern gate runs after the build": (
        "tools/ci/ctest_pattern_gate.py" in workflow
        and workflow.index("cmake --build ")
        < workflow.index("tools/ci/ctest_pattern_gate.py")),
    # Every build in this workflow takes a bounded SHARE of the runner, never
    # the whole machine. The job runs on a shared self-hosted Mac that also
    # carries Pulp's required `macos` gate, and a bare `--parallel` (unbounded
    # `make -j`) starves it. build_parallelism_guard.py cannot catch this --
    # it deliberately does not scan .github/workflows/**, because `runs-on`
    # resolves dynamically -- so the assertion has to live here.
    #
    # Stated over the whole file rather than over one line, so a NEW build step
    # cannot land unbounded: every `--parallel` must be followed by a literal
    # count, and no job count may expand to the host's core count.
    "every build takes a bounded share": (
        not re.search(r"--parallel(?!\s+\d)", workflow_commands)
        and not re.search(r"-j\s*\$", workflow_commands)
        and not re.search(
            r"\$\(\s*(?:sysctl\s+-n\s+hw\.(?:ncpu|physicalcpu)"
            r"|nproc|getconf\s+_NPROCESSORS_ONLN)\s*\)", workflow_commands)),
    # A SKIP is never a PASS. The native editor capture must not be allowed to
    # report "Not Run" as green for any reason, tracked blocker included.
    "native capture tolerates no skip": (
        "<skipped" in workflow
        and "::warning::Spectr-native-shot SKIPPED" not in workflow),
    "editor-enabled artifact host gate": (
        bool(editor_enabled_artifact)
        and "Pulp host loads" not in headless_focused_regex),
    "stable AUv2 registrar validation": (
        "killall -KILL AudioComponentRegistrar" in workflow
        and "sleep 5" in workflow
        and "for attempt in 1 2" in workflow
        and "auval -v aufx Spec Pulp" in workflow),
    "PKG": ("pkgbuild --root" in workflow
            and 'ditto "$SPECTR_BUILD_DIR/Spectr.app"' in workflow
            and "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02" in workflow
            and all(pattern in workflow for pattern in (
                r"^(\./)?Applications/Spectr\.app/",
                r"^(\./)?Library/Audio/Plug-Ins/Components/Spectr\.component/",
                r"^(\./)?Library/Audio/Plug-Ins/VST3/Spectr\.vst3/",
                r"^(\./)?Library/Audio/Plug-Ins/CLAP/Spectr\.clap/",
            ))),
    "Shipyard workflow": config["cloud"]["default_workflow"] == "m5-product-acceptance",
    "no Namespace": "namespace" not in config_text.lower(),
    "no warm path": "$HOME/Code/pulp-sdk" not in config_text and "-B build" not in config_text,
    "release SHA": bool(re.fullmatch(r"[0-9a-f]{40}", pin["source_git_sha"])),
    "asset digest": bool(re.fullmatch(r"[0-9a-f]{64}", pin["asset_sha256"])),
    "source root authority": ("rev-parse --show-toplevel" in cmake
                              and "_spectr_git_root STREQUAL _spectr_source_root" in cmake),
    "package rechecks exact head": ("SPECTR_SHA_AFTER_BUILD" in package
                                    and "SPECTR_SHA_CACHED_AFTER_BUILD" in package),
    "features: the exact contract is accepted":
        not feature_mismatches({"audio_probes": False, "inspector": True}),
    "features: a new disabled capability is accepted":
        not feature_mismatches({"audio_probes": False, "inspector": True, "tracing": False}),
    "features: a new enabled capability is rejected":
        bool(feature_mismatches({"audio_probes": False, "inspector": True, "tracing": True})),
    "features: an enabled audio probe is rejected":
        bool(feature_mismatches({"audio_probes": True, "inspector": True})),
    "features: a missing required capability is rejected":
        bool(feature_mismatches({"inspector": True})),
    "features: a disabled inspector is rejected":
        bool(feature_mismatches({"audio_probes": False, "inspector": False})),
    "features: a non-object is rejected": bool(feature_mismatches(None)),
    "CMake boolean aliases": (all(cmake_bool(value) for value in ("1", "ON", "YES", "TRUE", "Y"))
                              and not any(cmake_bool(value) for value in
                                          ("", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND", "x-NOTFOUND"))),
}
failed = [name for name, passed in checks.items() if not passed]
if failed: raise SystemExit("product-acceptance contract failures: " + ", ".join(failed))
print(f"product-acceptance static contract: {len(checks)}/{len(checks)} checks passed")
