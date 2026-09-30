#pragma once

/// @file freeze_source.hpp
/// Spectr's freeze: a time-domain source that replaces what the live mask
/// shapes with a held spectrum of the input, while the dry path stays live.
///
/// The source sits AHEAD of the mask. It captures the raw input -- every
/// frequency, before any band, LFO, morph or mute touches it -- so the mask
/// keeps acting on the held sound exactly as it acts on live input, and a
/// release hands back live input through the same mask with nothing to
/// re-synchronise.
///
/// It is one object for both realisations of the mask. Both renderers accept
/// a time-domain wet source (`MaskRenderer::set_wet_source`): the linear-phase
/// renderer analyses the block this source writes, and the zero-latency one
/// convolves it. Because the source is owned by the processor rather than by
/// either renderer, switching the Latency mode while frozen hands the SAME
/// hold to the new renderer.
///
/// HOW IT HOLDS. The source runs its own short-hop analysis of the input
/// (`kFftSize` points every `kHop` samples) into `pulp::signal::FreezeHoldT`,
/// which averages the magnitudes of the most recent frames and advances
/// each bin's phase at its instantaneous frequency plus a small random walk.
/// The hold is rendered with `write_hold()` and resynthesised by overlap-add.
///
/// HOW IT ENGAGES. Nothing waits for an analysis latency. After the latch
/// the output ring is pre-rolled with the frames that would already have been
/// overlapping (the latched phases stepped forward hop by hop), so a full
/// hold is present from the fade's first sample, and a short equal-power
/// crossfade takes the output from live to held. The pre-roll is built over
/// the hop after the latch, a share of it per sample of audio, and the fade
/// starts at the next hop boundary: done at once, it was several
/// milliseconds of work in the one callback a tap lands in -- more than a
/// small host buffer's whole real-time budget. A release runs the same
/// fade the other way, and a freeze requested during that fade waits for it
/// to finish, so the hold being faded out is never swapped underneath the
/// fade.
///
/// NO GHOST, AND ORTHOGONAL TO THE LIVE SOUND. The latched frame's phases
/// encode WHEN things happened inside it. Advanced bin by bin they replay
/// that timing one analysis window later -- a drum hit that landed just
/// before the press comes back as a tick just after it. So every bin of the
/// hold is turned by a quarter cycle, +90 or -90 degrees at random, the same
/// on every channel (the image keeps its phase differences) -- except that
/// the main lobe of a tonal peak turns as one, so the partial keeps its
/// level and shape. Across bins the timing is scrambled and nothing replays.
/// A quarter cycle also makes each partial of the hold orthogonal to the
/// same partial still sounding live, so the equal-power engage fade is
/// level-flat for a steady tone as it is for noise, with no gain correction
/// riding the waveform (one that followed the waveform's own power sample
/// by sample modulated the fade at audio rate -- a click on drums).
///
/// LEVEL. Averaging magnitudes lowers noise-like material by a few dB; the
/// hold is matched to the live level of the window it was taken from. The
/// match is measured and applied at the hop grid -- a gain that glides
/// linearly across each hop -- so it is smooth and does not depend on the
/// host's block size. Its first value is measured on the hold's own first
/// hops before any of them is heard, so the hold enters at its matched level
/// rather than asking the fade to hide a correction still settling. The
/// quarter-cycle turn keeps the hold close to orthogonal to a steady live
/// sound for as long as it plays (its phases advance at the measured
/// frequencies), so the release fade is the same plain equal-power fade.
///
/// SILENCE. A freeze asked for over silence does not latch it: the request
/// stays armed, and live (silent) input passes, until every hop of input the
/// capture window analyses carries signal -- so a note that has only just
/// started is not held as mostly silence either.
///
/// Real-time: `prepare()` allocates; every other member is allocation-free,
/// lock-free and reads no clock. All members except `prepare()` belong to the
/// audio thread.

#include <pulp/signal/fft.hpp>
#include <pulp/signal/freeze_hold.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spectr {

class FreezeSource final : public pulp::signal::SpectralWetSourceStageT<float> {
public:
    /// Analysis geometry. The hop matches the one FreezeHold's reference
    /// timing was tuned at, so the default hold length and crossfade are the
    /// reference feel at 48 kHz; the window keeps the hold's frequency
    /// resolution equal to the mask's design grid.
    static constexpr int kFftSize = 8192;
    static constexpr int kHop = 512;
    static constexpr int kMaxChannels = 16;

    /// Hold length: the capture window the next latch averages.
    static constexpr double kDefaultHoldSeconds =
        pulp::signal::FreezeHoldReferenceTiming::kCaptureSeconds;
    static constexpr double kMinHoldSeconds = 0.05;
    static constexpr double kMaxHoldSeconds = 2.0;

    /// Time-domain engage/release crossfade.
    static constexpr double kCrossfadeSeconds = 0.048;

    /// Level match between the hold and the live input it was taken from:
    /// the longest stretch of the hold's own output its level is averaged
    /// over (a running mean until then, a follow after), and how far it may
    /// be corrected.
    static constexpr double kLevelMatchSeconds = 0.25;
    static constexpr float kMinLevelMatch = 0.8f;
    static constexpr float kMaxLevelMatch = 1.35f;

    /// Hops of the hold measured, before it is heard, to set its level.
    static constexpr int kLevelMeasureHops = 8;

    /// A tonal peak, for the hold's quarter-cycle turn: a local maximum this
    /// many times the mean magnitude of the bins within the neighbourhood,
    /// turned as one across its Hann main lobe.
    static constexpr float kPeakProminence = 4.0f;
    static constexpr int kPeakNeighbourhood = 16;
    static constexpr int kPeakLobe = 2;

    /// Mean power per sample, per channel, every hop of input under the
    /// capture window must reach before a freeze latches (-90 dBFS RMS).
    static constexpr double kSignalFloorPower = 1.0e-9;

    /// Where the source is in a freeze.
    enum class Phase : std::uint8_t {
        live,      ///< live input passes; nothing is held
        arming,    ///< freeze requested; waiting for a window with signal
        preparing, ///< latched; building the hold's pre-roll, live still passes
        engaging,  ///< crossfading live -> held
        held,      ///< the hold alone
        releasing, ///< crossfading held -> live
    };

    static double clamp_hold_seconds(double seconds) noexcept {
        if (!std::isfinite(seconds)) return kDefaultHoldSeconds;
        return std::clamp(seconds, kMinHoldSeconds, kMaxHoldSeconds);
    }

    /// Allocate for a sample rate and channel count. Control thread; the
    /// audio thread must not be inside process_block(). Returns false (and
    /// leaves the source unprepared, a pass-through) for an unsupported
    /// geometry.
    bool prepare(double sample_rate, int channels) {
        prepared_ = false;
        if (!(sample_rate > 0.0) || channels < 1 || channels > kMaxChannels)
            return false;
        sample_rate_ = sample_rate;
        channels_ = channels;
        bins_ = kFftSize / 2 + 1;

        pulp::signal::FreezeHold::Config config;
        config.fft_size = kFftSize;
        config.channels = channels;
        config.analysis_hop = kHop;
        config.sample_rate = sample_rate;
        config.capture_seconds = kDefaultHoldSeconds;
        config.max_capture_seconds = kMaxHoldSeconds;
        hold_.prepare(config);

        fft_ = pulp::signal::Fft(kFftSize);
        if (!fft_.ready()) return false;

        window_.assign(kFftSize, 0.0f);
        constexpr double two_pi = 6.28318530717958647692;
        for (int n = 0; n < kFftSize; ++n)
            window_[static_cast<std::size_t>(n)] = static_cast<float>(
                0.5 - 0.5 * std::cos(two_pi * n / kFftSize));
        // Hann analysis times Hann synthesis overlapped at kHop sums to a
        // constant; dividing by it makes a steady hold unity-gain.
        double overlap = 0.0;
        for (int n = 0; n < kFftSize; n += kHop)
            overlap += static_cast<double>(window_[static_cast<std::size_t>(n)])
                       * window_[static_cast<std::size_t>(n)];
        synthesis_scale_ = static_cast<float>(1.0 / overlap);
        synthesis_window_.resize(window_.size());
        for (std::size_t n = 0; n < window_.size(); ++n)
            synthesis_window_[n] = window_[n] * synthesis_scale_;

        const auto per_channel = static_cast<std::size_t>(kFftSize);
        const auto channel_count = static_cast<std::size_t>(channels);
        input_ring_.assign(channel_count * per_channel, 0.0f);
        ola_.assign(channel_count * per_channel, 0.0f);
        spectra_.assign(channel_count * per_channel, {});
        frame_ptrs_.assign(channel_count, nullptr);
        for (std::size_t ch = 0; ch < channel_count; ++ch)
            frame_ptrs_[ch] = spectra_.data() + ch * per_channel;
        time_.assign(per_channel, {});
        scratch_.assign(per_channel, 0.0f);

        // Input power per hop, over the longest stretch a capture window can
        // analyse, and per analysed frame over its whole window.
        hops_per_window_ = kFftSize / kHop;
        const int max_frames = hold_.config().max_capture_frames;
        hop_energy_.assign(static_cast<std::size_t>(max_frames - 1 + hops_per_window_), 0.0);
        frame_energy_.assign(static_cast<std::size_t>(max_frames), 0.0);

        level_scratch_.assign(channel_count * static_cast<std::size_t>(kLevelMeasureHops * kHop), 0.0f);
        rotation_.assign(static_cast<std::size_t>(bins_), 1);
        hop_rotor_.assign(static_cast<std::size_t>(bins_), {1.0f, 0.0f});
        magnitude_.assign(static_cast<std::size_t>(bins_), 0.0f);
        prefix_.assign(static_cast<std::size_t>(bins_) + 1, 0.0);
        crossfade_samples_ = std::max(
            1, static_cast<int>(std::lround(kCrossfadeSeconds * sample_rate)));

        requested_hold_seconds_ = kDefaultHoldSeconds;
        applied_hold_seconds_ = kDefaultHoldSeconds;
        prepared_ = true;
        reset();
        return true;
    }

    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] bool prepared_for(double sample_rate, int channels) const noexcept {
        return prepared_ && sample_rate == sample_rate_ && channels == channels_;
    }
    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }

    /// Full reset: live, no hold, no history. The freeze request is kept
    /// (the next frames re-arm it).
    void reset() noexcept {
        if (!prepared_) return;
        hold_.reset();
        std::fill(input_ring_.begin(), input_ring_.end(), 0.0f);
        std::fill(ola_.begin(), ola_.end(), 0.0f);
        write_pos_ = 0;
        hop_pos_ = 0;
        clear_energy_history_();
        phase_ = Phase::live;
        weight_step_ = 0;
        pending_engage_ = false;
        rng_ = kRngSeed;
        held_gain_ = held_gain_target_ = 1.0f;
    }

    /// A transport discontinuity: forget the input analysed so far, keep the
    /// hold and wherever the crossfade is. An armed freeze re-fills its
    /// capture window from input that arrives after this call.
    void clear_history() noexcept {
        if (!prepared_) return;
        hold_.clear_history();
        std::fill(input_ring_.begin(), input_ring_.end(), 0.0f);
        clear_energy_history_();
    }

    /// Freeze request. Takes effect at the next hop boundary.
    void set_frozen(bool frozen) noexcept { requested_ = frozen; }
    [[nodiscard]] bool frozen_requested() const noexcept { return requested_; }

    /// Hold length for the NEXT latch; a hold already playing keeps its own.
    void set_hold_seconds(double seconds) noexcept {
        requested_hold_seconds_ = clamp_hold_seconds(seconds);
    }
    [[nodiscard]] double hold_seconds() const noexcept { return requested_hold_seconds_; }
    /// Frames the next latch averages (for tests and diagnostics).
    [[nodiscard]] int capture_frames() const noexcept { return hold_.capture_frames(); }

    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    /// True while any held content reaches the output.
    [[nodiscard]] bool hold_audible() const noexcept {
        return phase_ == Phase::engaging || phase_ == Phase::held
            || phase_ == Phase::releasing;
    }
    /// Position of the crossfade: 0 = live only, 1 = hold only.
    [[nodiscard]] float hold_weight() const noexcept {
        return static_cast<float>(weight_step_) / static_cast<float>(crossfade_samples_);
    }
    [[nodiscard]] int crossfade_samples() const noexcept { return crossfade_samples_; }
    /// Override the engage/release crossfade length; 1 is a hard switch.
    /// Takes effect on the next fade. Audio thread, or while stopped.
    void set_crossfade_samples(int samples) noexcept {
        crossfade_samples_ = std::max(1, samples);
        weight_step_ = std::min(weight_step_, crossfade_samples_);
    }
    /// Override the signal floor a capture window must reach before a freeze
    /// latches (mean power per sample per channel); 0 latches silence.
    void set_signal_floor_power(double power) noexcept {
        signal_floor_power_ = std::max(0.0, power);
    }

    [[nodiscard]] const pulp::signal::FreezeHold& hold() const noexcept { return hold_; }

    // SPECTR-RENDER-PATH BEGIN
    //
    // Audio thread. No clock, no lock, no allocation: the schedule is counted
    // in samples from the stream, so a bounce and real-time playback hold the
    // same sound.
    void process_block(const float* const* input, float* const* wet,
                       int channels, int num_samples) noexcept override {
        if (!prepared_ || channels != channels_) {
            for (int ch = 0; ch < channels; ++ch)
                if (wet[ch] != input[ch])
                    std::copy(input[ch], input[ch] + num_samples, wet[ch]);
            return;
        }
        int done = 0;
        while (done < num_samples) {
            const int chunk = std::min(num_samples - done, kHop - hop_pos_);
            render_chunk_(input, wet, done, chunk);
            hop_pos_ += chunk;
            done += chunk;
            if (hop_pos_ == kHop) {
                hop_pos_ = 0;
                hop_boundary_();
            }
        }
    }

private:
    void render_chunk_(const float* const* input, float* const* wet,
                       int offset, int count) noexcept {
        const auto window = static_cast<std::size_t>(kFftSize);
        // Record the live input first: `wet` may alias `input`.
        for (int ch = 0; ch < channels_; ++ch) {
            float* ring = input_ring_.data() + static_cast<std::size_t>(ch) * window;
            const float* in = input[ch] + offset;
            std::size_t pos = write_pos_;
            for (int i = 0; i < count; ++i) {
                ring[pos] = in[i];
                if (++pos == window) pos = 0;
            }
            double energy = 0.0;
            for (int i = 0; i < count; ++i)
                energy += static_cast<double>(in[i]) * in[i];
            hop_energy_accum_ += energy;
        }

        if (phase_ == Phase::live || phase_ == Phase::arming
            || phase_ == Phase::preparing) {
            for (int ch = 0; ch < channels_; ++ch)
                if (wet[ch] != input[ch])
                    std::copy(input[ch] + offset, input[ch] + offset + count,
                              wet[ch] + offset);
            write_pos_ = (write_pos_ + static_cast<std::size_t>(count)) % window;
            // This chunk's share of the engage work, by samples, so no host
            // block carries more of it than its length warrants.
            if (phase_ == Phase::preparing)
                prepare_until_((prepare_steps_() * (hop_pos_ + count) + kHop - 1) / kHop);
            return;
        }

        const int direction = phase_ == Phase::releasing ? -1 : 1;
        const int start_step = weight_step_;
        for (int ch = 0; ch < channels_; ++ch) {
            const float* held = ola_.data() + static_cast<std::size_t>(ch) * window
                                + static_cast<std::size_t>(hop_pos_);
            const float* in = input[ch] + offset;
            float* out = wet[ch] + offset;
            int step = start_step;
            double energy = 0.0;
            for (int i = 0; i < count; ++i) {
                // The level match glides across the hop, whatever the block.
                const float glide = static_cast<float>(hop_pos_ + i + 1) / static_cast<float>(kHop);
                const float hold = held[i] * (held_gain_ + (held_gain_target_ - held_gain_) * glide);
                energy += static_cast<double>(held[i]) * held[i];
                if (phase_ == Phase::held) {
                    out[i] = hold;
                    continue;
                }
                step = std::clamp(step + direction, 0, crossfade_samples_);
                const float p = static_cast<float>(step)
                              / static_cast<float>(crossfade_samples_);
                const float angle = p * 1.57079632679489662f;
                const float g_hold = std::sin(angle);
                const float g_live = std::cos(angle);
                out[i] = g_live * in[i] + g_hold * hold;
            }
            held_energy_accum_ += energy;
            if (ch == channels_ - 1) weight_step_ = step;
        }
        if (phase_ == Phase::held) weight_step_ = crossfade_samples_;
        write_pos_ = (write_pos_ + static_cast<std::size_t>(count)) % window;

        if (phase_ == Phase::engaging && weight_step_ >= crossfade_samples_)
            phase_ = Phase::held;
        else if (phase_ == Phase::releasing && weight_step_ <= 0) {
            phase_ = Phase::live;
            if (pending_engage_) {
                pending_engage_ = false;
                if (requested_) phase_ = Phase::arming;
            }
        }
    }

    // One hop of input has been recorded and the ready segment of the output
    // ring consumed. Analyse, capture, latch, advance and resynthesise.
    void hop_boundary_() noexcept {
        // Mean input power of the hop just recorded, and of the analysis
        // window that ends with it.
        const auto hop_ring = hop_energy_.size();
        hop_energy_[hop_energy_pos_] = hop_energy_accum_
            / (static_cast<double>(kHop) * static_cast<double>(channels_));
        hop_energy_accum_ = 0.0;
        hop_energy_pos_ = (hop_energy_pos_ + 1) % hop_ring;
        double window_energy = 0.0;
        for (int back = 1; back <= hops_per_window_; ++back)
            window_energy += hop_energy_[(hop_energy_pos_ + hop_ring
                                          - static_cast<std::size_t>(back)) % hop_ring];
        frame_energy_[frame_energy_pos_] = window_energy / hops_per_window_;
        frame_energy_pos_ = (frame_energy_pos_ + 1) % frame_energy_.size();
        if (frames_since_clear_ < (1 << 30)) ++frames_since_clear_;

        if (hold_seconds_changed_()) hold_.set_capture_seconds(applied_hold_seconds_);
        update_level_match_();

        // Request edges. A release commits the hold to fading out; a freeze
        // asked for mid-release waits for the release to finish.
        if (requested_) {
            if (phase_ == Phase::live) phase_ = Phase::arming;
            else if (phase_ == Phase::releasing) pending_engage_ = true;
        } else {
            pending_engage_ = false;
            if (phase_ == Phase::arming || phase_ == Phase::preparing) {
                // Nothing of the hold has been heard.
                phase_ = Phase::live;
                hold_.set_frozen(false);
            } else if (phase_ == Phase::engaging || phase_ == Phase::held) {
                phase_ = Phase::releasing;
                hold_.set_frozen(false); // restarts the capture window
            }
        }

        if (phase_ == Phase::preparing) {
            // The pre-roll was built for this boundary: the ring is not
            // shifted, and the fade starts on the next sample.
            finish_prepare_();
            return;
        }

        const bool rendering = hold_audible();
        // Render the hold frame for the segment that starts now, BEFORE the
        // hold advances, so each frame carries the phases for its place.
        shift_output_();
        if (rendering) add_hold_frame_(0);

        if (phase_ == Phase::live || phase_ == Phase::arming
            || phase_ == Phase::releasing) {
            analyse_();
            if (phase_ == Phase::arming)
                hold_.set_frozen(window_has_signal_());
            const bool was_latched = hold_.is_latched();
            hold_.process_group(frame_ptrs_.data(), channels_, bins_);
            if (phase_ == Phase::arming && !was_latched && hold_.is_latched()) {
                begin_prepare_();
            } else if (phase_ == Phase::releasing && !hold_.is_latched()) {
                // The hold finished its own release; keep its phases moving
                // for as long as this fade still plays it.
                hold_.advance_hold(1);
            }
        } else {
            hold_.advance_hold(1);
        }
    }

    // A hop of the hold has played. Its raw power updates the estimate of
    // the hold's level -- a running mean seeded with the power measured at
    // the latch, then a slow follow -- and the gain the next hop glides to.
    void update_level_match_() noexcept {
        held_gain_ = held_gain_target_;
        if (!hold_audible()) {
            held_energy_accum_ = 0.0;
            return;
        }
        const double hop_power = held_energy_accum_
            / (static_cast<double>(kHop) * static_cast<double>(channels_));
        held_energy_accum_ = 0.0;
        held_age_samples_ += kHop;
        const double weight = static_cast<double>(kHop)
            / std::min(held_age_samples_, kLevelMatchSeconds * sample_rate_);
        held_power_ += std::min(1.0, weight) * (hop_power - held_power_);
        held_gain_target_ = static_cast<float>(std::clamp(
            std::sqrt(live_reference_power_ / std::max(held_power_, 1.0e-20)),
            static_cast<double>(kMinLevelMatch), static_cast<double>(kMaxLevelMatch)));
    }

    bool hold_seconds_changed_() noexcept {
        if (requested_hold_seconds_ == applied_hold_seconds_) return false;
        applied_hold_seconds_ = requested_hold_seconds_;
        return true;
    }

    void clear_energy_history_() noexcept {
        std::fill(hop_energy_.begin(), hop_energy_.end(), 0.0);
        std::fill(frame_energy_.begin(), frame_energy_.end(), 0.0);
        hop_energy_pos_ = 0;
        frame_energy_pos_ = 0;
        hop_energy_accum_ = 0.0;
        frames_since_clear_ = 0;
    }

    [[nodiscard]] bool window_has_signal_() const noexcept {
        // The frames a latch would average analysed this many hops of input
        // between them. Every one of those hops must carry signal, and must
        // have arrived after the last clear: a window that is part silence
        // (a note just starting, or the zeros a transport jump leaves) would
        // be averaged into a hold quieter and emptier than the sound.
        const int span = hold_.capture_frames() - 1 + hops_per_window_;
        if (frames_since_clear_ < span) return false;
        const auto ring = hop_energy_.size();
        for (int back = 1; back <= span; ++back) {
            const auto index = (hop_energy_pos_ + ring
                                - static_cast<std::size_t>(back)) % ring;
            if (!(hop_energy_[index] >= signal_floor_power_)) return false;
        }
        return true;
    }

    // Windowed FFT of the most recent kFftSize input samples, oldest first.
    void analyse_() noexcept {
        const auto window = static_cast<std::size_t>(kFftSize);
        for (int ch = 0; ch < channels_; ++ch) {
            const float* ring = input_ring_.data() + static_cast<std::size_t>(ch) * window;
            for (std::size_t n = 0; n < window; ++n)
                scratch_[n] = ring[(write_pos_ + n) % window] * window_[n];
            fft_.forward_real(scratch_.data(),
                              spectra_.data() + static_cast<std::size_t>(ch) * window);
        }
    }

    // Drop the segment just played and open a silent one at the far end.
    void shift_output_() noexcept {
        const auto window = static_cast<std::size_t>(kFftSize);
        const auto hop = static_cast<std::size_t>(kHop);
        for (int ch = 0; ch < channels_; ++ch) {
            float* ola = ola_.data() + static_cast<std::size_t>(ch) * window;
            std::copy(ola + hop, ola + window, ola);
            std::fill(ola + window - hop, ola + window, 0.0f);
        }
    }

    // Resynthesise the current hold frame and overlap-add it into the output
    // ring, as the frame placed `hops_back` hops before now: only its part
    // from now onward is added.
    void add_hold_frame_(int hops_back) noexcept {
        add_hold_frame_to_(ola_.data(), static_cast<std::size_t>(kFftSize),
                           -hops_back * kHop);
    }

    // The same, into `dest` (channels * `length`), with the frame's first
    // sample at `at` (negative: that many samples of it are already past).
    void add_hold_frame_to_(float* dest, std::size_t length, int at) noexcept {
        if (!hold_.write_hold(frame_ptrs_.data(), channels_, bins_)) return;
        synthesise_to_(dest, length, at);
    }

    // Resynthesise the spectra already in `spectra_` and overlap-add them
    // into `dest`, as add_hold_frame_to_() does after rendering the hold.
    void synthesise_to_(float* dest, std::size_t length, int at) noexcept {
        const auto window = static_cast<std::size_t>(kFftSize);
        const auto half = static_cast<std::size_t>(kFftSize / 2);
        // Only the part of the frame from `dest`'s start to its end is added.
        const std::size_t first = at < 0 ? static_cast<std::size_t>(-at) : 0;
        const auto room = static_cast<std::ptrdiff_t>(length) - at;
        const std::size_t last = room <= 0 ? 0
            : std::min(window, static_cast<std::size_t>(room));
        // Two real channels per complex inverse transform: one in the real
        // part, one in the imaginary part. The arithmetic is spelled out:
        // std::complex multiplication carries NaN/infinity recovery, and
        // this loop runs once per bin for every frame of the hold.
        for (int ch = 0; ch < channels_; ch += 2) {
            const bool pair = ch + 1 < channels_;
            const auto* a = spectra_.data() + static_cast<std::size_t>(ch) * window;
            const auto* b = pair ? a + window : nullptr;
            for (std::size_t k = 1; k < half; ++k) {
                // A quarter-cycle turn, one sign per peak region:
                // ka = a * (i * turn), kb = b * (i * turn).
                const float turn = static_cast<float>(rotation_[k]);
                const float ka_re = -turn * a[k].imag(), ka_im = turn * a[k].real();
                const float kb_re = pair ? -turn * b[k].imag() : 0.0f;
                const float kb_im = pair ? turn * b[k].real() : 0.0f;
                // time[k] = ka + i kb; time[N - k] = conj(ka) + i conj(kb).
                time_[k] = {ka_re - kb_im, ka_im + kb_re};
                time_[window - k] = {ka_re + kb_im, kb_re - ka_im};
            }
            // DC and Nyquist of a real signal are real; turned a quarter
            // cycle, they have nothing left to carry.
            time_[0] = {};
            time_[half] = {};
            fft_.inverse(time_.data());
            float* out_a = dest + static_cast<std::size_t>(ch) * length;
            float* out_b = pair ? out_a + length : nullptr;
            const std::ptrdiff_t shift = at;
            if (pair) {
                for (std::size_t n = first; n < last; ++n) {
                    const float w = synthesis_window_[n];
                    out_a[static_cast<std::ptrdiff_t>(n) + shift] += time_[n].real() * w;
                    out_b[static_cast<std::ptrdiff_t>(n) + shift] += time_[n].imag() * w;
                }
            } else {
                for (std::size_t n = first; n < last; ++n)
                    out_a[static_cast<std::ptrdiff_t>(n) + shift] += time_[n].real() * synthesis_window_[n];
            }
        }
    }

    // ENGAGE, SPREAD OVER ONE HOP. The hold latched on the frame just
    // analysed; its phases have already advanced one hop past that frame.
    // Before it is heard, the output ring is filled with the frames that
    // would already be overlapping (a pre-roll, so the full hold is present
    // from the fade's first sample) and the level of the hold's first hops is
    // measured. That is hops_per_window + kLevelMeasureHops - 1 inverse
    // transforms.
    //
    // Done inside the callback the latch lands in, it cost several
    // milliseconds in that one callback, past a 128-frame buffer's entire
    // real-time budget: a live host missed the deadline and dropped the
    // buffer, and the tap clicked (the output cut mid-swing) -- on engage
    // only, since a release renders nothing extra. An offline render has no
    // deadline and was clean.
    //
    // So the work is spread across the hop after the latch, a fixed number
    // of frames per sample of audio, whatever the host's block size; live
    // input passes meanwhile, and the fade starts at the next hop boundary,
    // with the pre-roll built for that boundary. It costs one hop of engage
    // latency and bounds every callback to a few frames of work.
    //
    // Inside the pre-roll the phases step without the random walk, so each
    // frame is the one before it with every bin turned by that bin's advance
    // over one hop: the hold is rendered from its phases once, and each later
    // frame is one complex multiply per bin. The hold's own phases step
    // exactly as before.
    void begin_prepare_() noexcept {
        // The live level the hold is matched to: the capture window's mean,
        // as it stands at the latch.
        const int frames = hold_.capture_frames();
        const auto ring = frame_energy_.size();
        double live = 0.0;
        for (int back = 1; back <= frames; ++back)
            live += frame_energy_[(frame_energy_pos_ + ring
                                   - static_cast<std::size_t>(back)) % ring];
        live_reference_power_ = live / static_cast<double>(frames);
        prepare_step_ = 0;
        phase_ = Phase::preparing;
    }

    // The steps of the preparation: one to set it up, hops_per_window
    // pre-roll frames, then kLevelMeasureHops - 1 measurement frames.
    [[nodiscard]] int prepare_steps_() const noexcept {
        return 1 + hops_per_window_ + kLevelMeasureHops - 1;
    }

    // Do the preparation's steps up to `target`.
    void prepare_until_(int target) noexcept {
        target = std::min(target, prepare_steps_());
        const auto window = static_cast<std::size_t>(kFftSize);
        const auto length = static_cast<std::size_t>(kLevelMeasureHops * kHop);
        for (; prepare_step_ < target; ++prepare_step_) {
            if (prepare_step_ == 0) {
                choose_rotation_();
                std::fill(ola_.begin(), ola_.end(), 0.0f);
                // The pre-roll is built for the NEXT hop boundary. A frame
                // placed `back` hops before it continues the latched frame by
                // (hops_per_window + 1 - back) hops; the current phases are
                // one hop on already, so the oldest (back = hops_per_window
                // - 1) is one step further on.
                hold_.rewind_hold_phases(-1);
                prepare_have_hold_ = hold_.write_hold(frame_ptrs_.data(), channels_, bins_);
                if (prepare_have_hold_) compute_hop_rotor_();
                continue;
            }
            const int step = prepare_step_ - 1;
            if (step < hops_per_window_) {
                const int back = hops_per_window_ - 1 - step;
                if (prepare_have_hold_)
                    synthesise_to_(ola_.data(), window, -back * kHop);
                // Step on without the random walk inside the pre-roll.
                if (back > 0) {
                    hold_.rewind_hold_phases(-1);
                    if (prepare_have_hold_) rotate_spectra_one_hop_();
                }
                continue;
            }
            // The level measurement: the output ring's first hops completed,
            // in a scratch copy, with the frames that will follow.
            const int ahead = step - hops_per_window_ + 1;
            if (ahead == 1)
                for (int ch = 0; ch < channels_; ++ch)
                    std::copy_n(ola_.data() + static_cast<std::size_t>(ch) * window, length,
                                level_scratch_.data() + static_cast<std::size_t>(ch) * length);
            hold_.rewind_hold_phases(-1);
            if (prepare_have_hold_) {
                rotate_spectra_one_hop_();
                synthesise_to_(level_scratch_.data(), length, ahead * kHop);
            }
        }
    }

    // The hop boundary the pre-roll was built for: finish anything a short
    // hop left undone, set the level, and start the fade.
    void finish_prepare_() noexcept {
        prepare_until_(prepare_steps_());
        // The measurement stepped the phases ahead; put them back.
        hold_.rewind_hold_phases(kLevelMeasureHops - 1);
        double held = 0.0;
        if (prepare_have_hold_) {
            const auto length = static_cast<std::size_t>(kLevelMeasureHops * kHop)
                              * static_cast<std::size_t>(channels_);
            double energy = 0.0;
            for (std::size_t n = 0; n < length; ++n)
                energy += static_cast<double>(level_scratch_[n]) * level_scratch_[n];
            held = energy / static_cast<double>(length);
        }
        // Past the newest frame, advance as every later hop does.
        hold_.advance_hold(1);
        phase_ = Phase::engaging;
        weight_step_ = 0;
        // The hold enters at its matched level: the fade is not asked to
        // hide a correction still settling.
        held_power_ = held;
        held_gain_ = held_gain_target_ = static_cast<float>(std::clamp(
            std::sqrt(live_reference_power_ / std::max(held_power_, 1.0e-20)),
            static_cast<double>(kMinLevelMatch), static_cast<double>(kMaxLevelMatch)));
        held_energy_accum_ = 0.0;
        held_age_samples_ = static_cast<double>(kLevelMeasureHops * kHop);
    }

    // Each bin's phase advance over one hop at the hold's instantaneous
    // frequency, as a unit rotor: what rewind_hold_phases(-1) adds.
    void compute_hop_rotor_() noexcept {
        const auto freq = hold_.instantaneous_frequency();
        const auto bins = std::min(static_cast<std::size_t>(bins_), freq.size());
        for (std::size_t k = 0; k < bins; ++k) {
            const double advance = freq[k] * static_cast<double>(kHop);
            hop_rotor_[k] = {static_cast<float>(std::cos(advance)),
                             static_cast<float>(std::sin(advance))};
        }
    }

    // Step every channel's spectrum in `spectra_` on by one hop.
    void rotate_spectra_one_hop_() noexcept {
        const auto window = static_cast<std::size_t>(kFftSize);
        const auto bins = static_cast<std::size_t>(bins_);
        for (int ch = 0; ch < channels_; ++ch) {
            auto* spectrum = spectra_.data() + static_cast<std::size_t>(ch) * window;
            for (std::size_t k = 0; k < bins; ++k) {
                const float re = spectrum[k].real(), im = spectrum[k].imag();
                const float c = hop_rotor_[k].real(), sn = hop_rotor_[k].imag();
                spectrum[k] = {re * c - im * sn, re * sn + im * c};
            }
        }
    }

    // A quarter-cycle turn for every bin of the new hold, +90 or -90
    // degrees, the same on every channel. Independent per bin, so no timing
    // survives -- except across the main lobe of a tonal peak (a bin well
    // above its neighbourhood), which turns as one, so the partial keeps its
    // level and shape.
    void choose_rotation_() noexcept {
        const auto bins = static_cast<std::size_t>(bins_);
        for (std::size_t k = 0; k < bins; ++k) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels_; ++ch) sum += hold_.held_magnitudes(ch)[k];
            magnitude_[k] = sum;
            rotation_[k] = next_sign_();
        }
        // Local means over +-kPeakNeighbourhood bins, by a running sum.
        prefix_[0] = 0.0;
        for (std::size_t k = 0; k < bins; ++k) prefix_[k + 1] = prefix_[k] + magnitude_[k];
        const auto reach = static_cast<std::size_t>(kPeakNeighbourhood);
        const auto lobe = static_cast<std::size_t>(kPeakLobe);
        for (std::size_t k = 1; k + 1 < bins; ++k) {
            const float m = magnitude_[k];
            if (!(m > magnitude_[k - 1] && m >= magnitude_[k + 1])) continue;
            const std::size_t lo = k > reach ? k - reach : 0;
            const std::size_t hi = std::min(bins, k + reach + 1);
            const double mean = (prefix_[hi] - prefix_[lo]) / static_cast<double>(hi - lo);
            if (!(m > kPeakProminence * mean)) continue;
            const std::int8_t sign = next_sign_();
            for (std::size_t j = k > lobe ? k - lobe : 0; j <= std::min(bins - 1, k + lobe); ++j)
                rotation_[j] = sign;
        }
    }

    // xorshift64*: deterministic from the stream, so a bounce and real-time
    // playback turn the same hold the same way.
    std::int8_t next_sign_() noexcept {
        rng_ ^= rng_ >> 12;
        rng_ ^= rng_ << 25;
        rng_ ^= rng_ >> 27;
        return ((rng_ * 0x2545f4914f6cdd1dull) >> 63) != 0 ? std::int8_t{1} : std::int8_t{-1};
    }
    // SPECTR-RENDER-PATH END

    pulp::signal::FreezeHold hold_{};
    pulp::signal::Fft fft_{};
    std::vector<float> window_;
    std::vector<float> synthesis_window_;         // window_ * synthesis_scale_
    float synthesis_scale_ = 1.0f;

    std::vector<float> input_ring_;               // channels * kFftSize
    std::vector<float> ola_;                      // channels * kFftSize
    std::vector<std::complex<float>> spectra_;    // channels * kFftSize
    std::vector<std::complex<float>*> frame_ptrs_;
    std::vector<std::complex<float>> time_;       // kFftSize
    std::vector<float> scratch_;                  // kFftSize

    std::vector<double> hop_energy_;
    std::vector<double> frame_energy_;
    std::size_t hop_energy_pos_ = 0;
    std::size_t frame_energy_pos_ = 0;
    double hop_energy_accum_ = 0.0;
    int frames_since_clear_ = 0;
    int hops_per_window_ = 16;

    static constexpr std::uint64_t kRngSeed = 0x2545f4914f6cdd1dull;
    std::vector<std::int8_t> rotation_;           // bins: +1 or -1 quarter cycle
    std::vector<std::complex<float>> hop_rotor_;  // bins: one hop's phase advance
    std::vector<float> magnitude_;                // bins, scratch
    std::vector<double> prefix_;                  // bins + 1, scratch
    std::uint64_t rng_ = kRngSeed;
    std::vector<float> level_scratch_;            // channels * kLevelMeasureHops * kHop
    double signal_floor_power_ = kSignalFloorPower;
    double live_reference_power_ = 0.0;
    double held_power_ = 0.0;
    double held_energy_accum_ = 0.0;
    float held_gain_ = 1.0f;
    float held_gain_target_ = 1.0f;
    double held_age_samples_ = 0.0;
    int crossfade_samples_ = 1;

    double sample_rate_ = 0.0;
    int channels_ = 0;
    int bins_ = 0;
    std::size_t write_pos_ = 0;
    int hop_pos_ = 0;
    bool prepared_ = false;
    bool requested_ = false;
    bool pending_engage_ = false;
    Phase phase_ = Phase::live;
    int weight_step_ = 0;
    int prepare_step_ = 0;
    bool prepare_have_hold_ = false;
    double requested_hold_seconds_ = kDefaultHoldSeconds;
    double applied_hold_seconds_ = kDefaultHoldSeconds;
};

} // namespace spectr
