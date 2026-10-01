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
/// TWO KINDS OF HOLD. Hold length is how much of the input is frozen. From
/// kLoopMinSeconds up that audio itself is looped -- the last Hold-length
/// seconds, repeated with a crossfaded, waveform-matched seam (see THE LOOP):
/// you hear the phrase repeat. Below it a loop would be a buzz, so the hold
/// is spectral: a steady resynthesis of the spectrum of that stretch.
///
/// HOW IT HOLDS (spectral). The source runs its own short-hop analysis of the input
/// (`kFftSize` points every `kHop` samples) into `pulp::signal::FreezeHoldT`,
/// which captures the recent frames and, at the latch, averages their
/// magnitudes and measures each bin's instantaneous frequency. The phases the
/// hold is rendered with are the source's own (see take_hold_()): chosen once
/// at the latch so the hold is stationary from its first frame, then advanced
/// hop by hop, and resynthesised by overlap-add.
///
/// HOW IT ENGAGES. Nothing waits for an analysis latency. After the latch
/// the output ring is pre-rolled with the frames that would already have been
/// overlapping (the latched phases stepped forward hop by hop), so a full
/// hold is present from the fade's first sample, and a short crossfade takes
/// the output from live to held (see THE ENGAGE FADE). The pre-roll is built over
/// the hop after the latch, a share of it per sample of audio, and the fade
/// starts at the next hop boundary: done at once, it was several
/// milliseconds of work in the one callback a tap lands in -- more than a
/// small host buffer's whole real-time budget. A release runs the same
/// fade the other way, and a freeze requested during that fade waits for it
/// to finish, so the hold being faded out is never swapped underneath the
/// fade.
///
/// STATIONARY FROM THE FIRST FRAME. A hold that starts from the latched
/// frame's phases and lets every bin run at its own measured frequency enters
/// as a coherent copy of the moment and then, as bins that share one partial
/// drift apart, sags over a few hundred milliseconds into a quieter, more
/// diffuse steady state -- heard as the sound being frozen and then frozen
/// again. So a tonal peak's main lobe is locked: every bin of it plays at the
/// peak's frequency, so the partial never decoheres; and every other bin
/// starts at a uniformly random phase, the diffuse state it would drift into.
///
/// NO GHOST. The latched frame's phases encode WHEN things happened inside
/// it. Advanced bin by bin they replay that timing one analysis window later
/// -- a drum hit that landed just before the press comes back as a tick just
/// after it. The random phases of the non-tonal bins keep none of that
/// timing, and a locked lobe takes a steady sinusoid's shape, which places
/// its partial nowhere in particular.
///
/// THE ENGAGE FADE: THE SOUND CONTINUES. A locked lobe starts at the latched
/// frame's phase and runs at its partial's measured frequency, so each held
/// partial is the live partial carried on, in phase: nothing is turned or
/// bent while the fade runs. (Turning the partials a quarter cycle, as an
/// earlier version did to make them orthogonal to the live ones, swept each
/// one's phase by 90 degrees over the fade -- a 20-40 cent glide heard as a
/// brief doubled sound at the press.) The tonal partials and the noise-like
/// rest are resynthesised into separate rings and fade by different laws
/// under the one live gain cos: the partials by 1 - cos, so each partial's
/// amplitude is exactly 1 throughout; the rest by sin, so the noise power is
/// exactly 1 throughout. No gain rides the waveform (one that followed the
/// waveform's own power sample by sample modulated the fade at audio rate --
/// a click on drums). A locked lobe takes no random walk, so it stays in
/// phase with a steady live partial for as long as it plays. A release is an
/// equal-power fade normalised by the hold's and the live input's measured
/// correlation over the hop before it: a steady tone still playing live
/// (correlated) and anything new (not) both release level.
///
/// LEVEL. Averaging magnitudes lowers noise-like material by a few dB; the
/// hold's noise-like rest is matched to the live level the tonal partials do
/// not account for, and the partials themselves play at the input's level.
/// The hold's level is predicted from its spectrum at the latch, before any
/// of it is heard (see predict_hold_power_()), and the gain is fixed for as
/// long as the hold plays: the hold is stationary, so nothing is left to
/// follow, and a gain still settling after the engage would itself be heard.
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
    /// how far it may be corrected.
    static constexpr float kMinLevelMatch = 0.5f;
    static constexpr float kMaxLevelMatch = 2.0f;
    /// Peak-to-RMS ratio taken for the hold's random-phase part in its
    /// peak estimate: a Gaussian's typical peak over the few hundred
    /// milliseconds a capture window spans (12 dB).
    static constexpr double kNoiseCrest = 4.0;

    /// Resolution of the table the hold's level is predicted from: frame
    /// phase steps over a turn, and points across a hop.
    static constexpr int kLevelThetaSteps = 1024;
    static constexpr int kLevelTaps = 32;

    /// A tonal peak, whose partial the hold carries on in phase: a local maximum this
    /// many times the mean magnitude of the bins within the neighbourhood,
    /// turned as one across its Hann main lobe.
    static constexpr float kPeakProminence = 4.0f;
    static constexpr int kPeakNeighbourhood = 16;
    static constexpr int kPeakLobe = 2;

    /// Mean power per sample, per channel, every hop of input under the
    /// capture window must reach before a freeze latches (-90 dBFS RMS).
    static constexpr double kSignalFloorPower = 1.0e-9;

    /// A Hold length from this long up loops the audio itself: the last
    /// Hold-length seconds of input, repeated with a crossfaded seam. Below
    /// it the hold is spectral (a loop that short is a buzz, not a phrase).
    static constexpr double kLoopMinSeconds = 0.25;
    /// How far the loop's start may move from exactly Hold length before
    /// the end, to land where the audio before it matches the audio before
    /// the end; and how much audio that match compares.
    static constexpr double kLoopSearchSeconds = 0.005;
    static constexpr double kLoopMatchSeconds = 0.010;


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
        ola_tonal_.assign(channel_count * per_channel, 0.0f);
        latched_.assign(channel_count * per_channel, {});
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
        hop_peak_.assign(hop_energy_.size(), 0.0f);
        frame_energy_.assign(static_cast<std::size_t>(max_frames), 0.0);

        build_level_table_();
        lock_peak_.assign(static_cast<std::size_t>(bins_), 0);
        hold_phase_.assign(channel_count * static_cast<std::size_t>(bins_), 0.0);
        locked_.assign(static_cast<std::size_t>(bins_), 0);
        lock_mag_.assign(static_cast<std::size_t>(bins_), 0.0f);
        hold_freq_.assign(static_cast<std::size_t>(bins_), 0.0);
        jitter_ = static_cast<double>(hold_.config().phase_jitter);
        hop_rotor_.assign(static_cast<std::size_t>(bins_), {1.0f, 0.0f});
        magnitude_.assign(static_cast<std::size_t>(bins_), 0.0f);
        prefix_.assign(static_cast<std::size_t>(bins_) + 1, 0.0);
        crossfade_samples_ = std::max(
            1, static_cast<int>(std::lround(kCrossfadeSeconds * sample_rate)));

        // The loop: every sample of input is recorded, so a loop can be taken
        // from the moment the stream starts; the loop plays from its own copy
        // so recording never stops.
        loop_search_ = static_cast<std::int64_t>(std::lround(kLoopSearchSeconds * sample_rate));
        // The match is taken over the audio that arrives in the hop between
        // the latch and the engage, so it fits in a hop.
        loop_match_ = std::clamp<std::int64_t>(
            static_cast<std::int64_t>(std::lround(kLoopMatchSeconds * sample_rate)), 1, kHop);
        loop_min_ = static_cast<std::int64_t>(std::lround(kLoopMinSeconds * sample_rate));
        // The seam search compares every `loop_stride_`-th sample, so its cost
        // is the same at any sample rate.
        loop_stride_ = std::max<std::int64_t>(1, std::llround(sample_rate / 48000.0));
        const auto longest = static_cast<std::int64_t>(std::ceil(kMaxHoldSeconds * sample_rate));
        record_length_ = static_cast<std::size_t>(longest + loop_search_ + loop_match_ + 4 * kHop);
        record_.assign(channel_count * record_length_, 0.0f);
        loop_capacity_ = static_cast<std::size_t>(longest + loop_search_ + crossfade_samples_ + kHop);
        loop_.assign(channel_count * loop_capacity_, 0.0f);
        const auto candidates = static_cast<std::size_t>(2 * loop_search_ / loop_stride_ + 2);
        search_cross_.assign(candidates, 0.0);
        search_energy_.assign(candidates, 0.0);

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
        std::fill(ola_tonal_.begin(), ola_tonal_.end(), 0.0f);
        write_pos_ = 0;
        hop_pos_ = 0;
        clear_energy_history_();
        phase_ = Phase::live;
        weight_step_ = 0;
        pending_engage_ = false;
        loop_mode_ = false;
        fade_rho_ = 0.0f;
        std::fill(record_.begin(), record_.end(), 0.0f);
        recorded_ = 0;
        history_ = 0;
        rng_ = kRngSeed;
        walk_rng_ = kWalkSeed;
        held_gain_ = 1.0f;
        release_rho_ = 0.0f;
        rho_cross_ = rho_live_ = rho_hold_ = 0.0;
        last_rho_ = 0.0f;
    }

    /// A transport discontinuity: forget the input analysed so far, keep the
    /// hold and wherever the crossfade is. An armed freeze re-fills its
    /// capture window from input that arrives after this call.
    void clear_history() noexcept {
        if (!prepared_) return;
        hold_.clear_history();
        history_ = 0;
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
    /// True when the hold that is (or was last) latched loops the audio
    /// itself rather than holding its spectrum.
    [[nodiscard]] bool looping() const noexcept { return loop_mode_; }
    /// The latched loop's length in samples (0 before the first loop).
    [[nodiscard]] std::int64_t loop_length() const noexcept { return loop_length_; }
    /// Where the playing loop is: the index of the next loop sample it plays.
    [[nodiscard]] std::int64_t loop_position() const noexcept { return loop_position_; }
    /// One sample of the latched loop as a pass after the first plays it (the
    /// seam crossfaded in), for a reader that plays the loop on its own
    /// schedule (FreezeKeys). `at` must be in [0, loop_length()). Audio
    /// thread; valid while the hold is audible.
    ///
    /// The loop is copied out of the recording as its own first pass plays,
    /// so until that pass wraps only the samples before loop_position() are
    /// in the copy; the rest are read from the recording, which keeps every
    /// sample of the loop for longer than a pass.
    [[nodiscard]] float loop_sample(int channel, std::int64_t at) const noexcept {
        const float* loop = loop_.data() + static_cast<std::size_t>(channel) * loop_capacity_;
        const bool copied = loop_seam_ || at < loop_position_;
        const float start = copied ? loop[at] : recorded_at_(channel, loop_start_ + at);
        return seam_value_(loop, start, at);
    }
    /// The same sample on a chosen pass: `seam` false is the loop's first
    /// pass, the plain start with no crossfade in from the end (a reader that
    /// starts the loop from its top, FreezeKeys' restarting voices); true is
    /// a later pass, as loop_sample(channel, at).
    [[nodiscard]] float loop_sample(int channel, std::int64_t at, bool seam) const noexcept {
        if (seam) return loop_sample(channel, at);
        const float* loop = loop_.data() + static_cast<std::size_t>(channel) * loop_capacity_;
        const bool copied = loop_seam_ || at < loop_position_;
        return copied ? loop[at] : recorded_at_(channel, loop_start_ + at);
    }
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
    /// The gain the hold's noise-like part plays at: its level matched,
    /// once, to the live window it was taken from (for tests and
    /// diagnostics). Its tonal part always plays at the input's level.
    [[nodiscard]] float level_match() const noexcept { return held_gain_; }
    /// Render only one part of a spectral hold (for tests): its tonal
    /// partials, its noise-like rest, or both (the default).
    void set_hold_parts(bool tonal, bool noise) noexcept {
        play_tonal_ = tonal;
        play_noise_ = noise;
    }

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
            float* record = record_.data() + static_cast<std::size_t>(ch) * record_length_;
            const float* source = input[ch] + offset;
            auto at = static_cast<std::size_t>(recorded_ % static_cast<std::int64_t>(record_length_));
            for (int i = 0; i < count; ++i) {
                record[at] = source[i];
                if (++at == record_length_) at = 0;
            }
        }
        recorded_ += count;
        history_ = std::min<std::int64_t>(history_ + count, static_cast<std::int64_t>(record_length_));
        for (int ch = 0; ch < channels_; ++ch) {
            float* ring = input_ring_.data() + static_cast<std::size_t>(ch) * window;
            const float* in = input[ch] + offset;
            std::size_t pos = write_pos_;
            for (int i = 0; i < count; ++i) {
                ring[pos] = in[i];
                if (++pos == window) pos = 0;
            }
            double energy = 0.0;
            float peak = hop_peak_accum_;
            for (int i = 0; i < count; ++i) {
                energy += static_cast<double>(in[i]) * in[i];
                peak = std::max(peak, std::abs(in[i]));
            }
            hop_energy_accum_ += energy;
            hop_peak_accum_ = peak;
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
            if (phase_ == Phase::preparing) {
                if (loop_mode_) match_loop_end_(recorded_ - count, recorded_);
                else prepare_until_((prepare_steps_() * (hop_pos_ + count) + kHop - 1) / kHop);
            }
            return;
        }

        const int direction = phase_ == Phase::releasing ? -1 : 1;
        const int start_step = weight_step_;
        std::int64_t loop_at = loop_position_;
        bool seam = loop_seam_;
        // A spectral engage: the hold's tonal partials continue the live
        // ones in phase (correlated), its noise-like rest is unrelated to the
        // live noise. One live gain, cos, serves both: the tonal hold comes
        // in by 1 - cos, so each partial's amplitude stays exactly 1, and the
        // noise by sin, so the noise power stays exactly 1. Nothing is turned
        // or bent while it happens. Every other fade -- a release (the live
        // sound after it is whatever the player is playing now), and the
        // loop's engage -- is equal-power, normalised by the two sides'
        // measured correlation.
        const bool split = !loop_mode_ && phase_ == Phase::engaging;
        const float rho = phase_ == Phase::releasing ? release_rho_ : fade_rho_;
        double cross = 0.0, live_power = 0.0, hold_power = 0.0;
        for (int ch = 0; ch < channels_; ++ch) {
            const float* noise = ola_.data() + static_cast<std::size_t>(ch) * window
                                 + static_cast<std::size_t>(hop_pos_);
            const float* tonal = ola_tonal_.data() + static_cast<std::size_t>(ch) * window
                                 + static_cast<std::size_t>(hop_pos_);
            float* loop = loop_.data() + static_cast<std::size_t>(ch) * loop_capacity_;
            const float* in = input[ch] + offset;
            float* out = wet[ch] + offset;
            int step = start_step;
            loop_at = loop_position_;
            seam = loop_seam_;
            for (int i = 0; i < count; ++i) {
                float hold, hold_tonal = 0.0f, hold_noise = 0.0f;
                if (loop_mode_) {
                    hold = loop_sample_(ch, loop, loop_at, seam);
                    if (++loop_at == loop_length_) { loop_at = 0; seam = true; }
                } else {
                    hold_tonal = play_tonal_ ? tonal[i] : 0.0f;
                    hold_noise = play_noise_ ? noise[i] : 0.0f;
                    hold = hold_tonal + hold_noise;
                }
                if (phase_ == Phase::held) {
                    out[i] = hold;
                    // How alike the hold and the live input are, for the
                    // release fade.
                    cross += static_cast<double>(hold) * in[i];
                    live_power += static_cast<double>(in[i]) * in[i];
                    hold_power += static_cast<double>(hold) * hold;
                    continue;
                }
                step = std::clamp(step + direction, 0, crossfade_samples_);
                const float p = static_cast<float>(step)
                              / static_cast<float>(crossfade_samples_);
                const float angle = p * 1.57079632679489662f;
                const float g_hold = std::sin(angle);
                const float g_live = std::cos(angle);
                if (split) {
                    out[i] = g_live * in[i] + (1.0f - g_live) * hold_tonal + g_hold * hold_noise;
                    continue;
                }
                // Correlated sounds sum above an equal-power fade's unity:
                // take out what their measured correlation adds.
                const float norm = rho != 0.0f
                    ? 1.0f / std::sqrt(1.0f + 2.0f * rho * g_hold * g_live) : 1.0f;
                out[i] = (g_live * in[i] + g_hold * hold) * norm;
            }
            if (ch == channels_ - 1) weight_step_ = step;
        }
        rho_cross_ += cross;
        rho_live_ += live_power;
        rho_hold_ += hold_power;
        if (loop_mode_ && hold_audible()) {
            loop_position_ = loop_at;
            loop_seam_ = seam;
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
        hop_peak_[hop_energy_pos_] = hop_peak_accum_;
        hop_peak_accum_ = 0.0f;
        hop_energy_pos_ = (hop_energy_pos_ + 1) % hop_ring;
        double window_energy = 0.0;
        for (int back = 1; back <= hops_per_window_; ++back)
            window_energy += hop_energy_[(hop_energy_pos_ + hop_ring
                                          - static_cast<std::size_t>(back)) % hop_ring];
        frame_energy_[frame_energy_pos_] = window_energy / hops_per_window_;
        frame_energy_pos_ = (frame_energy_pos_ + 1) % frame_energy_.size();
        if (frames_since_clear_ < (1 << 30)) ++frames_since_clear_;

        if (hold_seconds_changed_()) hold_.set_capture_seconds(applied_hold_seconds_);
        // The correlation of the hold with the live input over the hop just
        // played (held only). Only a positive one is taken out of the fade:
        // a negative reading over one hop of unrelated sounds is chance, and
        // normalising for it would lift both sides mid-fade.
        if (phase_ == Phase::held && rho_live_ > 0.0 && rho_hold_ > 0.0)
            last_rho_ = static_cast<float>(std::clamp(
                rho_cross_ / std::sqrt(rho_live_ * rho_hold_), 0.0, 1.0));
        else if (phase_ != Phase::held)
            last_rho_ = 0.0f;
        rho_cross_ = rho_live_ = rho_hold_ = 0.0;

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
                // How alike the hold and the live sound were over the hop
                // before: a steady tone still playing live is the hold's
                // own partial, in phase; anything else is unrelated.
                release_rho_ = last_rho_;
            }
        }

        if (phase_ == Phase::preparing) {
            // The pre-roll was built for this boundary: the ring is not
            // shifted, and the fade starts on the next sample.
            if (loop_mode_) finish_loop_prepare_();
            else finish_prepare_();
            return;
        }

        const bool rendering = hold_audible();
        const bool spectral = rendering && !loop_mode_;
        // Render the hold frame for the segment that starts now, BEFORE the
        // hold advances, so each frame carries the phases for its place.
        shift_output_();
        if (spectral) add_hold_frame_(0);
        if (rendering && loop_mode_) copy_loop_tail_();

        if (phase_ == Phase::live || phase_ == Phase::arming
            || phase_ == Phase::releasing) {
            analyse_();
            const bool loop = applied_hold_seconds_ >= kLoopMinSeconds;
            if (phase_ == Phase::arming)
                hold_.set_frozen(!loop && window_has_signal_());
            const bool was_latched = hold_.is_latched();
            // The frame a spectral latch would take, before FreezeHold mixes
            // anything into it: the hold continues its partials in phase.
            if (phase_ == Phase::arming && !loop)
                std::copy(spectra_.begin(), spectra_.end(), latched_.begin());
            hold_.process_group(frame_ptrs_.data(), channels_, bins_);
            if (phase_ == Phase::arming) {
                if (loop) {
                    if (loop_ready_()) begin_loop_prepare_();
                } else if (!was_latched && hold_.is_latched()) {
                    loop_mode_ = false;
                    begin_prepare_();
                }
            }
        }
        // The hold's phases move on for as long as it is heard.
        if (spectral) advance_phases_(1, true);
    }

    // THE LOOP. A Hold length of kLoopMinSeconds or more freezes the audio
    // itself: the last Hold-length seconds of input, repeated. The loop's
    // start is moved (by up to kLoopSearchSeconds) to where the audio just
    // before it best matches the audio just before its end, so running off
    // the end into the start continues the waveform; every pass through the
    // start crossfades from the audio that actually followed the end (kept
    // for the purpose) into the start, over the same length and law as the
    // engage, normalised by the two sides' measured correlation so an
    // aligned seam is as level as an unrelated one. The engage is that same
    // seam with the live input as the audio that follows the end.
    //
    // The loop is taken from as much input as there is: a press sooner than
    // Hold length after the stream starts (or after a transport jump) loops
    // what has been heard, if that is at least kLoopMinSeconds.

    // The loop the next latch would take: its length, or 0 if there is not
    // yet enough input with signal in it.
    [[nodiscard]] std::int64_t loop_length_for_latch_() const noexcept {
        const auto wanted = static_cast<std::int64_t>(std::llround(applied_hold_seconds_ * sample_rate_));
        const std::int64_t length = std::min(wanted, history_ - loop_search_ - loop_match_);
        if (length < loop_min_) return 0;
        // Signal over the loop: its mean power, from the per-hop record.
        const auto ring = hop_energy_.size();
        const auto hops = static_cast<std::size_t>(std::min<std::int64_t>(
            static_cast<std::int64_t>(ring), (length + kHop - 1) / kHop));
        double power = 0.0;
        for (std::size_t back = 1; back <= hops; ++back)
            power += hop_energy_[(hop_energy_pos_ + ring - back) % ring];
        if (!(power / static_cast<double>(hops) >= signal_floor_power_)) return 0;
        return length;
    }

    [[nodiscard]] bool loop_ready_() const noexcept { return loop_length_for_latch_() > 0; }

    [[nodiscard]] float recorded_at_(int ch, std::int64_t absolute) const noexcept {
        const auto at = static_cast<std::size_t>(absolute % static_cast<std::int64_t>(record_length_));
        return record_[static_cast<std::size_t>(ch) * record_length_ + at];
    }

    // THE ENGAGE IS THE LOOP'S OWN SEAM. The loop ends exactly where the
    // engage fade starts -- the hop boundary after the latch -- so the fade
    // from the live input into the loop's start is the same seam every later
    // pass makes from the audio that followed the end: the live input IS
    // that audio. The start is matched against the 10 ms before the end
    // (or a hop, if shorter) as those samples arrive during that hop, so no
    // callback carries the search; the loop is copied out of the recording
    // as its first pass plays (the recording keeps every sample of it for
    // longer than a pass).
    void begin_loop_prepare_() noexcept {
        loop_end_ = recorded_ + kHop;
        const auto wanted = static_cast<std::int64_t>(std::llround(applied_hold_seconds_ * sample_rate_));
        const std::int64_t history = history_ + kHop;
        const std::int64_t length = std::min(wanted, history - loop_search_ - loop_match_);
        // Candidate starts, every loop_stride_-th, each with its match window
        // inside the recorded history.
        const std::int64_t earliest = loop_end_ - history + loop_match_;
        search_first_ = std::max(loop_end_ - length - loop_search_, earliest);
        const std::int64_t last = std::min(loop_end_ - length + loop_search_, loop_end_ - loop_min_);
        search_count_ = std::clamp<std::int64_t>((last - search_first_) / loop_stride_ + 1, 1,
                                                 static_cast<std::int64_t>(search_cross_.size()));
        std::fill(search_cross_.begin(), search_cross_.end(), 0.0);
        std::fill(search_energy_.begin(), search_energy_.end(), 0.0);
        end_energy_ = 0.0;
        loop_mode_ = true;
        hold_.set_frozen(false);
        phase_ = Phase::preparing;
    }

    // Samples [from, to) of the recording have arrived: those inside the
    // match window before the loop's end add to every candidate's score.
    void match_loop_end_(std::int64_t from, std::int64_t to) noexcept {
        const std::int64_t window_start = loop_end_ - loop_match_;
        for (std::int64_t t = std::max(from, window_start); t < std::min(to, loop_end_); ++t) {
            const std::int64_t back = loop_end_ - t; // 1 .. loop_match_
            if (back % loop_stride_ != 0) continue;
            for (int ch = 0; ch < channels_; ++ch) {
                const double now = recorded_at_(ch, t);
                end_energy_ += now * now;
                for (std::int64_t j = 0; j < search_count_; ++j) {
                    const double then = recorded_at_(ch, search_first_ + j * loop_stride_ - back);
                    search_cross_[static_cast<std::size_t>(j)] += then * now;
                    search_energy_[static_cast<std::size_t>(j)] += then * then;
                }
            }
        }
    }

    // The engage: the best-matched start, and the fade into it.
    void finish_loop_prepare_() noexcept {
        double best = -2.0;
        std::int64_t start = search_first_;
        for (std::int64_t j = 0; j < search_count_; ++j) {
            const double score = search_cross_[static_cast<std::size_t>(j)]
                / std::sqrt(std::max(search_energy_[static_cast<std::size_t>(j)] * end_energy_, 1.0e-30));
            if (score > best) { best = score; start = search_first_ + j * loop_stride_; }
        }
        loop_start_ = start;
        loop_length_ = loop_end_ - start;
        seam_length_ = std::min<std::int64_t>(
            crossfade_samples_, static_cast<std::int64_t>(loop_capacity_) - loop_length_);
        loop_position_ = 0;
        loop_seam_ = false;
        loop_tail_ = false;
        loop_rho_ = static_cast<float>(std::clamp(best, 0.0, 1.0));
        fade_rho_ = loop_rho_;
        held_gain_ = 1.0f;
        phase_ = Phase::engaging;
        weight_step_ = 0;
    }

    // What followed the loop's end in the input, once it has been recorded:
    // the seam fades out of it into the start.
    void copy_loop_tail_() noexcept {
        const std::int64_t tail = seam_length_;
        if (loop_tail_ || recorded_ < loop_end_ + tail) return;
        for (int ch = 0; ch < channels_; ++ch) {
            float* loop = loop_.data() + static_cast<std::size_t>(ch) * loop_capacity_;
            for (std::int64_t n = 0; n < tail; ++n)
                loop[loop_length_ + n] = recorded_at_(ch, loop_end_ + n);
        }
        loop_tail_ = true;
    }

    // One sample of the loop at `at`. The first pass reads the recording and
    // keeps a copy; across each later seam (within a crossfade of the
    // start) the audio that followed the end fades into the start.
    float loop_sample_(int ch, float* loop, std::int64_t at, bool seam) noexcept {
        if (!seam) {
            const float v = recorded_at_(ch, loop_start_ + at);
            loop[at] = v;
            return v;
        }
        return seam_value_(loop, loop[at], at);
    }

    // Loop sample `at` as a pass after the first plays it: within the seam,
    // once the audio that followed the end has been kept, that audio fades
    // into the start.
    [[nodiscard]] float seam_value_(const float* loop, float start, std::int64_t at) const noexcept {
        if (!loop_tail_ || at >= seam_length_) return start;
        const float p = (static_cast<float>(at) + 0.5f) / static_cast<float>(seam_length_);
        const float a = std::sin(p * 1.57079632679489662f);
        const float b = std::cos(p * 1.57079632679489662f);
        const float norm = 1.0f / std::sqrt(1.0f + 2.0f * loop_rho_ * a * b);
        return (a * start + b * loop[loop_length_ + at]) * norm;
    }

    bool hold_seconds_changed_() noexcept {
        if (requested_hold_seconds_ == applied_hold_seconds_) return false;
        applied_hold_seconds_ = requested_hold_seconds_;
        return true;
    }

    void clear_energy_history_() noexcept {
        std::fill(hop_energy_.begin(), hop_energy_.end(), 0.0);
        std::fill(hop_peak_.begin(), hop_peak_.end(), 0.0f);
        hop_peak_accum_ = 0.0f;
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
            for (auto* ring : {&ola_, &ola_tonal_}) {
                float* ola = ring->data() + static_cast<std::size_t>(ch) * window;
                std::copy(ola + hop, ola + window, ola);
                std::fill(ola + window - hop, ola + window, 0.0f);
            }
        }
    }

    // Resynthesise the current hold frame and overlap-add it into the output
    // ring, as the frame placed `hops_back` hops before now: only its part
    // from now onward is added.
    void add_hold_frame_(int hops_back) noexcept {
        if (!write_frame_()) return;
        synthesise_parts_(static_cast<std::size_t>(kFftSize), -hops_back * kHop);
    }

    // The frame in `spectra_`, its tonal partials into the tonal ring and
    // the rest into the noise ring, as the frame placed at `at`.
    void synthesise_parts_(std::size_t length, int at) noexcept {
        synthesise_to_(ola_tonal_.data(), length, at, true);
        synthesise_to_(ola_.data(), length, at, false);
    }

    // Resynthesise the spectra already in `spectra_` and overlap-add them
    // into `dest`: the tonal partials' bins, or the rest's.
    void synthesise_to_(float* dest, std::size_t length, int at, bool tonal) noexcept {
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
                // Only this part's bins.
                if ((locked_[k] != 0) != tonal) {
                    time_[k] = {};
                    time_[window - k] = {};
                    continue;
                }
                const float ka_re = a[k].real(), ka_im = a[k].imag();
                const float kb_re = pair ? b[k].real() : 0.0f;
                const float kb_im = pair ? b[k].imag() : 0.0f;
                // time[k] = ka + i kb; time[N - k] = conj(ka) + i conj(kb).
                time_[k] = {ka_re - kb_im, ka_im + kb_re};
                time_[window - k] = {ka_re + kb_im, kb_re - ka_im};
            }
            // DC and Nyquist: a held phase there would not be real, and they
            // carry nothing worth holding.
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
    // Before it is heard, the hold's phases and level are set (take_hold_)
    // and the output ring is filled with the frames that would already be
    // overlapping (a pre-roll, so the full hold is present from the fade's
    // first sample). That is hops_per_window inverse transforms.
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
    // frame is one complex multiply per bin.
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
        // ...and its peak, over every hop those frames analysed.
        const auto hop_ring = hop_peak_.size();
        const int span = frames - 1 + hops_per_window_;
        live_peak_ = 0.0f;
        for (int back = 1; back <= span; ++back)
            live_peak_ = std::max(live_peak_, hop_peak_[(hop_energy_pos_ + hop_ring
                                                        - static_cast<std::size_t>(back)) % hop_ring]);
        prepare_step_ = 0;
        phase_ = Phase::preparing;
    }

    // The steps of the preparation: one to set it up, then hops_per_window
    // pre-roll frames.
    [[nodiscard]] int prepare_steps_() const noexcept {
        return 1 + hops_per_window_;
    }

    // Do the preparation's steps up to `target`.
    void prepare_until_(int target) noexcept {
        target = std::min(target, prepare_steps_());
        const auto window = static_cast<std::size_t>(kFftSize);
        for (; prepare_step_ < target; ++prepare_step_) {
            if (prepare_step_ == 0) {
                take_hold_();
                std::fill(ola_.begin(), ola_.end(), 0.0f);
                std::fill(ola_tonal_.begin(), ola_tonal_.end(), 0.0f);
                // The pre-roll is built for the NEXT hop boundary. A frame
                // placed `back` hops before it continues the latched frame by
                // (hops_per_window + 1 - back) hops; take_hold_() leaves the
                // phases one hop on, so the oldest (back = hops_per_window
                // - 1) is one step further on.
                advance_phases_(1, false);
                prepare_have_hold_ = write_frame_();
                if (prepare_have_hold_) compute_hop_rotor_();
                continue;
            }
            const int back = hops_per_window_ - prepare_step_;
            if (prepare_have_hold_)
                synthesise_parts_(window, -back * kHop);
            // Step on without the random walk inside the pre-roll.
            if (back > 0) {
                advance_phases_(1, false);
                if (prepare_have_hold_) rotate_spectra_one_hop_();
            }
        }
    }

    // The hop boundary the pre-roll was built for: finish anything a short
    // hop left undone and start the fade, the hold at its matched level.
    void finish_prepare_() noexcept {
        prepare_until_(prepare_steps_());
        // Past the newest frame, advance as every later hop does.
        advance_phases_(1, true);
        phase_ = Phase::engaging;
        weight_step_ = 0;
        fade_rho_ = 0.0f;
    }

    // THE HOLD'S LEVEL, PREDICTED. A bin of the hold is one sinusoid: every
    // frame carries it at the bin's centre frequency, and the frame-to-frame
    // phase step theta (the hold frequency's advance over a hop, less the
    // centre's) decides how the overlapping frames add up. So the power the
    // hold will settle at is known from its spectrum alone, without playing
    // it: for a bin of magnitude M, (2M/N)^2 / 2 times the mean over a hop of
    // |sum_t w(m - tH) e^{i t theta}|^2 -- tabulated once per theta -- and
    // bins at independent random phases add in power. A locked lobe is one
    // partial, its bins coherent at one frequency, so they add in amplitude
    // over the hop. Measuring the hold's first hops instead read the level of
    // a moment of a hold whose neighbouring bins still beat against each
    // other (up to a dB off on noise), and cost an inverse transform a hop.
    void build_level_table_() {
        level_table_.assign(static_cast<std::size_t>(kLevelThetaSteps * kLevelTaps), {});
        level_gain2_.assign(static_cast<std::size_t>(kLevelThetaSteps), 0.0);
        for (int i = 0; i < kLevelThetaSteps; ++i) {
            const double theta = level_theta_(i);
            double g2 = 0.0;
            for (int tap = 0; tap < kLevelTaps; ++tap) {
                const double m = (tap + 0.5) * static_cast<double>(kHop) / kLevelTaps;
                std::complex<double> sum{};
                // Frames started j hops ago cover m + jH.
                for (int j = 0; j < hops_per_window_; ++j) {
                    const auto n = static_cast<std::size_t>(m + static_cast<double>(j * kHop));
                    const double w = static_cast<double>(synthesis_window_[n]);
                    sum += w * std::complex<double>(std::cos(-j * theta), std::sin(-j * theta));
                }
                level_table_[static_cast<std::size_t>(i * kLevelTaps + tap)] = sum;
                g2 += std::norm(sum);
            }
            level_gain2_[static_cast<std::size_t>(i)] = g2 / kLevelTaps;
        }
    }

    [[nodiscard]] static double level_theta_(int index) noexcept {
        constexpr double pi = 3.14159265358979323846;
        return -pi + 2.0 * pi * (index + 0.5) / kLevelThetaSteps;
    }

    // The table row nearest the frame-to-frame phase step of a bin centred
    // on `k` played at `freq` radians per sample.
    [[nodiscard]] static int level_index_(double freq, std::size_t k) noexcept {
        constexpr double pi = 3.14159265358979323846;
        constexpr double two_pi = 2.0 * pi;
        double theta = (freq - two_pi * static_cast<double>(k) / kFftSize) * kHop;
        theta -= two_pi * std::round(theta / two_pi);
        const int index = static_cast<int>((theta + pi) / two_pi * kLevelThetaSteps);
        return std::clamp(index, 0, kLevelThetaSteps - 1);
    }

    // The power the hold settles at, mean over channels, per sample: its
    // locked (tonal) and random-phase parts.
    struct HoldPower { double tonal = 0.0; double noise = 0.0; };
    [[nodiscard]] HoldPower predict_hold_power_() const noexcept {
        constexpr double two_pi = 6.28318530717958647692;
        const auto bins = static_cast<std::size_t>(bins_);
        const double scale = 2.0 / kFftSize;
        HoldPower total;
        for (int ch = 0; ch < channels_; ++ch) {
            const auto mags = hold_.held_magnitudes(ch);
            const double* phases = hold_phase_.data() + static_cast<std::size_t>(ch) * bins;
            double power = 0.0, tonal = 0.0;
            // DC and Nyquist carry nothing (see synthesise_to_).
            for (std::size_t k = 1; k + 1 < bins; ++k) {
                if (locked_[k]) continue;
                const double a = scale * static_cast<double>(mags[k]);
                power += 0.5 * a * a
                       * level_gain2_[static_cast<std::size_t>(level_index_(hold_freq_[k], k))];
            }
            // Locked lobes: their bins in order, one lobe at a time.
            std::size_t k = 1;
            while (k + 1 < bins) {
                if (!locked_[k]) { ++k; continue; }
                const auto peak = lock_peak_[k];
                const double freq = hold_freq_[static_cast<std::size_t>(peak)];
                std::complex<double> sum[kLevelTaps] = {};
                for (; k + 1 < bins && locked_[k] && lock_peak_[k] == peak; ++k) {
                    const double a = scale * static_cast<double>(mags[k]);
                    const auto row = static_cast<std::size_t>(level_index_(freq, k)) * kLevelTaps;
                    // Each bin's carrier relative to the partial's own.
                    const double beat = two_pi * static_cast<double>(k) / kFftSize - freq;
                    for (int tap = 0; tap < kLevelTaps; ++tap) {
                        const double m = (tap + 0.5) * static_cast<double>(kHop) / kLevelTaps;
                        const double angle = phases[k] + beat * m;
                        sum[tap] += a * std::complex<double>(std::cos(angle), std::sin(angle))
                                  * level_table_[row + static_cast<std::size_t>(tap)];
                    }
                }
                double lobe = 0.0;
                for (int tap = 0; tap < kLevelTaps; ++tap) lobe += std::norm(sum[tap]);
                tonal += 0.5 * lobe / kLevelTaps;
            }
            total.noise += power / static_cast<double>(channels_);
            total.tonal += tonal / static_cast<double>(channels_);
        }
        return total;
    }

    // Each bin's phase advance over one hop at its hold frequency, as a
    // unit rotor: what advance_phases_(1, false) adds.
    void compute_hop_rotor_() noexcept {
        const auto bins = static_cast<std::size_t>(bins_);
        for (std::size_t k = 0; k < bins; ++k) {
            const double advance = hold_freq_[k] * static_cast<double>(kHop);
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

    // THE HOLD'S OWN PHASES. FreezeHold supplies the averaged magnitudes,
    // the latched frame's phases and each bin's instantaneous frequency; the
    // phases the hold is rendered with are Spectr's, set here once at the
    // latch so the hold is STATIONARY from its first frame.
    //
    // Left to FreezeHold, every bin advances at its own frequency from the
    // latched frame's phases. Where one partial's main lobe spans bins that
    // measured different frequencies (detuned or beating partials, a chord's
    // neighbouring partials) those bins start coherent and drift apart: the
    // hold enters as a coherent copy of the moment and then, over a few
    // hundred milliseconds, sags into a quieter, more diffuse steady state.
    // Heard as the sound being frozen and then frozen again. So:
    //  - a tonal peak (a bin well above its neighbourhood) keeps its main
    //    lobe as one: every bin of the lobe advances at the peak's
    //    frequency, from the latched frame's phase, so the partial never
    //    decoheres and carries the live partial on in phase;
    //  - every other bin starts at a uniformly random phase: the diffuse
    //    state it would drift into, from the first frame. No latched timing
    //    survives in them either, so nothing replays.
    // The random draws are the same on every channel, so the image keeps its
    // phase differences.
    void take_hold_() noexcept {
        const auto bins = static_cast<std::size_t>(bins_);
        const auto freq = hold_.instantaneous_frequency();
        // The latched frame's own phases: FreezeHold's have taken a step of
        // its random walk since.
        const auto window = static_cast<std::size_t>(kFftSize);
        for (int ch = 0; ch < channels_; ++ch)
            for (std::size_t k = 0; k < bins; ++k)
                hold_phase_[static_cast<std::size_t>(ch) * bins + k] =
                    static_cast<double>(std::arg(latched_[static_cast<std::size_t>(ch) * window + k]));
        std::copy(freq.begin(), freq.end(), hold_freq_.begin());
        for (std::size_t k = 0; k < bins; ++k) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels_; ++ch) sum += hold_.held_magnitudes(ch)[k];
            magnitude_[k] = sum;
            locked_[k] = 0;
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
            for (std::size_t j = k > lobe ? k - lobe : 0; j <= std::min(bins - 1, k + lobe); ++j) {
                // A lobe shared with a stronger peak already claimed stays
                // with that peak.
                if (locked_[j] && lock_mag_[j] >= m) continue;
                hold_freq_[j] = hold_freq_[k];
                locked_[j] = 1;
                lock_mag_[j] = m;
                lock_peak_[j] = static_cast<std::int32_t>(k);
            }
        }
        // A locked lobe is given the shape of a steady sinusoid's: a periodic
        // Hann window is symmetric about N/2, so bin j of a steady partial
        // sits at -pi (j - k) from its peak bin k, whatever the partial's
        // frequency. The latched frame's own relative phases also say WHERE
        // in the window the partial's energy was; kept, a partial that was a
        // transient (a kick's body, a pluck) replays as a burst once every
        // window length for as long as the hold plays.
        constexpr double pi = 3.14159265358979323846;
        const double slope = -pi;
        for (std::size_t k = 0; k < bins; ++k) {
            if (!locked_[k] || lock_peak_[k] == static_cast<std::int32_t>(k)) continue;
            const auto peak = static_cast<std::size_t>(lock_peak_[k]);
            // A peak whose own bin a stronger neighbour claimed is no anchor.
            if (lock_peak_[peak] != static_cast<std::int32_t>(peak)) continue;
            const double offset = slope * (static_cast<double>(k) - static_cast<double>(peak));
            for (int ch = 0; ch < channels_; ++ch)
                hold_phase_[static_cast<std::size_t>(ch) * bins + k] =
                    hold_phase_[static_cast<std::size_t>(ch) * bins + peak] + offset;
        }
        constexpr double two_pi = 6.28318530717958647692;
        for (std::size_t k = 0; k < bins; ++k) {
            if (locked_[k]) continue;
            const double offset = two_pi * next_uniform_();
            for (int ch = 0; ch < channels_; ++ch)
                hold_phase_[static_cast<std::size_t>(ch) * bins + k] += offset;
        }
        // Each bin a hop on from the latched frame, as FreezeHold's are.
        for (std::size_t k = 0; k < bins; ++k)
            for (int ch = 0; ch < channels_; ++ch)
                hold_phase_[static_cast<std::size_t>(ch) * bins + k] +=
                    hold_freq_[k] * static_cast<double>(kHop);

        // THE LEVEL, set once. The tonal partials are the input's own,
        // averaged over a window where they held steady: they play at gain 1,
        // so a held tone is never re-levelled. Averaging magnitudes loses
        // level only in the noise-like rest, so the match is taken there:
        // the live power the partials do not account for, over the rest's
        // predicted power.
        const auto predicted = predict_hold_power_();
        hold_power_ = predicted.tonal + predicted.noise;
        const double target = std::max(live_reference_power_ - predicted.tonal,
                                        0.0);
        double gain = std::clamp(
            std::sqrt(target / std::max(predicted.noise, 1.0e-20)),
            static_cast<double>(kMinLevelMatch), static_cast<double>(kMaxLevelMatch));
        // Nor may the hold peak above the input it holds. Its random-phase
        // bins make it close to Gaussian whatever the input's waveform was:
        // on a mastered-loud input, with a small crest factor, a hold matched
        // in power would peak several dB over it (and an engage fade summing
        // the two would clip). There the boost stops where the estimated
        // peak meets the input's: the tonal part keeps the input's crest
        // factor, the random-phase part a Gaussian's, and the two combine in
        // power.
        const double live_crest = static_cast<double>(live_peak_)
                                / std::sqrt(std::max(live_reference_power_, 1.0e-20));
        const double tonal_peak2 = predicted.tonal * live_crest * live_crest;
        const double noise_peak2 = predicted.noise * kNoiseCrest * kNoiseCrest;
        if (gain > 1.0 && noise_peak2 > 0.0) {
            const double room = static_cast<double>(live_peak_) * live_peak_ - tonal_peak2;
            const double fit = room > 0.0 ? std::sqrt(room / noise_peak2) : 1.0;
            gain = std::max(1.0, std::min(gain, fit));
        }
        held_gain_ = static_cast<float>(gain);
        hold_peak_ = std::sqrt(tonal_peak2 + noise_peak2 * gain * gain);
    }

    // The hold frame at the current phases, into `spectra_`.
    bool write_frame_() noexcept {
        if (!hold_.has_hold()) return false;
        const auto bins = static_cast<std::size_t>(bins_);
        const auto window = static_cast<std::size_t>(kFftSize);
        for (int ch = 0; ch < channels_; ++ch) {
            const auto mags = hold_.held_magnitudes(ch);
            const double* phases = hold_phase_.data() + static_cast<std::size_t>(ch) * bins;
            auto* out = spectra_.data() + static_cast<std::size_t>(ch) * window;
            for (std::size_t k = 0; k < bins; ++k) {
                const auto p = static_cast<float>(phases[k]);
                const float m = locked_[k] ? mags[k] : mags[k] * held_gain_;
                out[k] = {m * std::cos(p), m * std::sin(p)};
            }
        }
        return true;
    }

    // Move the hold's phases by `hops` hops at each bin's hold frequency;
    // with `walk`, plus FreezeHold's small bounded random walk per hop, which
    // keeps a long hold from sounding periodic.
    void advance_phases_(int hops, bool walk) noexcept {
        constexpr double two_pi = 6.28318530717958647692;
        const auto bins = static_cast<std::size_t>(bins_);
        const double span = static_cast<double>(hops) * static_cast<double>(kHop);
        for (std::size_t k = 0; k < bins; ++k) {
            double advance = hold_freq_[k] * span;
            // The walk draws for every bin, so the stream does not depend on
            // the hold. A locked lobe does not walk: a steady partial cannot
            // sound periodic, and a walk would take it out of phase with the
            // same partial still playing live.
            if (walk) {
                const double draw = jitter_ * (2.0 * next_walk_() - 1.0);
                if (!locked_[k]) advance += draw;
            }
            for (int ch = 0; ch < channels_; ++ch) {
                double& phase = hold_phase_[static_cast<std::size_t>(ch) * bins + k];
                phase += advance;
                phase -= two_pi * std::round(phase / two_pi);
            }
        }
    }

    // xorshift64*: deterministic from the stream, so a bounce and real-time
    // playback turn the same hold the same way.
    // Uniform in [0, 1): the noise-like bins' starting phases.
    double next_uniform_() noexcept {
        rng_ ^= rng_ >> 12;
        rng_ ^= rng_ << 25;
        rng_ ^= rng_ >> 27;
        return static_cast<double>((rng_ * 0x2545f4914f6cdd1dull) >> 11) * (1.0 / 9007199254740992.0);
    }
    // Uniform in [0, 1) for the random walk: its own stream, so the walk
    // does not depend on how many turns a hold drew.
    double next_walk_() noexcept {
        walk_rng_ ^= walk_rng_ >> 12;
        walk_rng_ ^= walk_rng_ << 25;
        walk_rng_ ^= walk_rng_ >> 27;
        return static_cast<double>((walk_rng_ * 0x2545f4914f6cdd1dull) >> 11) * (1.0 / 9007199254740992.0);
    }
    // SPECTR-RENDER-PATH END

    pulp::signal::FreezeHold hold_{};
    pulp::signal::Fft fft_{};
    std::vector<float> window_;
    std::vector<float> synthesis_window_;         // window_ * synthesis_scale_
    float synthesis_scale_ = 1.0f;

    std::vector<float> input_ring_;               // channels * kFftSize
    std::vector<float> ola_;                      // channels * kFftSize: the noise-like part
    std::vector<float> ola_tonal_;                // channels * kFftSize: the tonal partials
    std::vector<std::complex<float>> latched_;    // channels * kFftSize: the latched frame
    bool play_tonal_ = true, play_noise_ = true;
    float release_rho_ = 0.0f;                    // the release fade's two sides' correlation
    float last_rho_ = 0.0f;                       // ...measured over the last held hop
    double rho_cross_ = 0.0, rho_live_ = 0.0, rho_hold_ = 0.0;
    std::vector<std::complex<float>> spectra_;    // channels * kFftSize
    std::vector<std::complex<float>*> frame_ptrs_;
    std::vector<std::complex<float>> time_;       // kFftSize
    std::vector<float> scratch_;                  // kFftSize

    std::vector<double> hop_energy_;
    std::vector<double> frame_energy_;
    std::size_t hop_energy_pos_ = 0;
    std::size_t frame_energy_pos_ = 0;
    double hop_energy_accum_ = 0.0;
    std::vector<float> hop_peak_;                 // per hop: the input's sample peak
    float hop_peak_accum_ = 0.0f;
    float live_peak_ = 0.0f;                      // over the analysed span, at the latch
    int frames_since_clear_ = 0;
    int hops_per_window_ = 16;

    static constexpr std::uint64_t kRngSeed = 0x2545f4914f6cdd1dull;
    std::vector<std::complex<float>> hop_rotor_;  // bins: one hop's phase advance
    std::vector<float> magnitude_;                // bins, scratch
    std::vector<double> prefix_;                  // bins + 1, scratch
    std::uint64_t rng_ = kRngSeed;
    static constexpr std::uint64_t kWalkSeed = 0x9e3779b97f4a7c15ull;
    std::uint64_t walk_rng_ = kWalkSeed;
    std::vector<double> hold_phase_;              // channels * bins: the hold's phases
    std::vector<double> hold_freq_;               // bins: radians per sample
    std::vector<std::uint8_t> locked_;            // bins: in a tonal peak's lobe
    std::vector<float> lock_mag_;                 // bins: that peak's magnitude
    double jitter_ = 0.0;
    std::vector<std::complex<double>> level_table_; // kLevelThetaSteps * kLevelTaps
    std::vector<double> level_gain2_;             // kLevelThetaSteps
    std::vector<std::int32_t> lock_peak_;         // bins: the peak a locked bin follows
    double hold_power_ = 0.0;                     // predicted, mean over channels
    double hold_peak_ = 0.0;                      // estimated
    double signal_floor_power_ = kSignalFloorPower;
    double live_reference_power_ = 0.0;
    float held_gain_ = 1.0f;
    int crossfade_samples_ = 1;

    double sample_rate_ = 0.0;
    int channels_ = 0;
    int bins_ = 0;
    std::size_t write_pos_ = 0;
    int hop_pos_ = 0;
    bool prepared_ = false;
    bool requested_ = false;
    bool pending_engage_ = false;

    // The loop (see THE LOOP).
    std::vector<float> record_;                   // channels * record_length_: every input sample
    std::size_t record_length_ = 1;
    std::int64_t recorded_ = 0;                   // samples recorded since reset
    std::int64_t history_ = 0;                    // ...of them since the last clear
    std::vector<float> loop_;                     // channels * loop_capacity_: the loop, then its tail
    std::size_t loop_capacity_ = 0;
    std::int64_t loop_search_ = 0, loop_match_ = 1, loop_min_ = 0, loop_stride_ = 1;
    std::int64_t seam_length_ = 1;
    std::int64_t loop_end_ = 0, loop_length_ = 0, loop_position_ = 0;
    std::int64_t search_first_ = 0, search_count_ = 1, loop_start_ = 0;
    std::vector<double> search_cross_, search_energy_; // per candidate start
    double end_energy_ = 0.0;
    bool loop_mode_ = false;
    bool loop_seam_ = false;                      // a pass after the first
    bool loop_tail_ = false;                      // the audio after the end is copied
    float fade_rho_ = 0.0f;                       // the engage fade's two sides' correlation
    float loop_rho_ = 0.0f;                       // the seam's
    Phase phase_ = Phase::live;
    int weight_step_ = 0;
    int prepare_step_ = 0;
    bool prepare_have_hold_ = false;
    double requested_hold_seconds_ = kDefaultHoldSeconds;
    double applied_hold_seconds_ = kDefaultHoldSeconds;
};

} // namespace spectr
