# Spectr Build Blockers

**Status: SUPERSEDED by `Spectr-Build-Signoff.md` on 2026-04-22.**
**All three blockers (B1, B2, B3) are resolved in the planning docs.**
**This file is retained as a historical record of the blocker pass.**

Original date: 2026-04-22

Original build status (now superseded): Not cleared to start implementation.

## Critical blockers

- `B1 — Edit-mode contract is not internally consistent.` `Spectr-V2-Product-Spec.md:107-116` keeps `Sculpt`, `Level`, `Boost`, `Flare`, `Smooth`, but the current standalone prototype exposes `glide` as the fifth mode (`/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr (standalone).html:208`, `editMode ... // sculpt|level|boost|flare|glide`). What must change: decide whether V1 ships `Glide` or `Smooth`, then align the product spec, handoff, and review notes to that exact mode name and behavior before coding starts.

- `B2 — V2 under-specifies the visible pattern workflow.` V2 §6.8 only promises reusable patterns, factory patterns, save/rename/duplicate/delete, snapshots, and morph (`Spectr-V2-Product-Spec.md:134-147`). The prototype visibly includes more than that: `IMPORT FILE`, `PASTE JSON`, inline paste/import, `SET AS DEFAULT`, `UPDATE FROM CURRENT`, `EXPORT (FILE)`, `EXPORT (CLIP)`, `EXPORT ALL (FILE)`, and `EXPORT ALL (CLIP)` (`/Users/danielraffel/Code/spectr-design/Spectr-2/src/pattern_manager.jsx:1`, `215-218`, `228-242`, `254-256`, `274-275`, `396-402`). What must change: either add these controls to the V1 contract or explicitly mark them prototype-only. The current package does neither, so a builder could incorrectly cut visible prototype workflow.

- `B3 — The fallback recall contract is still ambiguous if Pulp issue #625 stays open.` The handoff correctly says the preferred route needs a supplemental plugin-state path, and the fallback keeps current sound state in `StateStore` while restricting which richer workflow constructs survive host reload (`Spectr-V2-Pulp-Handoff.md:130-152`). `danielraffel/pulp#625` is still `OPEN`, with no comments, and no supplemental hook has landed in `core/format/`. What must change: before implementation starts, write down the exact fallback recall guarantee. At minimum, state whether patterns, A/B slots, and any other workflow state survive DAW session recall, Spectr preset recall only, or runtime only. Without that boundary the builder has to invent product behavior on day one.

## Optional corrections

- `O1 — V3 review notes now overstate prototype parity.` `Spectr-V3-Review-Notes.md:52-59` says every prototype-visible surface is represented, but B1 and B2 show that is no longer defensible. This is not a separate framework blocker, but it does make the package internally inconsistent for a fresh implementer.

- `O2 — Prototype chrome is still only partially captured.` The visible top/bottom chrome includes a help affordance, status banner, zoom readout, and `120fps` status (`/Users/danielraffel/Code/spectr-design/Spectr-2/src/chrome.jsx:62-104`, `185-220`). If these are intended shipping UI, say so; if not, mark them prototype-only so they do not drift into accidental scope.

## Six audit answers

1. `Is the Spectr planning package internally consistent?`
No. The main inconsistency is prototype parity: V2 and V3 both present parity as closed, but the current prototype still contains surfaces not captured cleanly by the package.

2. `Is every prototype-visible effect feature still in scope?`
No, not as written. The core effect thesis is intact, but the fifth edit mode and several visible pattern-manager controls are not fully represented in the current V2 contract.

3. `Has anything in Pulp moved since V3 was written?`
Only a docs-only branch moved. `feature/format-skills-clap-vst3-auv3` is now `0 1` ahead of `origin/main` at `0c6ef845`; `feature/clap-midi-cc-coverage` and `feature/au-v2-effect-midi-input` are still `0 0`. `origin/main` is still headed by `47e6aeb3`. `danielraffel/pulp#625` is still open.

4. `If #625 has not landed, does V2 §5.5 fallback still get Spectr to the V1 feature contract without cutting scope?`
Yes, probably, for the live sound contract: active spectral state, viewport, layout, response mode, engine mode, mix, and trim can still be made recall-safe through `StateStore`. The ambiguity is richer workflow recall. The package still needs to say exactly what host-session recall guarantees apply to patterns and snapshots under fallback.

5. `Is AU v2 still the right first Apple format, given current code?`
Yes. The Spectr scaffold already builds `AU` alongside `VST3`, `CLAP`, and `Standalone` (`/Users/danielraffel/Code/spectr/CMakeLists.txt:25-26`), the current build passes, and nothing in current Pulp makes `AU v3` a better first lane for a desktop-first V1 effect.

6. `Are there any missing decisions the build team will hit on day one?`
Yes: pick `Glide` vs `Smooth`; decide whether visible pattern import/export/default/update controls are V1 scope or prototype-only; and lock the fallback recall guarantee if `#625` is still open when Spectr begins.

## Current verification snapshot

- `Pulp main:` `git fetch --all --prune && git log --oneline origin/main -10` still shows `47e6aeb3` at the tip.
- `Upstream branches:` `feature/clap-midi-cc-coverage = 0 0`, `feature/au-v2-effect-midi-input = 0 0`, `feature/format-skills-clap-vst3-auv3 = 0 1`.
- `Issue state:` `gh issue view 625 -R danielraffel/pulp --json state,title,comments` returns `OPEN` with zero comments.
- `Current Pulp gaps re-verified:` AU v2 effect MIDI path still empty (`/Users/danielraffel/Code/pulp/core/format/src/au_v2_adapter.cpp:252-258`), CLAP still lacks full MIDI/CC coverage (`/Users/danielraffel/Code/pulp/core/format/src/clap_adapter.cpp:176-239`), `PresetManager` still drops the serialized blob (`/Users/danielraffel/Code/pulp/core/state/src/preset_manager.cpp:76-99`), `ParamInfo` still has no visibility flag (`/Users/danielraffel/Code/pulp/core/state/include/pulp/state/parameter.hpp:52-59`), and no supplemental processor-owned plugin-state hook exists in `core/format/`.
- `Spectr scaffold:` `pulp build && pulp test` passes locally. Built formats: `VST3`, `CLAP`, `AU`, `Standalone`. Tests passed: `3/3`.

No new GitHub issue was filed in this pass. The only framework-level gap that matters here is already tracked as `danielraffel/pulp#625`.

## Addendum — 2026-04-22 sampler hardening pass

A dedicated sampler-phase spec was added at
`/Users/danielraffel/Code/spectr/planning/Spectr-Sampler-Phase-Spec.md`.
It hardens Phase 4/5/6 scope from seam-preservation prose into a concrete
product contract (Capture → Freeze → Play, rendered-sample decision,
§4.2 UI enumeration drawn from the Sampler prototype's settings blob and
`chrome.jsx`). It explicitly pins the `#625` / side-channel decision as a
Phase 4 kickoff gate, which narrows B3 for sampler-side scope.

B3 is still a blocker for V1 effect work because the effect-side fallback
recall guarantee (which host-session data survives under fallback) must
still be written down before V1 implementation begins. The sampler spec
only handles the sampler side of that same question.

Verification re-ran on the same day:

- `origin/main` still at `47e6aeb3`.
- Upstream branches unchanged: clap-midi-cc `0 0`, au-v2-effect-midi `0 0`, format-skills `0 1` (docs-only).
- `danielraffel/pulp#625` still `OPEN`, no comments.
- `pulp build && pulp test` in `/Users/danielraffel/Code/spectr` → 3/3 passed.

Verdict unchanged: **build not cleared.** B1 and B2 still require spec
text changes; B3 still requires a written-down fallback recall guarantee.
