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
| REAPER plugin instance | `docs/windows-receipts-2026-10-10/utm-health-receipt.json` records an authenticated UTM ARM64EC session with Spectr CLAP discovered, instantiated on Track 1, and controls rendered. `proxmox-reaper-x64-receipt.json` records the x64 VST3 scanned, listed in the FX browser, and instantiated on Track 1. | PASS for ARM64EC CLAP and x64 VST3 instantiation; x64 editor remains open |
| REAPER screenshot | `.local-evidence/proxmox/reaper-x64-spectr-instantiated.png` plus the UTM session capture recorded in `utm-health-receipt.json` | PASS |
| Real REAPER render/audio | UTM receipt records a 5-second stereo 44.1 kHz 24-bit WAV, 220500 frames, peak -14.0 dB, clip 0, LUFS-M -14.7, SHA-256 `66a431843b92f3e063c9c6aa888a2b02fd279daecc0d36c584e772c87c7d0fb8` | PASS for ARM64EC CLAP; x64 audio remains open |
| Windows audio harness / Quality Lab | `arm64ec-audio-suite-receipt.json` records 51 cases and 213227 assertions passing. The host Quality Lab has not yet been run against the interactive plugin instance. | PASS for headless suite; host Quality Lab open |
| Perfetto trace | no current trace captured from the Windows host/plugin render path | OPEN |
| Ableton | Live 10 Trial launches on Proxmox, but its first-run trial panel prevented plugin interaction. Live 12.4.6 installation aborts on both current guests because UTM lacks AVX/OSXSAVE and the Xeon lacks AVX2. | Launch-only PASS; plugin/audio OPEN pending an AVX2-capable licensed guest |
| Durable one-off automation | `tools/windows/headless-build.ps1`, `launch-arm64-qemu.sh`, health guard, package/install/scan helpers | PASS for build/package; DAW rung remains fail-closed |
| Skills capture the lessons | Pulp `audio-harness` skill now records Windows ARM64/ARM64EC adapter boundary and rejects cache-only acceptance | PASS |
| Adversarial review completed | this receipt records every missing proof explicitly | PASS |

## Remaining must-fix

1. Diagnose the Proxmox x64 blank editor and configure a real/offline audio
   device or render path. The x64 scan and instantiation are proven, but the
   editor screenshot is blank white and no x64 render receipt exists. The
   current guest diagnostic is `Microsoft Basic Display Adapter`, driver
   `10.0.20348.1`, `AdapterRAM = 0`; VM 300 has no explicit accelerated
   display device in `qm config`. A display-driver/device change and a reboot
   are therefore required before treating x64 UI rendering as a plugin bug.
2. Run the established Windows Quality Lab against the accepted UTM host path
   and capture a Perfetto trace if the Windows instrumentation path supports it.
3. Validate Ableton only after an AVX2-capable Windows guest and a licensed
   installation are available; do not bypass activation or report launch as
   plugin acceptance.

Until those items are complete, the honest status is:

**Windows build artifacts proven; ARM64EC REAPER plugin/audio acceptance proven;
x64 editor/audio and Ableton plugin acceptance remain open.**
