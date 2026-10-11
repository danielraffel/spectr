"""Versioned canonical encoding for parity-state-v1."""
import hashlib
import json
import math
from decimal import Decimal


def canonical_json(value):
    if isinstance(value, dict):
        ordered = sorted(value, key=lambda item: item.encode("utf-16-be", "surrogatepass"))
        return "{" + ",".join(json.dumps(key, ensure_ascii=False) + ":" + canonical_json(value[key]) for key in ordered) + "}"
    if isinstance(value, list):
        return "[" + ",".join(canonical_json(item) for item in value) + "]"
    if isinstance(value, bool) or value is None or isinstance(value, str):
        return json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    if isinstance(value, int):
        if abs(value) > 2**53 - 1:
            raise ValueError("unsafe integer outside JavaScript safe range")
        return str(value)
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError("non-finite number is not canonical JSON")
        if value.is_integer() and abs(value) > 2**53 - 1:
            raise ValueError("unsafe integer outside JavaScript safe range")
        text = format(Decimal(repr(value)), "f").rstrip("0").rstrip(".")
        return "0" if text in ("", "-0") else text
    raise TypeError(f"unsupported canonical value: {type(value)!r}")


def canonical(value):
    """Return the canonical UTF-8 bytes used for parity state hashing."""
    return canonical_json(value).encode("utf-8")


def state_digest(state):
    unsigned = dict(state)
    unsigned.pop("stateSha256", None)
    return hashlib.sha256(canonical(unsigned)).hexdigest()
