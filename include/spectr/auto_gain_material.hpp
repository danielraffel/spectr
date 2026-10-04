#pragma once

/// @file auto_gain_material.hpp
/// Auto Gain v2 in Spectr: a thin adapter over the product-independent
/// loudness-compensation unit (spectr/upstream/loudness_compensation.hpp).
///
/// v1 (AutoGainReference, level_controls.hpp) asks how much louder the drawn
/// shape makes a K-weighted pink reference. That is right for full-range
/// material and wrong for narrow material: boosting 10 kHz on a bass line
/// changes almost nothing audible, yet v1 cuts the whole signal as if it had.
/// v2 asks the same question of the material actually going through the mask:
///
///   E = sum_f P_in(f) |K(f)|^2 |H(f)|^2  /  sum_f P_in(f) |K(f)|^2
///   make-up = -10 log10(E), clamped to +-24 dB
///
/// with H the effective band shape blended with the dry signal at Mix (v1's
/// formula, band for band) and K the BS.1770 K-weighting. P_in is the
/// long-term spectrum of the WET source -- the live input, or while frozen the
/// held material the mask is shaping -- which is why the estimator is fed from
/// a wet-source tap (AutoGainWetTap) and not from the host input.
///
/// What this adapter owns, and the unit does not: turning a
/// SpectralBandLayout plus Mix into band power gains (extend-edge bands, mutes,
/// the dry blend), the start-up prior (v1's reference, tabulated per bin),
/// Spectr's negative-control seams, and the per-slice event list the processor
/// applies sample-accurately.
///
/// No pumping: nothing reads the output; the spectrum is a several-second,
/// energy-weighted average that skips frames below the loudness gate (so it
/// holds through silence); material movement is slew-limited; a shape edit
/// retargets at once. Determinism: the estimator's frame grid is counted in
/// samples from prepare()/reset(), and a frame's new target is applied at the
/// frame's own sample, so the gain does not depend on host block size or
/// render speed.

#include "spectr/band_state.hpp"
#include "spectr/level_controls.hpp"
#include "spectr/mask_renderer.hpp"
#include "spectr/upstream/loudness_compensation.hpp"

#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

namespace spectr {

/// Which Auto Gain computation the processor runs. v2 is what AUTO does; v1
/// is kept for side-by-side measurement and is not reachable from any host or
/// editor control.
enum class AutoGainModel : int { reference_v1 = 1, material_v2 = 2 };
inline constexpr AutoGainModel kAutoGainShippingModel = AutoGainModel::material_v2;

/// Spectr's v2 tuning. Each value was chosen on the corpus sweep; the numbers
/// are in docs/level-controls.md.
[[nodiscard]] inline pulp_candidate::signal::LoudnessCompensationConfig
auto_gain_v2_config() noexcept {
    pulp_candidate::signal::LoudnessCompensationConfig c;
    c.time_constant_seconds = 3.0;
    c.prior_seconds = 0.5;
    c.gate_lufs = -60.0;
    c.material_slew_db_per_second = 6.0;
    c.max_cut_db = 24.0f;
    c.max_boost_db = 24.0f;
    return c;
}

/// Band power gains of an effective layout blended with the dry signal at
/// @p mix: extend_edge_band, so the first band owns everything below the
/// window and the last everything above it. Pure.
inline std::size_t auto_gain_band_powers(
    const pulp::signal::SpectralBandLayout& layout, float mix,
    std::span<pulp_candidate::signal::BandPowerGain> out) noexcept {
    const auto n = std::min({static_cast<std::size_t>(layout.active_bands),
                             layout.bands.size(), out.size()});
    if (n == 0 || !(layout.max_hz > layout.min_hz) || !(layout.min_hz > 0.0f)) return 0;
    const double m = std::clamp(static_cast<double>(mix), 0.0, 1.0);
    const double ratio = static_cast<double>(layout.max_hz) / static_cast<double>(layout.min_hz);
    for (std::size_t i = 0; i < n; ++i) {
        double lo = static_cast<double>(layout.min_hz)
            * std::pow(ratio, static_cast<double>(i) / static_cast<double>(n));
        double hi = static_cast<double>(layout.min_hz)
            * std::pow(ratio, static_cast<double>(i + 1) / static_cast<double>(n));
        if (i == 0) lo = 1.0;
        if (i + 1 == n) hi = 1.0e9;
        const auto& band = layout.bands[i];
        const double g = band.muted ? 0.0 : std::pow(10.0, static_cast<double>(band.gain_db) / 20.0);
        const double blended = m * g + (1.0 - m);
        out[i] = {lo, hi, blended * blended};
    }
    return n;
}

// SPECTR-RENDER-PATH BEGIN -- the adapter and the wet-source tap run on the
// audio thread (prepare() on the control thread): no clock, sleep, thread or
// lock (tools/ci/check_render_path_clock.py).
class AutoGainMaterial {
public:
    /// One frame's outcome inside a render slice: from `offset` (samples into
    /// the slice) on, the make-up target is `target_db`.
    struct Event {
        int offset = 0;
        float target_db = 0.0f;
    };
    static constexpr std::size_t kMaxEvents = 64;

    // ── control thread ──────────────────────────────────────────────────
    /// Allocates. @p reference must be prepared for the same rate: v1's
    /// K-weighted pink, per bin, is the start-up prior. @p design_grid is the
    /// renderers' design FFT size: the estimate is taken on the same bin grid,
    /// so the realised response weighs it bin for bin.
    void prepare(double sample_rate, int channels, const AutoGainReference& reference,
                 int design_grid) {
        sample_rate_ = sample_rate;
        auto config = auto_gain_v2_config();
        config.fft_size = design_grid;
        // Advisory-sweep seams (tools/autogain_sweep.cpp), read here on the
        // control thread, never on the audio thread.
        if (const char* tau = std::getenv("SPECTR_AUTOGAIN_TAU_S"))
            if (const double v = std::atof(tau); v > 0.0) config.time_constant_seconds = v;
        if (const char* slew = std::getenv("SPECTR_AUTOGAIN_SLEW_DB_S"))
            if (const double v = std::atof(slew); v > 0.0) config.material_slew_db_per_second = v;
        // Negative-control seams (level_plant, SPECTR_LEVEL_PLANT).
        if (level_plant("autogain-v2-unweighted")) config.weight_by_material = false;
        if (level_plant("autogain-v2-no-smoothing")) {
            config.time_constant_seconds = 0.0;
            config.material_slew_db_per_second = 0.0;
        }
        using pulp_candidate::signal::LongTermSpectrum;
        const int bins = LongTermSpectrum::bins_for(sample_rate, config);
        const double bin_hz = sample_rate / static_cast<double>((bins - 1) * 2);
        std::vector<double> prior(static_cast<std::size_t>(bins), 0.0);
        for (int k = 0; k < bins; ++k) {
            const double lo = std::max(0.0, (static_cast<double>(k) - 0.5) * bin_hz);
            const double hi = (static_cast<double>(k) + 0.5) * bin_hz;
            prior[static_cast<std::size_t>(k)] = reference.weight(lo, hi);
        }
        spectrum_.prepare(sample_rate, channels, config, prior);
        response_.assign(static_cast<std::size_t>(spectrum_.bins()), 1.0);
        magnitudes_.assign(static_cast<std::size_t>(
            std::max(design_grid, spectrum_.fft_size()) / 2 + 1), 1.0);
        reset();
    }

    [[nodiscard]] bool prepared() const noexcept { return spectrum_.prepared(); }
    [[nodiscard]] const pulp_candidate::signal::LongTermSpectrum& spectrum() const noexcept {
        return spectrum_;
    }

    // ── audio thread ────────────────────────────────────────────────────
    /// Forget the material: back to the prior, frame grid restarted.
    void reset() noexcept {
        spectrum_.reset();
        target_.reset();
        events_ = 0;
        slice_pos_ = 0;
        enabled_ = false;
    }

    /// Start a render slice with the shape it renders, AUTO's state, and
    /// whether the shape (or AUTO, or Mix) changed since the last slice.
    /// Returns true when the target moved now (prime, or an edit); target_db()
    /// is then the new value. Clears the slice's frame events.
    ///
    /// On a change the response is re-derived: from @p renderer's own
    /// realised magnitude when it can give one on this grid (what the
    /// listener hears, edge shaping included), else from the drawn bands.
    bool begin_slice(const pulp::signal::SpectralBandLayout& shape, float mix,
                     bool enabled, bool shape_changed,
                     const MaskRenderer* renderer) noexcept {
        enabled_ = enabled;
        events_ = 0;
        slice_pos_ = 0;
        if (target_.primed() && !shape_changed) return false;
        if (enabled) derive_response_(shape, mix, renderer);
        return target_.retarget(enabled ? shape_target_db() : 0.0f);
    }

    /// The wet source's block, from the tap, in stream order.
    void push(const float* const* x, int channels, int num_samples) noexcept {
        spectrum_.push(x, channels, num_samples, [&](int consumed) {
            on_frame_(slice_pos_ + consumed);
        });
        slice_pos_ = std::min(slice_pos_ + std::max(0, num_samples), 1 << 30);
    }

    [[nodiscard]] std::size_t event_count() const noexcept { return events_; }
    [[nodiscard]] const Event& event(std::size_t i) const noexcept { return event_list_[i]; }
    [[nodiscard]] float target_db() const noexcept { return target_.value_db(); }
    [[nodiscard]] std::uint64_t observed_frames() const noexcept { return spectrum_.observed_frames(); }
    [[nodiscard]] std::uint64_t gated_frames() const noexcept { return spectrum_.gated_frames(); }

    /// The make-up the current estimate gives the current slice's shape.
    [[nodiscard]] float shape_target_db() const noexcept {
        if (!spectrum_.prepared()) return 0.0f;
        const auto& c = spectrum_.config();
        const pulp_candidate::signal::MakeupLimits limits{c.max_cut_db, c.max_boost_db};
        if (response_per_bin_)
            return pulp_candidate::signal::makeup_gain_db(response_, spectrum_.spectrum(), limits);
        return pulp_candidate::signal::band_makeup_gain_db(
            std::span<const pulp_candidate::signal::BandPowerGain>(bands_.data(), band_count_),
            [this](double lo, double hi) { return spectrum_.weight(lo, hi); }, limits);
    }
    /// Whether the last derived response was the renderer's realised one.
    [[nodiscard]] bool response_is_realised() const noexcept { return response_per_bin_; }

private:
    void derive_response_(const pulp::signal::SpectralBandLayout& shape, float mix,
                          const MaskRenderer* renderer) noexcept {
        band_count_ = auto_gain_band_powers(shape, mix, bands_);
        response_per_bin_ = false;
        if (renderer == nullptr || !spectrum_.prepared() || level_plant("autogain-v2-drawn-response"))
            return;
        const int grid = renderer->design_grid_size();
        if (grid <= 0 || static_cast<std::size_t>(grid / 2 + 1) > magnitudes_.size()) return;
        if (!renderer->realised_magnitude(shape, sample_rate_, table_, magnitudes_)) return;
        const double m = std::clamp(static_cast<double>(mix), 0.0, 1.0);
        const auto bins = response_.size();
        const double ratio = static_cast<double>(grid) / static_cast<double>(spectrum_.fft_size());
        const auto design_bins = static_cast<std::size_t>(grid / 2 + 1);
        for (std::size_t k = 0; k < bins; ++k) {
            const auto j = std::min(design_bins - 1, static_cast<std::size_t>(
                std::lround(static_cast<double>(k) * ratio)));
            const double blended = m * magnitudes_[j] + (1.0 - m);
            response_[k] = blended * blended;
        }
        response_per_bin_ = true;
    }

    void on_frame_(int offset_in_slice) noexcept {
        if (!enabled_ || !target_.primed()) return;
        const auto& c = spectrum_.config();
        const double hop_seconds =
            static_cast<double>(spectrum_.hop()) / spectrum_.sample_rate();
        if (!target_.follow(shape_target_db(), hop_seconds, c.material_slew_db_per_second))
            return;
        const Event e{offset_in_slice, target_.value_db()};
        // A slice longer than kMaxEvents hops (~2.7 s at 48 kHz): the last
        // entry carries the latest target, its position the only approximation.
        if (events_ < kMaxEvents) event_list_[events_++] = e;
        else event_list_[kMaxEvents - 1] = e;
    }

    pulp_candidate::signal::LongTermSpectrum spectrum_;
    pulp_candidate::signal::MakeupTarget target_;
    std::array<pulp_candidate::signal::BandPowerGain, kMaxBands> bands_{};
    std::size_t band_count_ = 0;
    // The realised response on the estimator's grid, and the scratch the
    // renderer computes it in (a table is far too large for the stack).
    std::vector<double> response_;
    std::vector<double> magnitudes_;
    MaskRenderer::Table table_{};
    bool response_per_bin_ = false;
    double sample_rate_ = 48000.0;
    bool enabled_ = false;
    int slice_pos_ = 0;
    std::array<Event, kMaxEvents> event_list_{};
    std::size_t events_ = 0;
};

/// The wet-source stage the renderers actually call: it runs the freeze
/// source, then shows the estimator what the mask is about to shape. While
/// Freeze holds, that is the held material, not the live input.
class AutoGainWetTap final : public pulp::signal::SpectralWetSourceStageT<float> {
public:
    void bind(pulp::signal::SpectralWetSourceStageT<float>* source,
              AutoGainMaterial* material) noexcept {
        source_ = source;
        material_ = material;
    }
    void process_block(const float* const* input, float* const* wet,
                       int channels, int num_samples) noexcept override {
        if (source_ != nullptr) {
            source_->process_block(input, wet, channels, num_samples);
        } else {
            for (int ch = 0; ch < channels; ++ch)
                if (wet[ch] != input[ch])
                    std::copy(input[ch], input[ch] + num_samples, wet[ch]);
        }
        if (material_ != nullptr) material_->push(wet, channels, num_samples);
    }

private:
    pulp::signal::SpectralWetSourceStageT<float>* source_ = nullptr;
    AutoGainMaterial* material_ = nullptr;
};

// SPECTR-RENDER-PATH END

} // namespace spectr
