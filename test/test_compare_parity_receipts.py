#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from parity_state_canonical import state_digest

try:
    from PIL import Image
except ModuleNotFoundError as error:
    if error.name != "PIL":
        raise
    print("SKIP: Pillow is required for parity receipt image checks")
    raise SystemExit(77)


def main():
    root = Path(__file__).resolve().parents[1]
    tool = root / "tools" / "compare_parity_receipts.py"
    with tempfile.TemporaryDirectory(prefix="spectr-parity-") as temp:
        out = Path(temp)
        browser_png = out / "browser.png"
        native_png = out / "native.png"
        Image.new("RGBA", (4, 3), (20, 40, 60, 255)).save(browser_png)
        browser_png.replace(out / "before.png")
        browser_png = out / "before.png"
        native_png.write_bytes(browser_png.read_bytes())
        source_artifact = out / "source.html"
        source_artifact.write_text("source fixture")
        import hashlib
        source_sha = hashlib.sha256(source_artifact.read_bytes()).hexdigest()
        native_binary = out / "native-binary"
        native_binary.write_bytes(b"fixture")
        state = {"schema": "spectr-parity-state-v1", "version": 1,
                 "source": {"sha256": source_sha}, "viewport": {"width": 4, "height": 3, "deviceScaleFactor": 1}}
        state["native"] = {"binarySha256": hashlib.sha256(b"fixture").hexdigest()}
        state_hash = state_digest(state)
        state["stateSha256"] = state_hash
        native_receipt = {"schema": "spectr-native-shot-receipt-v1",
                          "stateSha256": state_hash, "sourceSha256": source_sha,
                          "binarySha256": state["native"]["binarySha256"],
                          "pngSha256": hashlib.sha256(native_png.read_bytes()).hexdigest(),
                          "dimensions": {"width": 4, "height": 3},
                          "deterministicAnalyzer": True,
                          "readiness": {"contract": "spectr-parity-v1", "analyzerSequence": 2}}
        receipt = {
            "schema": "spectr-html-cdp-comparison-v1",
            "sourceSha256": source_sha,
            "stateSha256": state_hash,
            "fixedViewport": {"width": 4, "height": 3, "deviceScaleFactor": 1},
            "checks": {"strict": True},
            "positive": {"ready": {"ready": "complete", "sourceEditorReady": True,
                                     "parityReady": {"contract": "spectr-parity-v1", "analyzerSequence": 2}},
                         "info": {"rootChildren": 1, "canvas": [{}]},
                         "consoleErrors": [], "networkFailures": [],
                         "before": {"path": str(browser_png),
                                      "sha256": __import__("hashlib").sha256(browser_png.read_bytes()).hexdigest()}},
        }
        receipt_path = out / "browser-receipt.json"
        state_path = out / "state.json"
        native_receipt_path = out / "native-receipt.json"
        receipt_path.write_text(json.dumps(receipt))
        state_path.write_text(json.dumps(state))
        native_receipt_path.write_text(json.dumps(native_receipt))
        report = out / "report.json"
        run = subprocess.run([sys.executable, str(tool), "--browser-receipt", str(receipt_path),
                              "--browser-png", str(browser_png),
                              "--native-png", str(native_png), "--state", str(state_path),
                              "--native-receipt", str(native_receipt_path),
                              "--source-artifact", str(source_artifact),
                              "--native-binary", str(native_binary),
                              "--output", str(report)], capture_output=True, text=True)
        assert run.returncode == 0, run.stderr
        assert json.loads(report.read_text())["comparison"]["exact"]

        negative = subprocess.run([sys.executable, str(tool), "--browser-receipt", str(receipt_path),
                                   "--browser-png", str(browser_png),
                                   "--native-png", str(native_png), "--state", str(state_path),
                                   "--native-receipt", str(native_receipt_path),
                                   "--source-artifact", str(source_artifact),
                                   "--native-binary", str(native_binary),
                                   "--output", str(out / "negative.json"), "--plant-negative"],
                                  capture_output=True, text=True)
        assert negative.returncode == 0, negative.stderr
        assert json.loads((out / "negative.json").read_text())["comparison"]["differingPixels"] > 0
        mismatch = out / "mismatch.png"
        Image.new("RGBA", (4, 3), (21, 40, 60, 255)).save(mismatch)
        mismatch_receipt = dict(native_receipt)
        mismatch_receipt["pngSha256"] = hashlib.sha256(mismatch.read_bytes()).hexdigest()
        mismatch_receipt_path = out / "mismatch-receipt.json"
        mismatch_receipt_path.write_text(json.dumps(mismatch_receipt))
        rejected = subprocess.run([sys.executable, str(tool), "--browser-receipt", str(receipt_path),
                                   "--browser-png", str(browser_png),
                                   "--native-png", str(mismatch), "--state", str(state_path),
                                   "--native-receipt", str(mismatch_receipt_path),
                                   "--source-artifact", str(source_artifact),
                                   "--native-binary", str(native_binary),
                                   "--output", str(out / "rejected.json")], capture_output=True, text=True)
        assert rejected.returncode != 0, rejected.stdout
        unsupported = dict(state)
        unsupported["version"] = 2
        unsupported_path = out / "unsupported-state.json"
        unsupported_path.write_text(json.dumps(unsupported))
        version_rejected = subprocess.run([sys.executable, str(tool), "--browser-receipt", str(receipt_path),
                                           "--browser-png", str(browser_png),
                                           "--native-png", str(native_png), "--state", str(unsupported_path),
                                           "--native-receipt", str(native_receipt_path),
                                           "--source-artifact", str(source_artifact),
                                           "--native-binary", str(native_binary),
                                           "--output", str(out / "unsupported.json")], capture_output=True, text=True)
        assert version_rejected.returncode != 0, version_rejected.stdout
    print("PASS: parity receipt exact match and planted negative")


if __name__ == "__main__":
    main()
