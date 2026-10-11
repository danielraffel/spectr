#!/usr/bin/env python3
"""Keep browser and native deterministic-fixture rejection behavior aligned."""
import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from parity_state_canonical import state_digest


def main():
    root = Path(__file__).resolve().parents[1]
    binary = root / "build-native" / "Spectr-native-shot"
    chrome = Path("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
    source = root / "resources/editor.html"
    fixture = json.loads((root / "test/fixtures/spectr-parity-analyzer-v1.json").read_text())
    base_state = json.loads((root / "artifacts/parity-current/state-v12.json").read_text())
    if not binary.exists() or not chrome.exists() or not source.exists():
        print("SKIP: conformance prerequisites are unavailable")
        return 77
    mutations = [{"sequences": "not-an-array"}, {"minHz": 0}]
    with tempfile.TemporaryDirectory(prefix="spectr-fixture-conformance-") as temp:
        out = Path(temp)
        for index, mutation in enumerate(mutations):
            bad = copy.deepcopy(fixture)
            bad.update(mutation)
            bad_path = out / f"bad-{index}.json"
            bad_path.write_text(json.dumps(bad))
            state = copy.deepcopy(base_state)
            state["analyzer"]["fixtureSha256"] = hashlib.sha256(bad_path.read_bytes()).hexdigest()
            state["stateSha256"] = state_digest(state)
            state_path = out / f"state-{index}.json"
            state_path.write_text(json.dumps(state))
            native = subprocess.run(
                [str(binary), f"--out={out / f'native-{index}'}", "--backend=skia"],
                env={**os.environ, "SPECTR_DETERMINISTIC_ANALYZER": "1", "SPECTR_ANALYZER_FIXTURE": str(bad_path)},
                capture_output=True, text=True)
            browser = subprocess.run(
                ["node", str(root / "tools/compare_source_html_chromium.mjs"), "--source", str(source),
                 "--output", str(out / f"browser-{index}"), "--chrome", str(chrome), "--state", str(state_path),
                 "--analyzer-fixture", str(bad_path)], capture_output=True, text=True)
            assert native.returncode != 0, native.stdout + native.stderr
            assert "unsupported deterministic analyzer fixture" in (native.stdout + native.stderr)
            assert browser.returncode != 0, browser.stdout + browser.stderr
            assert "invalid deterministic analyzer fixture" in (browser.stdout + browser.stderr)
    print("PASS: browser and native reject the same malformed analyzer fixtures")
    return 0


raise SystemExit(main())
