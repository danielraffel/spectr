// Freeze: a held spectrum of the input, taken ahead of the mask.
//
// Every contract here is measured on audio the product actually rendered --
// the processor through HeadlessHost, or the freeze source alone where a
// negative control needs a knob the processor does not expose -- and each
// carries a control that must read differently, so a green row is a reading
// of the product rather than of a blind instrument.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/format/headless.hpp>
#include <pulp/state/parameter_event_queue.hpp>

#include "spectr/editor_bridge.hpp"
#include "spectr/freeze_source.hpp"
#include "spectr/spectr.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using spectr::FreezeSource;
using spectr::MaskRenderMode;
using spectr::Spectr;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

std::size_t at(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * kSampleRate));
}

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

using Stimulus = std::function<std::pair<float, float>(std::size_t)>;
// Called before each block with the block's first sample. It may queue
// parameter events for the block and mark it as a transport jump.
using BeforeBlock = std::function<void(std::size_t, pulp::state::ParameterEventQueue&,
                                       pulp::format::ProcessContext&)>;
// Called after each block with the sample just past it.
using AfterBlock = std::function<void(std::size_t)>;

/// A processor under a HeadlessHost, in one render mode.
struct Rig {
    pulp::format::HeadlessHost host{spectr::create_spectr};
    Spectr* plugin = nullptr;
    int block = 256;

    explicit Rig(MaskRenderMode mode, int block_size = 256) : block(block_size) {
        plugin = dynamic_cast<Spectr*>(host.processor());
        REQUIRE(plugin != nullptr);
        REQUIRE(plugin->set_render_mode(mode));
        host.prepare(kSampleRate, block);
    }

    void set(pulp::state::ParamID id, float value) { host.state().set_value(id, value); }

    void mute_all(bool muted) {
        for (std::size_t band = 0; band < spectr::kMaxBands; ++band)
            set(spectr::band_mute_param_id(band), muted ? 1.0f : 0.0f);
    }

    int latency() const { return plugin->latency_samples(); }

    /// Is `t` inside the block that starts at sample `n`? Block starts are
    /// where a between-blocks store write lands.
    bool hits(std::size_t n, double t) const {
        const auto s = at(t);
        return n <= s && s < n + static_cast<std::size_t>(block);
    }

    Stereo run(std::size_t total, const Stimulus& stimulus,
               const BeforeBlock& before = {}, const AfterBlock& after = {}) {
        Stereo out;
        out.left.reserve(total);
        out.right.reserve(total);
        pulp::midi::MidiBuffer midi_in, midi_out;
        std::size_t position = 0;
        while (position < total) {
            const auto n = std::min<std::size_t>(static_cast<std::size_t>(block),
                                                 total - position);
            pulp::state::ParameterEventQueue events;
            pulp::format::ProcessContext ctx;
            if (before) before(position, events, ctx);
            pulp::audio::Buffer<float> in(2, n), buf(2, n);
            for (std::size_t i = 0; i < n; ++i) {
                const auto [l, r] = stimulus(position + i);
                in.channel(0)[i] = l;
                in.channel(1)[i] = r;
            }
            const float* in_ptrs[] = {in.channel(0).data(), in.channel(1).data()};
            pulp::audio::BufferView<const float> iv(in_ptrs, 2, n);
            auto ov = buf.view();
            host.process(ov, iv, midi_in, midi_out, events, ctx);
            out.left.insert(out.left.end(), buf.channel(0).begin(), buf.channel(0).end());
            out.right.insert(out.right.end(), buf.channel(1).begin(), buf.channel(1).end());
            position += n;
            if (after) after(position);
        }
        return out;
    }
};

Stimulus tone(double hz, float amplitude, double stop_seconds = 1.0e9) {
    return [=](std::size_t n) {
        if (n >= at(stop_seconds)) return std::pair<float, float>{0.0f, 0.0f};
        const auto v = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * n / kSampleRate));
        return std::pair<float, float>{v, v};
    };
}

/// Least-squares amplitude and phase of one sinusoid in a window: immune to
/// leakage from the others, and to window length not being a whole period.
struct Tone { double amplitude = 0.0; double phase = 0.0; };
Tone fit(const std::vector<float>& x, std::size_t from, std::size_t count, double hz) {
    double c = 0.0, s = 0.0;
    const auto end = std::min(x.size(), from + count);
    for (std::size_t n = from; n < end; ++n) {
        const double w = 2.0 * kPi * hz * static_cast<double>(n) / kSampleRate;
        c += x[n] * std::cos(w);
        s += x[n] * std::sin(w);
    }
    const double k = 2.0 / static_cast<double>(end - from);
    return {std::hypot(c, s) * k, std::atan2(c, s)};
}

double rms_db(const std::vector<float>& x, std::size_t from, std::size_t count) {
    double e = 0.0;
    const auto end = std::min(x.size(), from + count);
    for (std::size_t n = from; n < end; ++n) e += static_cast<double>(x[n]) * x[n];
    return 10.0 * std::log10(e / static_cast<double>(end - from) + 1.0e-20);
}

/// The largest second difference in a region: a waveform discontinuity or a
/// kink shows here long before it shows in an RMS.
double worst_kink(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double worst = 0.0;
    to = std::min(to, x.size());
    for (std::size_t n = from + 2; n < to; ++n)
        worst = std::max(worst, std::abs(static_cast<double>(x[n]) - 2.0 * x[n - 1] + x[n - 2]));
    return worst;
}

double wrap(double phase) {
    return phase - 2.0 * kPi * std::round(phase / (2.0 * kPi));
}

/// 0.3 amplitude, 440 Hz until 1 s, then 660 Hz, joined by a 5 ms
/// raised-cosine crossfade so the input itself has no kink to mistake for one
/// the product made.
double switching_tone(std::size_t n) {
    const double t = static_cast<double>(n) / kSampleRate;
    const double c = std::clamp((t - 1.0) / 0.005, 0.0, 1.0);
    const double w = 0.5 - 0.5 * std::cos(kPi * c);
    return 0.3 * ((1.0 - w) * std::sin(2.0 * kPi * 440.0 * t)
                  + w * std::sin(2.0 * kPi * 660.0 * t));
}

const char* mode_name(MaskRenderMode mode) {
    return mode == MaskRenderMode::linear_phase ? "Mixing" : "Tracking";
}

constexpr MaskRenderMode kModes[] = {MaskRenderMode::linear_phase,
                                     MaskRenderMode::zero_latency};

/// A spectral hold (the default Hold length) and a loop of the audio itself.
constexpr double kHoldLengths[] = {FreezeSource::kDefaultHoldSeconds, 1.0};

} // namespace

// ── Capture is ahead of the mask ───────────────────────────────────────────

TEST_CASE("Freeze holds the input ahead of the mask", "[freeze][spectral]") {
    // Every band is muted before the freeze and unmuted after the input has
    // stopped. A capture taken after the mask would have held silence; one
    // taken ahead of it holds the tone, which the unmuted mask then lets out.
    for (const double hold : kHoldLengths)
    for (const auto mode : kModes) {
        const auto render = [&](bool freeze) {
            Rig rig(mode);
            rig.plugin->set_freeze_hold_seconds(hold);
            rig.mute_all(true);
            return rig.run(at(3.5), tone(1000.0, 0.3f, /*stop=*/1.5),
                [&](std::size_t n, auto&, auto&) {
                    if (rig.hits(n, 1.0) && freeze) rig.set(spectr::kParamFreeze, 1.0f);
                    if (rig.hits(n, 2.0)) rig.mute_all(false);
                });
        };
        const auto frozen = render(true);
        const auto control = render(false);
        const auto held = fit(frozen.left, at(3.0), at(0.4), 1000.0).amplitude;
        const auto live = fit(control.left, at(3.0), at(0.4), 1000.0).amplitude;
        INFO(mode_name(mode) << " hold " << hold << " s: held=" << held << " without freeze=" << live);
        CHECK(held > 0.15);
        // Control: without a freeze the same schedule is silent there, so the
        // tone above can only have come from the hold.
        CHECK(live < 0.003);
    }
}

TEST_CASE("The live mask and LFO keep acting on the held sound", "[freeze][modulation]") {
    // A square LFO over the whole bank, started after the input has stopped:
    // the held tone must rise and fall with it. Depth 0 is the control -- the
    // same measurement on a steady hold reads flat.
    for (const double hold : kHoldLengths)
    for (const auto mode : kModes) {
        const auto swing = [&](float depth) {
            Rig rig(mode);
            rig.plugin->set_freeze_hold_seconds(hold);
            for (std::size_t band = 0; band < 32; ++band)
                rig.set(spectr::band_gain_param_id(band), -12.0f);
            const auto out = rig.run(at(3.2), tone(1000.0, 0.3f, /*stop=*/1.0),
                [&](std::size_t n, auto&, auto&) {
                    if (rig.hits(n, 0.6)) rig.set(spectr::kParamFreeze, 1.0f);
                    if (rig.hits(n, 1.2)) {
                        rig.set(spectr::kParamLfoEnabled, 1.0f);
                        rig.set(spectr::kParamLfoShape,
                                static_cast<float>(spectr::LfoShape::Square));
                        rig.set(spectr::kParamLfoRate, 1.0f);
                        rig.set(spectr::kParamLfoDepth, depth);
                        rig.set(spectr::kParamLfoTarget,
                                static_cast<float>(spectr::ModulationTarget::WholeBank));
                    }
                });
            double lo = 1.0e9, hi = 0.0;
            for (std::size_t n = at(2.0); n + at(0.02) < at(3.2); n += at(0.02)) {
                const double a = fit(out.left, n, at(0.02), 1000.0).amplitude;
                lo = std::min(lo, a);
                hi = std::max(hi, a);
            }
            return std::pair<double, double>{lo, hi};
        };
        const auto [lo, hi] = swing(1.0f);
        const auto [flat_lo, flat_hi] = swing(0.0f);
        INFO(mode_name(mode) << " hold " << hold << " s: modulated " << lo << ".." << hi
             << " unmodulated " << flat_lo << ".." << flat_hi);
        REQUIRE(flat_lo > 0.01);                // the hold is audible at all
        CHECK(hi > lo * 4.0);                   // the LFO moves it by > 12 dB
        CHECK(flat_hi < flat_lo * 1.5);         // and without the LFO it is steady
    }
}

// ── Engaging and releasing ─────────────────────────────────────────────────

TEST_CASE("Freeze engages within a block and a fade, and releases, without a click",
          "[freeze][click][latency]") {
    // The input changes from 440 Hz to 660 Hz at the moment freeze is
    // pressed, so the hold (440) and the live input (660) differ across the
    // engage fade, and again across the release fade -- a hard switch between
    // them is a step in the waveform. The second difference measures it.
    const auto stimulus = [](std::size_t n) {
        const auto v = static_cast<float>(switching_tone(n));
        return std::pair<float, float>{v, v};
    };
    // The steepest bend a 0.3 amplitude 660 Hz sine makes on its own.
    const double tone_kink = 0.3 * std::pow(2.0 * kPi * 660.0 / kSampleRate, 2.0);

    for (const auto mode : kModes) {
        Rig rig(mode);
        std::size_t held_at = 0;
        std::size_t pressed_at = 0;
        const auto out = rig.run(at(3.0), stimulus,
            [&](std::size_t n, auto&, auto&) {
                if (rig.hits(n, 1.0)) {
                    rig.set(spectr::kParamFreeze, 1.0f);
                    pressed_at = n;
                }
                if (rig.hits(n, 2.0)) rig.set(spectr::kParamFreeze, 0.0f);
            },
            [&](std::size_t n) {
                if (held_at == 0 && rig.plugin->freeze_source().phase()
                                        == FreezeSource::Phase::held)
                    held_at = n;
            });
        const auto latency = static_cast<std::size_t>(rig.latency());
        const double engage_ms =
            (static_cast<double>(held_at) - static_cast<double>(pressed_at)) * 1000.0 / kSampleRate;
        const double block_ms = rig.block * 1000.0 / kSampleRate;
        INFO(mode_name(mode) << " fully held " << engage_ms << " ms after the press");
        REQUIRE(held_at > 0);
        CHECK(engage_ms <= block_ms + 60.0);

        const double engage_kink = worst_kink(out.left, at(1.0) + latency - at(0.01),
                                              at(1.0) + latency + at(0.25));
        const double release_kink = worst_kink(out.left, at(2.0) + latency - at(0.01),
                                               at(2.0) + latency + at(0.25));
        INFO("engage kink " << engage_kink << " release kink " << release_kink
             << " tone's own " << tone_kink);
        CHECK(engage_kink < 3.0 * tone_kink);
        CHECK(release_kink < 3.0 * tone_kink);
    }
}

TEST_CASE("Freeze's fades hold the level on a steady tone and on noise",
          "[freeze][click]") {
    // Every 5 ms RMS window across each fade, against the quieter of the
    // steady levels on either side. A 5 ms window of noise (or of a tone that
    // is not a whole number of periods long) wanders by itself, so the same
    // windows of the same input rendered WITHOUT freezing are the control:
    // the fade may add at most 1 dB to whatever dip the signal already shows.
    const auto worst_dip = [](const std::vector<float>& x, std::size_t at_sample) {
        const double before = rms_db(x, at_sample - at(0.1), at(0.08));
        const double after = rms_db(x, at_sample + at(0.2), at(0.08));
        double worst = 0.0;
        for (std::size_t n = at_sample; n < at_sample + at(0.12); n += at(0.005))
            worst = std::min(worst, rms_db(x, n, at(0.005)) - std::min(before, after));
        return worst;
    };
    std::mt19937 rng(7);
    std::normal_distribution<float> gaussian(0.0f, 0.1f);
    std::vector<float> noise(at(3.0));
    for (auto& v : noise) v = gaussian(rng);
    const Stimulus inputs[] = {
        tone(440.0, 0.3f),
        [&](std::size_t n) { return std::pair<float, float>{noise[n], noise[n]}; },
    };
    const char* input_names[] = {"tone", "noise"};
    for (const auto mode : kModes) {
        for (int input = 0; input < 2; ++input) {
            const auto render = [&](bool freeze) {
                Rig rig(mode);
                return std::pair<Stereo, int>{
                    rig.run(at(3.0), inputs[input], [&](std::size_t n, auto&, auto&) {
                        if (rig.hits(n, 1.0) && freeze) rig.set(spectr::kParamFreeze, 1.0f);
                        if (rig.hits(n, 2.0) && freeze) rig.set(spectr::kParamFreeze, 0.0f);
                    }),
                    rig.latency()};
            };
            const auto [frozen, latency] = render(true);
            const auto [live, unused] = render(false);
            (void)unused;
            for (const double press : {1.0, 2.0}) {
                const auto where = at(press) - at(press) % 256 + static_cast<std::size_t>(latency);
                const double dip = worst_dip(frozen.left, where);
                const double natural = worst_dip(live.left, where);
                INFO(mode_name(mode) << " " << input_names[input]
                     << (press < 1.5 ? " engage" : " release") << ": dip " << dip
                     << " dB, the same windows unfrozen " << natural << " dB");
                CHECK(dip > natural - 1.0);
            }
        }
    }
}

TEST_CASE("The click measurement sees a hard switch", "[freeze][click][control]") {
    // The negative control for the row above: the same stimulus through the
    // freeze source with its crossfade shortened to one sample. If the kink
    // measurement could not see this, its passes above would mean nothing.
    const double tone_kink = 0.3 * std::pow(2.0 * kPi * 660.0 / kSampleRate, 2.0);
    const auto render = [&](int crossfade) {
        FreezeSource source;
        REQUIRE(source.prepare(kSampleRate, 1));
        if (crossfade > 0) source.set_crossfade_samples(crossfade);
        std::vector<float> in(at(2.5)), out(at(2.5));
        for (std::size_t n = 0; n < in.size(); ++n)
            in[n] = static_cast<float>(switching_tone(n));
        for (std::size_t n = 0; n < in.size(); n += 256) {
            source.set_frozen(n >= at(1.0) && n < at(2.0));
            const float* i[] = {in.data() + n};
            float* o[] = {out.data() + n};
            source.process_block(i, o, 1, static_cast<int>(std::min<std::size_t>(256, in.size() - n)));
        }
        return std::max(worst_kink(out, at(1.0), at(1.2)), worst_kink(out, at(2.0), at(2.2)));
    };
    const double faded = render(0);
    const double hard = render(1);
    INFO("faded " << faded << " hard " << hard << " tone " << tone_kink);
    CHECK(faded < 3.0 * tone_kink);
    CHECK(hard > 10.0 * tone_kink);
}

// ── Image, transport, Latency switch ───────────────────────────────────────

TEST_CASE("Freeze keeps the stereo image", "[freeze][stereo]") {
    // Two partials with different inter-channel relationships: 500 Hz with R
    // leading by 1 rad, and 1500 Hz in anti-phase. The hold, measured long
    // after the input stopped, must keep both relationships and the balance.
    const auto stimulus = [](std::size_t n) {
        if (n >= at(1.0)) return std::pair<float, float>{0.0f, 0.0f};
        const double t = static_cast<double>(n) / kSampleRate;
        const double a = std::sin(2.0 * kPi * 500.0 * t);
        const double b = std::sin(2.0 * kPi * 1500.0 * t);
        return std::pair<float, float>{
            static_cast<float>(0.2 * a + 0.2 * b),
            static_cast<float>(0.2 * std::sin(2.0 * kPi * 500.0 * t + 1.0) - 0.2 * b)};
    };
    for (const auto mode : kModes) {
        Rig rig(mode);
        const auto out = rig.run(at(2.6), stimulus, [&](std::size_t n, auto&, auto&) {
            if (rig.hits(n, 0.5)) rig.set(spectr::kParamFreeze, 1.0f);
        });
        const auto from = at(2.0), count = at(0.5);
        const auto l1 = fit(out.left, from, count, 500.0), r1 = fit(out.right, from, count, 500.0);
        const auto l2 = fit(out.left, from, count, 1500.0), r2 = fit(out.right, from, count, 1500.0);
        const double d1 = wrap(r1.phase - l1.phase);
        const double d2 = wrap(r2.phase - l2.phase);
        INFO(mode_name(mode) << " 500 Hz: L " << l1.amplitude << " R " << r1.amplitude
             << " dphi " << d1 << "; 1500 Hz: L " << l2.amplitude << " R " << r2.amplitude
             << " dphi " << d2);
        REQUIRE(l1.amplitude > 0.05);
        REQUIRE(l2.amplitude > 0.05);
        CHECK(std::abs(d1 - 1.0) < 0.15);
        CHECK(std::abs(std::abs(d2) - kPi) < 0.15);
        CHECK(std::abs(20.0 * std::log10(r1.amplitude / l1.amplitude)) < 1.0);
        CHECK(std::abs(20.0 * std::log10(r2.amplitude / l2.amplitude)) < 1.0);
    }
}

TEST_CASE("A transport jump keeps a playing hold", "[freeze][transport]") {
    for (const auto mode : kModes) {
        const auto render = [&](bool freeze) {
            Rig rig(mode);
            const auto out = rig.run(at(3.0), tone(1000.0, 0.3f, /*stop=*/1.0),
                [&](std::size_t n, auto&, pulp::format::ProcessContext& ctx) {
                    if (rig.hits(n, 0.5) && freeze) rig.set(spectr::kParamFreeze, 1.0f);
                    if (rig.hits(n, 1.5)) ctx.reset_requested = true;
                });
            if (freeze) CHECK(rig.plugin->freeze_source().phase() == FreezeSource::Phase::held);
            return out;
        };
        const auto frozen = render(true);
        const auto control = render(false);
        const double before = fit(frozen.left, at(1.2), at(0.25), 1000.0).amplitude;
        const double after = fit(frozen.left, at(2.4), at(0.5), 1000.0).amplitude;
        const double silent = fit(control.left, at(2.4), at(0.5), 1000.0).amplitude;
        INFO(mode_name(mode) << " before jump " << before << " after " << after
             << " unfrozen " << silent);
        REQUIRE(before > 0.1);
        CHECK(std::abs(20.0 * std::log10(after / before)) < 1.5);
        CHECK(silent < 0.003);
    }
}

TEST_CASE("Clearing history keeps the hold; a full reset drops it", "[freeze][transport][control]") {
    // The processor calls clear_history() on a transport jump. The control is
    // reset(), which is what a jump must NOT do: the same measurement has to
    // tell the two apart.
    const auto render = [](bool full_reset) {
        FreezeSource source;
        REQUIRE(source.prepare(kSampleRate, 1));
        std::vector<float> in(at(2.0)), out(at(2.0));
        for (std::size_t n = 0; n < at(1.0); ++n)
            in[n] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 1000.0 * n / kSampleRate));
        source.set_frozen(true);
        for (std::size_t n = 0; n < in.size(); n += 256) {
            if (n == at(1.2)) { if (full_reset) source.reset(); else source.clear_history(); }
            const float* i[] = {in.data() + n};
            float* o[] = {out.data() + n};
            source.process_block(i, o, 1, 256);
        }
        return fit(out, at(1.5), at(0.4), 1000.0).amplitude;
    };
    const double kept = render(false);
    const double dropped = render(true);
    INFO("clear_history " << kept << " reset " << dropped);
    CHECK(kept > 0.2);
    CHECK(dropped < 0.003);
}

TEST_CASE("Switching Latency mode keeps a playing hold", "[freeze][render-mode]") {
    for (const auto from : kModes) {
        const auto to = from == MaskRenderMode::linear_phase ? MaskRenderMode::zero_latency
                                                             : MaskRenderMode::linear_phase;
        const auto render = [&](bool freeze) {
            Rig rig(from);
            return rig.run(at(3.0), tone(1000.0, 0.3f, /*stop=*/1.0),
                [&](std::size_t n, auto&, auto&) {
                    if (rig.hits(n, 0.5) && freeze) rig.set(spectr::kParamFreeze, 1.0f);
                    if (rig.hits(n, 1.5)) REQUIRE(rig.plugin->set_render_mode(to));
                });
        };
        const auto frozen = render(true);
        const auto control = render(false);
        const double before = fit(frozen.left, at(1.2), at(0.25), 1000.0).amplitude;
        const double after = fit(frozen.left, at(2.4), at(0.5), 1000.0).amplitude;
        const double silent = fit(control.left, at(2.4), at(0.5), 1000.0).amplitude;
        INFO(mode_name(from) << " -> " << mode_name(to) << ": before " << before
             << " after " << after << " unfrozen " << silent);
        REQUIRE(before > 0.1);
        CHECK(std::abs(20.0 * std::log10(after / before)) < 1.5);
        CHECK(silent < 0.003);
    }
}

// ── Mix, silence, tail ─────────────────────────────────────────────────────

TEST_CASE("Below 100% Mix the dry leg stays live while the wet leg holds",
          "[freeze][mix]") {
    // 440 Hz is frozen; the input then moves to 880 Hz. At Mix 50% both are
    // heard -- the hold through the wet leg, the new input through the dry
    // one. At 100% the new input must be absent: the control that shows the
    // 880 Hz reading comes from the dry leg and not from a leaking hold.
    const auto stimulus = [](std::size_t n) {
        const double hz = n < at(0.6) ? 440.0 : 880.0;
        const auto v = static_cast<float>(0.3 * std::sin(2.0 * kPi * hz * n / kSampleRate));
        return std::pair<float, float>{v, v};
    };
    for (const double hold : kHoldLengths)
    for (const auto mode : kModes) {
        const auto render = [&](float mix) {
            Rig rig(mode);
            rig.plugin->set_freeze_hold_seconds(hold);
            rig.set(spectr::kMix, mix);
            return rig.run(at(2.2), stimulus, [&](std::size_t n, auto&, auto&) {
                if (rig.hits(n, 0.5)) rig.set(spectr::kParamFreeze, 1.0f);
            });
        };
        const auto half = render(50.0f);
        const auto full = render(100.0f);
        const double held = fit(half.left, at(1.6), at(0.5), 440.0).amplitude;
        const double dry = fit(half.left, at(1.6), at(0.5), 880.0).amplitude;
        const double leak = fit(full.left, at(1.6), at(0.5), 880.0).amplitude;
        INFO(mode_name(mode) << " hold " << hold << " s: held " << held << " dry " << dry
             << " leak at 100% " << leak);
        CHECK(held > 0.1);
        CHECK(dry == Catch::Approx(0.15).margin(0.02));
        CHECK(leak < 0.005);
    }
}

TEST_CASE("A freeze asked for over silence waits for signal", "[freeze][silence]") {
    // Freeze is on from the first sample; the input is silent for a second,
    // then a tone plays for 0.6 s and stops. The hold must be the tone.
    const auto stimulus = [](std::size_t n) {
        if (n < at(1.0) || n >= at(1.6)) return std::pair<float, float>{0.0f, 0.0f};
        const auto v = static_cast<float>(0.3 * std::sin(2.0 * kPi * 1000.0 * n / kSampleRate));
        return std::pair<float, float>{v, v};
    };
    for (const auto mode : kModes) {
        Rig rig(mode);
        rig.set(spectr::kParamFreeze, 1.0f);
        bool armed_through_silence = true;
        const auto out = rig.run(at(2.8), stimulus, {}, [&](std::size_t n) {
            if (n > at(0.05) && n < at(1.0))
                armed_through_silence = armed_through_silence
                    && rig.plugin->freeze_source().phase() == FreezeSource::Phase::arming;
        });
        const double held = fit(out.left, at(2.2), at(0.5), 1000.0).amplitude;
        INFO(mode_name(mode) << " held after the input stopped: " << held);
        CHECK(armed_through_silence);
        CHECK(held > 0.15);
    }
}

TEST_CASE("The silence check is what keeps silence from latching",
          "[freeze][silence][control]") {
    // Negative control: with the floor at zero, the same schedule latches
    // the silence and the tone never reaches the hold.
    const auto held_after = [](bool floor) {
        FreezeSource source;
        REQUIRE(source.prepare(kSampleRate, 1));
        if (!floor) source.set_signal_floor_power(0.0);
        source.set_frozen(true);
        std::vector<float> in(at(2.4)), out(at(2.4));
        for (std::size_t n = at(1.0); n < at(1.6); ++n)
            in[n] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 1000.0 * n / kSampleRate));
        for (std::size_t n = 0; n < in.size(); n += 256) {
            const float* i[] = {in.data() + n};
            float* o[] = {out.data() + n};
            source.process_block(i, o, 1, 256);
        }
        return fit(out, at(1.9), at(0.4), 1000.0).amplitude;
    };
    const double with_floor = held_after(true);
    const double without = held_after(false);
    INFO("floor " << with_floor << " no floor " << without);
    CHECK(with_floor > 0.15);
    CHECK(without < 0.003);
}

TEST_CASE("Spectr reports a constant infinite tail and never flags a tail edge",
          "[freeze][tail]") {
    // A held spectrum sounds without input, so the tail is infinite -- and it
    // is infinite always, on every format. A tail that followed Freeze had to
    // be announced on each edge from the audio thread, inside the callback a
    // tap lands in: AU v2 turned that into TailTime listener calls on the
    // render thread, VST3 into a component reload. With a constant tail there
    // is nothing to announce, on a press, a release, or a Latency switch.
    Rig rig(MaskRenderMode::zero_latency);
    auto* plugin = rig.plugin;
    int flags = 0, holds = 0;
    bool always_infinite = plugin->descriptor().tail_samples == -1;
    auto last = FreezeSource::Phase::live;
    rig.run(at(2.0), tone(1000.0, 0.3f),
        [&](std::size_t n, auto&, auto&) {
            if (rig.hits(n, 0.5)) rig.set(spectr::kParamFreeze, 1.0f);
            if (rig.hits(n, 1.0)) rig.set(spectr::kParamFreeze, 0.0f);
            if (rig.hits(n, 1.3)) rig.set(spectr::kParamFreeze, 1.0f);
            if (rig.hits(n, 1.6)) rig.set(spectr::kParamFreeze, 0.0f);
        },
        [&](std::size_t) {
            if (plugin->consume_tail_changed_flag()) ++flags;
            always_infinite = always_infinite && plugin->descriptor().tail_samples == -1;
            const auto phase = plugin->freeze_source().phase();
            if (phase == FreezeSource::Phase::held && last != phase) ++holds;
            last = phase;
        });
    // Both freezes really held and released: the script is not vacuous.
    REQUIRE(holds == 2);
    REQUIRE(plugin->freeze_source().phase() == FreezeSource::Phase::live);
    (void)plugin->consume_latency_changed_flag();
    REQUIRE(plugin->set_render_mode(MaskRenderMode::linear_phase));
    // The instrument sees a flag when one is raised: a Latency switch still
    // moves the latency, which every host must hear.
    CHECK(plugin->consume_latency_changed_flag());
    if (plugin->consume_tail_changed_flag()) ++flags;
    CHECK(flags == 0);
    CHECK(always_infinite);
    CHECK(plugin->descriptor().tail_samples == -1);
}

namespace {

/// The longest run, in samples, of 5 ms windows whose RMS sits more than
/// 20 dB below `reference_db`, inside [from, to).
std::size_t longest_quiet_run(const std::vector<float>& x, std::size_t from,
                              std::size_t to, double reference_db) {
    const std::size_t window = at(0.005);
    std::size_t run = 0, worst = 0;
    for (std::size_t n = from; n + window <= std::min(to, x.size()); n += window) {
        if (rms_db(x, n, window) < reference_db - 20.0) {
            run += window;
            worst = std::max(worst, run);
        } else {
            run = 0;
        }
    }
    return worst;
}

} // namespace

TEST_CASE("A host reload at the same geometry keeps a playing hold",
          "[freeze][reload]") {
    // A host deactivate/reactivate (release()+prepare()) at the same rate and
    // width must not throw the hold away or leave a hole longer than a
    // crossfade. The control reloads without a freeze and must go silent, so
    // the "after" reading can only come from the hold.
    for (const auto mode : kModes) {
        const auto render = [&](bool freeze) {
            Rig rig(mode);
            bool reloaded = false;
            auto out = rig.run(at(3.0), tone(1000.0, 0.3f, /*stop=*/1.0),
                [&](std::size_t n, auto&, auto&) {
                    if (rig.hits(n, 0.5) && freeze) rig.set(spectr::kParamFreeze, 1.0f);
                    if (rig.hits(n, 1.5)) {
                        rig.host.release();
                        rig.host.prepare(kSampleRate, rig.block);
                        reloaded = true;
                    }
                });
            REQUIRE(reloaded);
            if (freeze) {
                CHECK(rig.host.state().get_value(spectr::kParamFreeze) >= 0.5f);
                CHECK(rig.plugin->freeze_source().phase() == FreezeSource::Phase::held);
            }
            return out;
        };
        const auto frozen = render(true);
        const auto control = render(false);
        const double before = fit(frozen.left, at(1.2), at(0.25), 1000.0).amplitude;
        const double after = fit(frozen.left, at(2.4), at(0.5), 1000.0).amplitude;
        const double silent = fit(control.left, at(2.4), at(0.5), 1000.0).amplitude;
        const double held_db = rms_db(frozen.left, at(1.2), at(0.25));
        // The reload lands on the block containing 1.5 s.
        const std::size_t reload_at = (at(1.5) / 256) * 256;
        const auto gap = longest_quiet_run(frozen.left, reload_at, at(2.4), held_db);
        const auto crossfade = static_cast<std::size_t>(
            std::lround(FreezeSource::kCrossfadeSeconds * kSampleRate));
        INFO(mode_name(mode) << ": before " << before << " after " << after
             << " unfrozen " << silent << " gap " << gap << " samples");
        REQUIRE(before > 0.1);
        CHECK(std::abs(20.0 * std::log10(std::max(after, 1e-9) / before)) < 1.5);
        CHECK(gap <= crossfade);
        CHECK(silent < 0.003);
    }
}

TEST_CASE("A host reload at a new sample rate drops the hold and re-arms",
          "[freeze][reload]") {
    // A different rate (or width) invalidates the held spectrum: it must not
    // be replayed at the wrong pitch or read through a mis-sized buffer. The
    // Freeze parameter survives, so the source re-arms and latches the new
    // input -- 660 Hz after the reload, never the 1000 Hz from before it.
    for (const auto mode : kModes) {
        Rig rig(mode);
        constexpr double kNewRate = 44100.0;
        rig.run(at(1.2), tone(1000.0, 0.3f),
            [&](std::size_t n, auto&, auto&) {
                if (rig.hits(n, 0.5)) rig.set(spectr::kParamFreeze, 1.0f);
            });
        REQUIRE(rig.plugin->freeze_source().phase() == FreezeSource::Phase::held);
        rig.host.release();
        rig.host.prepare(kNewRate, rig.block);
        CHECK(rig.plugin->freeze_source().prepared_for(kNewRate, 2));
        CHECK(rig.plugin->freeze_source().phase() == FreezeSource::Phase::live);
        CHECK(rig.host.state().get_value(spectr::kParamFreeze) >= 0.5f);

        const auto at_new = [&](double s) {
            return static_cast<std::size_t>(std::llround(s * kNewRate));
        };
        const auto new_tone = [&](std::size_t n) {
            const auto v = static_cast<float>(
                0.3 * std::sin(2.0 * kPi * 660.0 * n / kNewRate));
            return std::pair<float, float>{v, v};
        };
        auto out = rig.run(at_new(3.0), [&](std::size_t n) {
            return n < at_new(1.0) ? new_tone(n) : std::pair<float, float>{0.0f, 0.0f};
        });
        CHECK(rig.plugin->freeze_source().phase() == FreezeSource::Phase::held);
        // fit() assumes kSampleRate; rescale the frequencies to read the
        // 44.1 kHz render correctly.
        const double scale = kSampleRate / kNewRate;
        const double new_hold = fit(out.left, at_new(2.0), at_new(0.5), 660.0 * scale).amplitude;
        const double old_hold = fit(out.left, at_new(2.0), at_new(0.5), 1000.0 * scale).amplitude;
        double worst = 0.0;
        for (float v : out.left) worst = std::max(worst, static_cast<double>(std::abs(v)));
        INFO(mode_name(mode) << ": 660 Hz hold " << new_hold << " stale 1000 Hz "
             << old_hold << " peak " << worst);
        CHECK(new_hold > 0.1);
        CHECK(old_hold < 0.01);
        CHECK(std::isfinite(worst));
        CHECK(worst < 1.0);
    }
}

// ── Automation, projection, settings ───────────────────────────────────────

TEST_CASE("Freeze rides host automation events and reaches the editor's live projection",
          "[freeze][automation]") {
    Rig rig(MaskRenderMode::zero_latency, 512);
    // An automation event mid-block, with no store write at all: the audio
    // path must act on the event itself.
    bool sent = false;
    std::size_t sent_at = 0;
    bool requested_after_event = false;
    rig.run(at(1.0), tone(1000.0, 0.3f),
        [&](std::size_t n, auto& events, auto&) {
            if (!sent && rig.hits(n, 0.5)) {
                REQUIRE(events.push({spectr::kParamFreeze, 300, 1.0f, 0}));
                sent = true;
                sent_at = n;
            }
        },
        [&](std::size_t n) {
            if (sent && n == sent_at + 512) {
                requested_after_event = rig.plugin->freeze_source().frozen_requested();
                // What an adapter does once the block is done: commit the
                // block's final value to the store.
                rig.set(spectr::kParamFreeze, 1.0f);
            }
        });
    CHECK(requested_after_event);
    CHECK(rig.plugin->freeze_source().frozen_requested());
    CHECK(rig.plugin->freeze_source().hold_audible());

    // The store side: a host write advances the automation revision (so the
    // editor gets a live projection) and the projection carries the value.
    // The sync worker may already have adopted the write above; applying
    // here makes sure it has, and the release below is this test's own.
    (void)rig.plugin->apply_surface_params(false);
    const auto project = [&] {
        return spectr::make_editor_live_state_payload(
            *rig.plugin, rig.plugin->host_automation_revision());
    };
    REQUIRE(project().hasObjectMember("freeze"));
    CHECK(project()["freeze"]["frozen"].getBool());
    const auto before = rig.plugin->host_automation_revision();
    rig.set(spectr::kParamFreeze, 0.0f);
    REQUIRE(rig.plugin->apply_surface_params(false));
    CHECK(rig.plugin->host_automation_revision() > before);
    CHECK_FALSE(project()["freeze"]["frozen"].getBool());
    // The hold length is a Settings value, not an automatable lane, so the
    // live projection does not carry it.
    CHECK_FALSE(project()["freeze"].hasObjectMember("hold_seconds"));
}

TEST_CASE("Hold length selects the capture window and persists with the session",
          "[freeze][settings]") {
    Rig rig(MaskRenderMode::zero_latency);
    const auto frames_after = [&](double seconds) {
        rig.plugin->set_freeze_hold_seconds(seconds);
        rig.run(at(0.05), tone(1000.0, 0.3f));
        return rig.plugin->freeze_source().capture_frames();
    };
    // Default: the reference timing, 8 hops of 512 at 48 kHz.
    CHECK(rig.plugin->freeze_hold_seconds()
          == Catch::Approx(FreezeSource::kDefaultHoldSeconds));
    CHECK(frames_after(FreezeSource::kDefaultHoldSeconds) == 8);
    CHECK(frames_after(0.5) == 47);
    // Clamped to the advertised range.
    CHECK(frames_after(10.0) == static_cast<int>(std::lround(
              FreezeSource::kMaxHoldSeconds * kSampleRate / FreezeSource::kHop)));
    CHECK(rig.plugin->freeze_hold_seconds() == Catch::Approx(FreezeSource::kMaxHoldSeconds));
    CHECK(frames_after(0.0) == static_cast<int>(std::lround(
              FreezeSource::kMinHoldSeconds * kSampleRate / FreezeSource::kHop)));

    rig.plugin->set_freeze_hold_seconds(0.75);
    const auto blob = rig.plugin->serialize_plugin_state();
    Rig other(MaskRenderMode::zero_latency);
    REQUIRE(other.plugin->freeze_hold_seconds() != Catch::Approx(0.75));
    REQUIRE(other.plugin->deserialize_plugin_state(blob));
    CHECK(other.plugin->freeze_hold_seconds() == Catch::Approx(0.75));
}
