# Windows/Spectr adversarial completion audit — 2026-10-09

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
| REAPER plugin instance | Fresh 2026-10-10 authenticated RDP session: cache cleared, full VST rescan completed, and REAPER lists `Spectr.vst3` under “Plug-ins that failed to scan” | FAIL / open |
| REAPER screenshot | no authenticated observed plugin window | FAIL / open |
| Real REAPER render/audio | no host render receipt | FAIL / open |
| Windows audio harness / Quality Lab | not run against the current ARM64EC artifact | FAIL / open |
| Perfetto trace | not captured after plugin load/render | FAIL / open |
| Ableton | not installed in guest | NOT RUN |
| Durable one-off automation | `tools/windows/headless-build.ps1`, `launch-arm64-qemu.sh`, health guard, package/install/scan helpers | PASS for build/package; DAW rung remains fail-closed |
| Skills capture the lessons | Pulp `audio-harness` skill now records Windows ARM64/ARM64EC adapter boundary and rejects cache-only acceptance | PASS |
| Adversarial review completed | this receipt records every missing proof explicitly | PASS |

## Remaining must-fix

1. Rebuild and reinstall the Pulp ARM64EC Release SDK so every static library,
   including `pulp-standalone-native.lib`, matches the Release Spectr tree. The
   Spectr test lane now gates import-fidelity sources on `PULP_HAS_DESIGN_IMPORT=1`;
   the focused audio test is green.
2. Make REAPER accept the ARM64X/ARM64EC VST3. The fresh scan receipt at
   `docs/windows-receipts-2026-10-10/reaper-scan-failure-receipt.json` proves
   the current native ARM64 binary is rejected after a full cache reset. The
   next test must use the ARM64X/ARM64EC artifact and preserve the failed-scan
   list, exact host/plugin hashes, observed instance, screenshot, and a real
   rendered WAV.
3. Run the established Windows audio harness/Quality Lab and capture a Perfetto
   trace from the accepted host/render path.
4. Repair or explicitly retire the UTM desktop import. The current tested build
   authority is direct QEMU; the UTM 5.0.6 VM starts but does not produce a
   usable guest SSH banner within the bounded health window.

Until those items are complete, the honest status is:

**Windows build artifacts proven; Windows DAW/plugin acceptance incomplete.**
