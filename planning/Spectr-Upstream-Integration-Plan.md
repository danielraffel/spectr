# Spectr Upstream Integration Plan

Status: Ready to execute when upstream Pulp PRs merge
Date: 2026-04-22

This plan codifies the exact steps to pick up Pulp upstream work in Spectr
when it lands. Two upstream waves matter:

1. **`#625` supplemental plugin-state** — the preferred-route blocker for
   Spectr's state architecture. Decides §5.4 vs §5.5 in the V2 handoff.
2. **AU v2 + CLAP MIDI coverage** — `#627` (CLAP CC/PB/NE/choke/MIDI2) and
   the queued AU v2 effect MIDI PR. Not V1 effect blockers, but needed
   for the Phase 4 sampler.

## Section A — `#625` Integration Gate

### A.1 When to check

Before starting Milestone 4 (state registration) in the V1 Build Plan.
Also any time the build is paused waiting on `#625`.

### A.2 How to check

```bash
gh issue view 625 -R danielraffel/pulp --json state,closedAt
gh pr list -R danielraffel/pulp --search "625 in:title,body" --state merged --json number,title,mergedAt
git -C /Users/danielraffel/Code/pulp fetch --all --prune
grep -rn "serialize_plugin_state\|deserialize_plugin_state\|plugin_state_io" \
    /Users/danielraffel/Code/pulp/core/format/ \
    /Users/danielraffel/Code/pulp/core/state/
```

### A.3 Decision tree

- **Issue CLOSED, PR merged to `origin/main`, grep returns hits** →
  use `Spectr-V2-Pulp-Handoff.md` §5.4 (preferred route). Proceed to
  Section C for the Pulp upgrade + verification steps, then resume the
  V1 Build Plan at Milestone 4.
- **Issue still OPEN** → do not start Milestone 4. Resume non-state
  work on Milestones 1–3, or pause. Do not hand-roll §5.4 against a
  moving API.
- **Issue was closed-not-planned or scope-changed** → stop and read the
  closing comment. If the API differs from what V2 handoff §5.4 assumes,
  update §5.4 before starting Milestone 4.

### A.4 What §5.4 commits Spectr to (reference)

Per the `#625` peek (2026-04-22, branch `codex/625-supplemental-plugin-state`):

- `virtual std::vector<uint8_t> Processor::serialize_plugin_state() const`
  returning Spectr's `StateTree` JSON blob (variable band layouts, pattern
  library, snapshot banks, analyzer mode, edit mode).
- `virtual bool Processor::deserialize_plugin_state(std::span<const uint8_t>)`
  accepting that blob. Empty span = legacy blob / reset to defaults.
  Return `false` on malformed input.
- Do not call `pulp::format::plugin_state_io::{serialize,deserialize}`
  directly — adapters do that for you.
- Legacy `StateStore`-only blobs still round-trip cleanly because of the
  `PULP` vs `PLST` magic discrimination in the wrapper.

## Section B — AU v2 + CLAP MIDI Coverage Pickup

This is the user-supplied 7-step playbook for when the upstream MIDI work
lands. Not needed for V1 effect (no MIDI dependency). Needed for Phase 4
sampler or any future MIDI-reactive effect features.

### B.1 Upgrade Pulp

```bash
cd /Users/danielraffel/Code/spectr
/Users/danielraffel/Code/pulp/build/tools/cli/pulp upgrade
# or the /upgrade skill if in a Claude session
```

Read the migration notes for the hop. There is a **breaking-but-opt-in**
change in `pulp_add_plugin()` for AU v2 MIDI effects. Without the opt-in,
behaviour is unchanged for existing plugins.

### B.2 Decide if Spectr needs MIDI on CLAP / AU v2

Opt in only if Spectr's processor consumes any of:

- CC messages
- pitch bend
- channel / polyphonic aftertouch
- program change
- sysex
- note-expression
- note-choke events

V1 effect does **not** need MIDI — skip this section until Phase 4 sampler
or later. Phase 4 sampler runs as an AU *instrument* (`aumu`), not an
effect, so even then it is the CLAP lane and the AU v3 / AU instrument
lanes that matter — not AU v2 effect MIDI.

Without the opt-in, hosts still route note-on / note-off, but nothing
else.

### B.3 Opt in at both layers

**a. C++ descriptor.** In `Processor::descriptor()`:

```cpp
PluginDescriptor::accepts_midi = true;
```

Flags CLAP, VST3, AAX, LV2, WAM, and AU v3 to route MIDI.

**b. CMake (AU v2 only).** In `CMakeLists.txt`, add `ACCEPTS_MIDI` to
`pulp_add_plugin(...)`:

```cmake
pulp_add_plugin(Spectr
    FORMATS         VST3 AU CLAP Standalone
    ACCEPTS_MIDI
    ...
)
```

This flips the emitted AU component type from `aufx` (plain effect — host
won't deliver MIDI) to `aumf` (`kAudioUnitType_MusicEffect` — MIDI-accepting
effect). Without this, step (a) is a no-op for AU v2 because the bundle's
`.plist` tells Apple's AU cache there is no MIDI input.

### B.4 AU component-cache flush (one-time, AU v2 only)

Changing a plug-in's AU type from `aufx` to `aumf` requires DAWs to
re-scan:

```bash
killall -9 AudioComponentRegistrar
auval -a | grep -i spectr   # warm-cache + sanity-check
```

Logic / Live / etc. may also need their own plug-in re-scan.

### B.5 Validate

```bash
cd /Users/danielraffel/Code/spectr
/Users/danielraffel/Code/pulp/build/tools/cli/pulp validate
```

Runs `clap-validator` + `auval` + (optionally) `pluginval`. Should now
show MIDI input ports and pass traffic correctly.

### B.6 Known gaps to plan around

- **AU v2 effect MIDI output is NOT yet shipped** (tracked as `pulp#626`).
  If Spectr's processor writes to its `midi_out` buffer and that MIDI must
  reach the DAW via AU v2, either defer that feature or prefer CLAP / VST3
  / AU v3 for MIDI-producing scenarios.
- **CLAP outbound MIDI IS shipped** — `midi_out` shorts and sysex flow
  through `out_events->try_push` now.
- **Note-expression on CLAP** is translated to MIDI 1.0 equivalents for
  the tracker:
  - pressure → channel aftertouch
  - tuning → pitch bend
  - brightness → CC 74
  - volume → CC 7
  - pan → CC 10
  - vibrato / expression IDs fall through
  UMP-aware consumers should use the MIDI 2.0 UMP stream on
  `CLAP_EVENT_MIDI2`, not note-expressions, for those.

### B.7 Format-specific skills

Pulp now has per-format skills at `.agents/skills/{clap,vst3,auv2,auv3}/SKILL.md`.
When you need adapter-level help, reference the matching one explicitly.
The skill-sync gate in Pulp will force updates to these if adapters
change, so they stay current.

## Section C — Combined Pickup Day Checklist

When all three upstream items have landed (`#625`, CLAP MIDI CC, AU v2
effect MIDI), run this in order:

1. `git -C /Users/danielraffel/Code/pulp fetch --all --prune`
2. `git -C /Users/danielraffel/Code/pulp log --oneline origin/main -10` — confirm heads.
3. Section A checks — confirm `#625` hooks are on `main`.
4. Section B.1 — `pulp upgrade` and read migration notes.
5. For Spectr V1 effect: only Section A matters. Resume the V1 Build
   Plan at Milestone 4 with §5.4.
6. For Phase 4 sampler preparation: Section B.2 through B.5 for the
   sampler lane (CLAP instrument first).
7. Run `pulp build && pulp test && pulp validate` in the Spectr
   project. All should pass before declaring pickup complete.
8. Close the Spectr pickup tracking issue on this repo with a short
   summary of what landed and what changed in Spectr.

## Section D — If `#625` Stalls

**Explicit direction from the product owner (2026-04-22): do NOT
implement Spectr under `Spectr-V2-Pulp-Handoff.md` §5.5.** Wait for
`#625` to land and use §5.4. This is the plan of record.

If `#625` stalls:

- Keep Spectr's V1 Build Plan parked at Milestone 4 (state registration).
- Continue polling the integration gate per §A.2.
- If the wait is long enough to matter, use the idle time on
  route-agnostic work: polish Milestones 1–3, add tests, document, tune
  the DSP truth spike. Do not pre-register parameters under the §5.5
  shape.
- Escalate to the product owner before reconsidering §5.5. The migration
  cost from §5.5 to §5.4 (DAW session compat shim, parameter-surface
  reshape) is the specific reason the direction is "wait."

§5.5 remains in the handoff as documented history of the fallback
contract, not as an approved build path.
