# Spectr Windows REAPER proof status — 2026-10-12

## Current disposition

**PARTIAL / ARM64 BLOCKED.** The Windows build and headless audio tests are
working on both guests. Current REAPER acceptance is proven for Intel/x64 and
is not currently proven for the native ARM64 plugin. The ARM64 negative result
is retained so a cache entry or historical receipt cannot be mistaken for a
current host load.

## Intel/x64 Proxmox current check

Guest: Proxmox VM 300 on `macpro`, Windows SSH `pulp-rdp@192.168.86.21`.

Fresh focused command:

```text
C:\\spectr-win-proof\\build-win\\Spectr-test.exe "Spectr processes audio" --reporter compact
```

Result: **PASS**, `All tests passed (2 assertions in 1 test case)`.
The current test executable SHA-256 was
`136FACD102F18389E63A8E54FE070B43F4CDDB835A56DD3E56B6F3CD173DE9C7`.
The existing current x64 REAPER/render receipt remains
`windows-receipts-2026-10-10/proxmox-x64-reaper-render-receipt.json`; it proves
VST3 discovery, FX browser listing, visible editor, non-silent render, and
Audio Quality Lab validation on the Proxmox guest.

## ARM64 UTM current check

Guest: UTM/QEMU ARM64, forwarded SSH `127.0.0.1:50376`, RDP
`127.0.0.1:53390`, Windows 10 Pro ARM64, REAPER ARM64.

Current native ARM64 headless checks remain passing for audio processing,
internal modulation, host automation modulation, and LFO ramp. Current native
CLAP SHA-256 is
`F95B9EAA6EE994BA8051884F0DA89C3982B26D3788F460B27855D81912CF3201`.

### Native ARM64 GPU package correction

The package previously installed in the retained UTM guest was not an ARM64
GPU package: the installed CLAP was an x86-64, browser-free/headless artifact.
That explains the generic REAPER parameter page and must not be counted as the
Spectr editor. The corrected producer artifact is the ARM64 Release GPU build
from `C:\builds\spectr-arm64-gpu`:

- CLAP: `59F7B1D6CC67E42230FED10FE1A9835C6D06807AD22A330AB3C19FCA50498AAC`
- VST3: `A0611BC25BB3F31AF440C43E4EF82E684C88E9DFF861E55D1EAD19DE9E1A92A3`
- Standalone: `48FABA70E4DDA89653C8859A0A44470C551ED607F249BF0951EB667FC5F18999`
- PE machine: `Aarch64` for the CLAP and VST3 binaries.
- Build-info records Skia `chrome/m153`, Graphite Dawn, `wgpu-native`, and the
  ARM64 MSVC build.

The corrected CLAP, VST3, `wgpu_native.dll`, and `icudtl.dat` were installed in
the UTM guest and REAPER's ARM64 scan caches were cleared. REAPER then rejected
both the corrected Spectr ARM64 CLAP and a known-good ARM64 PulpGain CLAP; the
resulting cache files contain only the scan hash header and no plugin entry.
The same behavior occurs for the corrected ARM64 VST3. This is a current
REAPER ARM64 host compatibility boundary, not evidence that the native Spectr
editor rendered. The generic parameter page remains a negative control.

After a fresh REAPER launch and removal of the CLAP/VST3 scan caches, the
visible REAPER project-load warning reported:

```text
VST3: Spectr (Pulp) unavailable
CLAP: Spectr (Pulp)
```

The current screenshot is
`arm64-reaper-clap-current-unavailable.png` with SHA-256
`4F152DB55575969D9DF3B602E741FBC5DF1612C00442EDCEDD1CC0CEB635F44A`.
This is a negative host proof, not an acceptance claim.

## Remaining gate

Build the Pulp dependencies and Spectr plugin as ARM64EC/ARM64X, including the
Skia/Dawn GPU UI dependencies needed for the parity frontend. Install that
exact artifact in a disposable ARM64 guest, clear REAPER caches, then require
all of the following in one fresh receipt:

1. REAPER FX browser listing and a visible Spectr instance.
2. Screenshot bound to the exact plugin hash.
3. Non-silent REAPER render and audio-quality checks.
4. Headless modulation/audio tests and exact artifact hashes.

Until that gate passes, the supported demo path is Intel/x64 REAPER on
Proxmox. To prove the UTM plugin path we need an ARM64EC/ARM64X package (or an
x64 GPU package hosted by an x64 REAPER process); a pure ARM64 plugin is not
accepted by the current REAPER ARM64 scanner. The UTM image remains useful for
headless ARM64 validation and does not consume a TartCI/macOS runner.

A later clean scan of the replacement package produced a second current
negative screenshot, `arm64-reaper-vst3-scan-current-unavailable.png`, showing
REAPER's `VST Plug-in Scan` dialog stuck on `Spectr.vst3` with the
“not responding” message. Its SHA-256 is
`2D00A6A795D85B379BE128AB4A7348B6FFD861580BC281306C3225BCFDBC554D`.
