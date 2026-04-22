# Spectr Review Agent Prompt

Copy the section below into a new agent to generate a v2 review and planning pass for Spectr **before** implementation begins.

The prompt is self-contained: it references the v1 docs, the prototype, the Pulp framework, and the scaffolded Spectr project, and it specifies the v2 deliverables.

---

You are reviewing and upgrading the Spectr planning package.

Your job is not just to comment on it. Your job is to produce a stronger v2.

## Primary Goal

Review the existing Spectr planning artifacts, compare them against the current prototypes and the actual Pulp implementation (on origin/main at `/Users/danielraffel/Code/pulp`), and then create improved v2 planning docs.

## Inputs To Review

V1 planning docs (read these first — they are the artifacts you are reviewing):

- `/Users/danielraffel/Code/spectr/planning/Spectr-V1-Product-Spec.md`
- `/Users/danielraffel/Code/spectr/planning/Spectr-Pulp-Handoff.md`
- `/Users/danielraffel/Code/spectr/planning/README.md`

Prototype and notes (design ground truth):

- `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr (standalone).html`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr Sampler.html`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/src/`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/effect-ideas.txt`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/sampler-ideas.txt`

Framework reference (code is source of truth — do not trust docs over code):

- `/Users/danielraffel/Code/pulp/`
- Especially:
  - `core/state/` (StateStore, StateTree, PresetManager)
  - `core/signal/` (FFT, STFT, spectrogram, windowing, convolver, interpolator)
  - `core/view/include/pulp/view/visualization_bridge.hpp`
  - `core/format/src/{vst3,au_v2,clap}_adapter.cpp`
  - `examples/PulpSampler/`, `examples/PulpSynth/`, `examples/PulpDrums/`, `examples/mpe-synth/`
  - `docs/guides/packages/`
  - `docs/guides/formats.md`
  - `docs/reference/capabilities.md`

Scaffolded Spectr project (the code already exists):

- `/Users/danielraffel/Code/spectr/spectr.hpp`
- `/Users/danielraffel/Code/spectr/CMakeLists.txt`
- `/Users/danielraffel/Code/spectr/pulp.toml`
- `/Users/danielraffel/Code/spectr/test_spectr.cpp`

If you have RepoPrompt available, use `context_builder` against `/Users/danielraffel/Code/pulp` for framework validation. If not, use `rg`/`grep` + direct file reads.

## Hard Constraints

- Prototype-visible effect features remain in scope.
- Do not "improve" the plan by cutting prototype functionality.
- Phases may sequence implementation and hardening, but they may not quietly remove effect features already present in the prototype.
- Keep the product effect-first, sampler-forward.
- Respect Pulp realities instead of assuming ideal infrastructure that is not present.
- Where the v1 handoff claims a Pulp capability, verify it in code before accepting the claim. Where the handoff calls out a Pulp gap (e.g., `PresetManager` on-disk format, AU v2 effect MIDI input, CLAP MIDI coverage), verify it is still a gap before repeating it in v2.

## Specific Things To Audit

1. Product identity
   - Is the spec clearly describing Spectr as a frequency slicer rather than an EQ?
   - Does the plan preserve what is special about the prototype?

2. Prototype parity
   - Are all visible effect features represented in the plan?
   - Are any prototype features still implicitly downgraded, hand-waved, or under-specified?

3. Pulp state architecture
   - Validate whether `StateStore + StateTree` is the right recommended split for Spectr.
   - Check whether the handoff describes the boundary between automatable/public state and dynamic/internal state clearly enough.
   - Check whether presets, snapshots, and future frozen assets are cleanly separated.
   - Confirm the `PresetManager` caveat is still accurate: does Pulp's current `preset_manager.cpp` still write only named parameter floats, or has that changed?

4. DSP realism
   - Challenge whether the DSP contract is clear enough for implementation.
   - Identify any claims that are too vague, too optimistic, or not measurable.
   - Validate the built-in DSP inventory in the handoff against `core/signal/include/pulp/signal/` today.

5. Dependency strategy
   - Review whether the package recommendations are sensible.
   - Prefer Pulp built-ins where they are already strong.
   - Only recommend third-party additions where they materially help.
   - Prefer MIT/BSD-style libraries already documented in `/Users/danielraffel/Code/pulp/docs/guides/packages/`.

6. Shipping strategy
   - Are the phases realistic?
   - Are there hidden blockers around automation, state recall, latency, band-layout changes, or presets?
   - Is the milestone plan consistent with what `pulp create` already produced for the Spectr project?

7. Missing decisions
   - Identify the most important unresolved product or implementation decisions.
   - Distinguish between "must decide now" and "can defer to implementation."

## Deliverables

Create these files (overwriting any existing v2 drafts):

- `/Users/danielraffel/Code/spectr/planning/Spectr-V2-Product-Spec.md`
- `/Users/danielraffel/Code/spectr/planning/Spectr-V2-Pulp-Handoff.md`
- `/Users/danielraffel/Code/spectr/planning/Spectr-V2-Review-Notes.md`

## Output Requirements

In `Spectr-V2-Review-Notes.md`:

- Start with findings first.
- Call out contradictions, risks, and weak assumptions.
- Be direct.
- Separate critical issues from optional improvements.
- For every Pulp-capability claim in the v1 docs, record: `STATUS (true / partial / overstated / missing)`, 1–3 file refs, and one sentence.

In `Spectr-V2-Product-Spec.md`:

- Keep it product-facing.
- Preserve prototype-visible effect scope.
- Make phase boundaries clearer where needed.
- Make success criteria and non-goals more concrete.

In `Spectr-V2-Pulp-Handoff.md`:

- Keep it implementation-facing.
- Be explicit about Pulp state architecture, preset contract, and dependency recommendations.
- Distinguish built-ins from third-party package lanes.
- Call out any Pulp gaps (e.g., `PresetManager` default format, AU v2 effect MIDI) that Spectr must route around.
- Flag any areas that need code spikes before committing to full implementation.

## Review Standard

Assume the current v1 docs are serious but not final.

Your job is to make them:

- more internally consistent
- more faithful to the prototype
- more honest about Pulp constraints
- more useful as an actual build handoff

Do not stop at commentary. Produce the v2 files.
