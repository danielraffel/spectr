"""Ed25519 signature verification (RFC 8032), pure Python, no dependencies.

Used to check a Sparkle appcast's `sparkle:edSignature` against the public key
Spectr.app ships in Info.plist (SUPublicEDKey) -- the same check Sparkle makes
before installing an update. Verification only: nothing here can sign, so this
module never needs the private key.

Adapted from the RFC 8032 section 6 reference implementation.
"""
from __future__ import annotations

import base64
import hashlib

_P = 2**255 - 19
_L = 2**252 + 27742317777372353535851937790883648493
_D = (-121665 * pow(121666, _P - 2, _P)) % _P
_SQRT_M1 = pow(2, (_P - 1) // 4, _P)


def _add(a, b):
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    A = (y1 - x1) * (y2 - x2) % _P
    B = (y1 + x1) * (y2 + x2) % _P
    C = t1 * 2 * _D * t2 % _P
    D = z1 * 2 * z2 % _P
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F % _P, G * H % _P, F * G % _P, E * H % _P)


def _mul(s, p):
    q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _equal(a, b):
    x1, y1, z1, _ = a
    x2, y2, z2, _ = b
    return (x1 * z2 - x2 * z1) % _P == 0 and (y1 * z2 - y2 * z1) % _P == 0


def _recover_x(y, sign):
    if y >= _P:
        return None
    x2 = (y * y - 1) * pow(_D * y * y + 1, _P - 2, _P)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P != 0:
        x = x * _SQRT_M1 % _P
    if (x * x - x2) % _P != 0:
        return None
    if (x & 1) != sign:
        x = _P - x
    return x


_GY = 4 * pow(5, _P - 2, _P) % _P
_GX = _recover_x(_GY, 0)
_G = (_GX, _GY, 1, _GX * _GY % _P)


def _decompress(s: bytes):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % _P)


def _compress(p) -> bytes:
    x, y, z, _ = p
    zinv = pow(z, _P - 2, _P)
    x, y = x * zinv % _P, y * zinv % _P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def verify(public_key: bytes, message: bytes, signature: bytes) -> bool:
    if len(public_key) != 32 or len(signature) != 64:
        return False
    a = _decompress(public_key)
    if a is None:
        return False
    rs = signature[:32]
    r = _decompress(rs)
    if r is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= _L:
        return False
    h = int.from_bytes(hashlib.sha512(rs + public_key + message).digest(), "little") % _L
    return _equal(_mul(s, _G), _add(r, _mul(h, a)))


def verify_b64(public_key_b64: str, message: bytes, signature_b64: str) -> bool:
    try:
        pk = base64.b64decode(public_key_b64, validate=True)
        sig = base64.b64decode(signature_b64, validate=True)
    except (ValueError, base64.binascii.Error):
        return False
    return verify(pk, message, sig)


def public_key_from_seed(seed: bytes) -> bytes:
    """Derive the public key for a 32-byte seed (test fixtures only)."""
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return _compress(_mul(a, _G))


def sign(seed: bytes, message: bytes) -> bytes:
    """Sign with a 32-byte seed. For self-tests with throwaway keys only; release
    signing goes through Sparkle's sign_update or `pulp ship appcast`."""
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    prefix = h[32:]
    pk = _compress(_mul(a, _G))
    r = int.from_bytes(hashlib.sha512(prefix + message).digest(), "little") % _L
    rs = _compress(_mul(r, _G))
    k = int.from_bytes(hashlib.sha512(rs + pk + message).digest(), "little") % _L
    s = (r + k * a) % _L
    return rs + int.to_bytes(s, 32, "little")
