# Spectr Linux ARM64 qualification — 2026-11-07

This packet records the Linux ARM64 qualification evidence collected from the Omarchy AArch64 guest. It is a qualification record, not a release acceptance decision. The original packet is historical; the follow-up hosted-GPU log below comes from a rebuild against the pending Pulp Linux host-scheduling change.

The guest source was compared byte-for-byte with this checkout at `911d06fbe9ca6c3395e099b596e23cab5e5ae07f`. The source manifest covers all 1,003 tracked files. It also records one guest-only `tools/spectr-detectors/__pycache__/layout_common.cpython-314.pyc` entry; that bytecode file is not tracked. The manifest is [source-hashes.json](evidence/linux-qualification-2026-11-07/source-hashes.json).

The guest reported `aarch64`, kernel `7.2.3-2-aarch64-ARCH`, Hyprland via SDDM, a 1280x800 Wayland display, and JACK at 48 kHz with a 1024-frame buffer. The copied receipts and their SHA-256 values are:

| Evidence | SHA-256 |
| --- | --- |
| [module-receipt-pre-lifecycle.json](evidence/linux-qualification-2026-11-07/module-receipt-pre-lifecycle.json) | `87b0f14bb32d6311942c784bc35f9dd7c9283163fb2366d6d19c4947fc7372e7` |
| [audio-identity-measurements.json](evidence/linux-qualification-2026-11-07/audio-identity-measurements.json) | `202e2a39d3c5e9e8987469992da2a4f10cda61d2f9201dc74d0fff074f610834` |
| [audio-scaled-measurements.json](evidence/linux-qualification-2026-11-07/audio-scaled-measurements.json) | `cff454c4cb6e5c9c33f0b4b358bc9bae099398dc20e7ca9fe9772a88af9159b3` |
| [realtime-proof.txt](evidence/linux-qualification-2026-11-07/realtime-proof.txt) | `81f5ef039cf9d4e5d0b69ce501a1abb9431ea3ea6560d8ae4796c45695d6734d` |
| [loaded-maps.txt](evidence/linux-qualification-2026-11-07/loaded-maps.txt) | `c26082336bbef7055bbf164e9efdd842ea8b1d81755f8b18057b18e2e46cc516` |
| [standalone-editor.png](evidence/linux-qualification-2026-11-07/standalone-editor.png) | `1737cd094067d983b81db87600cf1a3c7cf6dc36d0ad59a04f38b7553774c3d8` |
| [reaper-hosted-gpu-latest.log](evidence/linux-qualification-2026-11-07/reaper-hosted-gpu-latest.log) | `b4ab5127ea72c15e329c11d56664e46a764712cfb0f9ed7bb5fc304994ebb297` |
| [reaper-user-service-frames.log](evidence/linux-qualification-2026-11-07/reaper-user-service-frames.log) | `92412b260df11bfbcb034cf7f38f33d32cb38b51c50569d205b5f12a7ea985c0` |

The module receipt records VST3 and CLAP build and installed-consumer hashes. The canonical installed VST3 module is `Spectr.vst3/Contents/aarch64-linux/Spectr.so`, and the installed CLAP module is `Spectr.clap`. The receipt records matching build/installed hashes:

- VST3: `eedb7dda9be373c4f54d4310f97f218492866fe5ebdb6d7c70b971d01e52a2c9`, 56,957,328 bytes.
- CLAP: `05ed4de301439978d8cce3a84523d36592989f64ecad0bf463028e2e77302aff`, 56,657,984 bytes.

The receipt also captures ELF dependency and relocation checks for both modules. These module hashes belong to the exact-source build made before the guest-only lifecycle diagnostic; they are historical evidence and must not be presented as hashes for a later rebuild. Any current rebuild will have different hashes.

The offline REAPER render is measured separately from editor presentation. The identity and scaled measurements both cover 192,000 frames at 48 kHz. VST3 and CLAP each move from RMS `0.0162579697` to `0.2576714024`, a measured gain change of `23.9999982255 dB` against the 24 dB expectation. Both records pass. The automation helper for the identity run is [audio-proof-identity.lua](evidence/linux-qualification-2026-11-07/audio-proof-identity.lua). The Lua script reads each host's `lo`/`hi` parameter range before inserting points, but its `/tmp` paths are diagnostic paths for the guest run and require adaptation for reuse.

The real-time receipt reports both VST3 and CLAP loaded in REAPER, JACK mode, 48 kHz, 1024 frames, 508 observed samples, and `underrun=0`. The measured peak is approximately `0.1591004` for each format. [loaded-maps.txt](evidence/linux-qualification-2026-11-07/loaded-maps.txt) records live mappings for the canonical installed CLAP and VST3 modules. This is runtime audio evidence; it does not prove visual editor acceptance.

The standalone editor has a real unlocked presentation in [standalone-editor.png](evidence/linux-qualification-2026-11-07/standalone-editor.png). The accompanying [standalone-receipt.json](evidence/linux-qualification-2026-11-07/standalone-receipt.json) records the guest, display, JACK mode, 48 kHz rate, 1024-frame buffer, and 265 exported AT-SPI objects. [standalone.log](evidence/linux-qualification-2026-11-07/standalone.log) records the editor opening at 990x645. The log reports `gpu=false (gpu-unavailable)`, so this is standalone CPU/editor presentation evidence and not GPU qualification.

The original hosted screenshot remains blank in [hosted-vst3-blank.png](evidence/linux-qualification-2026-11-07/hosted-vst3-blank.png) and is retained as a negative-control artifact. A fresh rebuild after the Pulp GPU move-order and Linux host-scheduling changes reports a presentable Vulkan Dawn surface, Graphite Skia initialization, and GPU attachment for both VST3 and CLAP in [reaper-hosted-gpu-latest.log](evidence/linux-qualification-2026-11-07/reaper-hosted-gpu-latest.log). A user-session REAPER run now records sustained `LINUX_FRAME_TICK` callbacks for both hosted editors in [reaper-user-service-frames.log](evidence/linux-qualification-2026-11-07/reaper-user-service-frames.log), proving the host-driven animation scheduler is active. A fresh hosted screenshot with all controls visible and an input receipt is still required before hosted GUI acceptance.

| Qualification slice | Result | Evidence boundary |
| --- | --- | --- |
| Exact source parity | PASS | 1,003 tracked files match the guest manifest. |
| Installed VST3/CLAP module identity | PASS, historical | Matching hashes and ELF receipts are pre-lifecycle-fix module evidence. |
| Offline DSP and parameter automation | PASS | Both formats measure 23.9999982255 dB over the 24 dB expectation. |
| Real-time JACK audio | PASS | Both formats ran with zero reported underruns. |
| Standalone editor presentation | PASS | Unlocked screenshot and AT-SPI/editor-open receipt. |
| Hosted REAPER surface creation | PASS | Fresh log shows Vulkan/Skia initialization and GPU attachment for VST3 and CLAP. |
| Hosted REAPER editor presentation | OPEN | Fresh full-control screenshot and input receipt are still required; timer-driven animation is now proven. |
| GPU editor qualification | OPEN | Hosted GPU attachment is proven; standalone receipt and timer-capability proof remain separate. |
| Linux product/release acceptance | NOT READY | Hosted GUI proof and current post-fix module provenance remain unresolved. |
