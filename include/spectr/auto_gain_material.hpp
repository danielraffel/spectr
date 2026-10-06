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
/// holds through silence); material movement is slewed (slow near the target,
/// proportionally faster far from it); a shape edit retargets at once.
/// Following the material: a level-independent change detector and a
/// level-drop rule restart the estimate when the material really changes; a
/// Freeze release switches the wet estimate to the live one at once; a locate
/// keeps the estimate; the estimate is saved with the session. Determinism:
/// the frame grid is counted in samples from prepare() and from each locate,
/// and a frame's new target is applied at its own stream sample plus the
/// renderer's latency, so the gain does not depend on host block size or
/// render speed. Design record and measurements: docs/level-controls.md.

#include <pulp/runtime/trace.hpp>
#include "spectr/band_state.hpp"
#include "spectr/test_seams.hpp"
#include "spectr/freeze_source.hpp"
#include "spectr/level_controls.hpp"
#include "spectr/mask_renderer.hpp"
#include "spectr/upstream/loudness_compensation.hpp"

#include <pulp/runtime/triple_buffer.hpp>
#include <pulp/signal/spectral_band_mask.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>

#include <atomic>

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
    c.fast_time_constant_seconds = 0.25;
    c.level_drop_db = 10.0;
    c.level_drop_window_seconds = 4.0;
    c.alternating_time_constant_seconds = 12.0;
    c.local_level_seconds = 2.0;
    c.level_weight_exponent = 1.0;
    c.memory_seconds = 12.0;
    c.material_slew_rate_per_second = 10.0;
    c.material_slew_max_db_per_second = 120.0;
    c.material_slew_knee_db = 0.0;
    c.change_threshold_db = 4.0;
    c.change_seconds = 0.7;
    c.change_huge_db = 12.0;
    c.change_huge_seconds = 0.4;
    c.change_drop_threshold_db = 2.0;
    c.change_drop_level_db = 3.0;
    c.change_max_decay_db_per_second = 6.0;
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
    /// A new make-up target from `at` (absolute stream sample, counted from
    /// prepare()) on, reached over `ramp_seconds`. `at` is the frame's own
    /// sample plus the renderer's latency, so the gain moves with the audio
    /// the frame described rather than ahead of it.
    struct Event {
        std::int64_t at = 0;
        float target_db = 0.0f;
        float ramp_seconds = 0.0f;
    };
    static constexpr std::size_t kMaxEvents = 128;
    /// Ramp for a material event: one hop, so successive frames' targets join
    /// into a continuous line. A Freeze release jumps over kJumpRampSeconds.
    static constexpr float kJumpRampSeconds = 0.02f;
    /// After a change of material, the new estimate is followed once it
    /// spans this much audio.
    static constexpr double kRestartSettleSeconds = 0.5;
    /// A target step at most this big is wobble, smoothed over the 0.3 s ramp.
    static constexpr float kSmallStepDb = 0.5f;

    // ── control thread ──────────────────────────────────────────────────
    /// Allocates. @p reference must be prepared for the same rate: v1's
    /// K-weighted pink, per bin, is the start-up prior. @p design_grid is the
    /// renderers' design FFT size: the estimate is taken on the same bin grid,
    /// so the realised response weighs it bin for bin.
    ///
    /// A re-prepare at the same geometry keeps the estimate (hosts re-prepare
    /// before a bounce, on a buffer-size change, mid-song): only the frame
    /// grid restarts. At a new rate or channel count the estimate is carried
    /// across band-compressed (rate-independent) and restored warm.
    void prepare(double sample_rate, int channels, const AutoGainReference& reference,
                 int design_grid) {
        sample_rate_ = sample_rate;
        auto config = auto_gain_v2_config();
        config.fft_size = design_grid;
        // Advisory-sweep seams (tools/autogain_sweep.cpp), read here on the
        // control thread, never on the audio thread.
        if (const char* tau = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_TAU_S"))
            if (const double v = std::atof(tau); v > 0.0) config.time_constant_seconds = v;
        if (const char* k = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_KNEE_DB"))
            config.material_slew_knee_db = std::atof(k);
        if (const char* d = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_LEVEL_DROP_DB"))
            config.level_drop_db = std::atof(d);
        if (const char* g = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_WEIGHT_EXP"))
            config.level_weight_exponent = std::atof(g);
        if (const char* l = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_LOCAL_S"))
            config.local_level_seconds = std::atof(l);
        if (const char* t = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_DROP_THRESHOLD_DB"))
            config.change_drop_threshold_db = std::atof(t);
        if (const char* t = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_DROP_LEVEL_DB"))
            config.change_drop_level_db = std::atof(t);
        if (const char* t = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_DROP_MIN_LOUD_S"))
            config.change_drop_min_loud_seconds = std::atof(t);
        if (const char* t = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_MAX_DECAY_DB_S"))
            config.change_max_decay_db_per_second = std::atof(t);
        if (const char* slew = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_SLEW_DB_S"))
            if (const double v = std::atof(slew); v > 0.0) config.material_slew_db_per_second = v;
        // Negative-control seams (level_plant, SPECTR_LEVEL_PLANT).
        if (level_plant("autogain-v2-unweighted")) config.weight_by_material = false;
        if (level_plant("autogain-v2-no-smoothing")) {
            config.time_constant_seconds = 0.0;
            config.material_slew_db_per_second = 0.0;
        }
        // The first v2's transient behaviour: no change detection, no
        // level-drop rule, no switch of legs at a Freeze edge, a fixed 6 dB/s.
        stale_plant_ = level_plant("autogain-v2-stale-on-change") || level_plant("autogain-v2a");
        if (stale_plant_) {
            config.level_drop_db = 0.0;
            config.material_slew_rate_per_second = 0.0;
        }
        // The second v2's detector: three frames, no memory of the past.
        const bool short_plant = level_plant("autogain-v2-short-persistence");
        if (short_plant) config.memory_seconds = 0.0;
        // v2c's detector: one threshold, whatever the level did.
        if (short_plant || level_plant("autogain-v2-no-drop-path")) config.change_drop_threshold_db = 0.0;
        restore_as_warm_plant_ = level_plant("autogain-v2-restore-as-warm");
        reprepare_reset_plant_ = level_plant("autogain-v2-reprepare-reset");
        // v2c: the detector ran only while AUTO was on.
        off_detect_plant_ = level_plant("autogain-v2-detect-only-when-on");
        restored_agree_fade_ = level_plant("autogain-v2-restore-fixed-fade") ? 1.0 : 3.0;
        if (const char* f = SPECTR_TEST_ENV("SPECTR_AUTOGAIN_RESTORED_FADE"))
            if (const double v = std::atof(f); v >= 1.0) restored_agree_fade_ = v;
        detector_.threshold_db = config.change_threshold_db;
        detector_.huge_db = config.change_huge_db;
        {
            const double hop_s = static_cast<double>(
                pulp_candidate::signal::LongTermSpectrum::fft_size_for(sample_rate, config) / 2)
                / sample_rate;
            detector_.frames = std::max(1, static_cast<int>(std::ceil(config.change_seconds / hop_s)));
            detector_.huge_frames = std::max(1, static_cast<int>(
                std::ceil(config.change_huge_seconds / hop_s)));
            if (short_plant) detector_.frames = detector_.huge_frames = 3;
            detector_.drop_threshold_db = config.change_drop_threshold_db;
            detector_.drop_level_db = config.change_drop_level_db;
            detector_.max_decay_db_per_frame = config.change_max_decay_db_per_second * hop_s;
            detector_.drop_min_loud_frames = static_cast<int>(
                std::ceil(config.change_drop_min_loud_seconds / hop_s));
            detector_.level_fast_alpha = 1.0 - std::exp(-hop_s / config.fast_time_constant_seconds);
            detector_.level_slow_alpha = 1.0 - std::exp(-hop_s / config.time_constant_seconds);
        }
        if (!reprepare_reset_plant_ && spectrum_.prepared_for(sample_rate, channels, config)
            && design_grid_ == std::max(64, design_grid)) {
            restart_grid();
            target_.reset();
            return;
        }
        pulp_candidate::signal::SpectrumBands carried{};
        if (spectrum_.prepared() && !stale_plant_ && !reprepare_reset_plant_)
            spectrum_.export_bands(carried);
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
        if (carried.valid) spectrum_.import_bands(carried, LongTermSpectrum::Import::warm);
    }

    [[nodiscard]] bool prepared() const noexcept { return spectrum_.prepared(); }
    [[nodiscard]] const pulp_candidate::signal::LongTermSpectrum& spectrum() const noexcept {
        return spectrum_;
    }

    // ── audio thread ────────────────────────────────────────────────────
    /// Cold start: forget the material, back to the prior, frame grid
    /// restarted. prepare() only -- a transport jump keeps the material
    /// (restart_grid()).
    void reset() noexcept {
        spectrum_.reset();  // drops a pending frame: nothing it describes survives
        target_.reset();
        detector_.reset();
        clear_events_();
        stream_pos_ = 0;
        enabled_ = false;
        restored_prior_ = false;
        export_countdown_ = 0;
    }

    /// A transport jump or host Reset: the frame grid restarts (chunking
    /// determinism), the material and the target are kept, so playback from
    /// a locate starts at the right level.
    void restart_grid() noexcept {
        flush_();
        spectrum_.restart_grid();
        detector_.reset();
        // Pending events describe audio before the jump.
        clear_events_();
    }

    /// The tap's freeze source moved between live and held. Engaging: the
    /// wet leg is now a held past, unrelated to the live input. Releasing:
    /// the wet leg becomes the live input again, whose spectrum the dry leg
    /// kept warm through the hold.
    void freeze_engaged() noexcept {
        flush_();
        if (stale_plant_) return;
        spectrum_.legs_uncorrelated();
    }
    void freeze_released() noexcept {
        flush_();
        if (stale_plant_) return;
        spectrum_.wet_becomes_dry();
        detector_.hold_off();
        // The release is an edge this adapter knows the sample of: the gain
        // jumps with it (over the freeze crossfade) instead of slewing after.
        if (enabled_ && target_.primed() && target_.retarget(shape_target_db())) {
            last_event_db_ = target_.value_db();
            push_event_({stream_pos_ + latency_, target_.value_db(), kJumpRampSeconds});
        }
    }

    /// Samples until the estimator's next frame completes (the tap splits
    /// the freeze source's block there, so a Freeze edge reaches the
    /// estimator at the same sample however the host cut the stream).
    [[nodiscard]] int samples_to_next_frame() const noexcept {
        return spectrum_.prepared() ? spectrum_.samples_to_next_frame() : 1 << 30;
    }

    // ── session state ───────────────────────────────────────────────────
    /// Any thread: the last published band-compressed estimate (audio thread
    /// writes it every few audible frames).
    [[nodiscard]] pulp_candidate::signal::SpectrumBands saved_estimate() const noexcept {
        auto bands = snapshot_.read();
        // Restored but not played yet: what was restored is still the estimate.
        if (!bands.valid) bands = offered_;
        return bands;
    }
    /// Control thread: hand a saved estimate to the audio thread, which
    /// adopts it at the start of its next block (adopt_pending()).
    void offer_saved_estimate(const pulp_candidate::signal::SpectrumBands& bands) noexcept {
        int expected = kSlotEmpty;
        // Wait out an in-progress adoption (a few microseconds at most).
        for (int spin = 0; spin < 1000000; ++spin) {
            expected = slot_state_.load(std::memory_order_acquire);
            if (expected == kSlotReading) continue;
            if (slot_state_.compare_exchange_weak(expected, kSlotWriting,
                                                  std::memory_order_acq_rel))
                break;
        }
        if (slot_state_.load(std::memory_order_relaxed) != kSlotWriting) return;
        pending_ = bands;
        offered_ = bands;
        slot_state_.store(kSlotReady, std::memory_order_release);
    }
    /// Audio thread, block start: adopt a saved estimate if one was offered.
    void adopt_pending() noexcept {
        int expected = kSlotReady;
        if (!slot_state_.compare_exchange_strong(expected, kSlotReading,
                                                 std::memory_order_acq_rel))
            return;
        flush_();
        // A restored session may play different material: the saved estimate
        // is where playback starts, and the material takes over as from a
        // cold start (prior import). The snapshot is written here, on the
        // audio thread, its only writer.
        spectrum_.import_bands(pending_, restore_as_warm_plant_
            ? pulp_candidate::signal::LongTermSpectrum::Import::warm
            : pulp_candidate::signal::LongTermSpectrum::Import::prior);
        restored_prior_ = !restore_as_warm_plant_;
        snapshot_.write(pending_);
        target_.reset();
        detector_.reset();
        slot_state_.store(kSlotEmpty, std::memory_order_release);
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
        // Anything this slice changes feeds a pending frame's update.
        if (!target_.primed() || shape_changed || enabled != enabled_) flush_();
        enabled_ = enabled;
        mix_ = std::clamp(static_cast<double>(mix), 0.0, 1.0);
        slice_start_ = stream_pos_;
        if (target_.primed() && !shape_changed) return false;
        // The response is derived whether AUTO is on or not: the change
        // detector runs while AUTO is off too (on_frame_), so switching AUTO
        // on after the material changed starts from the new material.
        if (enabled || !off_detect_plant_) derive_response_(shape, mix, renderer);
        // Switching AUTO on (or any edit) is also when a material change
        // that happened while AUTO was off is noticed.
        if (enabled && !stale_plant_ && spectrum_.shape_frames() >= 2
            && spectrum_.prior_weight() <= 0.0
            && std::abs(target_db_for_(spectrum_.fast_shape_legs())
                        - target_db_for_(spectrum_.slow_shape_legs()))
                   > detector_.threshold_db)
            confirm_change_();
        const bool moved = target_.retarget(enabled ? shape_target_db() : 0.0f);
        last_event_db_ = target_.value_db();
        // An edit supersedes targets computed for the shape before it.
        if (moved) clear_events_();
        return moved;
    }

    /// The wet source's block and the live input it was made from, from the
    /// tap, in stream order.
    void push(const float* const* wet, const float* const* live, int channels,
              int num_samples) noexcept {
        spectrum_.push_at(wet, live, channels, num_samples, stream_pos_,
                          [this](std::int64_t frame_end) { on_frame_(frame_end); });
        stream_pos_ += std::max(0, num_samples);
    }

    /// Read the negative-control seams once, off the audio thread.
    static void prime_plants() noexcept { (void)deferral_plant_(); }

    /// The renderer's latency, in samples (the processor sets it each slice).
    ///
    /// A frame's event is due one latency after the frame, so its work can be
    /// staged across that many samples -- at a small host buffer, across
    /// several callbacks instead of in the one that completed the frame. The
    /// work is scheduled by stream position and finishes before the event is
    /// due, and everything that feeds the frame's update flushes it first
    /// (flush_()), so the output is unchanged by the staging.
    void set_latency(int samples) noexcept {
        samples = std::max(0, samples);
        if (samples != latency_) flush_();
        latency_ = samples;
        spectrum_.set_frame_deferral(
            deferral_plant_() ? 0 : std::min(latency_, spectrum_.hop()));
    }
    /// Where the current slice starts in the stream.
    [[nodiscard]] std::int64_t slice_start() const noexcept { return slice_start_; }
    /// The next event due at or before @p at (absolute), or nullptr. Taking
    /// it removes it.
    [[nodiscard]] const Event* take_due(std::int64_t at) noexcept {
        if (events_ == 0 || event_list_[head_].at > at) return nullptr;
        const Event* e = &event_list_[head_];
        head_ = (head_ + 1) % kMaxEvents;
        --events_;
        return e;
    }
    [[nodiscard]] float target_db() const noexcept { return target_.value_db(); }
    [[nodiscard]] std::uint64_t observed_frames() const noexcept { return spectrum_.observed_frames(); }
    [[nodiscard]] std::uint64_t gated_frames() const noexcept { return spectrum_.gated_frames(); }

    /// The make-up the current estimate gives the current slice's shape.
    [[nodiscard]] float shape_target_db() const noexcept {
        return target_db_for_(spectrum_.legs());
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

    [[nodiscard]] float target_db_for_(const pulp_candidate::signal::LegSpectra& legs) const noexcept {
        if (!spectrum_.prepared() || !response_valid_) return 0.0f;
        const auto& c = spectrum_.config();
        return pulp_candidate::signal::blend_makeup_gain_db(
            response_, legs, mix_, {c.max_cut_db, c.max_boost_db});
    }

    // A persistent gap between the fast and slow shapes. Four readings: the
    // material is one of the two halves of an alternation already settled
    // (nothing to do); it went back to the material before the last change
    // (an alternation: merge the two); it came back to what the last change
    // went to, after something the slow estimate absorbed (an alternation:
    // settle on the estimate that holds both); or it is new (restart).
    void confirm_change_() noexcept { confirm_change_(target_db_for_(spectrum_.fast_shape_legs())); }
    void confirm_change_(float fast) noexcept {
        const double near = detector_.threshold_db;
        const auto close = [&](const pulp_candidate::signal::LegSpectra& legs) {
            return std::abs(static_cast<double>(target_db_for_(legs)) - fast) <= near;
        };
        if (spectrum_.alternating()
            && (close(spectrum_.alternate_shape_legs(0)) || close(spectrum_.alternate_shape_legs(1)))) {
            spectrum_.cancel_candidate();
            return;
        }
        if (spectrum_.has_previous() && close(spectrum_.previous_shape_legs())) {
            spectrum_.merge_with_previous();
            return;
        }
        if (spectrum_.has_arrived() && close(spectrum_.arrived_shape_legs())) {
            spectrum_.settle_alternation();
            return;
        }
        spectrum_.restart();
    }

    void clear_events_() noexcept { head_ = 0; events_ = 0; }
    void push_event_(const Event& e) noexcept {
        if (events_ == kMaxEvents) {
            // Overflow (a block far longer than any host's): fold into the
            // newest, keeping its target.
            event_list_[(head_ + events_ - 1) % kMaxEvents] = e;
            return;
        }
        event_list_[(head_ + events_) % kMaxEvents] = e;
        ++events_;
    }

    void flush_() noexcept {
        spectrum_.flush_deferred([this](std::int64_t frame_end) { on_frame_(frame_end); });
    }
    // SPECTR_PLANT_AUTOGAIN_UNSTAGED restores the whole frame in one callback.
    static bool deferral_plant_() noexcept {
        static const bool planted = SPECTR_TEST_ENV("SPECTR_PLANT_AUTOGAIN_UNSTAGED") != nullptr;
        return planted;
    }

    void on_frame_(std::int64_t frame_end) noexcept {
        PULP_TRACE_SCOPE_NAMED("dsp", "autogain.on_frame");
        // Publish the session-state copy every few audible frames.
        if (spectrum_.last_frame_audible() && --export_countdown_ <= 0) {
            export_countdown_ = 4;
            spectrum_.export_bands(export_scratch_);
            if (export_scratch_.valid) snapshot_.write(export_scratch_);
        }
        // A restored estimate fades like a cold start's prior while the
        // material disagrees with it (other material takes over as fast as
        // from a cold start), and over kRestoredAgreeFade times as long while
        // the material agrees (the same song reopened: the few frames the
        // material's own estimate starts from no longer swing the gain).
        if (restored_prior_) {
            if (spectrum_.prior_weight() <= 0.0) {
                restored_prior_ = false;
                spectrum_.set_prior_fade_scale(1.0);
            } else if (response_valid_ && spectrum_.last_frame_audible()
                       && spectrum_.shape_frames() >= 1) {
                const double gap = std::abs(
                    static_cast<double>(target_db_for_(spectrum_.fast_shape_legs()))
                    - target_db_for_(spectrum_.prior_legs()));
                spectrum_.set_prior_fade_scale(
                    gap <= detector_.threshold_db ? 1.0 / restored_agree_fade_ : 1.0);
            }
        }
        // The change detector (not during a cold or restored start, whose
        // prior is still fading -- that IS the move to the material). It runs
        // while AUTO is off as well, so the estimate it keeps is the current
        // material's when AUTO is switched on.
        if ((enabled_ || !off_detect_plant_) && !stale_plant_ && response_valid_
            && spectrum_.last_frame_audible()
            && spectrum_.shape_frames() >= 2 && spectrum_.prior_weight() <= 0.0) {
            using E = pulp_candidate::signal::MaterialChangeDetector::Event;
            const float fast = target_db_for_(spectrum_.fast_shape_legs());
            switch (detector_.observe(fast, target_db_for_(spectrum_.slow_shape_legs()),
                                      spectrum_.last_frame_lufs())) {
            case E::run_started: spectrum_.begin_candidate(); break;
            case E::run_broken: spectrum_.cancel_candidate(); break;
            case E::confirmed: confirm_change_(fast); break;
            case E::none: break;
            }
        }
        if (!enabled_ || !target_.primed()) return;
        const auto& c = spectrum_.config();
        // Just restarted: hold the target until the new running mean spans
        // kRestartSettleSeconds (a beat of a groove), or its first frames --
        // one kick, one hat -- would swing the gain.
        if (spectrum_.restarts() > 0 && spectrum_.observed_frames() > 0
            && static_cast<double>(spectrum_.frames_since_restart()) * spectrum_.hop_seconds()
                   < kRestartSettleSeconds
            && spectrum_.frames_since_restart() < spectrum_.observed_frames())
            return;
        if (!target_.follow(shape_target_db(), spectrum_.hop_seconds(),
                            c.material_slew_db_per_second, c.material_slew_rate_per_second,
                            c.material_slew_max_db_per_second, c.material_slew_knee_db))
            return;
        // Small steps (a groove's wobble) ride the long 0.3 s ramp, which
        // smooths them; a real move rides a one-hop ramp so successive
        // frames' targets join into a line that keeps up with it.
        const float step = std::abs(target_.value_db() - last_event_db_);
        last_event_db_ = target_.value_db();
        push_event_({frame_end + latency_, target_.value_db(),
                     step <= kSmallStepDb ? kAutoGainRampSeconds
                                          : static_cast<float>(spectrum_.hop_seconds())});
    }

    static constexpr int kSlotEmpty = 0, kSlotWriting = 1, kSlotReady = 2, kSlotReading = 3;

    pulp_candidate::signal::LongTermSpectrum spectrum_;
    pulp_candidate::signal::MakeupTarget target_;
    pulp_candidate::signal::MaterialChangeDetector detector_;
    bool stale_plant_ = false;
    bool restore_as_warm_plant_ = false;
    bool reprepare_reset_plant_ = false;
    bool off_detect_plant_ = false;
    // A restored estimate is the prior now; how much longer it fades while
    // the material agrees with it.
    bool restored_prior_ = false;
    double restored_agree_fade_ = 3.0;
    int export_countdown_ = 0;
    pulp_candidate::signal::SpectrumBands export_scratch_{};
    mutable pulp::runtime::TripleBuffer<pulp_candidate::signal::SpectrumBands> snapshot_{
        pulp_candidate::signal::SpectrumBands{}};
    std::atomic<int> slot_state_{kSlotEmpty};
    pulp_candidate::signal::SpectrumBands pending_{};
    // Control thread only: the last estimate offered from a session.
    pulp_candidate::signal::SpectrumBands offered_{};
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
    std::int64_t stream_pos_ = 0;
    std::int64_t slice_start_ = 0;
    int latency_ = 0;
    float last_event_db_ = 0.0f;
    std::array<Event, kMaxEvents> event_list_{};
    std::size_t head_ = 0;
    std::size_t events_ = 0;
};

/// The wet-source stage the renderers actually call: it runs the freeze
/// source, then shows the estimator what the mask is about to shape. While
/// Freeze holds, that is the held material, not the live input.
class AutoGainWetTap final : public pulp::signal::SpectralWetSourceStageT<float> {
public:
    void bind(FreezeSource* source, AutoGainMaterial* material) noexcept {
        source_ = source;
        material_ = material;
        holding_ = false;
    }
    /// The block is split where the estimator completes a frame, so a Freeze
    /// edge reaches it at the same sample whatever the host's block size (the
    /// freeze source itself counts in samples, so splitting its block changes
    /// nothing it does).
    void process_block(const float* const* input, float* const* wet,
                       int channels, int num_samples) noexcept override {
        const int count = std::clamp(channels, 0, kMaxChannels);
        int done = 0;
        while (done < num_samples) {
            int chunk = num_samples - done;
            if (material_ != nullptr)
                chunk = std::max(1, std::min(chunk, material_->samples_to_next_frame()));
            for (int ch = 0; ch < count; ++ch) {
                in_[static_cast<std::size_t>(ch)] = input[ch] + done;
                out_[static_cast<std::size_t>(ch)] = wet[ch] + done;
            }
            if (source_ != nullptr) {
                source_->process_block(in_.data(), out_.data(), count, chunk);
                const bool holding = source_->hold_audible()
                    && source_->phase() != FreezeSource::Phase::releasing;
                if (material_ != nullptr && holding != holding_) {
                    if (holding) material_->freeze_engaged();
                    else material_->freeze_released();
                }
                holding_ = holding;
            } else {
                for (int ch = 0; ch < count; ++ch)
                    if (out_[static_cast<std::size_t>(ch)] != in_[static_cast<std::size_t>(ch)])
                        std::copy(in_[static_cast<std::size_t>(ch)],
                                  in_[static_cast<std::size_t>(ch)] + chunk,
                                  out_[static_cast<std::size_t>(ch)]);
            }
            if (material_ != nullptr)
                material_->push(out_.data(), in_.data(), count, chunk);
            done += chunk;
        }
    }

private:
    static constexpr int kMaxChannels = 64;
    FreezeSource* source_ = nullptr;
    AutoGainMaterial* material_ = nullptr;
    bool holding_ = false;
    std::array<const float*, kMaxChannels> in_{};
    std::array<float*, kMaxChannels> out_{};
};

// SPECTR-RENDER-PATH END

} // namespace spectr
