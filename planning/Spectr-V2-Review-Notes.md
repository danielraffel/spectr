# Spectr V2 Review Notes

Status: Complete  
Date: 2026-04-22  
Pulp baseline reviewed: `main` at `47e6aeb3`

This review compares the v1 planning package against the current Spectr prototypes and the current Pulp codebase. It also accounts for the stated upstream plan to finish the active Pulp format/MIDI work before Spectr implementation begins.

## Critical Findings

1. The product identity is strong and should remain unchanged.
Spectr is clearly strongest when framed as a frequency slicer and spectral isolator, not as an EQ. The prototype-visible effect scope in v1 was largely preserved and should stay preserved in v2.

2. The biggest unresolved issue is not DSP. It is state contract.
The v1 `StateStore + StateTree` split was directionally correct but operationally incomplete. Current Pulp adapters persist `StateStore` blobs for host save/load, not `StateTree`, and `ParamInfo` has no visibility flag for hidden or private parameters. That means any sound-defining field that only lives in `StateTree` will not round-trip through DAW session recall on current Pulp.

3. Spectr likely still needs one framework-level improvement from Pulp.
If Spectr wants manageable host automation plus full host/session recall of viewport semantics, canonical band state, and richer snapshot payloads, the cleanest route is an optional processor-owned supplemental state blob alongside `StateStore` in the format adapters. Without that, Spectr either has to accept a much larger host-visible parameter surface or narrow what survives host session reload.

4. The earlier AU v2 and CLAP MIDI warnings are real on current `main`, but they are already being worked.
The user has parallel Pulp work in progress for CLAP controller/MIDI coverage and AU v2 effect MIDI input. Those gaps should still be documented as current-main facts in this review, but they should be re-verified before they are repeated in a later v3 pass.

5. AU v2 is still the correct first Apple format for Spectr V1.
Pulp can build both AU v2 and AU v3. AU v3 has real code support, but desktop AU v3 still carries the extension/sandbox/deployment burden and the docs are not fully consistent about maturity. For a desktop-first effect, AU v2 remains the pragmatic lane; AU v3 stays a later Apple-extension lane unless iOS/iPad becomes a near-term product goal.

## Optional Improvements

- Use Pulp built-ins first for FFT, STFT, spectrogram, windowing, convolution, and analyzer publication before reaching for third-party DSP.
- Treat `pulp::view::ABCompare` as a helpful two-slot `StateStore` helper, not as Spectr's whole snapshot system.
- Keep AU v3 packaging and install automation out of the critical path unless Apple extension shipping is an explicit requirement.

## Where V1 Needed Tightening

- V1 correctly said that `PresetManager` is not enough, but it did not push the consequence far enough: on current Pulp, Spectr's host/session recall cannot rely on `StateTree` alone.
- V1 placed viewport bounds, analyzer mode, snapshot store, and pattern state in `StateTree` without distinguishing which parts must survive host session recall and which parts can remain preset-only or editor-only.
- V1 recommended a smaller public parameter surface without acknowledging that current Pulp lacks a generic private-state persistence path in the format adapters.

## Pulp Capability Claim Audit

| Claim From V1 | Status | Evidence | Note |
|---|---|---|---|
| `StateStore` is Pulp's canonical flat plugin state and host-facing save/load substrate. | true | [store.hpp](/Users/danielraffel/Code/pulp/core/state/include/pulp/state/store.hpp:22), [au_v2_adapter.cpp](/Users/danielraffel/Code/pulp/core/format/src/au_v2_adapter.cpp:285) | `StateStore` is explicitly the single source of truth for parameters and is what the format adapters serialize. |
| `StateTree` is a realistic companion for dynamic and hierarchical state. | partial | [state_tree.hpp](/Users/danielraffel/Code/pulp/core/state/include/pulp/state/state_tree.hpp:3), [state_tree.hpp](/Users/danielraffel/Code/pulp/core/state/include/pulp/state/state_tree.hpp:115), [au_v2_adapter.cpp](/Users/danielraffel/Code/pulp/core/format/src/au_v2_adapter.cpp:290) | `StateTree` is implemented and JSON-capable, but the generic plugin adapters do not persist it. |
| `PresetManager` only persists a JSON map of named parameter floats and does not persist the `StateStore` blob or `StateTree`. | true | [preset_manager.cpp](/Users/danielraffel/Code/pulp/core/state/src/preset_manager.cpp:47), [preset_manager.cpp](/Users/danielraffel/Code/pulp/core/state/src/preset_manager.cpp:76), [preset_manager.cpp](/Users/danielraffel/Code/pulp/core/state/src/preset_manager.cpp:117) | The implementation computes `store_.serialize()` but writes only parameter-name float pairs, and Apple factory path wiring is still blank by default. |
| `VisualizationBridge` is the main analyzer/publication surface Spectr should use. | partial | [visualization_bridge.hpp](/Users/danielraffel/Code/pulp/core/view/include/pulp/view/visualization_bridge.hpp:4), [visualization_bridge.hpp](/Users/danielraffel/Code/pulp/core/view/include/pulp/view/visualization_bridge.hpp:71), [audio_bridge.hpp](/Users/danielraffel/Code/pulp/core/view/include/pulp/view/audio_bridge.hpp:67) | `VisualizationBridge` is real and well suited for Spectr's analyzer, but Pulp also has simpler audio-to-UI bridges, so it is not the only publication path. |
| Pulp already includes the main DSP building blocks Spectr needs: FFT, STFT, spectrogram helpers, windowing, convolver, and interpolation. | true | [signal.hpp](/Users/danielraffel/Code/pulp/core/signal/include/pulp/signal/signal.hpp:3) | The current `pulp/signal` umbrella header exports the core spectral and filtering primitives Spectr needs for an effect-first build. |
| AU v2 effect MIDI input is not wired on current Pulp `main`. | true | [au_v2_adapter.cpp](/Users/danielraffel/Code/pulp/core/format/src/au_v2_adapter.cpp:252) | The current AU v2 effect path creates empty `midi_in` and `midi_out` buffers and never feeds host MIDI into the effect processor. |
| CLAP controller and advanced MIDI coverage are still limited on current Pulp `main`. | true | [clap_adapter.cpp](/Users/danielraffel/Code/pulp/core/format/src/clap_adapter.cpp:176) | Current CLAP processing handles note on/off and sysex, but not the fuller controller and MIDI event surface the user is now fixing upstream. |
| Pulp can build AU v3 today. | true | [PulpUtils.cmake](/Users/danielraffel/Code/pulp/tools/cmake/PulpUtils.cmake:216), [au_adapter.mm](/Users/danielraffel/Code/pulp/core/format/src/au_adapter.mm:196) | AU v3 has a real CMake path and a real adapter, even if it is not the best first Spectr target. |
| AU v3 is a more complex shipping lane than AU v2 for a desktop-first effect. | true | [formats.md](/Users/danielraffel/Code/pulp/docs/guides/formats.md:270), [macos.md](/Users/danielraffel/Code/pulp/docs/guides/platforms/macos.md:138), [PulpUtils.cmake](/Users/danielraffel/Code/pulp/tools/cmake/PulpUtils.cmake:232) | AU v3 ships as a sandboxed app extension and the generic install helper does not currently add an AU v3 install copy step. |
| The current Spectr scaffold already targets AU v2, not AU v3. | true | [CMakeLists.txt](/Users/danielraffel/Code/spectr/CMakeLists.txt:25) | The existing Spectr project is scaffolded for `VST3 AU CLAP Standalone`, with no AU v3 target yet. |

## Framework Work Spectr Still May Need

These are the remaining upstream items that should be reviewed after the current Pulp format/MIDI branches land:

1. Preferred issue: processor-owned supplemental plugin state in format adapters.
This would let Spectr keep host-visible automation focused while still persisting richer working state for host session recall.

2. Optional follow-up: parameter visibility or automation metadata.
This matters less if the supplemental-state route exists, but it becomes more important if Spectr has to encode more internal state through `StateStore`.

3. Nice-to-have: AU v3 install/distribution helper improvements.
This is not a Spectr V1 blocker unless AU v3 becomes a committed first-wave format.

## Re-Verification Notes For V3

- Re-check CLAP MIDI/controller coverage after `feature/clap-midi-cc-coverage` lands.
- Re-check AU v2 effect MIDI input after `feature/au-v2-effect-midi-input` lands.
- Re-check any format-skills/doc statements after `feature/format-skills-clap-vst3-auv3` lands.
- If a second reviewer agrees that supplemental plugin state is still missing and still needed, they should file a GitHub issue against Pulp rather than only restating the problem in prose.
