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
/// What this adapter owns, and the unit does not: asking the active renderer
/// for the response it realises for a SpectralBandLayout (and its minimum
/// phase, where it designs one), the start-up prior (v1's reference,
/// tabulated per bin), Spectr's negative-control seams, the per-slice event
/// list the processor applies sample-accurately, and the wet-source tap that
/// feeds the estimator the wet leg (the held sound while Freeze holds) and
/// the dry leg (the live input).
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
    c.relative_gate_lu = 10.0;
    // The dry leg is the live input; the wet leg is what the mask shapes
    // (the held sound while Freeze holds). Their cross-spectrum tells the
    // estimator, sample-deterministically, how they combine at Mix.
    c.track_dry_leg = true;
    c.material_slew_db_per_second = 6.0;
    c.max_cut_db = 24.0f;
    c.max_boost_db = 24.0f;
    return c;
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
        if (const char* gate = std::getenv("SPECTR_AUTOGAIN_REL_GATE_LU"))
            config.relative_gate_lu = std::atof(gate);  // <= 0 turns it off
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
        response_.assign(static_cast<std::size_t>(spectrum_.bins()), {1.0, 0.0});
        design_grid_ = std::max(64, design_grid);
        magnitudes_.assign(static_cast<std::size_t>(design_grid_ / 2 + 1), 1.0);
        phased_.assign(static_cast<std::size_t>(design_grid_ / 2 + 1), {1.0, 0.0});
        minimum_phase_.prepare(design_grid_);
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
        mix_ = std::clamp(static_cast<double>(mix), 0.0, 1.0);
        events_ = 0;
        slice_pos_ = 0;
        if (target_.primed() && !shape_changed) return false;
        if (enabled) derive_response_(shape, mix, renderer);
        return target_.retarget(enabled ? shape_target_db() : 0.0f);
    }

    /// The wet source's block and the live input it was made from, from the
    /// tap, in stream order.
    void push(const float* const* wet, const float* const* live, int channels,
              int num_samples) noexcept {
        spectrum_.push(wet, live, channels, num_samples, [&](int consumed) {
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
        if (!spectrum_.prepared() || !response_valid_) return 0.0f;
        const auto& c = spectrum_.config();
        return pulp_candidate::signal::blend_makeup_gain_db(
            response_, spectrum_.legs(), mix_, {c.max_cut_db, c.max_boost_db});
    }
    /// Whether the last derived response was the renderer's realised one
    /// (false: the compiled table, the drawn steps).
    [[nodiscard]] bool response_is_realised() const noexcept { return response_realised_; }

private:
    // The wet leg's response for this shape on the estimator's grid: the
    // renderer's realised magnitude (or, without one, the compiled table),
    // with the minimum phase a minimum-phase realisation gives it when the
    // phase matters (Mix below 100 %).
    void derive_response_(const pulp::signal::SpectralBandLayout& shape, float mix,
                          const MaskRenderer* renderer) noexcept {
        response_realised_ = false;
        response_valid_ = false;
        if (!spectrum_.prepared()) return;
        const bool drawn = renderer == nullptr || level_plant("autogain-v2-drawn-response");
        int grid = drawn ? design_grid_ : renderer->design_grid_size();
        if (grid <= 0 || grid > design_grid_) grid = design_grid_;
        bool ok = false;
        if (!drawn) {
            ok = renderer->realised_magnitude(shape, sample_rate_, table_, magnitudes_);
            response_realised_ = ok;
        }
        if (!ok) {
            if (!pulp::signal::build_spectral_mask(shape, grid, static_cast<float>(sample_rate_),
                                                   table_))
                return;
            for (int b = 0; b < table_.num_bins; ++b)
                magnitudes_[static_cast<std::size_t>(b)] =
                    static_cast<double>(table_.gain_linear[static_cast<std::size_t>(b)]);
        }
        const auto design_bins = static_cast<std::size_t>(grid / 2 + 1);
        const double floor = drawn ? 0.0 : renderer->minimum_phase_floor();
        const bool phased = floor > 0.0 && mix < 1.0f && grid == design_grid_
            && minimum_phase_.compute(std::span<const double>(magnitudes_.data(), design_bins),
                                      floor, phased_);
        const double ratio = static_cast<double>(grid) / static_cast<double>(spectrum_.fft_size());
        for (std::size_t k = 0; k < response_.size(); ++k) {
            const auto j = std::min(design_bins - 1, static_cast<std::size_t>(
                std::lround(static_cast<double>(k) * ratio)));
            response_[k] = phased ? phased_[j] : std::complex<double>(magnitudes_[j], 0.0);
        }
        response_valid_ = true;
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
    // The wet leg's response on the estimator's grid, and the scratch it is
    // computed in (a table is far too large for an audio thread's stack).
    std::vector<std::complex<double>> response_;
    std::vector<double> magnitudes_;
    std::vector<std::complex<double>> phased_;
    pulp_candidate::signal::MinimumPhaseResponse minimum_phase_;
    MaskRenderer::Table table_{};
    int design_grid_ = 8192;
    double mix_ = 1.0;
    bool response_valid_ = false;
    bool response_realised_ = false;
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
        if (material_ != nullptr) material_->push(wet, input, channels, num_samples);
    }

private:
    pulp::signal::SpectralWetSourceStageT<float>* source_ = nullptr;
    AutoGainMaterial* material_ = nullptr;
};

// SPECTR-RENDER-PATH END

} // namespace spectr
