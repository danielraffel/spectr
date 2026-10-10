#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path

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
        source_sha = "a" * 64
        state = {"schema": "spectr-parity-state-v1", "version": 1,
                 "source": {"sha256": source_sha}, "viewport": {"width": 4, "height": 3}}
        import hashlib
        state["native"] = {"binarySha256": hashlib.sha256(b"fixture").hexdigest()}
        state_hash = hashlib.sha256(json.dumps(state, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        state["stateSha256"] = state_hash
        native_receipt = {"schema": "spectr-native-shot-receipt-v1",
                          "stateSha256": state_hash, "sourceSha256": source_sha,
                          "binarySha256": state["native"]["binarySha256"],
                          "pngSha256": hashlib.sha256(native_png.read_bytes()).hexdigest(),
                          "dimensions": {"width": 4, "height": 3}}
        receipt = {
            "schema": "spectr-html-cdp-comparison-v1",
            "sourceSha256": source_sha,
            "stateSha256": state_hash,
            "positive": {"before": {"path": str(browser_png),
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
                              "--native-png", str(native_png), "--state", str(state_path),
                              "--native-receipt", str(native_receipt_path),
                              "--output", str(report)], capture_output=True, text=True)
        assert run.returncode == 0, run.stderr
        assert json.loads(report.read_text())["comparison"]["exact"]

        negative = subprocess.run([sys.executable, str(tool), "--browser-receipt", str(receipt_path),
                                   "--native-png", str(native_png), "--state", str(state_path),
                                   "--native-receipt", str(native_receipt_path),
                                   "--output", str(out / "negative.json"), "--plant-negative"],
                                  capture_output=True, text=True)
        assert negative.returncode == 0, negative.stderr
        assert json.loads((out / "negative.json").read_text())["comparison"]["differingPixels"] > 0
    print("PASS: parity receipt exact match and planted negative")


if __name__ == "__main__":
    main()
