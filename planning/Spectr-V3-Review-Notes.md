# Spectr V3 Review Notes

Status: Complete (confirmation — no v3 spec or handoff needed)
Date: 2026-04-22
Reviewer: Claude (V3 reviewer)
Pulp baseline reviewed: `origin/main` at `47e6aeb3` (chore: bump versions, #624)

## Bottom Line

**V2 is materially correct. No v3 spec or handoff is needed.**

Historical note: this review was written before later in-flight branch work.
Current planning should treat `Spectr-Build-Signoff.md` plus the sampler-phase
spec as the up-to-date go/no-go package, and should treat the current `#625`
branch shape as aligning with V2 handoff §5.4 if it merges.

One concrete action came out of this review: the supplemental plugin-state
capability v2 flagged as "the cleanest route" was filed as a GitHub issue
against Pulp — `danielraffel/pulp#625`. Everything else in v2 holds.

## Critical Findings

### F1 — The three "upstream Pulp branches" are empty

`feature/clap-midi-cc-coverage`, `feature/au-v2-effect-midi-input`, and
`feature/format-skills-clap-vst3-auv3` all exist as locked worktrees under
`/Users/danielraffel/Code/pulp/.claude/worktrees/agent-*` but each one
points at the same commit as `main` (`47e6aeb3`). `git rev-list
--left-right --count origin/main...<branch>` returns `0 0` for all three.

Per the user: three agents are working in parallel on those isolated
worktrees right now; none of the work has committed yet. That matches what
git shows. So the v2 "Phase 0: Upstream readiness" gate is still entirely
open — not partially landed.

**Consequence:** every Pulp gap v2 documented against `main` is still
present on `main`. V3 does not need to re-write v2 to account for anything
that has already changed, because nothing has.

### F2 — Every v2-flagged Pulp gap was re-verified on main

| V2 claim | Still true on main? | Evidence |
|---|---|---|
| AU v2 **effect** MIDI input not wired | Yes | `core/format/src/au_v2_adapter.cpp:252` — `midi::MidiBuffer midi_in, midi_out;` constructed empty and passed unchanged to `processor_->process()`. No `HandleMIDIEvent` override, no `aufx→aumf` switch. (Being fixed on `feature/au-v2-effect-midi-input`.) |
| CLAP event coverage missing CC / PB / NE / choke / MIDI2 | Yes | `core/format/src/clap_adapter.cpp:176` handles only `CLAP_EVENT_NOTE_ON`, `CLAP_EVENT_NOTE_OFF`, `CLAP_EVENT_MIDI_SYSEX`, `CLAP_EVENT_PARAM_VALUE`, `CLAP_EVENT_PARAM_MOD`. No `CLAP_EVENT_MIDI` / `CLAP_EVENT_MIDI2`. (Being fixed on `feature/clap-midi-cc-coverage`.) |
| `PresetManager` only persists `name: float` pairs | Yes | `core/state/src/preset_manager.cpp:76-99` — computes `store_.serialize()`, then writes only `"<name>": <float>` pairs and discards the blob. |
| Format adapters do not persist `StateTree` | Yes | `grep -rn "state_tree\|StateTree" core/format/src/*.cpp` returns empty. |
| No supplemental processor-owned plugin-state path in adapters | Yes | No `set_plugin_state`/`get_plugin_state`/`opaque_state` anywhere in `core/format/` or `core/state/`. This is what `danielraffel/pulp#625` now requests. |
| `ParamInfo` has no visibility / automatable / hidden flag | Yes | `core/state/include/pulp/state/parameter.hpp:52` — fields are `id, name, unit, range, group_id, to_string, from_string`. No way to register a hidden param. |
| AU v3 ships as app extension with sandbox / packaging burden | Yes | Unchanged since v2 review. |
| `ABCompare` is a real two-slot `StateStore` snapshot helper | Yes | `core/view/include/pulp/view/ab_compare.hpp` — backed by `store_->serialize/deserialize`. |
| Built-in FFT / STFT / spectrogram / windowing / convolver / interpolator | Yes | All present under `core/signal/include/pulp/signal/`. |
| `VisualizationBridge` is the right analyzer surface | Yes | `core/view/include/pulp/view/visualization_bridge.hpp`. |

### F3 — The six audit questions, answered

1. **Does v2 preserve all visible effect features from the prototype?**
   Yes. V2 §6 enumerates band layouts `32/40/48/56/64`, viewport + overview
   strip, analyzer modes (`Peak/Average/Both/Off`), edit modes
   (`Sculpt/Level/Boost/Flare/Glide`), action controls
   (`Reset/Clear/Invert/Mute All/Fit View`), response modes
   (`Live/Precision`), engine modes (`IIR/FFT/Hybrid`), patterns, snapshots,
   morph, selection, and group edit. Every prototype-visible surface is
   represented.

2. **Is the frequency-slicer identity still crisp, or did the plan drift
   back toward EQ language?**
   Crisp. V2 §1 and §3 frame Spectr as a frequency slicer and spectral
   isolator, explicitly not an EQ, not a mastering EQ, not a stem separator,
   not a decorative analyzer. The language did not drift.

3. **Is the recommendation on `StateStore`, `StateTree`, snapshots,
   patterns, and host/session recall now correct?**
   Directionally yes. V2's "preferred route" (§5.4) requires a Pulp
   capability that does not exist yet (a supplemental plugin-state path in
   the adapters). V2 already acknowledges this via §5.5 fallback. V3
   upgrades the call by filing `danielraffel/pulp#625` — see F4.

4. **After the active Pulp branches, does Spectr still need a supplemental
   plugin-state capability from Pulp?**
   Yes. None of the three in-flight branches touch the plugin-state
   contract — they are scoped to MIDI coverage and docs. After they land,
   Spectr will still face the exact same host/session recall problem for
   variable band layouts, snapshot banks, and viewport bounds. Filed as
   `danielraffel/pulp#625`.

5. **Is AU v2 still the right first Apple format for Spectr V1?**
   Yes. Nothing material changed. The Spectr scaffold targets AU v2
   (`/Users/danielraffel/Code/spectr/CMakeLists.txt:25`). AU v2 effect
   parameter feedback already works on main
   (`core/format/src/au_v2_adapter.cpp:285` — diffing + `SetParameter` +
   `AUEventListenerNotify`). AU v3 still carries app-extension / sandbox /
   install-copy complexity that a desktop-first effect does not need to pay
   for V1.

6. **Are the dependency recommendations still sensible and minimal?**
   Yes. Built-ins first is correct; the named third-party lanes
   (`signalsmith-stretch`, `dr_libs`, `libsamplerate` / `r8brain-free-src`,
   `pffft`, `cycfi-q`) are still the right candidates for the right
   reasons. No change.

### F4 — Pulp GitHub issue filed

`danielraffel/pulp#625` — "Adapters: expose a supplemental processor-owned
plugin-state blob for host/session recall."

The issue includes:

- concrete problem statement rooted in Spectr's variable band layouts,
  snapshot banks, and viewport-as-sound-state requirements
- file-path evidence for why the gap is real (`ParamInfo` with no visibility
  flag, adapters not persisting `StateTree`, `PresetManager` dropping the
  serialize blob)
- a proposed shape (two `Processor` virtual hooks + adapter plumbing in
  VST3 / AU v2 / AU v3 / CLAP) that preserves backward compatibility
- eight acceptance criteria
- explicit non-goals (not opining on blob contents, not coupling to
  `StateTree`, not changing `PresetManager`)

If upstream wants to solve this differently, the issue description makes it
easy to argue. If the user wants to pick this up themselves, the
acceptance criteria are ready to drive a PR.

## Optional Improvements Not Worth A V3 Rewrite

These are polish-level notes, not corrections:

- V2 handoff §12 lists "remaining Pulp work to consider" as prose. Now
  that the high-priority item is filed as `#625`, future v2 or v3 passes
  can cross-reference the issue number directly.
- V2 handoff §5.4 "preferred route" and §5.5 "fallback route" are the
  right shape, but the decision point is sharper now: if `#625` lands
  before Spectr implementation begins, §5.4 is the committed plan;
  otherwise §5.5 is.
- V2 already notes the AU v2 effect MIDI gap and CLAP CC gap, but does
  not say whether Spectr V1 actually needs either. It does not — Spectr
  V1 is an effect with no MIDI dependency. Those gaps matter for *Pulp
  quality*, not for Spectr V1 blockers. Future passes could make that
  distinction explicit so the Phase 0 gate does not over-block Spectr.

## What Changed Between V2 And V3

Nothing product-side. V3 is purely:

- a confirmation that v2 is materially correct against current `origin/main`
- a concrete GitHub issue (`#625`) for the one framework capability v2
  identified as still-needed
- a re-verification of every v2 Pulp claim against code (table in F2)
- a clean record that the three upstream branches are locked worktrees
  with no commits yet, so v2's Phase 0 gate is still fully open

## Deliverable Summary

- Created: `/Users/danielraffel/Code/spectr/planning/Spectr-V3-Review-Notes.md` (this file)
- Not created (not needed): `Spectr-V3-Product-Spec.md`, `Spectr-V3-Pulp-Handoff.md`
- Filed: `danielraffel/pulp#625` — supplemental plugin-state capability
- README will be updated to reference this file and the issue.
