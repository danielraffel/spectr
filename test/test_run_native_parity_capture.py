#!/usr/bin/env python3
import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image
except ModuleNotFoundError as error:
    if error.name != "PIL":
        raise
    print("SKIP: Pillow is required for native parity receipt checks")
    raise SystemExit(77)


def main():
    root = Path(__file__).resolve().parents[1]
    tool = root / "tools" / "run_native_parity_capture.py"
    with tempfile.TemporaryDirectory(prefix="spectr-native-receipt-") as temp:
        out = Path(temp)
        fake = out / "native-shot"
        fake.write_text("#!/bin/sh\n"
                        f"{sys.executable} -c 'from PIL import Image; import sys, pathlib; "
                        "p=pathlib.Path(sys.argv[1].split(\"=\",1)[1]); p.mkdir(parents=True,exist_ok=True); "
                        "Image.new(\"RGBA\",(4,3),(20,40,60,255)).save(p/\"parity-deterministic-analyzer.png\"); print(\"OK  parity-deterministic-analyzer\")' \"$1\"\n")
        fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
        binary = out / "binary"
        binary.write_bytes(b"fixture")
        source_sha = "a" * 64
        state = {"schema": "spectr-parity-state-v1", "version": 1,
                 "source": {"sha256": source_sha},
                 "viewport": {"width": 4, "height": 3, "png": {"width": 4, "height": 3}},
                 "native": {"binarySha256": hashlib.sha256(b"fixture").hexdigest()}}
        state["stateSha256"] = hashlib.sha256(json.dumps(state, sort_keys=True,
                                                           separators=(",", ":")).encode()).hexdigest()
        state_path = out / "state.json"
        state_path.write_text(json.dumps(state))
        result = subprocess.run([sys.executable, str(tool), "--native-shot", str(fake),
                                 "--binary", str(binary), "--state", str(state_path),
                                 "--output", str(out / "capture")], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        receipt = json.loads((out / "capture" / "native-receipt.json").read_text())
        assert receipt["schema"] == "spectr-native-shot-receipt-v1"
        assert receipt["stateSha256"] == state["stateSha256"]
        assert receipt["dimensions"] == {"width": 4, "height": 3}
    print("PASS: native parity capture emits state, binary, PNG, and dimension receipt")


if __name__ == "__main__":
    main()
