#!/usr/bin/env python3
"""Static, non-dispatching checks for Spectr's local-first acceptance lane."""
from pathlib import Path
import json, re, tomllib

ROOT = Path(__file__).resolve().parents[2]
workflow = (ROOT / ".github/workflows/m5-product-acceptance.yml").read_text()
config_text = (ROOT / ".shipyard/config.toml").read_text()
config = tomllib.loads(config_text)
pin = json.loads((ROOT / "tools/ci/pulp-sdk-release.json").read_text())
checks = {
    "exact name": "name: Spectr M5 Product Acceptance" in workflow,
    "manual only": "workflow_dispatch:" in workflow and not re.search(r"(?m)^  (push|pull_request|schedule):", workflow),
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
    "PKG": ("pkgbuild --root" in workflow
            and 'ditto "$SPECTR_BUILD_DIR/Spectr.app"' in workflow
            and "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02" in workflow),
    "Shipyard workflow": config["cloud"]["default_workflow"] == "m5-product-acceptance",
    "no Namespace": "namespace" not in config_text.lower(),
    "no warm path": "$HOME/Code/pulp-sdk" not in config_text and "-B build" not in config_text,
    "release SHA": bool(re.fullmatch(r"[0-9a-f]{40}", pin["source_git_sha"])),
    "asset digest": bool(re.fullmatch(r"[0-9a-f]{64}", pin["asset_sha256"])),
}
failed = [name for name, passed in checks.items() if not passed]
if failed: raise SystemExit("product-acceptance contract failures: " + ", ".join(failed))
print(f"product-acceptance static contract: {len(checks)}/{len(checks)} checks passed")
