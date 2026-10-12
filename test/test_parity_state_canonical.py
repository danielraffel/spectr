#!/usr/bin/env python3
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from parity_state_canonical import canonical, state_digest

fixture = json.loads((Path(__file__).parent / "fixtures/parity-state-canonical.json").read_text())
assert state_digest(fixture) == fixture["stateSha256"]
try:
    canonical({"unsafe": 2**53})
except ValueError:
    pass
else:
    raise AssertionError("unsafe integers must be rejected")
print("PASS: parity state canonical fixture")
