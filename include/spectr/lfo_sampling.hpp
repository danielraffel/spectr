#pragma once
/// @file lfo_sampling.hpp
/// Spectr's LFO shapes sampled without aliasing, on Pulp's
/// `pulp/signal/lfo_sampling.hpp`: the exact mean over a phase interval for a
/// destination that takes one value per block, the band-limited value for one
/// that takes a value every sample, and the Freeze gate read over an
/// interval. Kept apart from modulation.hpp, which stays free of Pulp
/// headers so a detector can compile it on its own.
#include "spectr/modulation.hpp"

#include <pulp/signal/lfo_sampling.hpp>

#include <algorithm>
#include <cmath>

namespace spectr {

/// Spectr's four shapes in Pulp's sampling vocabulary: the triangle with its
/// apex at the half cycle, the square at half width, the saw rising.
inline constexpr pulp::signal::LfoWave lfo_sampling_wave(LfoShape shape) noexcept {
    switch (shape) {
        case LfoShape::Triangle: return pulp::signal::LfoWave::triangle;
        case LfoShape::Square: return pulp::signal::LfoWave::square;
        case LfoShape::Saw: return pulp::signal::LfoWave::saw_up;
        case LfoShape::Sine:
        default: return pulp::signal::LfoWave::sine;
    }
}

/// The exact mean of @p shape over the phase interval [@p from, @p to]: the
/// band-limited value for a destination that takes one value per block (every
/// spectral target, the Freeze gate, the Length / Bands / Preset steps). A
/// cycle shorter than the block collapses to the wave's average instead of
/// folding into a block-rate stepper; a slow LFO reads the value at the
/// block's centre. Agrees with `lfo_value` as the interval vanishes.
inline float lfo_mean(LfoShape shape, double from, double to) noexcept {
    return static_cast<float>(pulp::signal::lfo_wave_mean(lfo_sampling_wave(shape), from, to));
}

/// The band-limited value of @p shape for a destination that takes a value
/// EVERY sample (the Output gain), at @p phase with @p increment cycles per
/// sample: polyBLEP / polyBLAMP at the square's, saw's and triangle's corners,
/// the sine exact. Equals `lfo_value` away from a corner and at sub-audio
/// rates.
inline float lfo_bandlimited(LfoShape shape, double phase, double increment) noexcept {
    return static_cast<float>(
        pulp::signal::lfo_wave_bandlimited(lfo_sampling_wave(shape), phase, increment));
}

/// `lfo_mean` through a shape fade: the fade is linear, so the mean of the
/// mix is the mix of the means.
inline float lfo_mean(const LfoShapeFade& fade, double from, double to) noexcept {
    if (fade.mix >= 1.0f || fade.from == fade.to) return lfo_mean(fade.to, from, to);
    const float a = lfo_mean(fade.from, from, to);
    const float b = lfo_mean(fade.to, from, to);
    return a + (b - a) * std::clamp(fade.mix, 0.0f, 1.0f);
}

/// `lfo_bandlimited` through a shape fade.
inline float lfo_bandlimited(const LfoShapeFade& fade, double phase, double increment) noexcept {
    if (fade.mix >= 1.0f || fade.from == fade.to)
        return lfo_bandlimited(fade.to, phase, increment);
    const float a = lfo_bandlimited(fade.from, phase, increment);
    const float b = lfo_bandlimited(fade.to, phase, increment);
    return a + (b - a) * std::clamp(fade.mix, 0.0f, 1.0f);
}

/// The Freeze gate read over a phase INTERVAL -- one slice of audio -- so a
/// gate evaluated once per slice does not fold a fast LFO into chatter. The
/// threshold shapes compare the waveform's mean over the interval (above the
/// slice Nyquist that mean is the wave's average, so the gate settles to one
/// state); the square, whose gate is a window of the cycle, is on when more
/// than half the interval lies inside its window. A vanishing interval is the
/// point gate at @p from.
inline bool lfo_freeze_gate(LfoShape shape, double from, double to, float duty) noexcept {
    const float d = std::clamp(duty, 0.0f, 1.0f);
    if (d <= 0.0f) return false;
    if (d >= 1.0f) return true;
    if (to < from) {
        const double t = from;
        from = to;
        to = t;
    }
    const double span = to - from;
    if (span < 1.0e-9) return lfo_freeze_gate(shape, from, duty);
    switch (shape) {
        case LfoShape::Square: {
            // The window is phase in [0.25 - d/2, 0.25 + d/2) each cycle; in
            // u = phase - (0.25 - d/2) it is [0, d) each cycle, whose
            // cumulative measure is floor(u) * d + min(frac(u), d).
            const auto measure = [d](double u) {
                const double whole = std::floor(u);
                return whole * d + std::min(u - whole, static_cast<double>(d));
            };
            const double lo = 0.25 - 0.5 * d;
            const double inside = measure(to - lo) - measure(from - lo);
            return inside > 0.5 * span;
        }
        case LfoShape::Triangle:
        case LfoShape::Saw:
            return lfo_mean(shape, from, to) > 1.0f - 2.0f * d;
        case LfoShape::Sine:
        default:
            return lfo_mean(shape, from, to)
                > static_cast<float>(std::cos(3.14159265358979323846 * d));
    }
}

}  // namespace spectr
