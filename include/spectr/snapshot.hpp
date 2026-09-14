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
// The governing rule is: a snapshot captures EVERYTHING, and morph
// selectively applies the dimensions it can carry continuously.
// Capture is never the place to decide what morph does — a capture-time
// choice discards information the user cannot get back, and leaves the
// ambiguous case where A and B were captured under different choices.
//
//   - gain_db   morphs. Linear in dB space, matching the slider's mental
//               model: at t=0.5 the gain sits halfway between A and B in
//               dB terms.
//   - muted     does not interpolate — it is a discrete sentinel, not a
//               level. It is picked from whichever slot dominates at the
//               current t (A below 0.5, B at/above).
//   - viewport  morphs, in LOG-frequency space, and only when the caller
//               asks for it (see `morph_viewports`). The frequency axis is
//               logarithmic, so a linear lerp of min_hz/max_hz would crawl
//               through the low end, which is where most of the visual and
//               audible action is. In Spectr the viewport is sound-defining
//               state, not camera state — it sets the band↔frequency mapping
//               the spectral mask is built from — so morphing it changes
//               what is heard, not merely what is drawn.
//   - layout    does NOT morph. Band count is discrete, and the five
//               selectable counts (32/40/48/56/64) do not nest on a common
//               band grid, so there is no lossless representation of two
//               different layouts to interpolate between. Morph therefore
//               leaves the active layout exactly where it is.

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

    /// Empty the named slot.
    ///
    /// The whole slot goes back to its default, not just the flag: a cleared
    /// slot and a slot that was never captured are the same thing to every
    /// reader, and leaving a stale field behind one would mean a later
    /// `populated = true` resurrected data the user believed gone. Clearing
    /// an already-empty slot is a no-op rather than an error, so a caller
    /// that clears both slots need not ask which were filled.
    void clear(Slot s) noexcept { get(s) = FieldSnapshot{}; }

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

/// Interpolate two viewports at t ∈ [0, 1]. Values outside the range are
/// clamped.
///
/// The interpolation is performed on log(min_hz) and log(max_hz), then
/// converted back, so a sweep moves by a constant RATIO per unit t rather
/// than by a constant number of Hz. Concretely: morphing 20 Hz → 2000 Hz
/// passes through 200 Hz at the midpoint, not 1010 Hz. A linear lerp would
/// spend almost the whole sweep in the top octave.
///
/// An invalid endpoint cannot be interpolated through, so the dominant slot
/// (A below t=0.5, B at/above) is returned unchanged in that case; if both
/// are invalid the default viewport is returned. The result always satisfies
/// `Viewport::valid()`.
Viewport morph_viewports(const Viewport& a,
                         const Viewport& b,
                         float t) noexcept;

} // namespace spectr
