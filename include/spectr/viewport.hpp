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

    /// Frequency (Hz) at band index `i` of a layout with `n` visible bands.
    /// Uses log-spaced centres; edge bands sit at min_hz and max_hz.
    float band_center_hz(std::size_t i, std::size_t n) const noexcept {
        if (n <= 1) return min_hz;
        const float lmin = std::log(min_hz);
        const float lmax = std::log(max_hz);
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
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
        const auto idx = static_cast<std::size_t>(t * static_cast<float>(n - 1) + 0.5f);
        return idx >= n ? n - 1 : idx;
    }
};

} // namespace spectr
