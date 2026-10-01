#pragma once

/// @file freeze_keys.hpp
/// Freeze Keys: while Freeze holds a sound, MIDI notes play that sound
/// chromatically up and down the keyboard. Unfrozen, MIDI does nothing.
///
/// FreezeKeys is a wet-source stage that wraps the processor's FreezeSource.
/// It runs the source first, so the source's capture, latch, engage and
/// release are exactly what they are without it, and then -- only while a key
/// has been played into the current hold -- replaces the held sound with the
/// sum of the voices. Like the hold, the voices sit AHEAD of the mask: the
/// mask, its LFOs and Mix act on them exactly as on the held sound, and the
/// dry leg stays live below 100% Mix.
///
/// THE ROOT. The root note (default MIDI 60, C3 in Logic's naming) plays the
/// frozen sound at its own pitch; every other key transposes by its distance
/// from the root, in equal-tempered semitones. Notes more than kMaxTranspose
/// semitones from the root are ignored.
///
/// KEYS MODE. Freeze alone behaves exactly as before: the hold plays. The
/// first key played into a hold switches the frozen bus to keys mode -- the
/// hold fades out under the first voice's attack and from then on only held
/// notes sound (no key held: the frozen bus is silent) until Freeze is
/// released. Releasing Freeze releases every voice and ignores MIDI; the live
/// input comes back on the source's own release fade. A key pressed while a
/// freeze is still arming (waiting for signal, or building its pre-roll)
/// starts sounding as soon as the hold does.
///
/// TWO KINDS OF VOICE, as there are two kinds of hold:
///  - SPECTRAL (a Hold length below FreezeSource::kLoopMinSeconds). The hold
///    is a spectrum, so a voice resynthesises that spectrum with every
///    frequency scaled. Its tonal peaks (the same prominence test the source
///    locks lobes with) become partials: each is rebuilt at the scaled
///    frequency with the exact Hann main lobe of a sinusoid at that
///    fractional bin, so the pitch is exact rather than snapped to the bin
///    grid. Every other bin is a noise bin: its magnitude is read off the
///    held spectrum at bin j / ratio (stretched spectral envelope, power
///    kept as varispeed keeps it) at a random phase that runs at the scaled
///    frequency with a small random walk, so it does not repeat. Each voice
///    is its own overlap-add stream (kVoiceFftSize points every kVoiceHop
///    samples, 4x overlap), so it starts and stops with a sample-accurate
///    envelope. Its frame plan and pre-roll (the frames already overlapping
///    its first sample) are built over kVoicePrepareSamples after the
///    note-on, a share per sample, so a chord's note-on never lands in one
///    host callback; that is the spectral voice's note latency (10.7 ms at
///    48 kHz). A loop voice sounds at its note-on.
///  - LOOP (Hold length from kLoopMinSeconds). The hold is audio, so a voice
///    is a varispeed (sampler) read of the loop -- 4-point Hermite
///    interpolation, the seam crossfaded as the source plays it -- starting
///    where the loop is playing at the note-on, so the root note continues
///    the frozen loop exactly. Pitch and loop duration change together, as
///    on a sampler. A time-preserving (formant/duration-keeping) shifter is
///    the alternative; see docs/freeze-keys.md.
///
/// ENVELOPES. Attack kAttackSeconds, release kReleaseSeconds, both click-free
/// ramps; velocity sets the level, (velocity / 127)^2, so velocity 127 plays
/// at the hold's own level. kMaxVoices notes sound at once; the oldest held
/// note is released to make room for a new one.
///
/// Real-time: prepare() allocates; every other member is allocation-free,
/// lock-free and reads no clock. Note events are queued with a sample offset
/// inside the host block (begin_block(), then note_on / note_off) and applied
/// at that sample.

#include "spectr/freeze_source.hpp"

#include <pulp/signal/fft.hpp>
#include <pulp/signal/spectral_mask_processor.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spectr {

class FreezeKeys final : public pulp::signal::SpectralWetSourceStageT<float> {
public:
    static constexpr int kDefaultRootNote = 60;
    static constexpr int kMaxTranspose = 36;
    static constexpr int kMaxVoices = 8;          ///< notes sounding at once
    static constexpr int kVoiceSlots = 12;        ///< + room for release tails
    static constexpr double kAttackSeconds = 0.010;
    static constexpr double kReleaseSeconds = 0.080;

    /// Spectral voices: the hold's own transform size, so the voice reads the
    /// held spectrum bin for bin, at a quarter-window hop.
    static constexpr int kVoiceFftSize = FreezeSource::kFftSize;
    static constexpr int kVoiceHop = kVoiceFftSize / 4;
    static constexpr int kMaxPartials = 1024;
    /// Bins either side of a partial's fractional centre it is drawn into:
    /// the Hann main lobe (+-2) and the first side lobe.
    static constexpr int kLobeReach = 3;

    /// A spectral voice is prepared over this many samples after its
    /// note-on (its frame plan and pre-roll, a share per sample, so a chord
    /// never lands in one callback) and sounds after them: its note latency.
    static constexpr int kVoicePrepareSamples = 512;
    static constexpr int kPrepareSteps = 1 + kVoiceFftSize / kVoiceHop;
    /// Successive voices start this far apart in their hop (mod kVoiceHop):
    /// eight in a row land on eight distinct eighths of it.
    static constexpr std::uint64_t kHopStagger = 640;

    static constexpr int kMaxEvents = 512;
    /// Longest stretch rendered at once while keys sound: the live leg's fade
    /// gain is interpolated across it.
    static constexpr int kMaxChunk = 64;

    explicit FreezeKeys(FreezeSource& source) noexcept : source_(source) {}

    /// Allocate for a sample rate and channel count. Control thread, with the
    /// audio thread outside process_block(). The source must be prepared for
    /// the same geometry. Returns false (and leaves keys off: a pure
    /// pass-through of the source) for an unsupported geometry.
    bool prepare(double sample_rate, int channels) {
        prepared_ = false;
        if (!(sample_rate > 0.0) || channels < 1 || channels > FreezeSource::kMaxChannels)
            return false;
        sample_rate_ = sample_rate;
        channels_ = channels;
        bins_ = kVoiceFftSize / 2 + 1;
        fft_ = pulp::signal::Fft(kVoiceFftSize);
        if (!fft_.ready()) return false;

        const auto n = static_cast<std::size_t>(kVoiceFftSize);
        const auto bins = static_cast<std::size_t>(bins_);
        const auto ch = static_cast<std::size_t>(channels);
        constexpr double two_pi = 6.28318530717958647692;
        synthesis_window_.assign(n, 0.0f);
        double overlap = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(two_pi * static_cast<double>(i) / kVoiceFftSize);
            synthesis_window_[i] = static_cast<float>(w);
        }
        for (std::size_t i = 0; i < n; i += kVoiceHop)
            overlap += static_cast<double>(synthesis_window_[i]) * synthesis_window_[i];
        for (auto& w : synthesis_window_) w = static_cast<float>(w / overlap);
        build_kernel_table_();
        build_unit_table_();

        time_.assign(n, {});
        frame_.assign(ch * bins, {});
        // The hold, analysed once per hold for every voice played into it.
        locked_.assign(bins, 0);
        unit_.assign(ch * bins, {1.0f, 0.0f});
        omega_.assign(bins, 0.0f);
        mags_.assign(ch * bins, 0.0f);
        sums_.assign(bins, 0.0f);
        prefix_.assign(bins + 1, 0.0);
        partials_.assign(static_cast<std::size_t>(kMaxPartials), {});
        partial_amp_.assign(static_cast<std::size_t>(kMaxPartials) * ch, {});

        const std::size_t entries = static_cast<std::size_t>(kMaxPartials) * (2 * kLobeReach + 1);
        for (auto& v : voices_) {
            v.ola.assign(ch * n, 0.0f);
            v.noise.assign(ch * bins, {});
            v.rotor.assign(bins, {1.0f, 0.0f});
            v.step.assign(bins, {1.0f, 0.0f});
            v.entry_bin.assign(entries, 0);
            v.entry_partial.assign(entries, 0);
            v.entry_coeff.assign(entries * ch, {});
            v.partial_rotor.assign(static_cast<std::size_t>(kMaxPartials), {1.0, 0.0});
            v.partial_step.assign(static_cast<std::size_t>(kMaxPartials), {1.0, 0.0});
            v.active = false;
        }
        input_copy_.assign(ch * kMaxChunk, 0.0f);
        voice_sum_.assign(ch * kMaxChunk, 0.0f);
        attack_samples_ = std::max(1, static_cast<int>(std::lround(kAttackSeconds * sample_rate)));
        release_samples_ = std::max(1, static_cast<int>(std::lround(kReleaseSeconds * sample_rate)));
        prepared_ = true;
        reset();
        return true;
    }

    [[nodiscard]] bool prepared() const noexcept { return prepared_; }

    /// Silence every voice and forget queued events and held keys.
    void reset() noexcept {
        for (auto& v : voices_) v.active = false;
        event_count_ = 0;
        event_read_ = 0;
        keys_mode_ = false;
        mode_step_ = 0;
        mode_delay_ = 0;
        analysed_ = false;
        pending_ = false;
        key_velocity_.fill(0);
        rng_ = kRngSeed;
    }

    /// Off: MIDI is ignored and the stage is a pass-through of the source.
    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    void set_root_note(int note) noexcept { root_note_ = std::clamp(note, 0, 127); }
    [[nodiscard]] int root_note() const noexcept { return root_note_; }

    /// Start of a host block: events queued from here are stamped relative
    /// to its first sample. Events a previous block never reached (its audio
    /// never passed through this stage) are applied now.
    void begin_block() noexcept {
        while (event_read_ < event_count_) apply_(events_[event_read_++]);
        event_count_ = 0;
        event_read_ = 0;
        block_origin_ = clock_;
    }

    /// Queue a note at `offset` samples into the current host block.
    /// Velocity 0 is a note-off. False when the queue is full or keys are off.
    bool note_on(int offset, int note, int velocity) noexcept {
        if (velocity <= 0) return note_off(offset, note);
        return push_(offset, Event::Kind::on, note, velocity);
    }
    bool note_off(int offset, int note) noexcept {
        return push_(offset, Event::Kind::off, note, 0);
    }
    bool all_notes_off(int offset) noexcept {
        return push_(offset, Event::Kind::all_off, 0, 0);
    }

    /// True once a key has been played into the current hold.
    [[nodiscard]] bool keys_mode() const noexcept { return keys_mode_; }
    /// Voices still making sound (held or releasing).
    [[nodiscard]] int sounding_voices() const noexcept {
        int count = 0;
        for (const auto& v : voices_) count += v.active ? 1 : 0;
        return count;
    }

    // SPECTR-RENDER-PATH BEGIN
    void process_block(const float* const* input, float* const* wet,
                       int channels, int num_samples) noexcept override {
        if (!prepared_ || channels != channels_ || !enabled_) {
            source_.process_block(input, wet, channels, num_samples);
            clock_ += num_samples;
            return;
        }
        int done = 0;
        while (done < num_samples) {
            // Events due by now.
            while (event_read_ < event_count_
                   && events_[event_read_].time <= clock_)
                apply_(events_[event_read_++]);
            follow_source_();
            int span = num_samples - done;
            if (event_read_ < event_count_)
                span = static_cast<int>(std::min<std::int64_t>(
                    span, std::max<std::int64_t>(1, events_[event_read_].time - clock_)));
            if (!busy_()) {
                // Nothing of the keys is sounding: the source alone, untouched.
                offset_call_(input, wet, done, span);
                clock_ += span;
                done += span;
                continue;
            }
            span = std::min(span, kMaxChunk);
            render_keys_(input, wet, done, span);
            clock_ += span;
            done += span;
        }
    }

private:
    struct Event {
        enum class Kind : std::uint8_t { on, off, all_off };
        std::int64_t time = 0;
        Kind kind = Kind::on;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
    };

    struct Voice {
        bool active = false;
        bool held = false;
        bool loop = false;
        int note = 0;
        std::uint64_t age = 0;
        float level = 1.0f;     // velocity
        double ratio = 1.0;
        // Envelope: 0..1; it rises by attack_inc while held, and falls from
        // release_from to 0 over the release.
        float env = 0.0f;
        float attack_inc = 0.0f;
        float release_dec = 0.0f;
        // Loop voice.
        double position = 0.0;
        // Spectral voice, before it sounds: samples to go, and how much of
        // its preparation (frame plan, then pre-roll frames) is done.
        int delay = 0;
        int prepare_step = 0;
        int prepare_elapsed = 0;
        // Spectral voice: its overlap-add stream and what it draws per frame.
        int hop_pos = 0;
        std::vector<float> ola;                          // channels * N
        std::vector<std::complex<float>> noise;          // channels * bins
        std::vector<std::complex<float>> rotor;          // bins
        std::vector<std::complex<float>> step;           // bins
        int entries = 0;
        std::vector<std::int32_t> entry_bin;
        std::vector<std::int32_t> entry_partial;
        std::vector<std::complex<float>> entry_coeff;    // entries * channels
        int partial_count = 0;
        std::vector<std::complex<double>> partial_rotor;
        std::vector<std::complex<double>> partial_step;
    };

    struct Partial {
        double bin = 0.0;   // fractional centre, in bins
    };

    bool push_(int offset, Event::Kind kind, int note, int velocity) noexcept {
        if (!enabled_ || event_count_ >= kMaxEvents) return false;
        Event e;
        e.time = block_origin_ + std::max(0, offset);
        e.kind = kind;
        e.note = static_cast<std::uint8_t>(std::clamp(note, 0, 127));
        e.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 0, 127));
        // Keep the queue in time order (hosts send sorted events; a stray
        // one is inserted where it belongs).
        int at = event_count_;
        while (at > event_read_ && events_[at - 1].time > e.time) {
            events_[at] = events_[at - 1];
            --at;
        }
        events_[at] = e;
        ++event_count_;
        return true;
    }

    void offset_call_(const float* const* input, float* const* wet, int offset, int count) noexcept {
        const float* in[FreezeSource::kMaxChannels];
        float* out[FreezeSource::kMaxChannels];
        for (int ch = 0; ch < channels_; ++ch) {
            in[ch] = input[ch] + offset;
            out[ch] = wet[ch] + offset;
        }
        source_.process_block(in, out, channels_, count);
    }

    [[nodiscard]] bool hold_playing_() const noexcept {
        const auto phase = source_.phase();
        return source_.frozen_requested()
            && (phase == FreezeSource::Phase::engaging || phase == FreezeSource::Phase::held);
    }

    [[nodiscard]] bool busy_() const noexcept {
        if (keys_mode_) return true;
        for (const auto& v : voices_) if (v.active) return true;
        return false;
    }

    // Keys follow the freeze: a released freeze releases every voice and
    // ends keys mode once the live input is back; keys pressed before the
    // hold was audible start when it is.
    void follow_source_() noexcept {
        if (!hold_playing_()) {
            analysed_ = false;
            if (!source_.frozen_requested()) {
                key_velocity_.fill(0);
                pending_ = false;
            }
            for (auto& v : voices_) if (v.active && v.held) release_(v);
            if (!source_.hold_audible()) { keys_mode_ = false; mode_step_ = 0; }
            return;
        }
        if (pending_) {
            pending_ = false;
            for (int note = 0; note < 128; ++note)
                if (key_velocity_[static_cast<std::size_t>(note)] > 0 && !note_sounding_(note))
                    start_voice_(note, key_velocity_[static_cast<std::size_t>(note)]);
        }
    }

    [[nodiscard]] bool note_sounding_(int note) const noexcept {
        for (const auto& v : voices_) if (v.active && v.held && v.note == note) return true;
        return false;
    }

    void apply_(const Event& e) noexcept {
        if (e.kind == Event::Kind::all_off) {
            key_velocity_.fill(0);
            for (auto& v : voices_) if (v.active && v.held) release_(v);
            return;
        }
        const int note = e.note;
        if (e.kind == Event::Kind::off) {
            key_velocity_[static_cast<std::size_t>(note)] = 0;
            for (auto& v : voices_) if (v.active && v.held && v.note == note) release_(v);
            return;
        }
        // Unfrozen, MIDI does nothing.
        if (!source_.frozen_requested()) return;
        if (std::abs(note - root_note_) > kMaxTranspose) return;
        key_velocity_[static_cast<std::size_t>(note)] = e.velocity;
        if (!hold_playing_()) { pending_ = true; return; }
        // A key repeated while it still sounds restarts as a new voice.
        for (auto& v : voices_) if (v.active && v.held && v.note == note) release_(v);
        start_voice_(note, e.velocity);
    }

    void release_(Voice& v) noexcept {
        v.held = false;
        v.release_dec = 1.0f / static_cast<float>(release_samples_);
    }

    void start_voice_(int note, int velocity) noexcept {
        // At most kMaxVoices held: the oldest held note makes room.
        int held = 0;
        Voice* oldest_held = nullptr;
        for (auto& v : voices_) {
            if (!(v.active && v.held)) continue;
            ++held;
            if (!oldest_held || v.age < oldest_held->age) oldest_held = &v;
        }
        if (held >= kMaxVoices && oldest_held) release_(*oldest_held);
        Voice* slot = nullptr;
        for (auto& v : voices_) if (!v.active) { slot = &v; break; }
        if (!slot) {
            // Every slot sounding: take the quietest release tail.
            for (auto& v : voices_)
                if (!v.held && (!slot || v.env < slot->env)) slot = &v;
            if (!slot) return;
        }
        Voice& v = *slot;
        v.note = note;
        v.age = ++age_;
        v.ratio = std::exp2(static_cast<double>(note - root_note_) / 12.0);
        const float vel = static_cast<float>(velocity) / 127.0f;
        v.level = vel * vel;
        v.loop = source_.looping();
        v.env = 0.0f;
        v.attack_inc = 1.0f / static_cast<float>(attack_samples_);
        v.release_dec = 0.0f;
        v.active = false;
        if (v.loop) {
            if (source_.loop_length() < 4) return;
            v.position = static_cast<double>(source_.loop_position());
        } else {
            if (!analysed_) analyse_hold_();
            if (!analysed_) return;
            // Its frame plan and pre-roll are built over the next
            // kVoicePrepareSamples, a share per sample, and it sounds after.
            v.prepare_step = 0;
            v.prepare_elapsed = 0;
            v.delay = kVoicePrepareSamples;
        }
        v.held = true;
        v.active = true;
        if (!keys_mode_) {
            keys_mode_ = true;
            mode_step_ = 0;
            mode_delay_ = v.loop ? 0 : v.delay;
            // The root note of a loop continues the very audio it replaces.
            mode_loop_ = v.loop && note == root_note_;
        }
    }

    // The live leg's share of the source's output right now: its fade law.
    [[nodiscard]] float live_gain_() const noexcept {
        switch (source_.phase()) {
        case FreezeSource::Phase::held: return 0.0f;
        case FreezeSource::Phase::engaging:
        case FreezeSource::Phase::releasing:
            return std::cos(source_.hold_weight() * 1.57079632679489662f);
        default: return 1.0f;
        }
    }

    void render_keys_(const float* const* input, float* const* wet, int offset, int count) noexcept {
        const auto stride = static_cast<std::size_t>(kMaxChunk);
        // The source may write `wet` over `input`: keep the live input.
        for (int ch = 0; ch < channels_; ++ch)
            std::copy(input[ch] + offset, input[ch] + offset + count,
                      input_copy_.data() + static_cast<std::size_t>(ch) * stride);
        const float g0 = live_gain_();
        offset_call_(input, wet, offset, count);
        const float g1 = live_gain_();

        std::fill(voice_sum_.begin(), voice_sum_.end(), 0.0f);
        for (auto& v : voices_) if (v.active) render_voice_(v, count);

        for (int i = 0; i < count; ++i) {
            // The hold's share: it fades out under the first voice's attack
            // -- linearly when that voice continues the very loop it
            // replaces (they are correlated), on an equal-power law otherwise.
            float hold_share = 1.0f;
            if (keys_mode_ && mode_delay_ > 0) {
                --mode_delay_;
            } else if (keys_mode_) {
                if (mode_step_ < attack_samples_) ++mode_step_;
                const float m = static_cast<float>(mode_step_) / static_cast<float>(attack_samples_);
                hold_share = mode_loop_ ? 1.0f - m : std::cos(m * 1.57079632679489662f);
            }
            const float g = g0 + (g1 - g0) * static_cast<float>(i + 1) / static_cast<float>(count);
            for (int ch = 0; ch < channels_; ++ch) {
                const auto base = static_cast<std::size_t>(ch) * stride;
                const float in = input_copy_[base + static_cast<std::size_t>(i)];
                float& out = wet[ch][offset + i];
                out = hold_share * out + (1.0f - hold_share) * g * in
                    + voice_sum_[base + static_cast<std::size_t>(i)];
            }
        }
    }

    // Advance a voice's envelope one sample; false once it has died.
    bool envelope_(Voice& v, float& gain, bool equal_power) noexcept {
        if (v.held) {
            v.env = std::min(1.0f, v.env + v.attack_inc);
        } else {
            v.env -= v.release_dec;
            if (v.env <= 0.0f) { v.env = 0.0f; return false; }
        }
        gain = equal_power ? std::sin(v.env * 1.57079632679489662f) : v.env;
        gain *= v.level;
        return true;
    }

    void render_voice_(Voice& v, int count) noexcept {
        const auto stride = static_cast<std::size_t>(kMaxChunk);
        if (v.loop) {
            const std::int64_t length = source_.loop_length();
            if (length < 4) { v.active = false; return; }
            const double len = static_cast<double>(length);
            for (int i = 0; i < count; ++i) {
                float gain = 0.0f;
                if (!envelope_(v, gain, false)) { v.active = false; return; }
                const auto base = static_cast<std::int64_t>(v.position);
                const float t = static_cast<float>(v.position - static_cast<double>(base));
                const std::int64_t i0 = base == 0 ? length - 1 : base - 1;
                const std::int64_t i2 = base + 1 >= length ? base + 1 - length : base + 1;
                const std::int64_t i3 = i2 + 1 >= length ? i2 + 1 - length : i2 + 1;
                for (int ch = 0; ch < channels_; ++ch) {
                    const float y0 = source_.loop_sample(ch, i0);
                    const float y1 = source_.loop_sample(ch, base);
                    const float y2 = source_.loop_sample(ch, i2);
                    const float y3 = source_.loop_sample(ch, i3);
                    // 4-point, 3rd-order Hermite.
                    const float c1 = 0.5f * (y2 - y0);
                    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                    const float y = ((c3 * t + c2) * t + c1) * t + y1;
                    voice_sum_[static_cast<std::size_t>(ch) * stride + static_cast<std::size_t>(i)] += gain * y;
                }
                v.position += v.ratio;
                if (v.position >= len) v.position -= len;
            }
            return;
        }
        const auto n = static_cast<std::size_t>(kVoiceFftSize);
        int i = 0;
        if (v.delay > 0) {
            // Silent while it is prepared, a share of the work per sample.
            if (!v.held) { v.active = false; return; }
            const int use = std::min(count, v.delay);
            v.prepare_elapsed += use;
            v.delay -= use;
            prepare_until_(v, v.delay == 0 ? kPrepareSteps
                : (kPrepareSteps * v.prepare_elapsed + kVoicePrepareSamples - 1) / kVoicePrepareSamples);
            if (v.delay > 0) return;
            i = use;
        }
        for (; i < count; ++i) {
            float gain = 0.0f;
            if (!envelope_(v, gain, true)) { v.active = false; return; }
            for (int ch = 0; ch < channels_; ++ch)
                voice_sum_[static_cast<std::size_t>(ch) * stride + static_cast<std::size_t>(i)] +=
                    gain * v.ola[static_cast<std::size_t>(ch) * n + static_cast<std::size_t>(v.hop_pos)];
            if (++v.hop_pos == kVoiceHop) {
                v.hop_pos = 0;
                for (int ch = 0; ch < channels_; ++ch) {
                    float* ola = v.ola.data() + static_cast<std::size_t>(ch) * n;
                    std::copy(ola + kVoiceHop, ola + n, ola);
                    std::fill(ola + n - kVoiceHop, ola + n, 0.0f);
                }
                synthesise_frame_(v, 0);
            }
        }
    }

    // ── The spectral voice ──────────────────────────────────────────────

    // The hold, once per hold: its magnitudes, each bin's channel phase
    // relationships and frequency, and its tonal peaks as partials with a
    // fractional centre and a per-channel amplitude and phase.
    void analyse_hold_() noexcept {
        const auto& hold = source_.hold();
        if (!hold.has_hold()) return;
        const auto bins = static_cast<std::size_t>(bins_);
        const auto freq = hold.instantaneous_frequency();
        if (freq.size() < bins) return;
        constexpr double two_pi = 6.28318530717958647692;
        for (int ch = 0; ch < channels_; ++ch) {
            const auto mags = hold.held_magnitudes(ch);
            const auto phases = hold.held_phases(ch);
            const auto ref = hold.held_phases(0);
            if (mags.size() < bins || phases.size() < bins) return;
            for (std::size_t k = 0; k < bins; ++k) {
                mags_[static_cast<std::size_t>(ch) * bins + k] = mags[k];
                const double d = phases[k] - ref[k];
                unit_[static_cast<std::size_t>(ch) * bins + k] = {
                    static_cast<float>(std::cos(d)), static_cast<float>(std::sin(d))};
            }
        }
        for (std::size_t k = 0; k < bins; ++k) {
            float s = 0.0f;
            for (int ch = 0; ch < channels_; ++ch) s += mags_[static_cast<std::size_t>(ch) * bins + k];
            sums_[k] = s;
            locked_[k] = 0;
            omega_[k] = static_cast<float>(freq[k]);
        }
        // Tonal peaks, by the source's own test.
        prefix_[0] = 0.0;
        for (std::size_t k = 0; k < bins; ++k) prefix_[k + 1] = prefix_[k] + sums_[k];
        const auto reach = static_cast<std::size_t>(FreezeSource::kPeakNeighbourhood);
        const auto lobe = static_cast<std::size_t>(FreezeSource::kPeakLobe);
        partial_count_ = 0;
        for (std::size_t k = 2; k + 2 < bins && partial_count_ < kMaxPartials; ++k) {
            const float m = sums_[k];
            if (!(m > sums_[k - 1] && m >= sums_[k + 1])) continue;
            const std::size_t lo = k > reach ? k - reach : 0;
            const std::size_t hi = std::min(bins, k + reach + 1);
            const double mean = (prefix_[hi] - prefix_[lo]) / static_cast<double>(hi - lo);
            if (!(m > FreezeSource::kPeakProminence * mean)) continue;
            // The centre: the measured instantaneous frequency when it lies
            // within the peak's bin, else a parabola through the log peak.
            double centre = static_cast<double>(freq[k]) * kVoiceFftSize / two_pi;
            if (!(std::abs(centre - static_cast<double>(k)) <= 0.75)) {
                const double a = std::log(std::max(sums_[k - 1], 1e-20f));
                const double b = std::log(std::max(sums_[k], 1e-20f));
                const double c = std::log(std::max(sums_[k + 1], 1e-20f));
                const double den = a - 2.0 * b + c;
                centre = static_cast<double>(k) + (den < 0.0 ? 0.5 * (a - c) / den : 0.0);
            }
            const auto kernel = std::abs(kernel_(static_cast<double>(k) - centre));
            if (!(kernel > 1e-6)) continue;
            auto& p = partials_[static_cast<std::size_t>(partial_count_)];
            p.bin = centre;
            for (int ch = 0; ch < channels_; ++ch) {
                // Amplitude a: |X[k]| = (a / 2) |K(k - centre)|.
                const float a = 2.0f * mags_[static_cast<std::size_t>(ch) * bins + k] / static_cast<float>(kernel);
                partial_amp_[static_cast<std::size_t>(partial_count_) * static_cast<std::size_t>(channels_)
                             + static_cast<std::size_t>(ch)] =
                    a * unit_[static_cast<std::size_t>(ch) * bins + k];
            }
            for (std::size_t j = k - lobe; j <= k + lobe; ++j) locked_[j] = 1;
            ++partial_count_;
        }
        analysed_ = true;
    }

    // A voice's frame plan at its ratio: noise bins read off the stretched
    // envelope, partials drawn as scaled sinusoids. As in the hold, the
    // partials play at the input's level and only the noise-like rest
    // carries the hold's level match.
    void layout_voice_(Voice& v) noexcept {
        const auto bins = static_cast<std::size_t>(bins_);
        const double r = v.ratio;
        const float noise_scale = static_cast<float>(1.0 / std::sqrt(r)) * source_.level_match();
        constexpr double two_pi = 6.28318530717958647692;
        const double hop = static_cast<double>(kVoiceHop);
        for (std::size_t j = 0; j < bins; ++j) {
            const double s = static_cast<double>(j) / r;
            const auto k0 = static_cast<std::size_t>(s);
            bool silent = j == 0 || j + 1 >= bins || k0 + 1 >= bins - 1
                       || locked_[k0] || locked_[k0 + 1];
            const float f = static_cast<float>(s - static_cast<double>(k0));
            const std::size_t near = f < 0.5f ? k0 : k0 + 1;
            double omega = silent ? 0.0 : static_cast<double>(omega_[near]) * r;
            if (!(omega > 0.0 && omega < 3.14159265358979323846)) silent = true;
            for (int ch = 0; ch < channels_; ++ch) {
                auto& out = v.noise[static_cast<std::size_t>(ch) * bins + j];
                if (silent) { out = {}; continue; }
                const float* m = mags_.data() + static_cast<std::size_t>(ch) * bins;
                const float mag = (m[k0] + f * (m[k0 + 1] - m[k0])) * noise_scale;
                out = mag * unit_[static_cast<std::size_t>(ch) * bins + near];
            }
            // A noise bin's step, from the unit table: a noise bin's exact
            // frequency is not heard, and a table read is cheaper than a sincos.
            const double turns = silent ? 0.0 : omega * hop / two_pi;
            const auto index = static_cast<std::int64_t>(std::llround(
                (turns - std::floor(turns)) * static_cast<double>(unit_table_.size())));
            v.step[j] = unit_table_[static_cast<std::size_t>(index) & (unit_table_.size() - 1)];
            v.rotor[j] = random_unit_();
        }
        int entries = 0;
        int count = 0;
        const double top = static_cast<double>(bins) - 2.0 - kLobeReach;
        for (int p = 0; p < partial_count_; ++p) {
            const double centre = partials_[static_cast<std::size_t>(p)].bin * r;
            if (centre < kLobeReach + 1.0 || centre > top) continue;
            const double omega = two_pi * centre / kVoiceFftSize;
            v.partial_step[static_cast<std::size_t>(count)] = {std::cos(omega * hop), std::sin(omega * hop)};
            const auto u = random_unit_();
            v.partial_rotor[static_cast<std::size_t>(count)] = {u.real(), u.imag()};
            const auto first = static_cast<int>(std::ceil(centre - kLobeReach));
            const auto last = static_cast<int>(std::floor(centre + kLobeReach));
            for (int j = first; j <= last; ++j) {
                const auto k = kernel_(static_cast<double>(j) - centre);
                const auto e = static_cast<std::size_t>(entries);
                v.entry_bin[e] = j;
                v.entry_partial[e] = count;
                for (int ch = 0; ch < channels_; ++ch)
                    v.entry_coeff[e * static_cast<std::size_t>(channels_) + static_cast<std::size_t>(ch)] =
                        0.5f * partial_amp_[static_cast<std::size_t>(p) * static_cast<std::size_t>(channels_)
                                            + static_cast<std::size_t>(ch)]
                        * std::complex<float>(static_cast<float>(k.real()), static_cast<float>(k.imag()));
                ++entries;
            }
            ++count;
        }
        v.entries = entries;
        v.partial_count = count;
    }

    // A spectral voice's preparation, up to step `target`: step 0 plans its
    // frames, then one step per pre-roll frame -- the kVoiceFftSize /
    // kVoiceHop frames already overlapping its first sample, oldest first.
    void prepare_until_(Voice& v, int target) noexcept {
        target = std::min(target, kPrepareSteps);
        for (; v.prepare_step < target; ++v.prepare_step) {
            if (v.prepare_step == 0) {
                layout_voice_(v);
                std::fill(v.ola.begin(), v.ola.end(), 0.0f);
                // Where in its hop the voice starts. The pre-roll frames sit
                // at the hop boundaries before ola[0] whatever this is, so any
                // start is a full overlap; staggering it keeps a chord's
                // voices from all drawing their next frame in one callback.
                v.hop_pos = static_cast<int>((v.age * kHopStagger) % kVoiceHop);
                continue;
            }
            const int back = kPrepareSteps - 1 - v.prepare_step;
            synthesise_frame_(v, -back * kVoiceHop);
        }
    }

    // Draw the voice's next frame, overlap-add it into its stream with its
    // first sample at `at` (negative: that much of it is already past), and
    // step every rotor on a hop.
    void synthesise_frame_(Voice& v, int at) noexcept {
        const auto bins = static_cast<std::size_t>(bins_);
        const auto n = static_cast<std::size_t>(kVoiceFftSize);
        const auto half = n / 2;
        for (int ch = 0; ch < channels_; ++ch) {
            auto* frame = frame_.data() + static_cast<std::size_t>(ch) * bins;
            const auto* noise = v.noise.data() + static_cast<std::size_t>(ch) * bins;
            for (std::size_t j = 0; j < bins; ++j) {
                const auto a = noise[j], b = v.rotor[j];
                frame[j] = {a.real() * b.real() - a.imag() * b.imag(),
                            a.real() * b.imag() + a.imag() * b.real()};
            }
        }
        for (int e = 0; e < v.entries; ++e) {
            const auto& rot = v.partial_rotor[static_cast<std::size_t>(v.entry_partial[static_cast<std::size_t>(e)])];
            const std::complex<float> r{static_cast<float>(rot.real()), static_cast<float>(rot.imag())};
            const auto j = static_cast<std::size_t>(v.entry_bin[static_cast<std::size_t>(e)]);
            for (int ch = 0; ch < channels_; ++ch) {
                const auto c = v.entry_coeff[static_cast<std::size_t>(e) * static_cast<std::size_t>(channels_)
                                             + static_cast<std::size_t>(ch)];
                auto& f = frame_[static_cast<std::size_t>(ch) * bins + j];
                f += std::complex<float>{c.real() * r.real() - c.imag() * r.imag(),
                                         c.real() * r.imag() + c.imag() * r.real()};
            }
        }
        // Two real channels per complex inverse transform.
        const std::size_t first = at < 0 ? static_cast<std::size_t>(-at) : 0;
        for (int ch = 0; ch < channels_; ch += 2) {
            const bool pair = ch + 1 < channels_;
            const auto* a = frame_.data() + static_cast<std::size_t>(ch) * bins;
            const auto* b = pair ? a + bins : nullptr;
            for (std::size_t k = 1; k < half; ++k) {
                const float ar = a[k].real(), ai = a[k].imag();
                const float br = pair ? b[k].real() : 0.0f, bi = pair ? b[k].imag() : 0.0f;
                time_[k] = {ar - bi, ai + br};
                time_[n - k] = {ar + bi, br - ai};
            }
            time_[0] = {};
            time_[half] = {};
            fft_.inverse(time_.data());
            float* out_a = v.ola.data() + static_cast<std::size_t>(ch) * n;
            float* out_b = pair ? out_a + n : nullptr;
            for (std::size_t i = first; i < n; ++i) {
                const float w = synthesis_window_[i];
                const auto o = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i) + at);
                out_a[o] += time_[i].real() * w;
                if (pair) out_b[o] += time_[i].imag() * w;
            }
        }
        // Step on a hop: each noise bin at its frequency plus a small random
        // walk (so a long note does not repeat), each partial exactly.
        for (std::size_t j = 0; j < bins; ++j) {
            const float walk = kWalk * (2.0f * next_uniform_() - 1.0f);
            const std::complex<float> w{1.0f - 0.5f * walk * walk, walk};
            auto z = v.rotor[j] * v.step[j] * w;
            // Back onto the unit circle (one Newton step: |z| is ~1).
            z *= 1.5f - 0.5f * std::norm(z);
            v.rotor[j] = z;
        }
        for (int p = 0; p < v.partial_count; ++p) {
            auto& z = v.partial_rotor[static_cast<std::size_t>(p)];
            z *= v.partial_step[static_cast<std::size_t>(p)];
            z *= 1.5 - 0.5 * std::norm(z);
        }
    }

    // K(d): the transform of the periodic Hann window at an offset of d bins,
    // sum_n w[n] e^{-i 2 pi d n / N}, tabulated over +-(kLobeReach + 1).
    [[nodiscard]] std::complex<double> kernel_(double d) const noexcept {
        const double x = (d + kKernelSpan) * kKernelSteps;
        if (!(x >= 0.0) || x >= static_cast<double>(kernel_table_.size() - 1)) return {};
        const auto i = static_cast<std::size_t>(x);
        const double f = x - static_cast<double>(i);
        return kernel_table_[i] + f * (kernel_table_[i + 1] - kernel_table_[i]);
    }

    void build_kernel_table_() {
        const int count = static_cast<int>(2 * kKernelSpan * kKernelSteps) + 1;
        kernel_table_.assign(static_cast<std::size_t>(count), {});
        constexpr double two_pi = 6.28318530717958647692;
        const double n = static_cast<double>(kVoiceFftSize);
        // sum_n e^{-i 2 pi d n / N}, in closed form.
        auto geometric = [&](double d) -> std::complex<double> {
            const double nearest = std::round(d);
            if (std::abs(d - nearest) < 1e-12)
                return std::fmod(std::abs(nearest), n) == 0.0 ? std::complex<double>{n, 0.0}
                                                              : std::complex<double>{};
            const std::complex<double> num = 1.0 - std::polar(1.0, -two_pi * d);
            const std::complex<double> den = 1.0 - std::polar(1.0, -two_pi * d / n);
            return num / den;
        };
        for (int i = 0; i < count; ++i) {
            const double d = static_cast<double>(i) / kKernelSteps - kKernelSpan;
            kernel_table_[static_cast<std::size_t>(i)] =
                0.5 * geometric(d) - 0.25 * geometric(d - 1.0) - 0.25 * geometric(d + 1.0);
        }
    }

    void build_unit_table_() {
        constexpr double two_pi = 6.28318530717958647692;
        for (std::size_t i = 0; i < unit_table_.size(); ++i) {
            const double a = two_pi * static_cast<double>(i) / static_cast<double>(unit_table_.size());
            unit_table_[i] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
        }
    }

    std::complex<float> random_unit_() noexcept {
        return unit_table_[static_cast<std::size_t>(next_bits_() >> 54) & (unit_table_.size() - 1)];
    }

    std::uint64_t next_bits_() noexcept {
        rng_ ^= rng_ >> 12;
        rng_ ^= rng_ << 25;
        rng_ ^= rng_ >> 27;
        return rng_ * 0x2545f4914f6cdd1dull;
    }
    float next_uniform_() noexcept {
        return static_cast<float>(next_bits_() >> 40) * (1.0f / 16777216.0f);
    }
    // SPECTR-RENDER-PATH END

    static constexpr double kKernelSpan = kLobeReach + 1.0;
    static constexpr int kKernelSteps = 1024;
    /// Per-hop random walk of a noise bin's phase (radians, uniform +-):
    /// FreezeHold's per-hop jitter, over the four source hops a voice hop
    /// spans.
    static constexpr float kWalk = 0.03f;
    static constexpr std::uint64_t kRngSeed = 0x6a09e667f3bcc909ull;

    FreezeSource& source_;
    pulp::signal::Fft fft_{};
    std::vector<float> synthesis_window_;
    std::vector<std::complex<double>> kernel_table_;
    std::array<std::complex<float>, 1024> unit_table_{};
    std::vector<std::complex<float>> time_;
    std::vector<std::complex<float>> frame_;       // channels * bins

    // The analysed hold.
    bool analysed_ = false;
    std::vector<std::uint8_t> locked_;
    std::vector<std::complex<float>> unit_;        // channels * bins: phase vs channel 0
    std::vector<float> omega_;
    std::vector<float> mags_;                      // channels * bins
    std::vector<float> sums_;
    std::vector<double> prefix_;
    std::vector<Partial> partials_;
    std::vector<std::complex<float>> partial_amp_; // partials * channels
    int partial_count_ = 0;

    std::array<Voice, kVoiceSlots> voices_{};
    std::uint64_t age_ = 0;
    std::array<Event, kMaxEvents> events_{};
    int event_count_ = 0;
    int event_read_ = 0;
    std::int64_t clock_ = 0;
    std::int64_t block_origin_ = 0;
    std::array<std::uint8_t, 128> key_velocity_{};
    bool pending_ = false;

    std::vector<float> input_copy_;                // channels * kMaxChunk
    std::vector<float> voice_sum_;                 // channels * kMaxChunk
    bool keys_mode_ = false;
    bool mode_loop_ = false;
    int mode_step_ = 0;
    int mode_delay_ = 0;                           // until the first voice sounds
    int attack_samples_ = 1;
    int release_samples_ = 1;
    int root_note_ = kDefaultRootNote;
    bool enabled_ = false;
    bool prepared_ = false;
    double sample_rate_ = 0.0;
    int channels_ = 0;
    int bins_ = 0;
    std::uint64_t rng_ = kRngSeed;
};

} // namespace spectr
