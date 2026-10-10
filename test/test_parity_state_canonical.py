#!/usr/bin/env python3
import hashlib
import json
import re
from decimal import Decimal
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
    def encode(value):
        if isinstance(value, dict):
            return "{" + ",".join(json.dumps(k, ensure_ascii=False) + ":" + encode(value[k]) for k in sorted(value)) + "}"
        if isinstance(value, list):
            return "[" + ",".join(encode(item) for item in value) + "]"
        if isinstance(value, bool) or value is None or isinstance(value, str):
            return json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        if isinstance(value, int):
            return str(value)
        text = format(Decimal(repr(value)), "f").rstrip("0").rstrip(".")
        return "0" if text in ("", "-0") else text
    encoded = encode(normalize(fixture)).encode()
    assert hashlib.sha256(encoded).hexdigest() == expected
    print("PASS: parity state canonical fixture")


if __name__ == "__main__":
    main()
