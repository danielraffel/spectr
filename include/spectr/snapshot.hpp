#pragma once

// Snapshot A/B + morph (Milestone 8).
//
// Spectr captures the complete sound-defining state into two slots
// (A and B) and can interpolate between them on the canonical 64-slot
// band model. The bank itself is plugin-owned and rides the
// supplemental plugin-state blob (§5.4 of the V2 handoff), so it
// survives host session reload alongside the live BandField and
// viewport. StateStore snapshots of the flat params are a separate
// concern handled by `pulp::view::ABCompare` over the processor's
// StateStore — see Spectr::ab_compare().
//
// For V1, a "snapshot" holds:
//   - the 64-slot BandField (per-band gain_db + muted)
//   - the viewport bounds
//   - the selected layout (so switching layouts doesn't quietly drop
//     bands during a morph)
//
// Morph interpolates per-band gain_db linearly in dB space (matches
// the slider's mental model: at t=0.5 the gain sits halfway between
// A and B in dB terms). Mute state is picked from whichever slot
// dominates at the current t (A below 0.5, B at/above). Viewport and
// layout do NOT morph continuously — they snap to whichever slot
// dominates to avoid nonsensical fractional band counts.

#include <array>
#include <cstddef>

#include "spectr/band_state.hpp"
#include "spectr/viewport.hpp"

namespace spectr {

/// Complete sound-defining state captured into a single slot.
struct FieldSnapshot {
    BandField field{};
    Viewport  viewport{};
    Layout    layout = Layout::Bands32;

    /// True once the slot has been populated at least once. Empty
    /// slots should not be fed to morph_fields; dispatch guards
    /// against it and falls back to the populated side.
    bool populated = false;
};

/// Two-slot snapshot bank.
struct SnapshotBank {
    enum class Slot : std::uint8_t { A = 0, B = 1 };

    FieldSnapshot a{};
    FieldSnapshot b{};

    /// Which slot is considered "active" for edits. UI decides what
    /// this means visually; the bank just stores the bit so the
    /// selection survives session reload.
    Slot active = Slot::A;

    FieldSnapshot&       get(Slot s)       noexcept { return s == Slot::A ? a : b; }
    const FieldSnapshot& get(Slot s) const noexcept { return s == Slot::A ? a : b; }

    /// Copy `current` into the named slot and mark it populated.
    void capture_into(Slot s, const BandField& current_field,
                      const Viewport& current_viewport,
                      Layout current_layout) noexcept {
        auto& dst = get(s);
        dst.field     = current_field;
        dst.viewport  = current_viewport;
        dst.layout    = current_layout;
        dst.populated = true;
    }

    /// Copy src → dst (mirroring `pulp::view::ABCompare::copy`).
    void copy(Slot src, Slot dst) noexcept {
        if (src == dst) return;
        get(dst) = get(src);
    }

    /// Swap A and B in place.
    void swap() noexcept { std::swap(a, b); }

    bool has(Slot s) const noexcept { return get(s).populated; }
};

/// Interpolate two BandFields at t ∈ [0, 1] into `out`. Values outside
/// the range are clamped. Per-band gain_db is a simple linear lerp in
/// dB space; mute state is A below 0.5 and B at/above 0.5.
///
/// Safe to call with `out` aliasing `a` or `b` — every slot is read
/// before the corresponding slot is written.
void morph_fields(BandField& out,
                  const BandField& a,
                  const BandField& b,
                  float t) noexcept;

} // namespace spectr
