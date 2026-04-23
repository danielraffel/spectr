#pragma once

// Edit engine — applies the prototype's five edit modes to a BandField.
//
// The modes come from planning/Spectr-V2-Product-Spec.md §6.5 and the
// prototype source (spectr-design/Spectr-2/Spectr (standalone source).html).
// Each mode transforms the canonical BandField in response to a drag
// gesture: start at band S with value S_v, drag to band C with value C_v,
// against an optional snapshot of gains-at-drag-start.
//
// Boost, Flare, and Glide use the snapshot as their baseline so the
// gesture is stateless after release — subsequent drags start from the
// snapshot taken at their own drag-start.

#include <array>
#include <cstddef>

#include "spectr/band_state.hpp"
#include "spectr/edit_modes.hpp"

namespace spectr {

/// A linear snapshot of canonical band gains — 64 slots of dB. Captured
/// at drag start and passed to edit-mode dispatch so Boost / Flare /
/// Glide stay stateless across the drag.
struct BandSnapshot {
    std::array<float, kMaxBands> gain_db{};
    std::array<bool,  kMaxBands> muted{};

    static BandSnapshot capture(const BandField& f) noexcept {
        BandSnapshot s{};
        for (std::size_t i = 0; i < kMaxBands; ++i) {
            s.gain_db[i] = f.bands[i].gain_db;
            s.muted[i]   = f.bands[i].muted;
        }
        return s;
    }
};

/// Drag gesture state at the moment an edit is dispatched. `start_*`
/// captures where the user pressed; `current_*` is where they are now.
/// Values are in dB in [-60, +12] unless a mode reinterprets them.
struct DragGesture {
    std::size_t start_band   = 0;
    float       start_value  = 0.0f;
    std::size_t current_band = 0;
    float       current_value= 0.0f;

    /// Number of visible slots under the current layout. Paint / Level /
    /// Glide all clamp operations to [0, n_visible).
    std::size_t n_visible    = 32;
};

/// Dispatch an edit mode. Mutates `field` in place.
///
/// The behaviours match the prototype exactly — see the source file
/// referenced at the top — and are also unit-tested under
/// test/test_edit_engine.cpp.
void dispatch_edit(EditMode            mode,
                   BandField&          field,
                   const DragGesture&  drag,
                   const BandSnapshot& snapshot_at_drag_start) noexcept;

// ── Individual mode primitives (exposed for tests and future UI hooks) ──

/// Sculpt: each band the drag passes through is painted to current_value.
void apply_sculpt(BandField& field, const DragGesture& drag) noexcept;

/// Level: flatten every band in the drag's swept range to current_value.
void apply_level(BandField& field, const DragGesture& drag) noexcept;

/// Boost: multiply each snapshot gain toward +/- direction proportional to
/// drag distance. Extreme drags push close to the ±limits.
void apply_boost(BandField& field,
                 const DragGesture& drag,
                 const BandSnapshot& snap) noexcept;

/// Flare: exaggerate the distance from 0 dB in the snapshot — positive
/// bands go more positive, negative bands more negative, scaled by drag
/// distance from start.
void apply_flare(BandField& field,
                 const DragGesture& drag,
                 const BandSnapshot& snap) noexcept;

/// Glide: interpolate each band smoothly toward current_value from its
/// snapshot baseline, weighted by the drag distance. Drag distance of 0
/// keeps snapshot; drag distance of "full height" lands at current_value.
void apply_glide(BandField& field,
                 const DragGesture& drag,
                 const BandSnapshot& snap) noexcept;

} // namespace spectr
