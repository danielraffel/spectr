# Spectr Planning

Working planning package for Spectr — a zoomable frequency slicer built on Pulp.

This package lives inside the Spectr project so the review agent, the build
agent, and the developer can all work from a single source of truth.

## Documents

- `Spectr-V1-Product-Spec.md` — original v1 product spec.
- `Spectr-Pulp-Handoff.md` — original v1 implementation handoff.
- `Spectr-V2-Product-Spec.md` — current product-facing v2 spec.
- `Spectr-V2-Pulp-Handoff.md` — current implementation-facing v2 handoff.
- `Spectr-V2-Review-Notes.md` — audit of v1 against current Pulp and the prototypes.
- `Spectr-Review-Agent-Prompt.md` — original prompt used to request the v2 pass.
- `Spectr-V3-Review-Agent-Prompt.md` — follow-up prompt for a reviewer to confirm v2 or generate v3 if needed.
- `Spectr-V3-Review-Notes.md` — v3 reviewer's confirmation that v2 is materially correct, plus the GitHub issue filed against Pulp (`danielraffel/pulp#625`) for the one framework capability still needed.
- `Spectr-Build-Signoff.md` — **current build-clearance status (2026-04-22): V1 effect implementation is cleared to start.** Summarizes B1/B2/B3 resolutions and lists the day-one punch list.
- `Spectr-Build-Blockers.md` — historical blocker record; superseded by the signoff above.
- `Spectr-Sampler-Phase-Spec.md` — hardened Phase 4/5/6 sampler spec (Capture → Freeze → Play); replaces the thin seam-preservation language in V2 §13 with a concrete product contract.
- `Spectr-UI-Park-Notes.md` — why the V1 editor UI is parked, the `danielraffel/pulp#651` framework gap, and the resume checklist.

## Source Inputs

Design / prototype references (external):

- Prototype effect: `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr (standalone).html`
- Prototype sampler: `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr Sampler.html`
- Effect notes: `/Users/danielraffel/Code/spectr-design/Spectr-2/effect-ideas.txt`
- Sampler notes: `/Users/danielraffel/Code/spectr-design/Spectr-2/sampler-ideas.txt`
- Prototype source: `/Users/danielraffel/Code/spectr-design/Spectr-2/src/`

Framework:

- Pulp repo: `/Users/danielraffel/Code/pulp`
- Pulp docs: `/Users/danielraffel/Code/pulp/docs/`
- Pulp packages catalog: `/Users/danielraffel/Code/pulp/docs/guides/packages/`

Spectr project:

- Scaffolded project: `/Users/danielraffel/Code/spectr/`
- Plugin header: `/Users/danielraffel/Code/spectr/spectr.hpp`
- CMake: `/Users/danielraffel/Code/spectr/CMakeLists.txt`
- Plugin manifest: `/Users/danielraffel/Code/spectr/pulp.toml`

## Current Position

Spectr should ship first as a distinctive audio effect:

- zoomable frequency slicing
- true band removal
- analyzer-guided isolation
- recombination of non-contiguous spectral regions

The sampler is not V1, but the V1 effect must preserve a clean path to:

- capture
- freeze
- play

Current planning position:

- the planning package is build-clear for the V1 effect
- the team may still choose to let nearby upstream Pulp work land first before
  coding begins
- the CLAP CC / AU v2 effect-MIDI branches are **not** V1 effect blockers; they
  matter for later sampler/instrument phases
- `danielraffel/pulp#625` (supplemental plugin-state blob) is the one upstream
  lane that changes Spectr's preferred state route: if it lands in time, use
  V2 handoff §5.4; otherwise use §5.5 fallback

## How To Use This Package

1. Read `Spectr-V2-Product-Spec.md` for the current product contract.
2. Read `Spectr-V2-Pulp-Handoff.md` for the current implementation guidance.
3. Read `Spectr-V2-Review-Notes.md` to understand the state-contract and format-level caveats.
4. Read `Spectr-V3-Review-Notes.md` for the most recent reviewer's confirmation of v2 and the framework-issue status (`danielraffel/pulp#625`).
5. Read `Spectr-Build-Signoff.md` — V1 effect is cleared; walk the day-one punch list before writing code.
6. Read `Spectr-Sampler-Phase-Spec.md` before planning Phase 4/5 sampler work.
7. Phase 0 kickoff must record which recall route is active (V2 handoff §5.4 preferred via `#625` vs §5.5 fallback) before parameter registration begins.
