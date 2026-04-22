# Spectr V2 Product Spec

Status: Draft  
Date: 2026-04-22  
Owner: Daniel Raffel  
Implementation target: Pulp (`/Users/danielraffel/Code/pulp`)  
Project: `/Users/danielraffel/Code/spectr/`

Assumption for this spec: the active upstream Pulp format/MIDI work lands before Spectr implementation begins.

## 1. Product Thesis

Spectr is not an EQ and not a spectrum analyzer.

Spectr is a frequency slicer: a zoomable spectral effect that lets users isolate, remove, and recombine narrow frequency-defined parts of a sound with unusual precision.

The first release ships as an audio effect. It must already feel complete and distinctive on its own, while preserving a clean path to a later sampler mode where frozen spectral results become playable sources.

## 2. Scope Rule

Prototype-visible effect features remain in scope.

Multiple phases are allowed, but phases may only sequence implementation, hardening, and shipping. They may not quietly remove effect features already present in the prototype.

## 3. Product Identity

### What Spectr is

- A precision tool for frequency isolation and recombination
- A creative sound-design instrument for taking a sound apart into spectral pieces
- A modern filter-bank-derived effect with zoomable frequency meaning

### What Spectr is not

- Not a conventional EQ
- Not a mastering EQ
- Not a stem separator
- Not a decorative analyzer
- Not a full sampler in V1

### Core promise

Users should be able to:

- zoom into a small spectral region and keep only that region
- kill targeted regions hard enough that the result feels categorical, not cosmetic
- preserve a few non-adjacent regions while removing most of the rest
- reshape a sound through subtraction and recombination, not just tonal balancing

## 4. V1 Release Definition

Spectr V1 succeeds if it proves four things:

1. It is faster and more decisive than a normal EQ workflow for spectral isolation.
2. It can remove spectral regions deeply enough to feel like slicing.
3. It can recombine non-contiguous spectral regions into musically useful new sounds.
4. It preserves the prototype's visible effect language instead of collapsing into a simpler product.

## 5. Primary Users

- Sound designers who want to extract or suppress very specific spectral regions
- Experimental producers who want to deconstruct and rebuild sounds
- Mixers who want more visual and selective isolation than a standard EQ gives them
- Artists who want to derive several new textures from one source by selective frequency removal

## 6. V1 Feature Contract

### 6.1 Main spectral field

V1 includes:

- discrete vertical spectral bands
- selectable band layouts: `32`, `40`, `48`, `56`, `64`
- explicit per-band gain state
- explicit per-band mute state
- edge-band behavior that feels like low-cut and high-cut boundaries

### 6.2 Viewport and navigation

V1 includes:

- a zoomable logarithmic frequency viewport
- viewport panning
- a full-range overview strip / minimap
- direct overview-drag navigation
- viewport state as part of the recalled working sound

### 6.3 Analyzer and targeting

V1 includes:

- analyzer overlay behind or beneath the bands
- analyzer modes: `Peak`, `Average`, `Both`, `Off`
- hover feedback for frequency and gain targeting
- analyzer behavior that clearly supports editing rather than acting as decoration

### 6.4 Edit interaction

V1 includes:

- single-band drag editing
- paint-across multi-band editing
- mute toggling
- selection and group-edit gestures
- direct gesture-driven shaping rather than menu-first editing

### 6.5 Prototype-visible edit modes

These remain in scope, with prototype keybindings preserved:

- `Sculpt` (S) — direct draw per band
- `Level` (L) — flatten selected/painted bands to one level
- `Boost` (B) — intensify or flatten an existing shape
- `Flare` (F) — exaggerate positive and negative contours away from 0 dB
- `Glide` (G) — during a drag, interpolate current band gains smoothly
  toward a target shape, using the snapshot taken at drag start

**Naming note:** the fifth mode is `Glide`, not `Smooth`. Earlier drafts
mislabelled it. The prototype source is authoritative
(`Spectr-design/Spectr-2/Spectr (standalone source).html:4081` —
`editMode ... // sculpt|level|boost|flare|glide`;
`:3275` — `{ k: 'glide', label: 'GLIDE', hint: 'G' }`).

### 6.6 Prototype-visible action controls

These remain in scope:

- `Reset`
- `Clear`
- `Invert`
- `Mute All`
- `Fit View`

### 6.7 Prototype-visible mode layers

These remain in scope as distinct user-facing surfaces:

- response mode: `Live`, `Precision`
- engine mode: `IIR`, `FFT`, `Hybrid`

### 6.8 Pattern and snapshot workflow

#### Patterns — full prototype-visible control set

The prototype's pattern manager
(`Spectr-design/Spectr-2/src/pattern_manager.jsx`) ships these controls,
all of which are V1 scope:

- **Save current** — captures current band shape as a new user pattern,
  auto-named `PATTERN NN` incrementing from the user pattern count
- **Rename** — user patterns only, inline edit
- **Duplicate** — works on factory or user patterns; produces a user
  pattern named `<source> COPY`
- **Delete** — user patterns only, with confirm
- **Update from current** — overwrite a user pattern with the current
  band shape (user patterns only)
- **Set as default** — marks a pattern with ★; loaded on plugin open
- **Apply** — via double-click on a row, or via an explicit APPLY button
  in the detail pane
- **Search / filter** — free-text match over pattern names
- **Factory vs User list separation** — two labelled sections with per-row
  `F` / `U` badge
- **Import JSON** — three entry points: file picker, clipboard paste button,
  inline paste textarea
- **Export selected** — to file, and to clipboard
- **Export all user patterns** — to file, and to clipboard
- **Chrome rail `PATTERNS ▾` dropdown** — lists factory + user inline with
  a `MANAGE…` entry that opens the full modal

A small factory pattern library ships: `Flat`, `Harmonic series`,
`Alternating`, `Comb`, `Vocal formants`, `Sub only`, `Downward tilt`,
`Air lift`.

#### Snapshots — A/B workflow

- A/B capture slots (prototype: ● dot indicates filled slot)
- Recall A, Recall B
- Morph slider between A and B
- Both snapshot slots are reachable from the bottom rail

#### Separation of concepts

Patterns and snapshots are distinct and must not be merged:

- Patterns store **relative** spectral shape only; they are reusable and
  portable across layouts and viewport settings
- Snapshots capture **full working state** for comparison and morphing

#### Persistence contract

See `Spectr-V2-Pulp-Handoff.md` §5.5 for what survives DAW session
reload under the current Pulp state contract. Short version: the live
working sound state survives; pattern libraries and snapshot banks
survive only when the user explicitly saves a Spectr preset.

## 7. Sound Contract

Spectr may use filter-bank, spectral, or hybrid internals. The product contract is behavioral.

### Precision mode

`Precision` is the truth mode for the product.

It must prioritize:

- narrow-band targeting
- practical silence for muted or fully removed regions
- stable reconstruction
- predictable behavior under extreme cuts, boosts, and zoom

### Live mode

`Live` is the lower-latency mode.

It may relax some precision, but it must preserve:

- the same overall editing language
- clearly recognizable band targeting
- stable playback and interaction while editing

### Engine modes

The visible `IIR`, `FFT`, and `Hybrid` engine selector stays in scope. Internal implementation details may evolve, but the user-facing distinction must survive into V1.

## 8. State And Recall Product Requirements

The product requirement is simple even if the implementation is not:

- current working state must recall reliably
- layout changes must not corrupt meaning
- presets must reload predictably
- A/B workflow must feel trustworthy

At minimum, the following must round-trip correctly in the shipping effect:

- active sound state
- viewport state
- selected band layout
- response mode
- engine mode
- mix and output trim

Rich snapshot and pattern persistence should also survive if the underlying Pulp state contract supports it cleanly before Spectr starts.

## 9. Shipping Targets

V1 shipping target:

- macOS desktop effect

Recommended first formats:

- `VST3`
- `AU v2`
- `CLAP`
- `Standalone` for development and validation

AU v3 is not the first Apple target for V1. It remains a later lane for extension-based Apple distribution or future iOS/iPad ambitions.

## 10. Phase Plan

### Phase 0: Upstream readiness

Lock the Spectr state-route decision before implementation starts.

The nearby Pulp MIDI/format branches may land before Spectr begins, but they are
not V1 effect blockers. The one upstream lane that materially changes Spectr's
preferred implementation route is `danielraffel/pulp#625` (supplemental
plugin-state). If it lands in time, Phase 0 should choose §5.4 in the handoff;
otherwise Spectr uses §5.5 fallback without cutting V1 effect scope.

Exit criteria:

- any nearby upstream branches that matter to the next Spectr phase are either
  landed, consciously deferred, or explicitly marked non-blocking
- Spectr's state contract path is decided
- AU v2 vs AU v3 first-wave format decision stays explicit

### Phase 1: Sound truth and state contract

Build the minimal internal Spectr engine and state path needed to prove:

- neutral or flat state behaves as intended
- muted bands feel like true removal
- viewport and band layout changes remap deterministically
- recall is trustworthy

Exit criteria:

- repeatable DSP acceptance tests exist
- the canonical Spectr state model is fixed
- the product can already prove the frequency-slicer thesis on real material

### Phase 2: Prototype-parity effect

Build the visible effect product to match the prototype scope.

Exit criteria:

- all prototype-visible effect controls are present
- band layouts `32/40/48/56/64` are live
- edit modes, analyzer modes, response modes, engine modes, actions, patterns, snapshots, and morph all exist
- overview strip, hover feedback, selection, and group editing are present

### Phase 3: Hardening and shipping

Polish the effect until it is shippable.

Exit criteria:

- session recall and preset recall are stable
- no obvious click, zipper, or CPU-spike failures in common edit paths
- latency and CPU behavior are characterized by engine/mode
- format builds for the chosen desktop targets are validated

### Phase 4: Sampler bridge

Do not turn V1 into an instrument. Build the seam that makes sampler mode obvious and tractable.

This phase prepares:

- freeze or capture semantics
- spectral-state-to-asset metadata
- playable frozen sources
- later multi-layer or band-group playback

## 11. Success Criteria

Spectr V1 is successful if the team can demonstrate all of the following:

- isolate a narrow band cluster and bounce a clearly reduced or transformed sound
- preserve three or more non-contiguous spectral regions in one gesture-driven workflow
- move between broad shaping and narrow slicing through zoom alone
- switch band layouts without corrupting recall
- compare A and B states and morph between them without the workflow feeling fragile

## 12. Non-Goals For V1

- full sampler or instrument mode
- in-plugin sample library browser
- per-band multi-output routing
- stem export manager
- mid/side workflows
- surround or immersive processing
- AI-assisted suggestions

## 13. Sampler-Forward Constraints

The effect build must leave a clean path to:

- freeze viewport to playable source
- map isolated bands or groups to layers
- route live input versus frozen source cleanly in the future sampler header model

V1 does not build those features yet, but it must not block them architecturally.
