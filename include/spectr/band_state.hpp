#pragma once

// Canonical band state model.
//
// Spectr always carries 64 canonical band slots regardless of the visible
// layout (32/40/48/56/64). Switching layouts is a projection onto the first
// N canonical slots — not a reshaping of the state model. Slots above N
// are held at neutral (0 dB, un-muted) while unused.
//
// See planning/Spectr-V2-Pulp-Handoff.md §5.4 / §6.1 for the rationale.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace spectr {

constexpr std::size_t kMaxBands = 64;

/// Per-band working state.
struct Band {
    float gain_db = 0.0f;   ///< in [-60, +12]
    bool  muted   = false;  ///< explicit; never "gain == -inf"
};

/// The canonical 64-slot band field.
struct BandField {
    std::array<Band, kMaxBands> bands{};

    /// Reset every slot to neutral (0 dB, not muted).
    void reset() noexcept {
        for (auto& b : bands) { b.gain_db = 0.0f; b.muted = false; }
    }

    /// Linear gain multiplier for a slot. Muted slots return 0.
    float linear_gain(std::size_t i) const noexcept {
        if (i >= kMaxBands) return 1.0f;
        const auto& b = bands[i];
        if (b.muted) return 0.0f;
        // dB → linear. 20 dB/decade.
        return std::pow(10.0f, b.gain_db * 0.05f);
    }
};

/// Selectable layout — the visible band count.
enum class Layout : std::uint8_t {
    Bands32 = 32,
    Bands40 = 40,
    Bands48 = 48,
    Bands56 = 56,
    Bands64 = 64,
};

inline std::size_t visible_count(Layout L) noexcept {
    return static_cast<std::size_t>(L);
}

} // namespace spectr
