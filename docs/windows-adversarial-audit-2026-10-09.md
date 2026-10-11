# Windows/Spectr adversarial completion audit — refreshed 2026-10-10

This is a fail-closed audit of the Windows development/test objective. It is
intentionally not a product acceptance claim.

| Requirement | Evidence | Verdict |
|---|---|---|
| Proxmox Intel Windows build | `tools/windows/proxmox-validate.sh`; VM 300 receipt, focused audio test, VM stopped afterward | PASS |
| One reusable Windows project base | `/Volumes/Atelier/VMs/bench/pulp-windows-build-24h2-arm64-utm-fresh-20261007.qcow2`; linked overlays only | PASS |
| Native ARM M5/M5S headless build | direct QEMU overlay; Pulp Release SDK, scalar SIMD, ARM64EC Spectr plugin links | PASS for plugin targets |
| Warm cache and disposable overlay | `tools/windows/launch-arm64-qemu.sh` creates a QCOW2 linked overlay and removes it on exit | PASS |
| No macOS CI runner consumption | launcher is local QEMU; no TartCI registration or runner labels | PASS |
| ARM64EC/ARM64X artifact shape | `dumpbin /headers`: `8664 machine (x64) (ARM64X)`; hashes in `docs/windows-receipts-2026-10-09/` | PASS |
| Focused Windows DSP test on this ARM64EC tree | `Spectr-test.exe "Spectr processes audio" --reporter compact` | PASS; 2 assertions in 1 test case |
| REAPER plugin instance | `docs/windows-receipts-2026-10-10/utm-health-receipt.json` records an authenticated UTM ARM64EC session with Spectr CLAP discovered, instantiated on Track 1, and controls rendered. `proxmox-reaper-x64-receipt.json` records the x64 VST3 scanned, listed in the FX browser, and instantiated on Track 1. `proxmox-x64-reaper-render-receipt.json` records the same x64 instance with the rendered editor visible. | PASS for ARM64EC CLAP and x64 VST3 instantiation/editor |
| REAPER screenshot | The UTM session capture is recorded in `utm-health-receipt.json`; the x64 CUA capture visibly showed the live Spectr parameter editor after Dummy Audio selection and REAPER restart. | PASS for observed UI; x64 capture is transcript-only |
| Real REAPER render/audio | UTM receipt records a 5-second stereo 44.1 kHz 24-bit WAV, 220500 frames, peak -14.0 dB, clip 0, LUFS-M -14.7, SHA-256 `66a431843b92f3e063c9c6aa888a2b02fd279daecc0d36c584e772c87c7d0fb8`; `proxmox-x64-reaper-render-receipt.json` records a 3-second stereo 44.1 kHz 24-bit WAV, 132300 frames, peak -8.7 dBFS, clip 0, LUFS-M -9.4, SHA-256 `2a521c655ee0739509f5bcd0433d1a56aa125c26da39be3f5dab122ab378f9a4`. | PASS for ARM64EC CLAP and x64 VST3 |
| Windows audio harness / Quality Lab | `arm64ec-audio-suite-receipt.json` records 51 cases and 213227 assertions passing. The x64 REAPER/Spectr render passes Pulp offline `audio validate summarize` and `doctor --thd`, plus the reference-free Audio Quality Lab; all JSON artifacts are retained beside the WAV. | PASS for headless suite and captured interactive render |
| Perfetto trace | `docs/windows-receipts-2026-10-10/proxmox-x64-perfetto-trace-receipt.json` records the traced Spectr Windows test build, Pulp/Perfetto hashes, seven nonzero `.pftrace` captures, and trace-processor SQL results (representative trace: 6,560 slices including 4,504 `tracking.render_block` spans). The hidden `[.][glitch-trace]` test passed 14 assertions. This is development-only evidence, not a shipping build or product-acceptance claim. | PASS for development trace path |
| Ableton | Live 10 Trial launches on Proxmox, but its first-run trial panel prevented plugin interaction. Live 12.4.6 installation aborts on both current guests because UTM lacks AVX/OSXSAVE and the Xeon lacks AVX2. | Launch-only PASS; plugin/audio OPEN pending an AVX2-capable licensed guest |
| Durable one-off automation | `tools/windows/headless-build.ps1`, `launch-arm64-qemu.sh`, health guard, package/install/scan helpers | PASS for build/package; DAW rung remains fail-closed |
| Skills capture the lessons | Pulp `audio-harness` skill now records Windows ARM64/ARM64EC adapter boundary and rejects cache-only acceptance | PASS |
| Adversarial review completed | this receipt records every missing proof explicitly | PASS |

## Remaining must-fix

1. Validate Ableton only after an AVX2-capable Windows guest and a licensed
   installation are available; do not bypass activation or report launch as
   plugin acceptance.

Until those items are complete, the honest status is:

**Windows build artifacts proven; ARM64EC and x64 REAPER plugin/editor/audio
acceptance proven; the Windows development trace path is proven. Ableton plugin
acceptance remains open pending a legitimate AVX2-capable licensed guest.**
