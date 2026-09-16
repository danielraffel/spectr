#pragma once

// Spectr's domain half of the macro overlay: what a macro value MEANS when
// the slots are band gains.
//
// `param_macro.hpp` owns the summing and nothing else. This file owns the
// three rules that need to know what a band is, and it is the ONLY place they
// are written — the audio thread and the control thread both reach the
// audible field through this function, so a rule stated here cannot drift
// between what is heard and what is drawn.
//
//   - A macro adds dB to its members, SHAPE-PRESERVING. It is the same act
//     as the editor's existing group drag: every member moves by the same
//     amount, so the contour the user drew inside the group survives. A
//     macro that set an absolute level would flatten it.
//   - Offsets SUM and are clamped ONCE. A slot in two macros gets both, then
//     one clamp into the band range. Per-macro clamping would make the result
//     depend on visit order; summing first keeps it commutative.
//   - A muted member gets NO offset, and a macro NEVER toggles mute. Mute is
//     a discrete act the user performed, and a macro modulates levels — the
//     same separation `preserve_authored_mutes` enforces for the LFOs, for
//     the same reason: a band the user silenced must stay silent, and must
//     come back at its authored level rather than at whatever the macro
//     happened to be worth.
//
// Slots at or above the visible count are inert here exactly as they are
// everywhere else in the surface. They are not an error — membership is kept
// across a layout change, so a member that scrolls out of view and back
// returns to the same macro.
//
// NON-DESTRUCTIVE. This never writes a band-gain parameter lane. Like the
// internal LFOs, a macro composes onto the field on its way to the mask and
// leaves canonical state untouched. Writing back would echo up to 64 host
// lanes per macro move — which is the exact problem macros exist to remove.

#include "spectr/band_state.hpp"
#include "spectr/param_macro.hpp"

#include <algorithm>
#include <cstddef>

namespace spectr {

/// Host-automatable macros. Four is a deliberate ceiling, not a placeholder:
/// each one costs a permanent parameter ID and a row in every host's
/// automation list, and four covers the "drive a few scattered groups" case
/// the feature was asked for. The reserved ID tail in `param_surface.hpp`
/// leaves room to raise it without moving anything already shipped.
inline constexpr std::size_t kMacroCount = 4;

using BandMacroBank = MacroBank<kMacroCount, kMaxBands>;

/// Compose the macro overlay onto @p field in place.
///
/// @p visible is the active layout's band count; slots at or above it are
/// inert. @p bank carries membership (editor state) and values (host
/// parameter lanes) for the same instant.
///
/// A slot no macro reaches is left BIT-IDENTICAL — the offset is exactly
/// zero and the add is skipped rather than performed with a zero, so this
/// function cannot perturb a field that has no macros assigned. That matters:
/// it runs on every block, and an inert feature must be provably inert.
inline void apply_macro_offsets(BandField& field, std::size_t visible,
                                const BandMacroBank& bank) noexcept {
    const auto offsets = resolve_macro_offsets(
        bank, [&field, visible](std::size_t, std::size_t slot,
                                float value) -> float {
            if (slot >= visible) return 0.0f;
            // Read before any write below; `resolve_macro_offsets` completes
            // the whole sum before this function touches a gain.
            if (field.bands[slot].muted) return 0.0f;
            return value;
        });
    for (std::size_t slot = 0; slot < kMaxBands; ++slot) {
        if (offsets[slot] == 0.0f) continue;
        field.bands[slot].gain_db =
            std::clamp(field.bands[slot].gain_db + offsets[slot],
                       kBandGainMinDb, kBandGainMaxDb);
    }
}

} // namespace spectr
