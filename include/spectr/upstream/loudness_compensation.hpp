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
/// THE MODEL. A shaping effect's output is a blend of a WET leg (the shaped
/// signal, w, through the response H) and a DRY leg (d) at a mix m:
///
///   y = m H w + (1 - m) d
///
/// With long-term, K-weighted per-bin spectra Pww = <|W|^2>, Pdd = <|D|^2>
/// and the cross-spectrum Pwd = <W conj(D)>, the output power per bin is
///
///   m^2 |H|^2 Pww + (1-m)^2 Pdd + 2 m (1-m) Re(H Pwd)
///
/// and the make-up is the ratio of the flat (H = 1) output to this, summed
/// over bins: -10 log10(sum out / sum flat), clamped. When the wet leg IS the
/// dry leg (an ordinary effect) all three spectra are one P and this is
/// sum P |m H + 1 - m|^2 / sum P; when the wet leg is unrelated to the dry one
/// (a freeze or a hold playing while the input moves on) the cross term
/// vanishes and the two legs add as powers. Nothing has to be told which case
/// it is in: the cross-spectrum measures it. The phase of H matters only
/// through the cross term, i.e. only below 100 % mix.
///
/// THE PIPELINE, exposed stage by stage:
///
///   1. k_weighting_power()         |K(f)|^2 of BS.1770 at a frequency.
///   2. LongTermSpectrum            stateful, RT-safe: Hann/FFT frames on a
///                                  fixed sample grid, K-weighted,
///                                  energy-weighted exponential averages of
///                                  Pww (and, when tracking a dry leg, Pdd and
///                                  Pwd), BS.1770-style absolute and relative
///                                  gating, starting from a caller-supplied
///                                  prior whose say decays quickly.
///   3. MinimumPhaseResponse        the minimum-phase response with a given
///                                  magnitude (folded cepstrum), for a
///                                  realisation that designs minimum phase.
///   4. blend_makeup_gain_db()      PURE: the model above.
///      makeup_gain_db()            PURE: the single-leg, real-response form.
///      band_makeup_gain_db()       PURE: the same over bands, weighted by any
///      SpectrumCdf                 cumulative weighting.
///   5. MakeupTarget                stateful: an edit moves the target at once;
///                                  the material moves it no faster than a slew
///                                  limit.
///
/// RT CONTRACT. prepare() allocates (control thread). Everything else --
/// reset(), push(), MinimumPhaseResponse::compute(), the pure functions,
/// SpectrumCdf::assign() into a prepared size, MakeupTarget -- is
/// allocation-free, lock-free and reads no clock. LongTermSpectrum advances
/// on a frame grid counted in samples from prepare()/reset(), so its state
/// after N samples does not depend on how the N samples were chunked.

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
    /// BS.1770's relative gate, LU: a frame more than this far below the
    /// running energy average of the frames that passed gate_lufs does not
    /// shape the spectrum, as integrated loudness does not count it either.
    /// <= 0: off.
    double relative_gate_lu = 10.0;
    /// Fastest the material may move the make-up target, dB/s. <= 0: no limit.
    double material_slew_db_per_second = 6.0;
    /// Make-up range.
    float max_cut_db = 24.0f;
    float max_boost_db = 24.0f;
    /// false: every bin weighs the same (ignores the material and K). Exists
    /// as a measurable negative control, not as a mode.
    bool weight_by_material = true;
    /// true: push() takes a dry leg too, and the estimator keeps Pdd and Pwd.
    bool track_dry_leg = false;
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

/// The long-term spectra of the two legs on one bin grid, K-weighted, on a
/// common scale. Single-leg estimates have dd and wd_re equal to ww and
/// wd_im zero.
struct LegSpectra {
    std::span<const double> ww;
    std::span<const double> dd;
    std::span<const double> wd_re;
    std::span<const double> wd_im;
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

[[nodiscard]] inline float clamp_makeup_db_(double ratio, MakeupLimits limits) noexcept {
    if (!(ratio > 0.0)) return limits.max_boost_db;
    return static_cast<float>(std::clamp(-10.0 * std::log10(ratio),
                                         -static_cast<double>(limits.max_cut_db),
                                         static_cast<double>(limits.max_boost_db)));
}

/// Make-up, dB, for the two-leg model in the file comment: @p response is
/// the wet leg's complex response per bin, @p s the legs' spectra on the same
/// grid, @p mix the wet share. 0 dB when there is nothing to measure; the
/// boost limit when the response removes everything.
[[nodiscard]] inline float blend_makeup_gain_db(std::span<const std::complex<double>> response,
                                                const LegSpectra& s, double mix,
                                                MakeupLimits limits) noexcept {
    const std::size_t n = std::min({response.size(), s.ww.size(), s.dd.size(),
                                    s.wd_re.size(), s.wd_im.size()});
    const double m = std::clamp(mix, 0.0, 1.0);
    const double a = m * m, b = (1.0 - m) * (1.0 - m), c = 2.0 * m * (1.0 - m);
    double out = 0.0, flat = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const auto h = response[k];
        out += a * std::norm(h) * s.ww[k] + b * s.dd[k]
             + c * (h.real() * s.wd_re[k] - h.imag() * s.wd_im[k]);
        flat += a * s.ww[k] + b * s.dd[k] + c * s.wd_re[k];
    }
    if (!(flat > 0.0)) return 0.0f;
    return clamp_makeup_db_(std::max(0.0, out) / flat, limits);
}

/// The single-leg, real-response form: per-bin power response |H|^2 against
/// a per-bin power spectrum on the same grid. A flat response is 0 dB.
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
    return clamp_makeup_db_(energy / total, limits);
}

/// One band of a band-wise response: its frequency span and power gain.
struct BandPowerGain {
    double lo_hz = 0.0;
    double hi_hz = 0.0;
    double power_gain = 1.0;
};

/// The single-leg form over bands, each weighted by `weight(lo_hz, hi_hz)`
/// -- the material's energy between two frequencies (SpectrumCdf::weight, or
/// any reference weighting).
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
    return clamp_makeup_db_(energy / total, limits);
}

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

// ── 3. Minimum-phase response ───────────────────────────────────────────────

/// The minimum-phase response with a given magnitude on an FFT grid, by the
/// folded real cepstrum: ln|H| -> cepstrum -> fold onto positive quefrency ->
/// exp(FFT). The magnitude is floored at @p floor first, as a minimum-phase
/// designer floors it.
class MinimumPhaseResponse {
public:
    /// Control thread; allocates. @p fft_size: the grid (bins = N/2 + 1).
    void prepare(int fft_size) {
        n_ = fft_size;
        fft_ = std::make_unique<pulp::signal::FftT<float>>(n_);
        work_.assign(static_cast<std::size_t>(n_), {});
    }
    [[nodiscard]] bool prepared() const noexcept { return fft_ && fft_->ready(); }

    /// @p magnitude: N/2 + 1 linear magnitudes. @p out: N/2 + 1 values.
    bool compute(std::span<const double> magnitude, double floor,
                 std::span<std::complex<double>> out) noexcept {
        const auto bins = static_cast<std::size_t>(n_ / 2 + 1);
        if (!prepared() || magnitude.size() < bins || out.size() < bins) return false;
        const double lo = floor > 0.0 ? floor : 1.0e-12;
        for (std::size_t k = 0; k < bins; ++k)
            work_[k] = {static_cast<float>(std::log(std::max(lo, magnitude[k]))), 0.0f};
        for (std::size_t k = bins; k < static_cast<std::size_t>(n_); ++k)
            work_[k] = work_[static_cast<std::size_t>(n_) - k];
        fft_->inverse(work_.data());  // real, even cepstrum
        const auto half = static_cast<std::size_t>(n_ / 2);
        for (std::size_t q = 1; q < half; ++q) {
            work_[q] = {2.0f * work_[q].real(), 0.0f};
            work_[static_cast<std::size_t>(n_) - q] = {0.0f, 0.0f};
        }
        work_[0] = {work_[0].real(), 0.0f};
        work_[half] = {work_[half].real(), 0.0f};
        fft_->forward(work_.data());  // ln|H| + j phase
        for (std::size_t k = 0; k < bins; ++k)
            out[k] = std::exp(std::complex<double>(work_[k].real(), work_[k].imag()));
        return true;
    }

private:
    int n_ = 0;
    std::unique_ptr<pulp::signal::FftT<float>> fft_;
    std::vector<std::complex<float>> work_;
};

// ── 2. The long-term spectrum ──────────────────────────────────────────────

class LongTermSpectrum {
public:
    /// Control thread; allocates. @p prior: per-bin power on this estimator's
    /// grid (see bins_for()), any scale; empty means flat. The estimate IS the
    /// prior until the first audible frame (both legs, coherent).
    void prepare(double sample_rate, int channels, const LoudnessCompensationConfig& config,
                 std::span<const double> prior = {}) {
        sample_rate_ = sample_rate > 0.0 ? sample_rate : 48000.0;
        channels_ = std::max(1, channels);
        config_ = config;
        fft_size_ = fft_size_for(sample_rate_, config);
        hop_ = fft_size_ / 2;
        bins_ = fft_size_ / 2 + 1;
        bin_hz_ = sample_rate_ / static_cast<double>(fft_size_);
        const auto nb = static_cast<std::size_t>(bins_);
        fft_ = std::make_unique<pulp::signal::FftT<float>>(fft_size_);
        window_ = pulp::signal::WindowFunction::generate<float>(
            fft_size_, pulp::signal::WindowFunction::Type::hann);
        const int legs = config.track_dry_leg ? 2 : 1;
        history_.assign(static_cast<std::size_t>(legs * channels_ * fft_size_), 0.0f);
        frame_in_.assign(static_cast<std::size_t>(fft_size_), 0.0f);
        wet_spectrum_.assign(static_cast<std::size_t>(fft_size_), {});
        dry_spectrum_.assign(static_cast<std::size_t>(fft_size_), {});
        for (auto* v : {&frame_ww_, &frame_dd_, &frame_re_, &frame_im_, &avg_ww_, &avg_dd_,
                        &avg_re_, &avg_im_, &ww_, &dd_, &re_, &im_, &k_power_, &prior_})
            v->assign(nb, 0.0);
        flat_.assign(nb, 1.0);
        zero_.assign(nb, 0.0);
        cdf_.prepare(nb);
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
        double total = 0.0;
        if (prior.size() >= nb)
            for (std::size_t b = 0; b < nb; ++b) total += std::max(0.0, prior[b]);
        for (std::size_t b = 0; b < nb; ++b)
            prior_[b] = total > 0.0 ? std::max(0.0, prior[b]) / total
                                    : 1.0 / static_cast<double>(nb);
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
        for (auto* v : {&avg_ww_, &avg_dd_, &avg_re_, &avg_im_})
            std::fill(v->begin(), v->end(), 0.0);
        prior_weight_ = 1.0;
        level_ = 0.0;
        level_frames_ = 0;
        relative_gated_frames_ = 0;
        observed_frames_ = 0;
        gated_frames_ = 0;
        write_pos_ = 0;
        filled_ = 0;
        hop_pos_ = 0;
        blend_();
    }

    /// Feed planar samples of the wet leg and, when tracking it, the dry leg
    /// (nullptr: the dry leg is the wet one). `on_frame(samples_consumed)`
    /// runs after every frame the call completes (audible or gated), with the
    /// estimate already updated.
    template <typename OnFrame>
    void push(const float* const* wet, const float* const* dry, int channels, int num_samples,
              OnFrame&& on_frame) noexcept {
        if (!prepared_ || wet == nullptr || num_samples <= 0) return;
        const int count = std::min(channels, channels_);
        const bool two = config_.track_dry_leg;
        int done = 0;
        while (done < num_samples) {
            const int chunk = std::min(num_samples - done, hop_ - hop_pos_);
            for (int leg = 0; leg < (two ? 2 : 1); ++leg) {
                const float* const* x = leg == 0 ? wet : (dry != nullptr ? dry : wet);
                for (int ch = 0; ch < count; ++ch)
                    record_(ring_(leg, ch), x[ch], done, chunk);
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
    template <typename OnFrame>
    void push(const float* const* x, int channels, int num_samples, OnFrame&& on_frame) noexcept {
        push(x, nullptr, channels, num_samples, on_frame);
    }
    void push(const float* const* x, int channels, int num_samples) noexcept {
        push(x, nullptr, channels, num_samples, [](int) {});
    }

    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] int fft_size() const noexcept { return fft_size_; }
    [[nodiscard]] int hop() const noexcept { return hop_; }
    [[nodiscard]] int bins() const noexcept { return bins_; }
    [[nodiscard]] double bin_hz() const noexcept { return bin_hz_; }
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] const LoudnessCompensationConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::uint64_t observed_frames() const noexcept { return observed_frames_; }
    /// Frames under the absolute gate (silence) / under the relative gate.
    [[nodiscard]] std::uint64_t gated_frames() const noexcept { return gated_frames_; }
    [[nodiscard]] std::uint64_t relative_gated_frames() const noexcept {
        return relative_gated_frames_;
    }
    /// K-weighted loudness of the last frame's wet leg (LUFS-equivalent).
    [[nodiscard]] double last_frame_lufs() const noexcept { return last_frame_lufs_; }
    /// The prior's remaining share of the estimate (1 until material is heard).
    [[nodiscard]] double prior_weight() const noexcept { return prior_weight_; }

    /// The wet leg's estimate per bin, K-weighted, normalised to sum 1 (the
    /// prior while nothing has been heard; flat when weight_by_material is
    /// off).
    [[nodiscard]] std::span<const double> spectrum() const noexcept {
        return config_.weight_by_material ? std::span<const double>(ww_) : flat_;
    }
    /// All the legs' estimates on the wet leg's scale.
    [[nodiscard]] LegSpectra legs() const noexcept {
        if (!config_.weight_by_material) return {flat_, flat_, flat_, zero_};
        if (!config_.track_dry_leg) return {ww_, ww_, ww_, zero_};
        return {ww_, dd_, re_, im_};
    }
    /// The wet estimate's energy between two frequencies; the whole weighs 1.
    [[nodiscard]] double weight(double lo_hz, double hi_hz) const noexcept {
        return cdf_.weight(lo_hz, hi_hz);
    }
    [[nodiscard]] const SpectrumCdf& cdf() const noexcept { return cdf_; }

private:
    [[nodiscard]] float* ring_(int leg, int ch) noexcept {
        return history_.data()
            + static_cast<std::size_t>((leg * channels_ + ch) * fft_size_);
    }
    void record_(float* ring, const float* src, int offset, int count) noexcept {
        if (src == nullptr) return;
        int pos = write_pos_;
        for (int i = 0; i < count; ++i) {
            const float v = src[offset + i];
            ring[pos] = std::isfinite(v) ? v : 0.0f;
            if (++pos == fft_size_) pos = 0;
        }
    }
    void transform_(float* ring, std::vector<std::complex<float>>& out) noexcept {
        // Oldest sample first: write_pos_ is where the next one lands.
        for (int i = 0; i < fft_size_; ++i) {
            int at = write_pos_ + i;
            if (at >= fft_size_) at -= fft_size_;
            frame_in_[static_cast<std::size_t>(i)] = ring[at] * window_[static_cast<std::size_t>(i)];
        }
        fft_->forward_real(frame_in_.data(), out.data());
    }

    void frame_() noexcept {
        const bool two = config_.track_dry_leg;
        for (auto* v : {&frame_ww_, &frame_dd_, &frame_re_, &frame_im_})
            std::fill(v->begin(), v->end(), 0.0);
        for (int ch = 0; ch < channels_; ++ch) {
            transform_(ring_(0, ch), wet_spectrum_);
            if (two) transform_(ring_(1, ch), dry_spectrum_);
            for (int b = 0; b < bins_; ++b) {
                const auto i = static_cast<std::size_t>(b);
                const std::complex<double> w(wet_spectrum_[i].real(), wet_spectrum_[i].imag());
                frame_ww_[i] += std::norm(w);
                if (two) {
                    const std::complex<double> d(dry_spectrum_[i].real(), dry_spectrum_[i].imag());
                    frame_dd_[i] += std::norm(d);
                    const auto x = w * std::conj(d);
                    frame_re_[i] += x.real();
                    frame_im_[i] += x.imag();
                }
            }
        }
        double total = 0.0;
        for (int b = 0; b < bins_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            const double kp = k_power_[i];
            frame_ww_[i] *= kp;
            frame_dd_[i] *= kp;
            frame_re_[i] *= kp;
            frame_im_[i] *= kp;
            total += frame_ww_[i];
        }
        const double mean_square = total * power_to_mean_square_;
        last_frame_lufs_ = mean_square > 0.0 ? -0.691 + 10.0 * std::log10(mean_square) : -1.0e9;
        if (!(last_frame_lufs_ >= config_.gate_lufs)) {
            ++gated_frames_;
            return;
        }
        // The running level the relative gate is measured against: every
        // frame above the absolute gate, energy-averaged like the spectrum.
        ++level_frames_;
        const double level_a = alpha_ >= 1.0 ? 1.0
            : std::max(alpha_, 1.0 / static_cast<double>(level_frames_));
        level_ += level_a * (mean_square - level_);
        if (config_.relative_gate_lu > 0.0
            && mean_square < level_ * std::pow(10.0, -config_.relative_gate_lu / 10.0)) {
            ++relative_gated_frames_;
            return;
        }
        ++observed_frames_;
        // The material's own averages: a running mean until they hold as
        // many frames as the time constant spans, an exponential average
        // after, so they are unbiased from the first frame.
        const double running = 1.0 / static_cast<double>(observed_frames_);
        const double a = alpha_ >= 1.0 ? 1.0 : std::max(alpha_, running);
        for (int b = 0; b < bins_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            avg_ww_[i] += a * (frame_ww_[i] - avg_ww_[i]);
            if (two) {
                avg_dd_[i] += a * (frame_dd_[i] - avg_dd_[i]);
                avg_re_[i] += a * (frame_re_[i] - avg_re_[i]);
                avg_im_[i] += a * (frame_im_[i] - avg_im_[i]);
            }
        }
        prior_weight_ *= prior_decay_;
        blend_();
    }

    // The published estimate: the prior's decaying share plus the material's
    // averages, all on the wet leg's scale (its total is 1 - prior share).
    void blend_() noexcept {
        double total = 0.0;
        if (observed_frames_ > 0)
            for (const double v : avg_ww_) total += v;
        const double pw = observed_frames_ > 0 && total > 0.0 ? prior_weight_ : 1.0;
        const double scale = total > 0.0 ? (1.0 - pw) / total : 0.0;
        const bool two = config_.track_dry_leg;
        for (int b = 0; b < bins_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            const double p = pw * prior_[i];
            ww_[i] = p + scale * avg_ww_[i];
            if (two) {
                dd_[i] = p + scale * avg_dd_[i];
                re_[i] = p + scale * avg_re_[i];
                im_[i] = scale * avg_im_[i];
            }
        }
        cdf_.assign(spectrum(), bin_hz_);
    }

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
    std::vector<std::complex<float>> wet_spectrum_;
    std::vector<std::complex<float>> dry_spectrum_;
    std::vector<double> frame_ww_, frame_dd_, frame_re_, frame_im_;
    std::vector<double> avg_ww_, avg_dd_, avg_re_, avg_im_;
    std::vector<double> ww_, dd_, re_, im_;
    std::vector<double> k_power_;
    std::vector<double> prior_;
    std::vector<double> flat_;
    std::vector<double> zero_;
    SpectrumCdf cdf_;
    std::uint64_t observed_frames_ = 0;
    std::uint64_t gated_frames_ = 0;
    std::uint64_t relative_gated_frames_ = 0;
    std::uint64_t level_frames_ = 0;
    double level_ = 0.0;
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
