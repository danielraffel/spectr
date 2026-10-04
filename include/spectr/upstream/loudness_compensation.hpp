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
///                                  energy-weighted slow (3 s) and fast (0.4 s)
///                                  averages of Pww (and, tracking a dry leg,
///                                  Pdd and Pwd); an absolute silence gate; a
///                                  level-drop rule; restart-from-fast on a
///                                  change of material; a warm start from a
///                                  saved SpectrumBands; a cold start from a
///                                  caller-supplied prior whose say decays.
///      MaterialChangeDetector      when the fast and slow make-up disagree.
///   3. MinimumPhaseResponse        the minimum-phase response with a given
///                                  magnitude (folded cepstrum), for a
///                                  realisation that designs minimum phase.
///   4. blend_makeup_gain_db()      PURE: the model above.
///      makeup_gain_db()            PURE: the single-leg, real-response form.
///      band_makeup_gain_db()       PURE: the same over bands, weighted by any
///      SpectrumCdf                 cumulative weighting.
///   5. MakeupTarget                stateful: an edit moves the target at once;
///                                  the material moves it with a slew that is
///                                  slow near the target and proportional to
///                                  the distance far from it.
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
#include <array>
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
    /// How long the prior keeps a say once material is heard: its weight
    /// falls linearly from 1 to 0 over this much audible material, so the
    /// estimate starts AT the prior and is the material's own after it. Short
    /// and finite on purpose: a prior with energy where the material has none
    /// dominates any boost there for as long as any of it lingers (an
    /// exponential fade left 20 dB of error for seconds).
    double prior_seconds = 0.5;
    /// Frames below this K-weighted loudness (LUFS-equivalent over the frame,
    /// channels summed as BS.1770 sums them) are not material: the estimate
    /// holds through them.
    double gate_lufs = -60.0;
    /// Time constant of the FAST estimate, seconds: the change detector's,
    /// and what the slow estimate restarts from when the material changes.
    double fast_time_constant_seconds = 0.4;
    /// Level-drop rule: when no audible frame in the last
    /// level_drop_window_seconds came within level_drop_db of the slow
    /// estimate's level, both estimates are rescaled to the recent level so
    /// quieter new material is not outweighed by the louder past. <= 0: off.
    double level_drop_db = 10.0;
    double level_drop_window_seconds = 0.6;
    /// Slowest rate the material moves the make-up target, dB/s (<= 0: no
    /// limit) -- and the proportional speed-up for a far target (see
    /// MakeupTarget::follow).
    double material_slew_db_per_second = 6.0;
    double material_slew_rate_per_second = 6.0;
    double material_slew_max_db_per_second = 80.0;
    /// The proportional speed-up counts only the distance beyond this, so a
    /// groove's wobble around the target stays on the slow floor.
    double material_slew_knee_db = 0.0;
    /// Change detector (MaterialChangeDetector): the fast and slow estimates'
    /// make-up for the current shape disagree by more than this for this many
    /// audible frames.
    double change_threshold_db = 3.0;
    int change_frames = 3;
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

/// A band-compressed, rate-independent copy of a LongTermSpectrum's estimate,
/// for saving with a session and restoring warm. kBands log-spaced bands from
/// 10 Hz to 24 kHz (the first band reaches down to DC, the last up to
/// Nyquist). Powers are normalised so the wet leg sums to 1; `level_ms` is
/// the wet leg's K-weighted mean square, which restores the level the
/// level-drop rule compares against.
struct SpectrumBands {
    static constexpr int kBands = 160;
    static constexpr double kLowHz = 10.0;
    static constexpr double kHighHz = 24000.0;
    bool valid = false;
    double level_ms = 0.0;
    std::array<float, kBands> ww{}, dd{}, re{}, im{};

    [[nodiscard]] static int band_of(double hz) noexcept {
        if (!(hz > kLowHz)) return 0;
        const double x = std::log(hz / kLowHz) / std::log(kHighHz / kLowHz)
                         * static_cast<double>(kBands);
        return std::clamp(static_cast<int>(std::floor(x)), 0, kBands - 1);
    }
    [[nodiscard]] static double centre_hz(int band) noexcept {
        return kLowHz * std::pow(kHighHz / kLowHz,
                                 (static_cast<double>(band) + 0.5) / static_cast<double>(kBands));
    }
};

/// Detects a genuine change of material from the gap between the make-up the
/// fast and the slow estimates give the current shape: more than
/// `threshold_db` apart for `frames` audible frames in a row, outside a
/// hold-off after the last change. Pure bookkeeping; deterministic.
class MaterialChangeDetector {
public:
    double threshold_db = 3.0;
    int frames = 3;
    int holdoff_frames = 6;

    void reset() noexcept { run_ = 0; holdoff_ = 0; }
    /// One audible frame. Returns true when the material has changed.
    bool observe(float fast_db, float slow_db) noexcept {
        if (holdoff_ > 0) { --holdoff_; run_ = 0; return false; }
        if (std::abs(static_cast<double>(fast_db) - slow_db) > threshold_db) ++run_;
        else run_ = 0;
        if (run_ < frames) return false;
        run_ = 0;
        holdoff_ = holdoff_frames;
        return true;
    }
    void hold_off() noexcept { run_ = 0; holdoff_ = holdoff_frames; }

private:
    int run_ = 0;
    int holdoff_ = 0;
};

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
        for (auto* v : {&frame_ww_, &frame_dd_, &frame_re_, &frame_im_, &k_power_, &prior_})
            v->assign(nb, 0.0);
        slow_.assign(nb);
        fast_.assign(nb);
        fast_shape_.assign(nb, 0.0);
        slow_shape_.assign(nb, 0.0);
        size_published_();
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
        fast_alpha_ = config.fast_time_constant_seconds > 0.0
            ? 1.0 - std::exp(-hop_seconds / config.fast_time_constant_seconds) : 1.0;
        prior_step_ = config.prior_seconds > 0.0
            ? hop_seconds / config.prior_seconds : 1.0;
        level_drop_frames_ = std::clamp(static_cast<int>(std::ceil(
            config.level_drop_window_seconds / hop_seconds)), 1, kMaxRecent);
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

    /// Cold start: back to the prior, and the frame grid restarts at the next
    /// sample.
    void reset() noexcept {
        slow_.clear();
        fast_.clear();
        slow_frames_ = 0;
        fast_frames_ = 0;
        shape_frames_ = 0;
        std::fill(fast_shape_.begin(), fast_shape_.end(), 0.0);
        std::fill(slow_shape_.begin(), slow_shape_.end(), 0.0);
        prior_weight_ = 1.0;
        observed_frames_ = 0;
        gated_frames_ = 0;
        level_drops_ = 0;
        restarts_ = 0;
        recent_count_ = 0;
        recent_pos_ = 0;
        drop_run_ = 0;
        settle_frames_ = 0;
        restart_grid();
        publish_();
    }

    /// A transport jump or host reset: the frame grid restarts at the next
    /// sample (so how the stream is chopped still cannot matter), and the
    /// estimate -- what the material sounds like -- is kept.
    void restart_grid() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        write_pos_ = 0;
        filled_ = 0;
        hop_pos_ = 0;
    }

    /// The material changed: both estimates forget it and start again as
    /// running means of what comes next. Energy weighting is why: seeded from
    /// anything that still holds the old material, a louder past would
    /// outweigh quieter new material for seconds. Until the first new frame
    /// lands the published estimate stays what it was.
    void restart() noexcept {
        slow_.clear();
        fast_.clear();
        std::copy(fast_shape_.begin(), fast_shape_.end(), slow_shape_.begin());
        shape_frames_ = std::min<std::int64_t>(shape_frames_, fast_equivalent_frames_());
        slow_frames_ = 0;
        fast_frames_ = 0;
        recent_count_ = 0;
        drop_run_ = 0;
        // Detection takes several frames, so the next frame's window lies
        // after the change: nothing to skip.
        ++restarts_;
    }
    /// Audible frames averaged since the last restart (or cold start).
    [[nodiscard]] std::int64_t frames_since_restart() const noexcept { return slow_frames_; }

    /// Freeze released: the wet leg becomes the live input again, whose
    /// spectrum the dry leg kept warm through the hold. Both estimates take
    /// the FAST dry estimate (the live input as it is now, not as it was
    /// when the hold began), and the slow one goes on as a running mean.
    void wet_becomes_dry() noexcept {
        if (!config_.track_dry_leg) return;
        std::copy(fast_.dd.begin(), fast_.dd.end(), fast_.ww.begin());
        std::copy(fast_.dd.begin(), fast_.dd.end(), fast_.re.begin());
        std::fill(fast_.im.begin(), fast_.im.end(), 0.0);
        slow_.copy_from(fast_);
        slow_frames_ = std::max<std::int64_t>(1, fast_equivalent_frames_());
        {
            const double t = fast_.total();
            for (int b = 0; b < bins_; ++b) {
                const auto i = static_cast<std::size_t>(b);
                slow_shape_[i] = fast_shape_[i] = t > 0.0 ? fast_.ww[i] / t : 0.0;
            }
            shape_frames_ = slow_frames_;
        }
        recent_count_ = 0;
        // The next frames' windows still hold the hold's last moments: they
        // describe neither leg, so they wait.
        settle_frames_ = fft_size_ / std::max(1, hop_);
        publish_();
    }

    /// Freeze engaged: the wet leg is now a held past, unrelated to the live
    /// input from here on.
    void legs_uncorrelated() noexcept {
        if (!config_.track_dry_leg) return;
        for (auto* e : {&slow_, &fast_}) {
            std::fill(e->re.begin(), e->re.end(), 0.0);
            std::fill(e->im.begin(), e->im.end(), 0.0);
        }
        publish_();
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

    // ── session state ──────────────────────────────────────────────────────
    /// The slow estimate, band-compressed (allocation-free).
    void export_bands(SpectrumBands& out) const noexcept {
        out = SpectrumBands{};
        if (!prepared_ || observed_frames_ == 0 && prior_weight_ >= 1.0) return;
        const auto& e = published_;
        double total = 0.0;
        for (int b = 0; b < bins_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            const int band = SpectrumBands::band_of(static_cast<double>(b) * bin_hz_);
            out.ww[static_cast<std::size_t>(band)] += static_cast<float>(e.ww[i]);
            out.dd[static_cast<std::size_t>(band)] += static_cast<float>(e.dd[i]);
            out.re[static_cast<std::size_t>(band)] += static_cast<float>(e.re[i]);
            out.im[static_cast<std::size_t>(band)] += static_cast<float>(e.im[i]);
            total += e.ww[i];
        }
        if (!(total > 0.0)) return;
        for (int band = 0; band < SpectrumBands::kBands; ++band) {
            const auto j = static_cast<std::size_t>(band);
            out.ww[j] = static_cast<float>(out.ww[j] / total);
            out.dd[j] = static_cast<float>(out.dd[j] / total);
            out.re[j] = static_cast<float>(out.re[j] / total);
            out.im[j] = static_cast<float>(out.im[j] / total);
        }
        out.level_ms = level_total_() * power_to_mean_square_;
        out.valid = true;
    }

    /// Start warm from a saved estimate: no prior, no cold start; the frame
    /// grid restarts. A band's energy is spread evenly over the bins whose
    /// centres fall in it (or given to the bin nearest its centre).
    void import_bands(const SpectrumBands& in) noexcept {
        if (!prepared_ || !in.valid || !(in.level_ms > 0.0) || !(power_to_mean_square_ > 0.0))
            return;
        std::array<int, SpectrumBands::kBands> count{};
        for (int b = 0; b < bins_; ++b)
            ++count[static_cast<std::size_t>(SpectrumBands::band_of(static_cast<double>(b) * bin_hz_))];
        slow_.clear();
        const double total = in.level_ms / power_to_mean_square_;
        for (int band = 0; band < SpectrumBands::kBands; ++band) {
            const auto j = static_cast<std::size_t>(band);
            if (count[j] > 0) continue;
            const auto b = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(
                SpectrumBands::centre_hz(band) / bin_hz_)), 0, bins_ - 1));
            slow_.ww[b] += total * in.ww[j];
            slow_.dd[b] += total * in.dd[j];
            slow_.re[b] += total * in.re[j];
            slow_.im[b] += total * in.im[j];
        }
        for (int b = 0; b < bins_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            const auto j = static_cast<std::size_t>(
                SpectrumBands::band_of(static_cast<double>(b) * bin_hz_));
            if (count[j] == 0) continue;
            const double share = total / static_cast<double>(count[j]);
            slow_.ww[i] += share * in.ww[j];
            slow_.dd[i] += share * in.dd[j];
            slow_.re[i] += share * in.re[j];
            slow_.im[i] += share * in.im[j];
        }
        if (!config_.track_dry_leg) {
            slow_.dd = slow_.ww;
            slow_.re = slow_.ww;
            std::fill(slow_.im.begin(), slow_.im.end(), 0.0);
        }
        fast_.copy_from(slow_);
        {
            const double t = slow_.total();
            for (int b = 0; b < bins_; ++b) {
                const auto i = static_cast<std::size_t>(b);
                slow_shape_[i] = fast_shape_[i] = t > 0.0 ? slow_.ww[i] / t : 0.0;
            }
            shape_frames_ = slow_frames_;
        }
        prior_weight_ = 0.0;
        slow_frames_ = static_cast<std::int64_t>(std::ceil(1.0 / std::max(alpha_, 1e-9)));
        fast_frames_ = fast_equivalent_frames_();
        observed_frames_ = std::max<std::uint64_t>(observed_frames_, 1);
        recent_count_ = 0;
        drop_run_ = 0;
        restart_grid();
        publish_();
    }

    // ── readings ────────────────────────────────────────────────────────────
    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] int fft_size() const noexcept { return fft_size_; }
    [[nodiscard]] int hop() const noexcept { return hop_; }
    /// Samples until the next frame boundary of the grid.
    [[nodiscard]] int samples_to_next_frame() const noexcept { return hop_ - hop_pos_; }
    [[nodiscard]] int bins() const noexcept { return bins_; }
    [[nodiscard]] double bin_hz() const noexcept { return bin_hz_; }
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] double hop_seconds() const noexcept {
        return static_cast<double>(hop_) / sample_rate_;
    }
    [[nodiscard]] const LoudnessCompensationConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::uint64_t observed_frames() const noexcept { return observed_frames_; }
    /// Frames under the absolute gate (silence).
    [[nodiscard]] std::uint64_t gated_frames() const noexcept { return gated_frames_; }
    [[nodiscard]] std::uint64_t level_drops() const noexcept { return level_drops_; }
    [[nodiscard]] std::uint64_t restarts() const noexcept { return restarts_; }
    /// K-weighted loudness of the last frame's wet leg (LUFS-equivalent).
    [[nodiscard]] double last_frame_lufs() const noexcept { return last_frame_lufs_; }
    /// The prior's remaining share of the estimate (1 until material is heard).
    [[nodiscard]] double prior_weight() const noexcept { return prior_weight_; }
    /// Whether the last frame was audible (above the absolute gate).
    [[nodiscard]] bool last_frame_audible() const noexcept { return last_audible_; }

    /// The slow (wet) estimate per bin, K-weighted, normalised to sum 1.
    [[nodiscard]] std::span<const double> spectrum() const noexcept {
        return config_.weight_by_material ? std::span<const double>(published_.ww) : flat_;
    }
    /// All the legs' slow estimates on the wet leg's scale.
    [[nodiscard]] LegSpectra legs() const noexcept { return legs_of_(published_); }
    /// The same of the fast estimate.
    [[nodiscard]] LegSpectra fast_legs() const noexcept { return legs_of_(published_fast_); }
    /// The change detector's pair: fast and slow averages of each audible
    /// frame's wet SHAPE (level-independent), as coherent single-leg spectra.
    [[nodiscard]] LegSpectra fast_shape_legs() const noexcept {
        if (!config_.weight_by_material) return {flat_, flat_, flat_, zero_};
        return {fast_shape_, fast_shape_, fast_shape_, zero_};
    }
    [[nodiscard]] LegSpectra slow_shape_legs() const noexcept {
        if (!config_.weight_by_material) return {flat_, flat_, flat_, zero_};
        return {slow_shape_, slow_shape_, slow_shape_, zero_};
    }
    [[nodiscard]] std::int64_t shape_frames() const noexcept { return shape_frames_; }
    /// The slow wet estimate's energy between two frequencies; the whole weighs 1.
    [[nodiscard]] double weight(double lo_hz, double hi_hz) const noexcept {
        return cdf_.weight(lo_hz, hi_hz);
    }
    [[nodiscard]] const SpectrumCdf& cdf() const noexcept { return cdf_; }

private:
    static constexpr int kMaxRecent = 32;

    struct Estimate {
        std::vector<double> ww, dd, re, im;
        void assign(std::size_t n) {
            for (auto* v : {&ww, &dd, &re, &im}) v->assign(n, 0.0);
        }
        void clear() noexcept {
            for (auto* v : {&ww, &dd, &re, &im}) std::fill(v->begin(), v->end(), 0.0);
        }
        void copy_from(const Estimate& o) noexcept {
            std::copy(o.ww.begin(), o.ww.end(), ww.begin());
            std::copy(o.dd.begin(), o.dd.end(), dd.begin());
            std::copy(o.re.begin(), o.re.end(), re.begin());
            std::copy(o.im.begin(), o.im.end(), im.begin());
        }
        void scale(double k) noexcept {
            for (auto* v : {&ww, &dd, &re, &im})
                for (auto& x : *v) x *= k;
        }
        [[nodiscard]] double total() const noexcept {
            double t = 0.0;
            for (const double x : ww) t += x;
            return t;
        }
    };

    [[nodiscard]] LegSpectra legs_of_(const Estimate& e) const noexcept {
        if (!config_.weight_by_material) return {flat_, flat_, flat_, zero_};
        if (!config_.track_dry_leg) return {e.ww, e.ww, e.ww, zero_};
        return {e.ww, e.dd, e.re, e.im};
    }
    [[nodiscard]] std::int64_t fast_equivalent_frames_() const noexcept {
        return static_cast<std::int64_t>(std::ceil(1.0 / std::max(fast_alpha_, 1e-9)));
    }
    [[nodiscard]] double level_total_() const noexcept {
        return slow_frames_ > 0 ? slow_.total() : 0.0;
    }

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

    void accumulate_(Estimate& e, double a) noexcept {
        const bool two = config_.track_dry_leg;
        for (int b = 0; b < bins_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            e.ww[i] += a * (frame_ww_[i] - e.ww[i]);
            if (two) {
                e.dd[i] += a * (frame_dd_[i] - e.dd[i]);
                e.re[i] += a * (frame_re_[i] - e.re[i]);
                e.im[i] += a * (frame_im_[i] - e.im[i]);
            }
        }
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
        last_audible_ = last_frame_lufs_ >= config_.gate_lufs;
        if (!last_audible_) {
            ++gated_frames_;
            return;
        }
        if (settle_frames_ > 0) {
            --settle_frames_;
            return;
        }
        ++observed_frames_;
        // Both estimates are energy-weighted (integrated loudness is): a
        // running mean until they hold as many frames as their time
        // constant spans, an exponential average after.
        ++slow_frames_;
        ++fast_frames_;
        const double a_slow = alpha_ >= 1.0 ? 1.0
            : std::max(alpha_, 1.0 / static_cast<double>(slow_frames_));
        const double a_fast = fast_alpha_ >= 1.0 ? 1.0
            : std::max(fast_alpha_, 1.0 / static_cast<double>(fast_frames_));
        accumulate_(slow_, a_slow);
        accumulate_(fast_, a_fast);
        // The change detector's pair: the same averages of each frame's
        // SHAPE (every audible frame weighs the same), so a change of
        // material shows within a few frames however much quieter it is.
        {
            ++shape_frames_;
            const double sa = alpha_ >= 1.0 ? 1.0
                : std::max(alpha_, 1.0 / static_cast<double>(shape_frames_));
            const double fa = fast_alpha_ >= 1.0 ? 1.0
                : std::max(fast_alpha_, 1.0 / static_cast<double>(shape_frames_));
            const double inv = total > 0.0 ? 1.0 / total : 0.0;
            for (int b = 0; b < bins_; ++b) {
                const auto i = static_cast<std::size_t>(b);
                const double x = frame_ww_[i] * inv;
                slow_shape_[i] += sa * (x - slow_shape_[i]);
                fast_shape_[i] += fa * (x - fast_shape_[i]);
            }
        }
        double dry_total = total;
        if (two) {
            dry_total = 0.0;
            for (const double x : frame_dd_) dry_total += x;
        }
        level_drop_(total, dry_total);
        prior_weight_ = std::max(0.0, prior_weight_ - prior_step_);
        publish_();
    }

    // Energy weighting lets loud past material outweigh quieter new material
    // for as long as it lingers. When no frame of the last
    // level_drop_window_seconds has come within level_drop_db of the slow
    // estimate's level, the level genuinely dropped (inside a groove the
    // loud hits keep reaching it), so both estimates are rescaled to the
    // recent level and the new material weighs as much as the old.
    void level_drop_(double wet_total, double dry_total) noexcept {
        const auto slot = static_cast<std::size_t>(recent_pos_);
        recent_[slot] = wet_total;
        recent_dry_[slot] = dry_total;
        recent_pos_ = (recent_pos_ + 1) % level_drop_frames_;
        recent_count_ = std::min(recent_count_ + 1, level_drop_frames_);
        if (!(config_.level_drop_db > 0.0) || recent_count_ < level_drop_frames_) return;
        double wet_max = 0.0, dry_max = 0.0;
        for (int i = 0; i < level_drop_frames_; ++i) {
            wet_max = std::max(wet_max, recent_[static_cast<std::size_t>(i)]);
            dry_max = std::max(dry_max, recent_dry_[static_cast<std::size_t>(i)]);
        }
        const double floor = std::pow(10.0, -config_.level_drop_db / 10.0);
        const double wet_level = sum_(slow_.ww);
        const double dry_level = config_.track_dry_leg ? sum_(slow_.dd) : wet_level;
        const double rw = wet_level > 0.0 && wet_max < wet_level * floor ? wet_max / wet_level : 1.0;
        const double rd = config_.track_dry_leg
            ? (dry_level > 0.0 && dry_max < dry_level * floor ? dry_max / dry_level : 1.0)
            : rw;
        if (rw >= 1.0 && rd >= 1.0) {
            drop_run_ = 0;
            return;
        }
        if (++drop_run_ < 2) return;
        drop_run_ = 0;
        if (rw < 1.0) {
            // The material the mask shapes got genuinely quieter: what it is
            // now is what counts, so start again from it.
            ++level_drops_;
            restart();
            return;
        }
        // Only the live (dry) leg dropped -- the hold plays on: bring the dry
        // leg's average down to its recent level.
        rescale_legs_(slow_, 1.0, rd);
        const double fd = config_.track_dry_leg ? sum_(fast_.dd) : 0.0;
        if (fd > dry_max && dry_max > 0.0) rescale_legs_(fast_, 1.0, dry_max / fd);
        recent_count_ = 0;
        ++level_drops_;
    }

    // Scale the wet leg's power by rw and the dry leg's by rd; their
    // cross-spectrum scales by the geometric mean.
    void rescale_legs_(Estimate& e, double rw, double rd) noexcept {
        const double rx = std::sqrt(rw * rd);
        for (auto& x : e.ww) x *= rw;
        if (!config_.track_dry_leg) return;
        for (auto& x : e.dd) x *= rd;
        for (auto& x : e.re) x *= rx;
        for (auto& x : e.im) x *= rx;
    }
    [[nodiscard]] static double sum_(const std::vector<double>& v) noexcept {
        double t = 0.0;
        for (const double x : v) t += x;
        return t;
    }

    // The published estimates: the prior's decaying share plus the material's
    // averages, each on its own wet leg's scale (its total is 1 - prior share).
    void blend_into_(const Estimate& avg, std::int64_t frames, Estimate& out) noexcept {
        // A restart cleared the averages: keep publishing the last estimate
        // until new material arrives (the prior is for a cold start only).
        if (frames == 0 && observed_frames_ > 0) return;
        const double total = frames > 0 ? avg.total() : 0.0;
        const double pw = frames > 0 && total > 0.0 ? prior_weight_ : 1.0;
        const double scale = total > 0.0 ? (1.0 - pw) / total : 0.0;
        const bool two = config_.track_dry_leg;
        for (int b = 0; b < bins_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            const double p = pw * prior_[i];
            out.ww[i] = p + scale * avg.ww[i];
            if (two) {
                out.dd[i] = p + scale * avg.dd[i];
                out.re[i] = p + scale * avg.re[i];
                out.im[i] = scale * avg.im[i];
            }
        }
    }
    void publish_() noexcept {
        if (published_.ww.size() != static_cast<std::size_t>(bins_)) {
            // prepare() sizes these; first publish after it.
            return;
        }
        blend_into_(slow_, slow_frames_, published_);
        blend_into_(fast_, fast_frames_, published_fast_);
        cdf_.assign(spectrum(), bin_hz_);
    }

    // Sizes the published estimates (prepare-time; part of prepare()).
    void size_published_() {
        published_.assign(static_cast<std::size_t>(bins_));
        published_fast_.assign(static_cast<std::size_t>(bins_));
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
    double fast_alpha_ = 0.0;
    double prior_step_ = 1.0;
    double prior_weight_ = 1.0;
    bool prepared_ = false;
    std::unique_ptr<pulp::signal::FftT<float>> fft_;
    std::vector<float> window_;
    std::vector<float> history_;
    std::vector<float> frame_in_;
    std::vector<std::complex<float>> wet_spectrum_;
    std::vector<std::complex<float>> dry_spectrum_;
    std::vector<double> frame_ww_, frame_dd_, frame_re_, frame_im_;
    Estimate slow_, fast_, published_, published_fast_;
    std::vector<double> k_power_;
    std::vector<double> prior_;
    std::vector<double> flat_;
    std::vector<double> zero_;
    SpectrumCdf cdf_;
    std::int64_t slow_frames_ = 0, fast_frames_ = 0, shape_frames_ = 0;
    std::vector<double> fast_shape_, slow_shape_;
    std::uint64_t observed_frames_ = 0;
    std::uint64_t gated_frames_ = 0;
    std::uint64_t level_drops_ = 0;
    std::uint64_t restarts_ = 0;
    std::array<double, kMaxRecent> recent_{}, recent_dry_{};
    int recent_pos_ = 0, recent_count_ = 0, level_drop_frames_ = 9, drop_run_ = 0;
    int settle_frames_ = 0;
    double last_frame_lufs_ = -1.0e9;
    bool last_audible_ = false;
    int write_pos_ = 0;
    int filled_ = 0;
    int hop_pos_ = 0;
};

// ── 5. The target ───────────────────────────────────────────────────────────

/// What the make-up gain is heading for. A shape edit (or a switch-on) moves
/// it at once -- the user asked for it. The material moves it with a slew
/// that is proportional to how far it has to go: at least
/// `min_slew_db_per_second` (so a steady groove's frame-to-frame wobble is
/// smoothed away), `rate_per_second` x the distance when that is faster
/// beyond a knee (exponential approach to a far target), at most
/// `max_slew_db_per_second`.
/// The caller ramps the applied gain toward it.
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
        return follow(wanted_db, seconds, slew_db_per_second, 0.0, 0.0);
    }

    /// The proportional form described above; the speed-up counts only the
    /// distance beyond @p knee_db.
    bool follow(float wanted_db, double seconds, double min_slew_db_per_second,
                double rate_per_second, double max_slew_db_per_second,
                double knee_db = 0.0) noexcept {
        if (!primed_) return retarget(wanted_db);
        float next = wanted_db;
        if (min_slew_db_per_second > 0.0) {
            const double distance = std::abs(static_cast<double>(wanted_db) - value_db_);
            double slew = std::max(min_slew_db_per_second,
                                   rate_per_second * std::max(0.0, distance - knee_db));
            if (max_slew_db_per_second > 0.0) slew = std::min(slew, max_slew_db_per_second);
            slew = std::max(slew, min_slew_db_per_second);
            const auto step = static_cast<float>(slew * std::max(0.0, seconds));
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
