#include "spectr/snapshot.hpp"

#include <algorithm>
#include <cmath>

namespace spectr {

void morph_fields(BandField& out,
                  const BandField& a,
                  const BandField& b,
                  float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    const bool b_dominant = (t >= 0.5f);

    for (std::size_t i = 0; i < kMaxBands; ++i) {
        const auto& ai = a.bands[i];
        const auto& bi = b.bands[i];
        out.bands[i].gain_db = ai.gain_db + (bi.gain_db - ai.gain_db) * t;
        out.bands[i].muted   = b_dominant ? bi.muted : ai.muted;
    }
}

Viewport morph_viewports(const Viewport& a,
                         const Viewport& b,
                         float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    const bool b_dominant = (t >= 0.5f);

    // An endpoint that cannot be expressed on the log axis cannot be
    // interpolated through. Fall back to the dominant slot rather than
    // inventing a window neither slot ever had.
    if (!a.valid() || !b.valid()) {
        const Viewport& dominant = b_dominant ? b : a;
        return dominant.valid() ? dominant : Viewport{};
    }
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;

    // Interpolate the log-frequency bounds, not the frequencies. This is the
    // axis the viewport is actually drawn and masked on, so a constant t-rate
    // is a constant ratio-rate: the sweep reads as uniform motion across the
    // display instead of stalling in the bottom decades.
    const float lmin = std::log(a.min_hz)
        + (std::log(b.min_hz) - std::log(a.min_hz)) * t;
    const float lmax = std::log(a.max_hz)
        + (std::log(b.max_hz) - std::log(a.max_hz)) * t;

    Viewport out;
    out.min_hz = std::exp(lmin);
    out.max_hz = std::exp(lmax);
    // Both endpoints are valid and the exponential is monotone, so this holds
    // analytically; it is asserted here because float rounding at an extreme
    // ratio is the one way it could not, and a rejected viewport downstream
    // fails the DSP mask closed.
    if (!out.valid()) return b_dominant ? b : a;
    return out;
}

} // namespace spectr
