#!/usr/bin/env python3
"""Static, non-dispatching checks for Spectr's local-first acceptance lane."""
from pathlib import Path
import json, re, tomllib
from validate_release_sdk import cmake_bool, feature_mismatches

ROOT = Path(__file__).resolve().parents[2]
workflow = (ROOT / ".github/workflows/m5-product-acceptance.yml").read_text()
headless_focused_match = re.search(
    r'PULP_TEST_MODE: "1"[\s\S]*?ctest --test-dir[\s\S]*?-R \'([^\']*)\'', workflow)
headless_focused_regex = headless_focused_match.group(1) if headless_focused_match else ""
editor_enabled_artifact = re.search(
    r"env CI=0 PULP_DISABLE_PLUGIN_EDITOR=0 PULP_HEADLESS=0 PULP_TEST_MODE=0[ \t]*\\\r?\n"
    r"[ \t]+ctest --test-dir[^\n]*[ \t]*\\\r?\n"
    r"[ \t]+-R '\^Pulp host loads'", workflow)
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
    "focused tests": ("ctest --test-dir" in workflow and " -R '" in workflow
                      and "^every native dropdown" in workflow
                      and "^remaining native modal" in workflow
                      and "^native settings" in workflow
                      and "^editor resize" in workflow),
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
