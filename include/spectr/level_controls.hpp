#pragma once

// Level controls: Intensity, Auto Gain, and the editor's Range.
//
// Three answers to "boosting bands gets loud" that never change what a band
// parameter stores:
//
//   Intensity  (host parameter 5000) scales the whole composed shape toward
//              flat in dB: effective_db = intensity x composed_db. A muted
//              band's linear gain becomes (1 - intensity), so a mute fades to
//              unity instead of jumping. 100 % is the drawn shape exactly;
//              0 % is a flat (unity) mask.
//   Auto Gain  (host parameter 5001) is a broadband post gain computed from
//              the ENERGY of the effective shape -- never from the output
//              signal, so it cannot pump. v2 (what AUTO runs,
//              auto_gain_material.hpp) weighs the shape by the long-term
//              K-weighted spectrum of the material the mask is shaping; v1
//              (AutoGainReference below, kept for comparison) weighs it by a
//              fixed K-weighted pink reference. It compensates the drawn /
//              morphed / macro shape scaled by Intensity and blended by Mix.
//              It deliberately does NOT follow LFO motion: a level LFO stays
//              audible as level.
//   Range      (editor state, not a parameter) is the plot's vertical scale
//              and the reach of a full-height edit: +-3 / 6 / 12 / 24 dB. It
//              never changes the sound or the band parameter range.
//
// PARAMETER IDs 5000..5009 ARE RESERVED FOR LEVEL CONTROLS. 5000 and 5001 are
// assigned; 5002..5009 are headroom (a Mix successor, a ceiling, ...). See
// docs/parameter-surface.md. They sit outside the surface slot cache on
// purpose: like Mix and Output trim, the audio owner reads them straight off
// the store (or the block's parameter cursor).

#include "spectr/band_state.hpp"

#include <pulp/signal/multi_channel_meter.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/state/parameter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <cstdlib>
#include <string_view>

namespace spectr {

inline constexpr pulp::state::ParamID kParamIntensity = 5000;
inline constexpr pulp::state::ParamID kParamAutoGain  = 5001;
/// Parameters this header adds to the static surface.
inline constexpr std::size_t kLevelParamCount = 2;

/// Intensity range, percent. 0..100: 100 % IS the drawn shape, so turning the
/// effect "up" past what was drawn is a drawing act (Boost), not this knob.
inline constexpr float kIntensityMinPercent = 0.0f;
inline constexpr float kIntensityMaxPercent = 100.0f;
inline constexpr float kIntensityDefaultPercent = 100.0f;

/// Auto Gain's value for a NEW instance. Loaded sessions that predate the
/// control always open with it Off (see Spectr::deserialize_plugin_state), so
/// flipping this constant changes new instances only.
///
/// Off. v1 compensated the drawn shape against a fixed K-weighted pink
/// reference and missed the loudness actually lost or gained by up to ~20 LU
/// on narrow material. v2 (auto_gain_material.hpp) weighs the material's own
/// spectrum and meets the default-on bar on the corpus sweep
/// (docs/level-controls.md), so the recommendation there is to flip this --
/// a product decision, not made here. Sessions that saved AUTO on keep it on,
/// and now run v2.
inline constexpr bool kAutoGainDefaultForNewInstances = false;

/// Full-scale (0 -> 100 %) Intensity slew. Longer than the LFO level's 60 ms
/// because Intensity moves EVERY band at once and each step is a mask
/// restage: measured through the AU host (tools/au_level_probe.cpp, an
/// unpaced offline render), a 0 -> 100 % jump on a +12 dB shape stepped
/// 2.6 dB per 512-frame block at 100 ms, over the 2.3 dB Bank-LFO yardstick,
/// and 1.3 dB at 200 ms. Still well under a quarter second on a knob.
inline constexpr double kIntensitySlewSeconds = 0.2;

/// Auto Gain's ramp to a new compensation target.
inline constexpr float kAutoGainRampSeconds = 0.3f;
/// How far Auto Gain v1 may move the level. Cut reaches the bands' full +24 dB
/// boost; make-up stops at +12 dB so a nearly muted shape is not dragged up
/// to full level along with its noise floor. (v2's range is
/// auto_gain_v2_config(): +-24 dB, because once the weighting is the
/// material's own, a cut that removes 20 dB of what is playing needs 20 dB
/// back.)
inline constexpr float kAutoGainMaxCutDb = 24.0f;
inline constexpr float kAutoGainMaxBoostDb = 12.0f;

/// The 0..1 Intensity factor for a parameter value in percent.
inline float intensity_factor(float percent) noexcept {
    if (!std::isfinite(percent)) return 1.0f;
    return std::clamp(percent, kIntensityMinPercent, kIntensityMaxPercent) / 100.0f;
}

/// The factor @p seconds later, moving toward @p target at full scale per
/// kIntensitySlewSeconds.
inline float slew_intensity(float current, float target, double seconds) noexcept {
    if (seconds <= 0.0 || current == target) return current;
    const float step = static_cast<float>(seconds / kIntensitySlewSeconds);
    if (current < target) return std::min(target, current + step);
    return std::max(target, current - step);
}

/// One band after Intensity. Exact at 1 (the band is returned unchanged, a
/// mute stays a mute), so a session at 100 % renders bit-identically to a
/// build without the control.
inline void apply_intensity(float& gain_db, bool& muted, float intensity) noexcept {
    if (intensity >= 1.0f) return;
    const float k = std::max(0.0f, intensity);
    if (muted) {
        // Linear 0 at 100 %, linear 1 at 0 %: the mute fades toward unity.
        const float linear = 1.0f - k;
        muted = false;
        gain_db = linear > 0.0f
            ? std::max(-180.0f, 20.0f * std::log10(linear))
            : -180.0f;
        return;
    }
    gain_db *= k;
}

inline void apply_intensity(pulp::signal::SpectralBandLayout& layout,
                            float intensity) noexcept {
    if (intensity >= 1.0f) return;
    const auto n = std::min<std::size_t>(layout.active_bands, layout.bands.size());
    for (std::size_t i = 0; i < n; ++i)
        apply_intensity(layout.bands[i].gain_db, layout.bands[i].muted, intensity);
}

// ── Auto Gain ────────────────────────────────────────────────────────────

/// A K-weighted pink reference spectrum, tabulated once per sample rate.
///
/// Pink noise has equal energy per octave, so its energy density is 1/f; the
/// BS.1770 K-weighting is what a loudness meter applies before summing. The
/// weight of a frequency span is therefore the integral of |K(f)|^2 / f over
/// it, and that integral -- cumulative, on a log-frequency grid -- is all
/// this holds. Built on the control thread (prepare); read lock-free after.
class AutoGainReference {
public:
    static constexpr std::size_t kGridPoints = 512;
    static constexpr double kLowHz = 20.0;
    static constexpr double kHighHz = 20000.0;

    void prepare(double sample_rate) noexcept {
        sample_rate_ = sample_rate;
        const auto coeffs = pulp::signal::k_weighting_coefficients(sample_rate);
        high_hz_ = std::min(kHighHz, 0.5 * sample_rate * 0.999);
        if (!(high_hz_ > kLowHz)) high_hz_ = kLowHz * 2.0;
        log_low_ = std::log(kLowHz);
        log_span_ = std::log(high_hz_) - log_low_;
        const auto power = [&](double hz) {
            if (!coeffs.valid) return 1.0;
            const double w = 2.0 * std::numbers::pi * hz / sample_rate;
            const std::complex<double> z1 = std::polar(1.0, -w);
            const std::complex<double> z2 = z1 * z1;
            const auto response = [&](const pulp::signal::LoudnessBiquadCoefficients& c) {
                return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
            };
            return std::norm(response(coeffs.shelf) * response(coeffs.high_pass));
        };
        // d(energy) = |K|^2 / f df = |K|^2 d(ln f): trapezoid in ln f.
        const double step = log_span_ / static_cast<double>(kGridPoints - 1);
        cumulative_[0] = 0.0;
        double previous = power(kLowHz);
        for (std::size_t i = 1; i < kGridPoints; ++i) {
            const double here = power(std::exp(log_low_ + step * static_cast<double>(i)));
            cumulative_[i] = cumulative_[i - 1] + 0.5 * (previous + here) * step;
            previous = here;
        }
        valid_ = true;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    /// Reference energy between two frequencies (clipped to 20 Hz .. 20 kHz).
    [[nodiscard]] double weight(double lo_hz, double hi_hz) const noexcept {
        if (!valid_ || !(hi_hz > lo_hz)) return 0.0;
        return cumulative_at_(hi_hz) - cumulative_at_(lo_hz);
    }

    /// The compensation, in dB, for a band layout whose gains are the
    /// EFFECTIVE ones (Intensity already applied), blended with the dry signal
    /// at @p mix (0..1). Pure; called once per audio sub-block.
    ///
    ///   E = sum_k w_k |mix * g_k + (1 - mix)|^2 / sum_k w_k
    ///   compensation = -10 log10(E), clamped to [-cut, +boost]
    ///
    /// The extend-edge policy makes the first band own everything below the
    /// window and the last everything above it, so those two bands are
    /// weighted out to 20 Hz and 20 kHz.
    [[nodiscard]] float compensation_db(const pulp::signal::SpectralBandLayout& layout,
                                        float mix) const noexcept {
        if (!valid_) return 0.0f;
        const auto n = std::min<std::size_t>(layout.active_bands, layout.bands.size());
        if (n == 0 || !(layout.max_hz > layout.min_hz) || !(layout.min_hz > 0.0f))
            return 0.0f;
        const double m = std::clamp(static_cast<double>(mix), 0.0, 1.0);
        const double ratio = static_cast<double>(layout.max_hz)
                             / static_cast<double>(layout.min_hz);
        double total = 0.0, energy = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double lo = static_cast<double>(layout.min_hz)
                * std::pow(ratio, static_cast<double>(i) / static_cast<double>(n));
            double hi = static_cast<double>(layout.min_hz)
                * std::pow(ratio, static_cast<double>(i + 1) / static_cast<double>(n));
            if (i == 0) lo = 1.0;
            if (i + 1 == n) hi = 1.0e9;
            const double w = weight(lo, hi);
            if (w <= 0.0) continue;
            const auto& band = layout.bands[i];
            const double g = band.muted
                ? 0.0 : std::pow(10.0, static_cast<double>(band.gain_db) / 20.0);
            const double blended = m * g + (1.0 - m);
            total += w;
            energy += w * blended * blended;
        }
        if (total <= 0.0) return 0.0f;
        const double mean = energy / total;
        if (!(mean > 0.0)) return kAutoGainMaxBoostDb;
        const double db = -10.0 * std::log10(mean);
        return static_cast<float>(std::clamp(db, -static_cast<double>(kAutoGainMaxCutDb),
                                             static_cast<double>(kAutoGainMaxBoostDb)));
    }

private:
    [[nodiscard]] double cumulative_at_(double hz) const noexcept {
        const double x = std::clamp((std::log(std::max(hz, 1e-9)) - log_low_) / log_span_,
                                    0.0, 1.0)
                         * static_cast<double>(kGridPoints - 1);
        const auto i = static_cast<std::size_t>(std::floor(x));
        if (i >= kGridPoints - 1) return cumulative_[kGridPoints - 1];
        const double t = x - static_cast<double>(i);
        return cumulative_[i] + t * (cumulative_[i + 1] - cumulative_[i]);
    }

    std::array<double, kGridPoints> cumulative_{};
    double sample_rate_ = 0.0;
    double high_hz_ = kHighHz;
    double log_low_ = 0.0;
    double log_span_ = 1.0;
    bool valid_ = false;
};

// ── Negative-control seam ────────────────────────────────────────────────

/// `SPECTR_LEVEL_PLANT=<name>` re-creates a defect so a test can prove it sees
/// it: `intensity-step` (no Intensity slew), `intensity-ignored` (Intensity
/// never reaches the mask), `autogain-follow-output` (Auto Gain chases the
/// output level block by block -- the pumping design this one replaces),
/// `autogain-v2-unweighted` (v2 ignores the material: every bin weighs the
/// same), `autogain-v2-no-smoothing` (each frame replaces v2's estimate and
/// the target is not slew-limited), `autogain-v2-drawn-response` (v2 weighs
/// the drawn band steps instead of the renderer's realised response; used by
/// the advisory sweep to show what the realised response buys),
/// `autogain-v2-reset-on-seek` (v2 forgets the material at every locate),
/// `autogain-v2-stale-on-change` (no change detector, no level-drop rule, no
/// Freeze leg switch, a fixed 6 dB/s), `autogain-v2a` (both: the first v2's
/// transient behaviour, for the sweep's comparison),
/// `autogain-v2-short-persistence` (a 3-frame detector with no memory of the
/// material before a change), `autogain-v2-restore-as-warm` (a restored
/// session's estimate taken as the estimate rather than as a prior),
/// `autogain-v2-reprepare-reset` (a host re-prepare forgets the estimate),
/// `autogain-v2-legacy-composed-only` (the old-session AUTO toggle seen only
/// on the composed path).
/// Read once per process; unset in every shipping run. The Spectr constructor
/// and prepare() make that first read (spectr.cpp,
/// prime_negative_control_seams), so the audio thread only ever loads it.
inline bool level_plant(const char* name) noexcept {
    static const char* const planted = std::getenv("SPECTR_LEVEL_PLANT");
    return planted != nullptr && std::string_view(planted) == name;
}

// ── Range (editor state) ─────────────────────────────────────────────────

/// The plot's vertical scale and the reach of a full-height edit, in dB.
inline constexpr std::array<int, 4> kEditorRangeChoicesDb{3, 6, 12, 24};
inline constexpr int kEditorRangeDefaultDb = 24;

inline constexpr bool valid_editor_range_db(int db) noexcept {
    for (const int choice : kEditorRangeChoicesDb)
        if (choice == db) return true;
    return false;
}

} // namespace spectr
