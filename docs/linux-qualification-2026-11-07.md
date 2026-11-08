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
| [rebuild-provenance-20261107.txt](evidence/linux-qualification-2026-11-07/rebuild-provenance-20261107.txt) | `0f21ae8fb86aa9a4ee44c2ee9ea0d677befad60d466c068373a63ce600c5edaa` |
| [reaper-user-service-receipt-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-user-service-receipt-20261107.txt) | `023d389903fbe1fe3136aeda94b6e7fa2800b7d607ed96776db7b0eef8de96f9` |
| [reaper-hosted-window-geometry-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-hosted-window-geometry-20261107.txt) | `199cd9f5fcb06a2df34a59f0b773f91a420468412cdfe1418426bb256caa7cb0` |
| [reaper-exact-scheduler-run-20261107.log](evidence/linux-qualification-2026-11-07/reaper-exact-scheduler-run-20261107.log) | `61e961d4adb1fc5f0005f09244d28fd7619f37b31719e91abc4195b8e4841981` |
| [reaper-hosted-utm-screen-20261107.png](evidence/linux-qualification-2026-11-07/reaper-hosted-utm-screen-20261107.png) | `e54368543a53daee23dc78a7e9c686908668bdd201a47e7a445d49a0ec69973b` |
| [reaper-modulation-proof-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-modulation-proof-20261107.txt) | `8a82b93a97f36be4d14c19cc9043b87455ff11331ebb4f38230ff34a9ac60534` |
| [reaper-hosted-vst3-clap-receipt-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-hosted-vst3-clap-receipt-20261107.txt) | `51422e4c3ad6688543464dfab15f49962c30e8c5f71b4f1eee4f2cf659cc1838` |
| [reaper-hosted-vst3-clap-gpu-20261107.log](evidence/linux-qualification-2026-11-07/reaper-hosted-vst3-clap-gpu-20261107.log) | `0851dad3566bc24463d98bac4c3db63b458ab55db3db6e17503da3c3bc0c4b40` |
| [reaper-hosted-live-screen-20261107.png](evidence/linux-qualification-2026-11-07/reaper-hosted-live-screen-20261107.png) | `fd62ba8d29d23e408fa9156f0aed770962a137a2e40b6abec9c587bda6949dff` |
| [reaper-clap-host-parameter-probe-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-clap-host-parameter-probe-20261107.txt) | `c174bedee5daa31d2868a69e2d5bb89341193164f56c696788d178ea8144d271` |
| [reaper-x11-child-event-probe-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-x11-child-event-probe-20261107.txt) | `7b543652c3a250b22b8b54add5fe3283df62a62d6ab130accad00c248f726246` |
| [reaper-hosted-modulation-utm-screen-20261107.png](evidence/linux-qualification-2026-11-07/reaper-hosted-modulation-utm-screen-20261107.png) | `2f9d2afdca94209d9aadcfdb18d721fb051aeaaedaf84423528b9d605204f0f8` |
| [reaper-input-probe-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-input-probe-20261107.txt) | `2b9c7280e8c0221e5778c84cae346733db08aa649d5d108bb89e8bcd2388b76c` |
| [reaper-audio-proof-20261108.txt](evidence/linux-qualification-2026-11-07/reaper-audio-proof-20261108.txt) | `f939517ff0924b3a0cf481fc62be299da01c568b5df668f3a5224242ad4c52e3` |
| [reaper-audio-metrics-20261108.txt](evidence/linux-qualification-2026-11-07/reaper-audio-metrics-20261108.txt) | `9f6c13ac5404ebda4a7deb817e3b5205bd2ef78490e5c2180840f8077ecb4062` |
| [reaper-audio-loaded-maps-20261108.txt](evidence/linux-qualification-2026-11-07/reaper-audio-loaded-maps-20261108.txt) | `d0e2fb5d523e24aa6e468b85ac63e5668748af03848f778a26f82034fc0b7234` |
| [reaper-modulation-proof-20261108.txt](evidence/linux-qualification-2026-11-07/reaper-modulation-proof-20261108.txt) | `c5146e6a9a95867f220929e515dc179eb74ee28af91968baa0a6020239750f6b` |
| [reaper-modulation-identity-20261108.txt](evidence/linux-qualification-2026-11-07/reaper-modulation-identity-20261108.txt) | `4ab737244c716acc3cbe2db3b31ae24d1ef10b4e4d49e55ec5ec82d7f1f32ae4` |
| [reaper-ydotool-pointer-probe-20261108.txt](evidence/linux-qualification-2026-11-07/reaper-ydotool-pointer-probe-20261108.txt) | `4a2eec0f07bf58d91a20291ca2c0ec57b021b745335bee0d63a588819c40658a` |

The module receipt records VST3 and CLAP build and installed-consumer hashes. The canonical installed VST3 module is `Spectr.vst3/Contents/aarch64-linux/Spectr.so`, and the installed CLAP module is `Spectr.clap`. The receipt records matching build/installed hashes:

- VST3: `eedb7dda9be373c4f54d4310f97f218492866fe5ebdb6d7c70b971d01e52a2c9`, 56,957,328 bytes.
- CLAP: `05ed4de301439978d8cce3a84523d36592989f64ecad0bf463028e2e77302aff`, 56,657,984 bytes.

The post-scheduler rebuild was performed in the same guest after replacing the six scheduler files with the exact current Pulp branch versions. Its installed hashes are recorded separately in [rebuild-provenance-20261107.txt](evidence/linux-qualification-2026-11-07/rebuild-provenance-20261107.txt): VST3 `6b4d180e58cda9450eef3a50b4dab61ef3ff7a511fc88fc2a1ac67899c739d99`, CLAP `84a328f0a8cf51dcf5c1d844b18709d4b5dfb6fca88192920fc14bafff891a67`, and standalone `173eed8a94a100af3d9a7a22259e14a9aacaf476db55495eee5c1e4a18117beb`. The guest Pulp checkout base is `dd2129dd4126096069336d903fd939142e7cb7a5`; the receipt records the six exact scheduler file hashes used for this rebuild. These hashes supersede the historical pre-lifecycle module hashes for any new hosted run.

The receipt also captures ELF dependency and relocation checks for both modules. These module hashes belong to the exact-source build made before the guest-only lifecycle diagnostic; they are historical evidence and must not be presented as hashes for a later rebuild. Any current rebuild will have different hashes.

The offline REAPER render is measured separately from editor presentation. The identity and scaled measurements both cover 192,000 frames at 48 kHz. VST3 and CLAP each move from RMS `0.0162579697` to `0.2576714024`, a measured gain change of `23.9999982255 dB` against the 24 dB expectation. Both records pass. The automation helper for the identity run is [audio-proof-identity.lua](evidence/linux-qualification-2026-11-07/audio-proof-identity.lua). The Lua script reads each host's `lo`/`hi` parameter range before inserting points, but its `/tmp` paths are diagnostic paths for the guest run and require adaptation for reuse.

The real-time receipt reports both VST3 and CLAP loaded in REAPER, JACK mode, 48 kHz, 1024 frames, 508 observed samples, and `underrun=0`. The measured peak is approximately `0.1591004` for each format. [loaded-maps.txt](evidence/linux-qualification-2026-11-07/loaded-maps.txt) records live mappings for the canonical installed CLAP and VST3 modules. This is runtime audio evidence; it does not prove visual editor acceptance.

A separate hosted modulation run exercised both formats while JACK was playing. [reaper-modulation-proof-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-modulation-proof-20261107.txt) records 48 kHz JACK, 1024-frame blocks, playback state `1`, Output automation ranges of approximately `0.4..0.59947` for both formats, peaks of `0.19362` (VST3) and `0.16113` (CLAP), and zero reported underruns. The corresponding [reaper-hosted-modulation-utm-screen-20261107.png](evidence/linux-qualification-2026-11-07/reaper-hosted-modulation-utm-screen-20261107.png) shows the hosted editor during the workload. This closes hosted audio/automation stress evidence; it does not by itself prove pointer input or a frame callback, because this run's journal contained zero `LINUX_FRAME_TICK` lines.

The controlled pointer probe in [reaper-input-probe-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-input-probe-20261107.txt) discovered both editor windows and dispatched XWayland clicks, but the monitored Mix and Output parameters stayed at `0.5`. The guest lacks a compositor-native pointer injector (`ydotool`/`dotool`), so this is a negative/diagnostic receipt and leaves input acceptance open.

The standalone editor has a real unlocked presentation in [standalone-editor.png](evidence/linux-qualification-2026-11-07/standalone-editor.png). The accompanying [standalone-receipt.json](evidence/linux-qualification-2026-11-07/standalone-receipt.json) records the guest, display, JACK mode, 48 kHz rate, 1024-frame buffer, and 265 exported AT-SPI objects. [standalone.log](evidence/linux-qualification-2026-11-07/standalone.log) records the editor opening at 990x645. The log reports `gpu=false (gpu-unavailable)`, so this is standalone CPU/editor presentation evidence and not GPU qualification.

The exact rebuilt standalone was launched again after installing the SDK host
change. [standalone-rebuilt-input-20261107.txt](evidence/linux-qualification-2026-11-07/standalone-rebuilt-input-20261107.txt)
binds the launch to its installed hash and the fresh unlocked screenshot.

The original hosted screenshot remains blank in [hosted-vst3-blank.png](evidence/linux-qualification-2026-11-07/hosted-vst3-blank.png) and is retained as a negative-control artifact. A fresh rebuild after the Pulp GPU move-order and Linux host-scheduling changes reports a presentable Vulkan Dawn surface, Graphite Skia initialization, and GPU attachment for both VST3 and CLAP in [reaper-hosted-gpu-latest.log](evidence/linux-qualification-2026-11-07/reaper-hosted-gpu-latest.log). A user-session REAPER run now records sustained `LINUX_FRAME_TICK` callbacks for both hosted editors in [reaper-user-service-frames.log](evidence/linux-qualification-2026-11-07/reaper-user-service-frames.log), proving the host-driven animation scheduler is active for that run. The fresh host-level UTM capture [reaper-hosted-utm-screen-20261107.png](evidence/linux-qualification-2026-11-07/reaper-hosted-utm-screen-20261107.png) is non-black and visibly contains the hosted Spectr editor, graph, bands, and controls. It is visual proof only: Omarchy notifications overlap the upper-right of the editor, and it does not establish input acceptance or bind the pixel result to the exact-head run.

A fresh user-session run after the rebuild instantiated both formats and applied the parameter probe; see [reaper-user-service-receipt-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-user-service-receipt-20261107.txt). Hyprland reported visible floating editors at 990x702 (VST3) and 990x703 (CLAP); see [reaper-hosted-window-geometry-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-hosted-window-geometry-20261107.txt). That earlier compositor capture was DPMS-off/black and remains window-state evidence only; the later host-level captures above are the current pixel evidence.

The exact rebuilt Linux host input bridge was installed and retested in a fresh
session. [rebuild-input-bridge-20261107.txt](evidence/linux-qualification-2026-11-07/rebuild-input-bridge-20261107.txt)
binds the run to Pulp `f5b33c74f24f4e062752cb8cdeb3d55a4dc2cbd6` and the new
installed VST3/CLAP/standalone hashes. Hover reaches the Mix control, but a
controlled press/drag still leaves VST3 Mix at `1.0`. Hosted press/drag remains
open; this negative result is the current exact-source boundary between X11
event delivery and Pulp control dispatch.

The fresh REAPER qualification receipt [reaper-hosted-vst3-clap-receipt-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-hosted-vst3-clap-receipt-20261107.txt)
was collected after the scheduler rebuild in the running Omarchy guest. REAPER
discovered both installed formats, instantiated each editor, and reported 212
parameters for each. The VST3 Mix parameter changed from `1.0` to
`0.3700000048` through the host API. The CLAP editor was instantiated and its
parameter surface was visible. A separate CLAP-only probe
([reaper-clap-host-parameter-probe-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-clap-host-parameter-probe-20261107.txt))
changed Mix from normalized `1.0` to `0.37` and observed that value across
thirty deferred host ticks. The earlier dual-instance receipt's unchanged CLAP
value was therefore a probe-order artifact, not evidence of a CLAP adapter
failure. The corresponding [GPU log](evidence/linux-qualification-2026-11-07/reaper-hosted-vst3-clap-gpu-20261107.log)
records presentable Vulkan/Dawn/Graphite surfaces and GPU attachment for both
formats on Virtio-GPU Venus.

The native X11 child-event probe
([reaper-x11-child-event-probe-20261107.txt](evidence/linux-qualification-2026-11-07/reaper-x11-child-event-probe-20261107.txt))
selected the actual embedded Spectr child window and listened with Xlib while
both Xdotool and Ydotool targeted it. It observed no ButtonPress,
ButtonRelease, MotionNotify, EnterNotify, or LeaveNotify events, while the
REAPER monitor kept Mix at `1.0`. Pulp's synthetic nested-window XSendEvent
test passes separately; this receipt identifies the remaining gap as the
Hyprland/Xwayland event transport in this UTM guest.

The accompanying [live hosted screen](evidence/linux-qualification-2026-11-07/reaper-hosted-live-screen-20261107.png)
is a fresh unlocked UTM capture from the same guest session. It visibly shows
the Spectr graph, bands, controls, and REAPER host chrome. The image is visual
evidence only; it is not used to claim pointer acceptance.

The exact-head rerun after replacing the scheduler files produced presentable Vulkan/Graphite surfaces and both editor attachments, but no `LINUX_FRAME_TICK` lines in [reaper-exact-scheduler-run-20261107.log](evidence/linux-qualification-2026-11-07/reaper-exact-scheduler-run-20261107.log). REAPER did not expose a timer callback on that idle run, so the previously captured callback log remains evidence for the separate user-session workload and is not reused as exact-head proof. The fresh host-level captures close the non-black pixel gate; exact-head continuous animation and pointer input remain open.

The fresh 2026-11-08 REAPER run was made after rebuilding Pulp with `PULP_ENABLE_DESIGN_IMPORT=ON` and `PULP_ENABLE_GPU=ON`, then rebuilding Spectr from source `911d06fbe9ca6c3395e099b596e23cab5e5ae07f` against Pulp `b1367c4de668e2a248c89dd322549aa2c85bdf73`. The installed modules were rehashed immediately before the run: VST3 `1224883b1580960966f328905550b29083b713ad370051640b76143cbe1cf5a4`, CLAP `ae4e39f095cd7924c698047adb9391240f7a890bd3e6de1d58e68ab833df1a05`, and standalone `ace01f57b4aeedc310f6f63832ee23619e0d5786e5a74ed71f450006aa0cc265`.

REAPER 7.85 discovered both `VST3: Spectr (Pulp)` and `CLAP: Spectr (Pulp)`, instantiated both editors, and reported 212 parameters for each. The host changed VST3 Mix from normalized `1.0` to `0.3700000048`; the CLAP editor was instantiated and its parameter surface was visible. Hyprland reported both visible floating editor windows (`VST3: Spectr (Pulp) - Track 1 [1/2]` and `CLAP: Spectr (Pulp) - Track 1 [2/2]`), each 990x673. The complete receipt is [reaper-fresh-qualification-20261108.txt](evidence/linux-qualification-2026-11-07/reaper-fresh-qualification-20261108.txt) (SHA-256 `c6410e2ab3d763586d9b170c0ccb6be5151d2348333011e2cdefcb7b8bc50b2e`). This closes fresh REAPER discovery, instantiation, editor-window, and host-parameter evidence; compositor-native pointer acceptance remains a separate open boundary.

The exact installed-hash audio and automation rerun on 2026-11-08 completed as the Omarchy desktop user. REAPER 7.85 instantiated VST3 and CLAP, inserted three Output envelope points for each, and rendered 192,000 stereo frames at 48 kHz. The rendered output is nonzero and changes at the automation boundary: RMS is `136381.7` through 1.5 seconds, `171963.6` from 1.5 to 2 seconds, and `2161504.3` from 2 to 4 seconds. The rendered WAV SHA-256 is `fb69e4f34fe7242dbfc45e854a297279e33bc7c1e8aaaf8e6a39cd3e2b11cc23`. The loaded-map receipt binds the run to the current installed VST3 and CLAP module hashes listed in the receipt.

A second exact-hash hosted modulation run configured the LFO and Output automation on both formats while JACK playback was active. VST3 and CLAP produced nonzero peaks, changing Output values, `playstate=1`, and an underrun count of zero.

A fresh compositor-native pointer probe used the guest's running `ydotoold` and moved the Hyprland cursor into the exact VST3 editor geometry. Native click and drag events did not change the monitored Mix parameter, so pointer acceptance remains open at the embedded XWayland child/control-dispatch boundary.

| Qualification slice | Result | Evidence boundary |
| --- | --- | --- |
| Exact source parity | PASS | 1,003 tracked files match the guest manifest. |
| Installed VST3/CLAP module identity | PASS, historical | Matching hashes and ELF receipts are pre-lifecycle-fix module evidence. |
| Offline DSP and parameter automation | PASS | Both formats measure 23.9999982255 dB over the 24 dB expectation. |
| Real-time JACK audio | PASS, exact current hashes | Fresh REAPER render and loaded-map receipts bind nonzero output to the current installed VST3/CLAP modules. |
| Hosted modulation and automation | PASS, exact current hashes | Fresh VST3/CLAP run shows active JACK playback, changing Output values, nonzero peaks, and zero underrun count. |
| Standalone editor presentation | PASS | Unlocked screenshot and AT-SPI/editor-open receipt. |
| Hosted REAPER surface creation | PASS | Fresh log shows Vulkan/Skia initialization and GPU attachment for VST3 and CLAP. |
| Hosted CLAP host-parameter write | PASS | CLAP-only probe changes Mix from normalized `1.0` to `0.37` and retains it across deferred host ticks. |
| Exact rebuilt hosted pointer input | FAIL / OPEN | Hover reaches Mix, but XWayland and ydotool press/drag leaves VST3 Mix at `1.0`; see the exact-hash receipt. |
| Hosted REAPER editor presentation | OPEN | Fresh non-black hosted pixels and a new exact rebuilt VST3/CLAP instantiation receipt are proven; pointer input and exact-head timer evidence remain open. |
| GPU editor qualification | OPEN | Hosted GPU attachment is proven; standalone receipt and timer-capability proof remain separate. |
| Linux product/release acceptance | NOT READY | Hosted GUI proof and current post-fix module provenance remain unresolved. |
