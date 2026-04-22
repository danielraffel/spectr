# Spectr Build Signoff

Status: **Build cleared to start V1 effect implementation**
Date: 2026-04-22
Reviewer: Claude (resolved-blockers pass)
Pulp baseline: `origin/main` at `47e6aeb3`
Supersedes: `Spectr-Build-Blockers.md`

## Verdict

All three V1 blockers identified in `Spectr-Build-Blockers.md` are
resolved in the planning docs. The V1 effect is cleared to start.

## What Changed

### B1 — Glide (not Smooth) — resolved

`Spectr-V2-Product-Spec.md` §6.5 now lists the fifth edit mode as
`Glide` with keybinding **G** and a one-line behaviour note drawn from
the prototype snapshot logic. All five keybindings `S / L / B / F / G`
are preserved. A naming-note paragraph cites the prototype source so
the build team cannot drift back to "Smooth."

Citation added in the spec:
`Spectr-design/Spectr-2/Spectr (standalone source).html:4081` →
`editMode ... // sculpt|level|boost|flare|glide`
`:3275` → `{ k: 'glide', label: 'GLIDE', hint: 'G' }`

### B2 — Pattern-manager control enumeration — resolved

`Spectr-V2-Product-Spec.md` §6.8 now enumerates every prototype-visible
pattern control from `Spectr-design/Spectr-2/src/pattern_manager.jsx`:

- save current (auto-named `PATTERN NN`)
- rename (user only)
- duplicate (factory or user → user `<name> COPY`)
- delete (user only, with confirm)
- update-from-current / overwrite (user only)
- set as default (★, loaded on plugin open)
- apply via double-click or APPLY button
- search / filter
- factory vs user list separation with `F` / `U` badges
- import JSON: file picker, clipboard paste, paste textarea
- export selected: file and clipboard
- export all user patterns: file and clipboard
- chrome rail `PATTERNS ▾` dropdown with inline list and `MANAGE…`

The eight factory patterns (`Flat`, `Harmonic series`, `Alternating`,
`Comb`, `Vocal formants`, `Sub only`, `Downward tilt`, `Air lift`) are
pinned in the same section.

§6.8 also calls out the separation of concerns — patterns store
relative spectral shape; snapshots capture full working state — and
points at the handoff §5.5 for persistence semantics.

### B3 — Fallback recall guarantee — resolved

`Spectr-V2-Pulp-Handoff.md` §5.5 is rewritten from abstract guidance
into a concrete buildable contract with five subsections:

- **§5.5.1 Recall guarantee** — what survives DAW session reload via
  `StateStore`, including the canonical 64-slot band representation so
  layout switches are projections, not parameter-set mutations.
- **§5.5.2 Runtime-only** — what explicitly does NOT survive session
  reload (snapshot payloads, pattern library, analyzer mode, edit mode,
  UI state), and why.
- **§5.5.3 Preset-only** — the Spectr-owned preset file format that
  carries the `StateStore` blob plus a `StateTree` JSON payload of the
  richer state, with schema version and plugin version.
- **§5.5.4 Host automation lane size** — quantifies the fallback cost
  (~136 registered parameters) and notes this drops if `#625` lands.
- **§5.5.5 Decision point** — Phase 0 kickoff must write down which
  route is in effect (§5.4 preferred via `#625` vs §5.5 fallback). The
  parameter-registration shape depends on that decision.

## Day-One Punch List For The Build Team

Short, concrete, ordered:

1. **Phase 0 kickoff first** — use `Spectr-V2-Pulp-Handoff.md` §5.4.
   `#625` is the plan of record per product-owner direction (2026-04-22);
   wait for it to land and do NOT implement under §5.5. The build plan
   parks Milestone 4 (state registration) behind the `#625` gate for
   exactly this reason. See `Spectr-Upstream-Integration-Plan.md` §A for
   the gate check.
2. **Register parameters to the chosen route's shape.** Under §5.5 this
   means the canonical 64-slot band representation plus the nine other
   `StateStore` fields in §5.5.1 — do not re-register on layout change.
3. **Implement edit modes with correct names and keybindings** —
   `Sculpt(S) / Level(L) / Boost(B) / Flare(F) / Glide(G)`. Do not
   substitute "Smooth."
4. **Implement pattern manager from the §6.8 enumeration, not the
   earlier short summary.** Every control in that list is scope.
5. **Use `VisualizationBridge` as the analyzer surface** — it already
   gives STFT + meter + waveform via a `TripleBuffer` lock-free
   transport (`core/view/include/pulp/view/visualization_bridge.hpp`).
6. **Do not rely on `PresetManager` for the on-disk preset format.**
   It persists only `name: float` pairs today
   (`core/state/src/preset_manager.cpp:76`). Implement Spectr's own
   preset file per §7 of the handoff.
7. **Defer all sampler work to after V1 ships.** See
   `Spectr-Sampler-Phase-Spec.md` for the later phases.

## Gates Outside The Planning Package

These are not Spectr V1 blockers but the build team should know their
state:

- `danielraffel/pulp#625` — still the one upstream lane that materially
  improves Spectr's V1 implementation route. It is in progress on
  `/Users/danielraffel/Code/pulp-625-plugin-state` branch
  `codex/625-supplemental-plugin-state`; the reviewed shape lines up with
  handoff §5.4 (`serialize_plugin_state` / `deserialize_plugin_state`,
  adapter-side `plugin_state_io`, backward-compatible `PLST` envelope).
  Not a V1 effect blocker because §5.5 fallback exists; it is the
  preferred route and a Phase 4 sampler blocker unless the alternate
  side-channel route in sampler spec §7 is used.
- `feature/clap-midi-cc-coverage` — PR `#627` open, CI running. Not
  needed for V1 effect (no MIDI dependency). Needed for Phase 4
  sampler.
- `feature/au-v2-effect-midi-input` — PR queued behind `#627`. Not
  needed for V1 effect or for Phase 4 sampler (the sampler runs as an AU
  instrument, not as an effect).
- `feature/format-skills-clap-vst3-auv3` — 1 commit ahead of main,
  docs-only.

## Verification Snapshot (2026-04-22)

- `git -C /Users/danielraffel/Code/pulp fetch --all --prune` — run.
- `git log --oneline origin/main -10` — head `47e6aeb3`, unchanged.
- `gh issue view 625 -R danielraffel/pulp --json state,title,comments`
  → OPEN, 0 comments.
- No `serialize_plugin_state` / `deserialize_plugin_state` /
  `supplemental_state` / `plugin_state_blob` references in
  `core/format/` or `core/state/`.
- `pulp build && pulp test` in `/Users/danielraffel/Code/spectr`
  → **3/3 passed**.
- Current AU v2 effect MIDI still empty
  (`core/format/src/au_v2_adapter.cpp:252`). Does not block V1 effect.
- Current CLAP event coverage still note on/off + sysex + param value/mod
  only (`core/format/src/clap_adapter.cpp:176`). Does not block V1 effect.
- `ParamInfo` still lacks a visibility / automatable flag
  (`core/state/include/pulp/state/parameter.hpp:52`). The §5.5.4
  automation-lane-size note exists specifically to make this honest.

## What This Signoff Does Not Cover

- Sampler phases: see `Spectr-Sampler-Phase-Spec.md`. That spec is a
  separate contract.
- AU v3: still not a V1 target. AU v2 remains the first Apple format.
- Non-V1 platforms (Windows VST3/CLAP, Linux): stay narrow until macOS
  effect is solid, per handoff §2.

## Clearance Statement

The V1 effect build may proceed. The three named blockers are resolved
in the docs. The day-one punch list above should be walked before any
DSP or UI code is written, and the Phase 0 kickoff note (§5.5.5) must
be committed to the repo before parameter registration is written.
