#pragma once

/// @file loudness_compensation.hpp
/// Loudness make-up for a static (or slowly edited) spectral shape, weighted
/// by the long-term spectrum of the material going through it.
///
/// UPSTREAM CANDIDATE. This header depends on Pulp and the standard library
/// only -- no product types in its interface -- so it can move to
/// pulp/signal/loudness_compensation.hpp mechanically (namespace
/// pulp_candidate::signal becomes pulp::signal). Its tests,
/// test/test_loudness_compensation.cpp, link Pulp::signal and Catch2 only and
/// move with it.
///
/// THE PIPELINE, exposed stage by stage:
///
///   1. k_weighting_power()         |K(f)|^2 of BS.1770 at a frequency.
///   2. LongTermSpectrum            stateful, RT-safe: Hann/FFT frames of the
///                                  material on a fixed sample grid, K-weighted,
///                                  energy-weighted exponential average, gated
///                                  below a loudness floor, starting from a
///                                  caller-supplied prior whose say decays
///                                  quickly once material is heard.
///   3. SpectrumCdf                 the cumulative of a per-bin spectrum, so the
///                                  energy between any two frequencies is two
///                                  lookups.
///   4. makeup_gain_db()            PURE. Per-bin form:
///                                    E = sum P(f) |H(f)|^2 / sum P(f)
///                                    make-up = -10 log10(E), clamped.
///      band_makeup_gain_db()       PURE. The same over bands with a power gain
///                                  each, weighted by any cumulative weighting.
///   5. MakeupTarget                stateful: a shape edit moves the target at
///                                  once; the material moves it no faster than
///                                  a slew limit.
///
/// RT CONTRACT. prepare() allocates (control thread). Everything else --
/// reset(), push(), the pure functions, SpectrumCdf::assign() into a prepared
/// size, MakeupTarget -- is allocation-free, lock-free and reads no clock.
/// LongTermSpectrum advances on a frame grid counted in samples from
/// prepare()/reset(), so its state after N samples does not depend on how the
/// N samples were chunked.

#include <pulp/signal/biquad.hpp>
#include <pulp/signal/fft.hpp>
#include <pulp/signal/frequency_response.hpp>
#include <pulp/signal/multi_channel_meter.hpp>
#include <pulp/signal/windowing.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace pulp_candidate::signal {

// SPECTR-RENDER-PATH BEGIN -- every stage below runs on an audio thread
// (prepare() allocates, on the control thread, but reads no clock either), so
// the whole unit is held to the render-path rule: no clock, sleep, thread or
// lock (tools/ci/check_render_path_clock.py).

/// Every tunable of the stateful stages.
struct LoudnessCompensationConfig {
    /// Time constant of the material spectrum, seconds. <= 0: no smoothing
    /// (each frame replaces the estimate).
    double time_constant_seconds = 3.0;
    /// How long the prior keeps a say once material is heard: its weight is
    /// exp(-t / prior_seconds) over t seconds of audible material, so the
    /// estimate starts AT the prior and is the material's own within a few
    /// of these. Short on purpose: a prior with energy where the material has
    /// none dominates any boost there for as long as it lingers.
    double prior_seconds = 0.5;
    /// Frames below this K-weighted loudness (LUFS-equivalent over the frame,
    /// channels summed as BS.1770 sums them) are not material: the estimate
    /// holds through them.
    double gate_lufs = -60.0;
    /// Fastest the material may move the make-up target, dB/s. <= 0: no limit.
    double material_slew_db_per_second = 6.0;
    /// Make-up range.
    float max_cut_db = 24.0f;
    float max_boost_db = 24.0f;
    /// false: every bin weighs the same (ignores the material and K). Exists
    /// as a measurable negative control, not as a mode.
    bool weight_by_material = true;
    /// Analysis frame length, seconds; the FFT size is the next power of two
    /// at or above it (4096 at 44.1/48 kHz). Hop is half a frame.
    double frame_seconds = 0.08;
    /// > 0: use exactly this FFT size instead (a power of two), so the
    /// estimate shares a bin grid with a response sampled on it.
    int fft_size = 0;
};

struct MakeupLimits {
    float max_cut_db = 24.0f;
    float max_boost_db = 24.0f;
};

// ── 1. K-weighting ──────────────────────────────────────────────────────────

/// |K(f)|^2 for BS.1770 K-weighting at @p sample_rate, through Pulp's own
/// K-weighting design and biquad response evaluator. 1 where the rate cannot
/// represent the design.
[[nodiscard]] inline double k_weighting_power(
    double hz, const pulp::signal::KWeightingCoefficients& k, double sample_rate) noexcept {
    if (!k.valid || !(sample_rate > 0.0)) return 1.0;
    const auto to_biquad = [](const pulp::signal::LoudnessBiquadCoefficients& c) {
        return pulp::signal::BiquadCoefficientsT<double>{c.b0, c.b1, c.b2, c.a1, c.a2};
    };
    const double omega = pulp::signal::angular_frequency(hz, sample_rate);
    const double m = pulp::signal::section_magnitude(to_biquad(k.shelf), omega)
                   * pulp::signal::section_magnitude(to_biquad(k.high_pass), omega);
    return m * m;
}

// ── 4. The pure make-up ────────────────────────────────────────────────────

/// Make-up, dB, for a per-bin power response |H|^2 against a per-bin power
/// spectrum on the same grid. A flat response is 0 dB; an empty or silent
/// spectrum is 0 dB (nothing to compensate); a response that removes
/// everything is the boost limit.
[[nodiscard]] inline float makeup_gain_db(std::span<const double> response_power,
                                          std::span<const double> spectrum_power,
                                          MakeupLimits limits) noexcept {
    const std::size_t n = std::min(response_power.size(), spectrum_power.size());
    double total = 0.0, energy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = spectrum_power[i];
        if (!(w > 0.0)) continue;
        total += w;
        energy += w * std::max(0.0, response_power[i]);
    }
    if (!(total > 0.0)) return 0.0f;
    const double mean = energy / total;
    if (!(mean > 0.0)) return limits.max_boost_db;
    return static_cast<float>(std::clamp(-10.0 * std::log10(mean),
                                         -static_cast<double>(limits.max_cut_db),
                                         static_cast<double>(limits.max_boost_db)));
}

/// One band of a band-wise response: its frequency span and power gain.
struct BandPowerGain {
    double lo_hz = 0.0;
    double hi_hz = 0.0;
    double power_gain = 1.0;
};

/// The same make-up over bands, each weighted by `weight(lo_hz, hi_hz)` -- the
/// material's energy between two frequencies (SpectrumCdf::weight, or any
/// reference weighting).
template <typename WeightFn>
[[nodiscard]] inline float band_makeup_gain_db(std::span<const BandPowerGain> bands,
                                               WeightFn&& weight,
                                               MakeupLimits limits) noexcept {
    double total = 0.0, energy = 0.0;
    for (const auto& band : bands) {
        const double w = weight(band.lo_hz, band.hi_hz);
        if (!(w > 0.0)) continue;
        total += w;
        energy += w * std::max(0.0, band.power_gain);
    }
    if (!(total > 0.0)) return 0.0f;
    const double mean = energy / total;
    if (!(mean > 0.0)) return limits.max_boost_db;
    return static_cast<float>(std::clamp(-10.0 * std::log10(mean),
                                         -static_cast<double>(limits.max_cut_db),
                                         static_cast<double>(limits.max_boost_db)));
}

// ── 3. Cumulative spectrum ─────────────────────────────────────────────────

/// The normalised cumulative of a power spectrum on a uniform bin grid. Bin k
/// spans [(k - 1/2) df, (k + 1/2) df]; the energy is linear inside a bin.
class SpectrumCdf {
public:
    /// Control thread: size for @p bins.
    void prepare(std::size_t bins) { cumulative_.assign(bins + 1, 0.0); }

    /// Allocation-free once prepared for at least power.size() bins.
    void assign(std::span<const double> power, double bin_hz) noexcept {
        bin_hz_ = bin_hz > 0.0 ? bin_hz : 1.0;
        bins_ = std::min(power.size(), cumulative_.empty() ? 0 : cumulative_.size() - 1);
        if (bins_ == 0) return;
        cumulative_[0] = 0.0;
        double sum = 0.0;
        for (std::size_t k = 0; k < bins_; ++k) {
            sum += std::max(0.0, power[k]);
            cumulative_[k + 1] = sum;
        }
        if (sum > 0.0)
            for (std::size_t k = 0; k <= bins_; ++k) cumulative_[k] /= sum;
    }

    /// Energy between two frequencies; the whole spectrum weighs 1.
    [[nodiscard]] double weight(double lo_hz, double hi_hz) const noexcept {
        if (bins_ == 0 || !(hi_hz > lo_hz)) return 0.0;
        return at_(hi_hz) - at_(lo_hz);
    }

private:
    [[nodiscard]] double at_(double hz) const noexcept {
        const double x = std::max(0.0, hz / bin_hz_ + 0.5);
        const auto i = static_cast<std::size_t>(std::floor(x));
        if (i >= bins_) return cumulative_[bins_];
        const double t = x - static_cast<double>(i);
        return cumulative_[i] + t * (cumulative_[i + 1] - cumulative_[i]);
    }

    std::vector<double> cumulative_;
    std::size_t bins_ = 0;
    double bin_hz_ = 1.0;
};

// ── 2. The long-term spectrum ──────────────────────────────────────────────

class LongTermSpectrum {
public:
    /// Control thread; allocates. @p prior: per-bin power on this estimator's
    /// grid (see bins_for()), any scale; empty means flat. The estimate IS the
    /// prior until the first audible frame.
    void prepare(double sample_rate, int channels, const LoudnessCompensationConfig& config,
                 std::span<const double> prior = {}) {
        sample_rate_ = sample_rate > 0.0 ? sample_rate : 48000.0;
        channels_ = std::max(1, channels);
        config_ = config;
        fft_size_ = fft_size_for(sample_rate_, config);
        hop_ = fft_size_ / 2;
        bins_ = fft_size_ / 2 + 1;
        bin_hz_ = sample_rate_ / static_cast<double>(fft_size_);
        fft_ = std::make_unique<pulp::signal::FftT<float>>(fft_size_);
        window_ = pulp::signal::WindowFunction::generate<float>(
            fft_size_, pulp::signal::WindowFunction::Type::hann);
        history_.assign(static_cast<std::size_t>(channels_ * fft_size_), 0.0f);
        frame_in_.assign(static_cast<std::size_t>(fft_size_), 0.0f);
        spectrum_.assign(static_cast<std::size_t>(fft_size_), {});
        frame_power_.assign(static_cast<std::size_t>(bins_), 0.0);
        k_power_.assign(static_cast<std::size_t>(bins_), 1.0);
        prior_.assign(static_cast<std::size_t>(bins_), 1.0);
        average_.assign(static_cast<std::size_t>(bins_), 0.0);
        blended_.assign(static_cast<std::size_t>(bins_), 0.0);
        flat_.assign(static_cast<std::size_t>(bins_), 1.0);
        cdf_.prepare(static_cast<std::size_t>(bins_));
        double window_energy = 0.0;
        for (const float w : window_) window_energy += static_cast<double>(w) * w;
        // Parseval over the half spectrum (FftT::forward_real is unit-scaled):
        // mean square of the windowed frame = 2 sum_half |X|^2 / (N sum w^2).
        power_to_mean_square_ = window_energy > 0.0
            ? 2.0 / (static_cast<double>(fft_size_) * window_energy) : 0.0;
        const auto k = pulp::signal::k_weighting_coefficients(sample_rate_);
        for (int b = 0; b < bins_; ++b)
            k_power_[static_cast<std::size_t>(b)] =
                k_weighting_power(static_cast<double>(b) * bin_hz_, k, sample_rate_);
        if (prior.size() >= static_cast<std::size_t>(bins_)) {
            double total = 0.0;
            for (int b = 0; b < bins_; ++b)
                total += std::max(0.0, prior[static_cast<std::size_t>(b)]);
            for (int b = 0; b < bins_; ++b)
                prior_[static_cast<std::size_t>(b)] = total > 0.0
                    ? std::max(0.0, prior[static_cast<std::size_t>(b)]) / total : 0.0;
        } else {
            for (auto& p : prior_) p = 1.0 / static_cast<double>(bins_);
        }
        const double hop_seconds = static_cast<double>(hop_) / sample_rate_;
        alpha_ = config.time_constant_seconds > 0.0
            ? 1.0 - std::exp(-hop_seconds / config.time_constant_seconds) : 1.0;
        prior_decay_ = config.prior_seconds > 0.0
            ? std::exp(-hop_seconds / config.prior_seconds) : 0.0;
        prepared_ = fft_ && fft_->ready() && static_cast<int>(window_.size()) == fft_size_;
        reset();
    }

    /// The FFT size prepare() picks for a rate and config.
    [[nodiscard]] static int fft_size_for(double sample_rate,
                                          const LoudnessCompensationConfig& config) noexcept {
        if (config.fft_size > 0) {
            int n = 64;
            while (n < config.fft_size && n < 65536) n *= 2;
            return n;
        }
        int n = 1024;
        while (static_cast<double>(n) < config.frame_seconds * sample_rate && n < 65536) n *= 2;
        return n;
    }
    [[nodiscard]] static int bins_for(double sample_rate,
                                      const LoudnessCompensationConfig& config) noexcept {
        return fft_size_for(sample_rate, config) / 2 + 1;
    }

    /// Back to the prior; the frame grid restarts at the next sample.
    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        std::fill(average_.begin(), average_.end(), 0.0);
        prior_weight_ = 1.0;
        observed_frames_ = 0;
        gated_frames_ = 0;
        write_pos_ = 0;
        filled_ = 0;
        hop_pos_ = 0;
        publish_();
    }

    /// Feed planar samples. `on_frame(samples_consumed_in_this_call)` runs
    /// after every frame the call completes (audible or gated), with the
    /// estimate already updated.
    template <typename OnFrame>
    void push(const float* const* x, int channels, int num_samples, OnFrame&& on_frame) noexcept {
        if (!prepared_ || x == nullptr || num_samples <= 0) return;
        const int count = std::min(channels, channels_);
        int done = 0;
        while (done < num_samples) {
            const int chunk = std::min(num_samples - done, hop_ - hop_pos_);
            for (int ch = 0; ch < count; ++ch) {
                const float* src = x[ch];
                if (src == nullptr) continue;
                float* ring = history_.data() + static_cast<std::size_t>(ch * fft_size_);
                int pos = write_pos_;
                for (int i = 0; i < chunk; ++i) {
                    const float v = src[done + i];
                    ring[pos] = std::isfinite(v) ? v : 0.0f;
                    if (++pos == fft_size_) pos = 0;
                }
            }
            write_pos_ = (write_pos_ + chunk) % fft_size_;
            filled_ = std::min(fft_size_, filled_ + chunk);
            hop_pos_ += chunk;
            done += chunk;
            if (hop_pos_ == hop_) {
                hop_pos_ = 0;
                if (filled_ == fft_size_) {
                    frame_();
                    on_frame(done);
                }
            }
        }
    }
    void push(const float* const* x, int channels, int num_samples) noexcept {
        push(x, channels, num_samples, [](int) {});
    }

    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] int fft_size() const noexcept { return fft_size_; }
    [[nodiscard]] int hop() const noexcept { return hop_; }
    [[nodiscard]] int bins() const noexcept { return bins_; }
    [[nodiscard]] double bin_hz() const noexcept { return bin_hz_; }
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] const LoudnessCompensationConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::uint64_t observed_frames() const noexcept { return observed_frames_; }
    [[nodiscard]] std::uint64_t gated_frames() const noexcept { return gated_frames_; }
    /// K-weighted loudness of the last frame (LUFS-equivalent).
    [[nodiscard]] double last_frame_lufs() const noexcept { return last_frame_lufs_; }

    /// The current estimate per bin, K-weighted, normalised to sum 1 (the
    /// prior while nothing has been heard; flat when weight_by_material is
    /// off).
    [[nodiscard]] std::span<const double> spectrum() const noexcept {
        if (!config_.weight_by_material) return flat_;
        if (observed_frames_ == 0) return prior_;
        return blended_;
    }
    /// The prior's remaining share of the estimate (1 until material is heard).
    [[nodiscard]] double prior_weight() const noexcept { return prior_weight_; }
    /// The estimate's energy between two frequencies; the whole weighs 1.
    [[nodiscard]] double weight(double lo_hz, double hi_hz) const noexcept {
        return cdf_.weight(lo_hz, hi_hz);
    }
    [[nodiscard]] const SpectrumCdf& cdf() const noexcept { return cdf_; }

private:
    void frame_() noexcept {
        std::fill(frame_power_.begin(), frame_power_.end(), 0.0);
        for (int ch = 0; ch < channels_; ++ch) {
            const float* ring = history_.data() + static_cast<std::size_t>(ch * fft_size_);
            // Oldest sample first: write_pos_ is where the next one lands.
            for (int i = 0; i < fft_size_; ++i) {
                int at = write_pos_ + i;
                if (at >= fft_size_) at -= fft_size_;
                frame_in_[static_cast<std::size_t>(i)] =
                    ring[at] * window_[static_cast<std::size_t>(i)];
            }
            fft_->forward_real(frame_in_.data(), spectrum_.data());
            for (int b = 0; b < bins_; ++b)
                frame_power_[static_cast<std::size_t>(b)] +=
                    static_cast<double>(std::norm(spectrum_[static_cast<std::size_t>(b)]));
        }
        double total = 0.0;
        for (int b = 0; b < bins_; ++b) {
            auto& p = frame_power_[static_cast<std::size_t>(b)];
            p *= k_power_[static_cast<std::size_t>(b)];
            total += p;
        }
        const double mean_square = total * power_to_mean_square_;
        last_frame_lufs_ = mean_square > 0.0 ? -0.691 + 10.0 * std::log10(mean_square) : -1.0e9;
        if (!(last_frame_lufs_ >= config_.gate_lufs)) {
            ++gated_frames_;
            return;
        }
        ++observed_frames_;
        // The material's own average: a running mean until it has as many
        // frames as the time constant spans, an exponential average after,
        // so it is unbiased from its first frame.
        const double running = 1.0 / static_cast<double>(observed_frames_);
        const double a = alpha_ >= 1.0 ? 1.0 : std::max(alpha_, running);
        double average_total = 0.0;
        for (int b = 0; b < bins_; ++b) {
            auto& v = average_[static_cast<std::size_t>(b)];
            v += a * (frame_power_[static_cast<std::size_t>(b)] - v);
            average_total += v;
        }
        // Blend with the prior, whose say decays with audible material.
        prior_weight_ *= prior_decay_;
        const double material = average_total > 0.0 ? (1.0 - prior_weight_) / average_total : 0.0;
        for (int b = 0; b < bins_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            blended_[i] = prior_weight_ * prior_[i] + material * average_[i];
        }
        publish_();
    }

    void publish_() noexcept { cdf_.assign(spectrum(), bin_hz_); }

    double sample_rate_ = 48000.0;
    int channels_ = 1;
    LoudnessCompensationConfig config_{};
    int fft_size_ = 4096;
    int hop_ = 2048;
    int bins_ = 2049;
    double bin_hz_ = 48000.0 / 4096.0;
    double power_to_mean_square_ = 0.0;
    double alpha_ = 0.0;
    double prior_decay_ = 0.0;
    double prior_weight_ = 1.0;
    bool prepared_ = false;
    std::unique_ptr<pulp::signal::FftT<float>> fft_;
    std::vector<float> window_;
    std::vector<float> history_;
    std::vector<float> frame_in_;
    std::vector<std::complex<float>> spectrum_;
    std::vector<double> frame_power_;
    std::vector<double> k_power_;
    std::vector<double> prior_;
    std::vector<double> average_;
    std::vector<double> blended_;
    std::vector<double> flat_;
    SpectrumCdf cdf_;
    std::uint64_t observed_frames_ = 0;
    std::uint64_t gated_frames_ = 0;
    double last_frame_lufs_ = -1.0e9;
    int write_pos_ = 0;
    int filled_ = 0;
    int hop_pos_ = 0;
};

// ── 5. The target ───────────────────────────────────────────────────────────

/// What the make-up gain is heading for. A shape edit (or a switch-on) moves
/// it at once -- the user asked for it; the material moves it no faster than
/// the slew limit. The caller ramps the applied gain toward it.
class MakeupTarget {
public:
    void reset() noexcept { primed_ = false; value_db_ = 0.0f; }
    [[nodiscard]] bool primed() const noexcept { return primed_; }
    [[nodiscard]] float value_db() const noexcept { return value_db_; }

    /// Jump to @p db. Returns whether the target changed.
    bool retarget(float db) noexcept {
        const bool moved = !primed_ || db != value_db_;
        value_db_ = db;
        primed_ = true;
        return moved;
    }

    /// Move toward @p wanted_db by at most slew x @p seconds (no limit when
    /// the slew is <= 0). Returns whether the target changed.
    bool follow(float wanted_db, double seconds, double slew_db_per_second) noexcept {
        if (!primed_) return retarget(wanted_db);
        float next = wanted_db;
        if (slew_db_per_second > 0.0) {
            const auto step = static_cast<float>(slew_db_per_second * std::max(0.0, seconds));
            next = value_db_ + std::clamp(wanted_db - value_db_, -step, step);
        }
        if (next == value_db_) return false;
        value_db_ = next;
        return true;
    }

private:
    bool primed_ = false;
    float value_db_ = 0.0f;
};

// SPECTR-RENDER-PATH END

} // namespace pulp_candidate::signal
