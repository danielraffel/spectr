#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path


def normalize(value):
    if isinstance(value, dict):
        return {key: normalize(value[key]) for key in sorted(value)}
    if isinstance(value, list):
        return [normalize(item) for item in value]
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value


def main():
    fixture = json.loads((Path(__file__).parent / "fixtures/parity-state-canonical.json").read_text())
    expected = fixture.pop("stateSha256")
    encoded = json.dumps(normalize(fixture), sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    assert hashlib.sha256(encoded).hexdigest() == expected
    print("PASS: parity state canonical fixture")


if __name__ == "__main__":
    main()
