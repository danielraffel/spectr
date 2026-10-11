# Windows ARM64/ARM64EC build receipts — historical 2026-10-09 evidence

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

The historical standalone receipt is still valid: Windows has no standalone
`WindowHost` factory, so `WindowHost::create()` returns null. That is separate
from the now-complete DAW path. The newer
`../windows-receipts-2026-10-10/utm-health-receipt.json` proves authenticated
ARM64EC REAPER discovery, CLAP instantiation, rendered controls, and a
non-silent audio render. The corresponding x64 VST3 proof is in
`../windows-receipts-2026-10-10/proxmox-reaper-x64-receipt.json`.

Ableton plugin acceptance remains open because the current ARM guest lacks
AVX/OSXSAVE and the current Proxmox Xeon lacks AVX2. Do not treat the old
scan-only receipts in this directory as current DAW acceptance.
