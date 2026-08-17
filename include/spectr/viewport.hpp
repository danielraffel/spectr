#pragma once

// Log-frequency viewport.
//
// Spectr maps the visible N bands across a log-Hz window defined by
// [min_hz, max_hz]. Zooming/panning changes band-to-frequency meaning, so the
// viewport is part of the sound-defining state — not ephemeral camera state.

#include <cmath>
#include <cstddef>

namespace spectr {

struct Viewport {
    float min_hz = 20.0f;
    float max_hz = 20000.0f;

    bool valid() const noexcept {
        return min_hz > 0.0f && max_hz > min_hz && max_hz <= 192000.0f;
    }

    /// Frequency (Hz) at the center of band `i`. The viewport bounds are the
    /// outer edges of N equal log-frequency partitions; the first/last bands
    /// may additionally own frequencies outside the viewport in the DSP mask.
    float band_center_hz(std::size_t i, std::size_t n) const noexcept {
        if (n == 0) return min_hz;
        const float lmin = std::log(min_hz);
        const float lmax = std::log(max_hz);
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        return std::exp(lmin + t * (lmax - lmin));
    }

    /// Band index containing a given frequency, for a layout with `n`
    /// visible bands. Clamps to [0, n-1].
    std::size_t band_for_hz(float hz, std::size_t n) const noexcept {
        if (n == 0) return 0;
        if (hz <= min_hz) return 0;
        if (hz >= max_hz) return n - 1;
        const float lmin = std::log(min_hz);
        const float lmax = std::log(max_hz);
        const float t = (std::log(hz) - lmin) / (lmax - lmin);
        const auto idx = static_cast<std::size_t>(t * static_cast<float>(n));
        return idx >= n ? n - 1 : idx;
    }
};

} // namespace spectr
