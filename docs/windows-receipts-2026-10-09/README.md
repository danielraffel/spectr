# Windows ARM64/ARM64EC build receipts — 2026-10-09

These receipts record current Spectr Windows work on the native ARM guest
running under QEMU on the M5S. The Pulp follow-up is published at
https://github.com/Generous-Corp/pulp/pull/9973.

The ARM64EC VST3, CLAP, and standalone targets linked successfully. The
focused test passed:

```text
Spectr-test.exe "Spectr processes audio" --reporter compact
All tests passed (2 assertions in 1 test case)
```

The authenticated RDP transport and Windows desktop are proven, and ARM64
REAPER is installed and launches. The standalone desktop path is still blocked:
Pulp's Windows build has no standalone `WindowHost` factory, so
`WindowHost::create()` returns null. The desktop receipt records this negative
proof explicitly.

Spectr is **not accepted in REAPER yet**. The required observed Spectr host
instance, real render, Windows audio harness/Quality Lab result, and Perfetto
trace remain open. A scan cache entry or `LoadLibrary` result is insufficient.
See `arm64ec-build-receipt.json` and `desktop-launch-receipt.json` for the
current receipts.
