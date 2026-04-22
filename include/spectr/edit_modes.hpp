#pragma once

// Edit modes — the five gestural paint behaviours from the prototype.
//
//   Sculpt (S) — direct draw per band
//   Level  (L) — flatten selected/painted bands to one level
//   Boost  (B) — intensify or flatten an existing shape
//   Flare  (F) — exaggerate positive and negative contours away from 0 dB
//   Glide  (G) — during a drag, interpolate toward a target shape using
//                the snapshot taken at drag start
//
// Naming note: the prototype's fifth mode is `Glide`, not `Smooth`. See
// planning/Spectr-V2-Product-Spec.md §6.5.

#include <cstdint>

namespace spectr {

enum class EditMode : std::uint8_t {
    Sculpt = 0,
    Level  = 1,
    Boost  = 2,
    Flare  = 3,
    Glide  = 4,
};

/// Returns the keybinding character for an edit mode. Uppercase ASCII.
constexpr char keybinding(EditMode m) noexcept {
    switch (m) {
        case EditMode::Sculpt: return 'S';
        case EditMode::Level:  return 'L';
        case EditMode::Boost:  return 'B';
        case EditMode::Flare:  return 'F';
        case EditMode::Glide:  return 'G';
    }
    return 'S';
}

/// Returns the short display label for an edit mode.
constexpr const char* label(EditMode m) noexcept {
    switch (m) {
        case EditMode::Sculpt: return "Sculpt";
        case EditMode::Level:  return "Level";
        case EditMode::Boost:  return "Boost";
        case EditMode::Flare:  return "Flare";
        case EditMode::Glide:  return "Glide";
    }
    return "Sculpt";
}

} // namespace spectr
