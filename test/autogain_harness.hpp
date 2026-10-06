#pragma once

// Auto Gain v1/v2 measurement harness, shared by the ctest gates
// (test/test_auto_gain_v2.cpp) and the advisory corpus sweep
// (tools/autogain_sweep.cpp).
//
// Everything is measured on the rendered output of a real Spectr instance
// through HeadlessHost, with Pulp's BS.1770 meter
// (pulp::signal::MultiChannelMeter) for loudness and the audio-analysis
// library for the finite / unclipped checks. No device, no clock.

#include <pulp/audio/analysis/audio_assertions.hpp>
#include <pulp/audio/analysis/audio_metrics.hpp>
#include <pulp/audio/buffer.hpp>
#include <pulp/format/headless.hpp>
#include <pulp/signal/multi_channel_meter.hpp>

#include "spectr/auto_gain_material.hpp"
#include "spectr/level_controls.hpp"
#include "spectr/param_surface.hpp"
#include "spectr/render_mode.hpp"
#include "spectr/spectr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace autogain_harness {

inline constexpr double kRate = 48000.0;
inline constexpr int kBlock = 512;
inline constexpr double kPi = 3.14159265358979323846;

struct Stereo {
    std::vector<float> l, r;
    std::size_t size() const { return l.size(); }
    static Stereo zeros(std::size_t n) {
        return {std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f)};
    }
};

inline std::size_t seconds(double s) { return static_cast<std::size_t>(s * kRate); }

// ── Corpus: deterministic, license-clean (generated here) ──────────────────

// One-pole low/high-pass helpers (per channel state carried by the caller).
struct OnePole {
    double a = 0.0, z = 0.0;
    static OnePole lowpass(double hz) { return {std::exp(-2.0 * kPi * hz / kRate), 0.0}; }
    double lp(double x) { z = (1.0 - a) * x + a * z; return z; }
    double hp(double x) { return x - lp(x); }
};

// Paul Kellet's pink filter over seeded white noise, independent per channel.
inline Stereo pink(std::size_t n, unsigned seed, double amplitude = 0.05) {
    Stereo s = Stereo::zeros(n);
    for (int ch = 0; ch < 2; ++ch) {
        std::mt19937 rng(seed + static_cast<unsigned>(ch) * 7919u);
        std::normal_distribution<double> white(0.0, 1.0);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        auto& out = ch == 0 ? s.l : s.r;
        for (std::size_t i = 0; i < n; ++i) {
            const double w = white(rng);
            b0 = 0.99886 * b0 + w * 0.0555179;
            b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522;
            b5 = -0.7616 * b5 - w * 0.0168980;
            out[i] = static_cast<float>(amplitude
                * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362));
            b6 = w * 0.115926;
        }
    }
    return s;
}

// A bass line: band-limited saw notes (41..98 Hz) through two 250 Hz poles.
// Essentially nothing above 2 kHz: the case v1 gets most wrong.
inline Stereo bass_line(std::size_t n) {
    Stereo s = Stereo::zeros(n);
    const double notes[] = {41.20, 55.00, 61.74, 73.42, 49.00, 82.41, 65.41, 98.00};
    OnePole p1 = OnePole::lowpass(250.0), p2 = OnePole::lowpass(250.0);
    double phase = 0.0;
    const std::size_t note_len = seconds(0.25);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t note = (i / note_len) % 8;
        const double hz = notes[note];
        phase += hz / kRate;
        phase -= std::floor(phase);
        double saw = 0.0;
        for (int h = 1; h * hz < 4000.0; ++h)
            saw += std::sin(2.0 * kPi * h * phase) / h;
        const double t = static_cast<double>(i % note_len) / kRate;
        const double env = std::min(1.0, t / 0.005) * std::exp(-t * 3.0);
        const double v = 0.35 * p2.lp(p1.lp(saw * env));
        s.l[i] = s.r[i] = static_cast<float>(v);
    }
    return s;
}

// A vowel-like formant buzz on a 130 Hz glottal train: vocal-range material.
// A 1 % / 5 Hz vibrato on the glottal phase (accumulated, so the partials
// stay put on average).
inline Stereo vocal(std::size_t n) {
    Stereo s = Stereo::zeros(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double v = 0;
        const double vib = 1.0 + 0.01 * std::sin(2.0 * kPi * 5.0 * static_cast<double>(i) / kRate);
        phase += 130.0 * vib / kRate;
        phase -= std::floor(phase);
        for (int h = 1; h <= 40; ++h) {
            const double hz = 130.0 * h;
            const double formant = std::exp(-std::pow((hz - 700.0) / 150.0, 2))
                                 + 0.6 * std::exp(-std::pow((hz - 1200.0) / 200.0, 2))
                                 + 0.2 * std::exp(-std::pow((hz - 2600.0) / 300.0, 2));
            v += formant * std::sin(2.0 * kPi * h * phase);
        }
        s.l[i] = s.r[i] = static_cast<float>(0.15 * v);
    }
    return s;
}

// Hats / cymbals: high-passed noise bursts, 8 per second, plus a ride wash.
inline Stereo hats(std::size_t n, unsigned seed = 31u) {
    Stereo s = Stereo::zeros(n);
    for (int ch = 0; ch < 2; ++ch) {
        std::mt19937 rng(seed + static_cast<unsigned>(ch));
        std::normal_distribution<double> w(0.0, 1.0);
        OnePole a = OnePole::lowpass(6000.0), b = OnePole::lowpass(6000.0);
        auto& out = ch == 0 ? s.l : s.r;
        const std::size_t step = seconds(0.125);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i % step) / kRate;
            const double env = 0.08 + std::exp(-t * 40.0);
            out[i] = static_cast<float>(0.12 * env * b.hp(a.hp(w(rng))));
        }
    }
    return s;
}

// A pad: three detuned saw voices per note of a slow chord, low-passed.
inline Stereo pad(std::size_t n) {
    Stereo s = Stereo::zeros(n);
    const double chord[] = {220.0, 277.18, 329.63, 440.0};
    for (int ch = 0; ch < 2; ++ch) {
        OnePole p1 = OnePole::lowpass(1800.0), p2 = OnePole::lowpass(1800.0);
        auto& out = ch == 0 ? s.l : s.r;
        std::array<double, 12> phase{};
        for (std::size_t i = 0; i < n; ++i) {
            double v = 0.0;
            for (int note = 0; note < 4; ++note)
                for (int d = 0; d < 3; ++d) {
                    const double hz = chord[note] * (1.0 + 0.004 * (d - 1) + 0.001 * ch);
                    auto& ph = phase[static_cast<std::size_t>(note * 3 + d)];
                    ph += hz / kRate;
                    ph -= std::floor(ph);
                    v += 2.0 * ph - 1.0;
                }
            const double swell = 0.8 + 0.2 * std::sin(2.0 * kPi * 0.2 * static_cast<double>(i) / kRate);
            out[i] = static_cast<float>(0.05 * swell * p2.lp(p1.lp(v)));
        }
    }
    return s;
}

// A drum loop at 120 BPM: kick on the beat, snare on 2 and 4, closed hats on
// eighths.
inline Stereo drum_loop(std::size_t n, unsigned seed = 17u) {
    Stereo s = Stereo::zeros(n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    OnePole hat_a = OnePole::lowpass(7000.0);
    const std::size_t beat = seconds(0.5), eighth = seconds(0.25);
    for (std::size_t i = 0; i < n; ++i) {
        const double tb = static_cast<double>(i % beat) / kRate;
        const double te = static_cast<double>(i % eighth) / kRate;
        const std::size_t beat_index = (i / beat) % 4;
        const double kick = std::sin(2.0 * kPi * (50.0 * tb + 60.0 / 25.0 * (1.0 - std::exp(-tb * 25.0))))
                            * std::exp(-tb * 9.0);
        const double noise = u(rng);
        const double snare = (beat_index % 2 == 1)
            ? (0.6 * noise + 0.4 * std::sin(2.0 * kPi * 190.0 * tb)) * std::exp(-tb * 18.0) : 0.0;
        const double hat = hat_a.hp(noise) * std::exp(-te * 70.0);
        s.l[i] = s.r[i] = static_cast<float>(0.45 * kick + 0.25 * snare + 0.18 * hat);
    }
    return s;
}

inline Stereo sine(std::size_t n, double hz = 1000.0, double amplitude = 0.125) {
    Stereo s = Stereo::zeros(n);
    for (std::size_t i = 0; i < n; ++i)
        s.l[i] = s.r[i] = static_cast<float>(
            amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    return s;
}

// Pink noise with silent gaps: 3 s on, 2 s of digital silence.
inline Stereo pink_with_gaps(std::size_t n, unsigned seed = 77u) {
    Stereo s = pink(n, seed);
    for (std::size_t i = 0; i < n; ++i)
        if ((i % seconds(5.0)) >= seconds(3.0)) s.l[i] = s.r[i] = 0.0f;
    return s;
}

inline Stereo scaled(Stereo s, double gain);

// ── Dynamic material: sparse, alternating, decaying, breakdowns, swells ──

inline Stereo kick_only(std::size_t n) {
    Stereo s = Stereo::zeros(n);
    const std::size_t beat = seconds(0.5);
    for (std::size_t i = 0; i < n; ++i) {
        const double tb = static_cast<double>(i % beat) / kRate;
        const double kick = std::sin(2.0 * kPi * (50.0 * tb + 60.0 / 25.0 * (1.0 - std::exp(-tb * 25.0))))
                            * std::exp(-tb * 9.0);
        s.l[i] = s.r[i] = static_cast<float>(0.45 * kick);
    }
    return s;
}

// Piano-like hits every `period` s: harmonics decaying faster the higher.
inline Stereo piano_hits(std::size_t n, double period, double f0 = 220.0) {
    Stereo s = Stereo::zeros(n);
    const std::size_t p = seconds(period);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i % p) / kRate;
        double v = 0.0;
        for (int h = 1; h <= 24; ++h)
            v += std::sin(2.0 * kPi * f0 * h * t) / h * std::exp(-t * (0.8 + 1.2 * h));
        s.l[i] = s.r[i] = static_cast<float>(0.12 * v * std::min(1.0, t / 0.005));
    }
    return s;
}

// A drum hit with a reverb-like tail that darkens as it decays.
inline Stereo hit_tail(std::size_t n, double period, unsigned seed = 55u) {
    Stereo s = Stereo::zeros(n);
    const std::size_t p = seconds(period);
    for (int ch = 0; ch < 2; ++ch) {
        std::mt19937 rng(seed + static_cast<unsigned>(ch));
        std::normal_distribution<double> w(0.0, 1.0);
        double z = 0.0;
        auto& out = ch == 0 ? s.l : s.r;
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i % p) / kRate;
            const double cutoff = 8000.0 * std::exp(-t * 1.2) + 300.0;
            const double a = std::exp(-2.0 * kPi * cutoff / kRate);
            z = (1.0 - a) * w(rng) + a * z;
            const double env = std::exp(-t * 1.5);
            const double hit = std::sin(2.0 * kPi * 60.0 * t) * std::exp(-t * 20.0);
            out[i] = static_cast<float>(0.3 * hit + 0.25 * env * z);
        }
    }
    return s;
}

inline Stereo mixed(const Stereo& a, const Stereo& b, double gb = 1.0) {
    Stereo s = a;
    for (std::size_t i = 0; i < s.size() && i < b.size(); ++i) {
        s.l[i] = static_cast<float>(s.l[i] + gb * b.l[i]);
        s.r[i] = static_cast<float>(s.r[i] + gb * b.r[i]);
    }
    return s;
}

inline Stereo with_envelope(Stereo s, const std::function<double(double)>& env) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double g = env(static_cast<double>(i) / kRate);
        s.l[i] = static_cast<float>(s.l[i] * g);
        s.r[i] = static_cast<float>(s.r[i] * g);
    }
    return s;
}

inline Stereo concat(const Stereo& a, const Stereo& b);

// Kick-only and full-drum bars alternating every 2 s (starts kick-only).
inline Stereo kick_full_alternating(std::size_t bars = 9) {
    const auto bar = seconds(2.0);
    Stereo x = kick_only(bar);
    for (std::size_t k = 1; k < bars; ++k)
        x = concat(x, k % 2 == 1 ? drum_loop(bar, 30u + static_cast<unsigned>(k)) : kick_only(bar));
    return x;
}

// Dense mix 8 s -> pad-only breakdown 4 s -> dense 8 s.
inline Stereo dense_breakdown() {
    const auto n8 = seconds(8.0), n4 = seconds(4.0);
    const auto dense = [&](std::size_t n, unsigned seed) {
        return scaled(mixed(mixed(mixed(mixed(bass_line(n), drum_loop(n, seed), 0.8), pad(n), 1.5),
                                  vocal(n), 0.6), hats(n, seed + 5), 0.7), 0.6);
    };
    Stereo x = dense(n8, 17u);
    x = concat(x, scaled(pad(n4), 0.9));
    return concat(x, dense(n8, 23u));
}

// Arrangement sections: a quiet bass-heavy verse and a full chorus about
// 6 dB louder.
inline Stereo verse(std::size_t n) {
    return scaled(mixed(mixed(bass_line(n), pad(n), 1.2), kick_only(n), 0.6), 0.5);
}
inline Stereo chorus(std::size_t n, unsigned seed = 23u) {
    return scaled(scaled(mixed(mixed(mixed(mixed(bass_line(n), drum_loop(n, seed), 0.8), pad(n), 1.5),
                                     vocal(n), 0.6), hats(n, seed + 5), 0.7), 0.6), 1.6);
}

// A drum loop whose level swells +-12 dB over 4 s.
inline Stereo drum_swell(std::size_t n) {
    return with_envelope(drum_loop(n), [](double t) {
        return std::pow(10.0, 12.0 * std::sin(2.0 * kPi * t / 4.0) / 20.0); });
}

inline Stereo concat(const Stereo& a, const Stereo& b) {
    Stereo s = a;
    s.l.insert(s.l.end(), b.l.begin(), b.l.end());
    s.r.insert(s.r.end(), b.r.begin(), b.r.end());
    return s;
}

// ── Shapes ──────────────────────────────────────────────────────────────

struct Shape {
    std::string name;
    std::function<void(std::size_t, spectr::Band&)> f;
    float intensity = 100.0f;
    float mix = 100.0f;
};

// Band index (of 32 over 20 Hz..20 kHz) holding a frequency.
inline std::size_t band_of(double hz) {
    return static_cast<std::size_t>(std::floor(32.0 * std::log10(hz / 20.0) / 3.0));
}

inline Shape region(const std::string& name, std::size_t first, std::size_t last, float db) {
    return {name, [first, last, db](std::size_t i, spectr::Band& b) {
                b.gain_db = (i >= first && i <= last) ? db : 0.0f; }};
}

// Six regions (low/mid/high x broad/narrow) at +-6/12/24 dB, plus Intensity
// and Mix variations of two of them.
inline std::vector<Shape> sweep_shapes() {
    struct Region { const char* name; std::size_t a, b; };
    const Region regions[] = {
        {"low broad", 0, 9},      // 20 .. ~170 Hz
        {"low narrow", band_of(80.0), band_of(80.0)},
        {"mid broad", 12, 22},    // ~270 Hz .. ~2.7 kHz
        {"mid narrow", band_of(1000.0), band_of(1000.0)},
        {"high broad", 24, 31},   // ~6 kHz .. 20 kHz
        {"high narrow", band_of(10000.0), band_of(10000.0)},
    };
    std::vector<Shape> out;
    for (const auto& r : regions)
        for (const float db : {6.0f, 12.0f, 24.0f, -6.0f, -12.0f, -24.0f}) {
            char label[64];
            std::snprintf(label, sizeof(label), "%s %+.0f", r.name, db);
            out.push_back(region(label, r.a, r.b, db));
        }
    auto add = [&](Shape s, const char* suffix, float intensity, float mix) {
        s.name += suffix;
        s.intensity = intensity;
        s.mix = mix;
        out.push_back(std::move(s));
    };
    add(region("high broad +24", 24, 31, 24.0f), " @int50", 50.0f, 100.0f);
    add(region("high broad +24", 24, 31, 24.0f), " @mix50", 100.0f, 50.0f);
    add(region("low broad -24", 0, 9, -24.0f), " @int50", 50.0f, 100.0f);
    add(region("low broad -24", 0, 9, -24.0f), " @mix50", 100.0f, 50.0f);
    out.push_back({"tilt -12..+12", [](std::size_t i, spectr::Band& b) {
                       b.gain_db = -12.0f + 24.0f * static_cast<float>(i) / 31.0f; }});
    return out;
}

// ── Rendering ───────────────────────────────────────────────────────────

enum class Mode { off, v1, v2 };

inline const char* mode_name(Mode m) {
    return m == Mode::off ? "off" : (m == Mode::v1 ? "v1" : "v2");
}

struct RenderOptions {
    spectr::MaskRenderMode render_mode = spectr::kDefaultRenderMode;
    std::vector<int> chunks{};         // empty: kBlock
    std::size_t freeze_at = 0;         // > 0: engage Freeze at this sample
    std::size_t release_at = 0;        // > 0: release Freeze at this sample
    // Host resets (ProcessContext::reset_requested: play from stop, a
    // locate) at the first block boundary at or after each of these samples.
    std::vector<std::size_t> resets{};
    // Called after the parameters are set and before prepare (a restore).
    std::function<void(pulp::format::HeadlessHost&, spectr::Spectr&)> setup{};
    // Called after the last block (e.g. to save the session).
    std::function<void(pulp::format::HeadlessHost&, spectr::Spectr&)> finish{};
    // Called before every host block with the sample position and the plugin.
    std::function<void(std::size_t, pulp::format::HeadlessHost&, spectr::Spectr&)> before{};
};

struct Render {
    Stereo out;
    std::vector<float> applied_db;   // auto_gain_applied_db() after each block
    std::vector<std::size_t> block_end;  // sample position after each block
};

inline Render render(const Stereo& in, const Shape& shape, Mode mode,
                     const RenderOptions& options = {}) {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto* plugin = host.processor_as<spectr::Spectr>();
    (void)plugin->set_render_mode(options.render_mode);
    plugin->set_auto_gain_model(mode == Mode::v1 ? spectr::AutoGainModel::reference_v1
                                                 : spectr::AutoGainModel::material_v2);
    host.state().set_value(spectr::kParamAutoGain, mode == Mode::off ? 0.0f : 1.0f);
    host.state().set_value(spectr::kParamIntensity, shape.intensity);
    host.state().set_value(spectr::kMix, shape.mix);
    for (std::size_t i = 0; i < spectr::kMaxBands; ++i) {
        spectr::Band b{};
        if (shape.f) shape.f(i, b);
        host.state().set_value(spectr::band_gain_param_id(i), b.gain_db);
        host.state().set_value(spectr::band_mute_param_id(i), b.muted ? 1.0f : 0.0f);
    }
    if (options.setup) options.setup(host, *plugin);
    host.prepare(kRate, 4096);
    Render r;
    std::size_t next_reset = 0;
    r.out = Stereo::zeros(in.size());
    std::size_t pos = 0, chunk = 0;
    while (pos < in.size()) {
        if (options.freeze_at > 0 && pos >= options.freeze_at
            && (options.release_at == 0 || pos < options.release_at)
            && host.state().get_value(spectr::kParamFreeze) < 0.5f)
            host.state().set_value(spectr::kParamFreeze, 1.0f);
        if (options.release_at > 0 && pos >= options.release_at
            && host.state().get_value(spectr::kParamFreeze) >= 0.5f)
            host.state().set_value(spectr::kParamFreeze, 0.0f);
        if (options.before) options.before(pos, host, *plugin);
        // Diagnostic: AG_TRACE=1 prints the applied gain, the target and the
        // estimator's restart / level-drop counters every 0.5 s (AG_TRACE=f:
        // every 0.1 s).
        if (std::getenv("AG_TRACE") && (std::getenv("AG_TRACE")[0] == 'b'
                || (pos % seconds(std::getenv("AG_TRACE")[0] == 'f' ? 0.1 : 0.5))
                       < static_cast<std::size_t>(kBlock))) {
            const auto& sp = plugin->auto_gain_material().spectrum();
            std::printf("  t=%.2f applied %+.2f target %+.2f restarts %llu drops %llu merges %llu alt %d frozen %d legacy %d\n",
                        static_cast<double>(pos) / kRate, plugin->auto_gain_applied_db(),
                        plugin->auto_gain_material().target_db(),
                        static_cast<unsigned long long>(sp.restarts()),
                        static_cast<unsigned long long>(sp.level_drops()),
                        static_cast<unsigned long long>(sp.merges()), static_cast<int>(sp.alternating()),
                        static_cast<int>(host.state().get_value(spectr::kParamFreeze) >= 0.5f),
                        static_cast<int>(plugin->auto_gain_legacy_v1()));
        }
        int n = options.chunks.empty()
            ? kBlock : options.chunks[chunk++ % options.chunks.size()];
        n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(n), in.size() - pos));
        const float* ip[] = {in.l.data() + pos, in.r.data() + pos};
        float* op[] = {r.out.l.data() + pos, r.out.r.data() + pos};
        pulp::audio::BufferView<const float> iv(ip, 2, static_cast<std::size_t>(n));
        pulp::audio::BufferView<float> ov(op, 2, static_cast<std::size_t>(n));
        if (!options.resets.empty()) {
            pulp::format::ProcessContext ctx{};
            ctx.is_playing = true;
            ctx.sample_rate = kRate;
            if (next_reset < options.resets.size() && pos >= options.resets[next_reset]) {
                ctx.reset_requested = true;
                ++next_reset;
            }
            host.process(ov, iv, ctx);
        } else {
            host.process(ov, iv);
        }
        pos += static_cast<std::size_t>(n);
        r.applied_db.push_back(plugin->auto_gain_applied_db());
        r.block_end.push_back(pos);
    }
    if (options.finish) options.finish(host, *plugin);
    return r;
}

// ── Metrics ─────────────────────────────────────────────────────────────

// BS.1770 integrated loudness over [from, to) (gated, as a meter reads it).
inline double integrated_lufs(const Stereo& s, std::size_t from = 0,
                              std::size_t to = static_cast<std::size_t>(-1)) {
    to = std::min(to, s.size());
    pulp::signal::MultiChannelMeter meter;
    meter.prepare(kRate, 2);
    for (std::size_t pos = from; pos < to; pos += kBlock) {
        const auto n = static_cast<int>(std::min<std::size_t>(kBlock, to - pos));
        const float* p[] = {s.l.data() + pos, s.r.data() + pos};
        meter.process(p, 2, n);
    }
    return meter.snapshot().lufs_integrated;
}

// Short-term (3 s) or momentary (400 ms) loudness every block from `from`,
// once the window is full.
inline std::vector<double> loudness_series(const Stereo& s, std::size_t from,
                                           bool momentary,
                                           std::size_t to = static_cast<std::size_t>(-1)) {
    to = std::min(to, s.size());
    pulp::signal::MultiChannelMeter meter;
    meter.prepare(kRate, 2);
    std::vector<double> series;
    const std::size_t fill = momentary ? seconds(0.4) : seconds(3.0);
    for (std::size_t pos = from; pos < to; pos += kBlock) {
        const auto n = static_cast<int>(std::min<std::size_t>(kBlock, to - pos));
        const float* p[] = {s.l.data() + pos, s.r.data() + pos};
        meter.process(p, 2, n);
        if (pos - from >= fill) {
            const auto snap = meter.snapshot();
            series.push_back(momentary ? snap.lufs_momentary : snap.short_term_lufs);
        }
    }
    return series;
}

inline double stddev(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    const double m = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    double acc = 0;
    for (const double x : v) acc += (x - m) * (x - m);
    return std::sqrt(acc / static_cast<double>(v.size()));
}

inline double stddev(const std::vector<float>& v) {
    return stddev(std::vector<double>(v.begin(), v.end()));
}

inline double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = p * static_cast<double>(v.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(idx));
    const auto hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (idx - static_cast<double>(lo)) * (v[hi] - v[lo]);
}

// Finite and unclipped, via the audio-analysis library.
inline bool finite_and_sane(const Stereo& s, std::string* why = nullptr) {
    for (const auto* ch : {&s.l, &s.r}) {
        const float* p[] = {ch->data()};
        pulp::audio::BufferView<const float> view(p, 1, ch->size());
        const auto m = pulp::test::audio::analyze(view, kRate);
        const auto finite = pulp::test::audio::assert_no_nan_inf(m);
        if (!finite.passed) { if (why) *why = finite.message; return false; }
    }
    return true;
}

// Momentary loudness (400 ms) at the end of every block from the start;
// -inf until the window holds signal.
inline std::vector<double> momentary_from_start(const Stereo& s) {
    pulp::signal::MultiChannelMeter meter;
    meter.prepare(kRate, 2);
    std::vector<double> series;
    for (std::size_t pos = 0; pos < s.size(); pos += kBlock) {
        const auto n = static_cast<int>(std::min<std::size_t>(kBlock, s.size() - pos));
        const float* p[] = {s.l.data() + pos, s.r.data() + pos};
        meter.process(p, 2, n);
        series.push_back(meter.snapshot().lufs_momentary);
    }
    return series;
}

// How a transition went: the applied gain's time to within 1 dB of where it
// settles (its mean over the second before `settled_at`, or the end), and the
// largest momentary-
// loudness error against a reference render (AUTO off, flat shape: what a
// perfect Auto Gain would sound like) over [from, to). Both renders must use
// kBlock blocks.
struct Transition {
    double t1db = 0.0;
    double max_momentary_error = 0.0;
    double seconds_over_6lu = 0.0;   // time the momentary error exceeds 6 LU
    double settled_db = 0.0;
};

inline Transition transition(const Render& r, const Render& reference, double from_s,
                             double to_s, double settled_at_s = -1.0) {
    Transition t;
    std::size_t settled_block = r.applied_db.size() - 1;
    if (settled_at_s > 0.0)
        for (std::size_t b = 0; b < r.block_end.size(); ++b)
            if (r.block_end[b] >= seconds(settled_at_s)) { settled_block = b; break; }
    // Where it settles: the mean over the last second before `settled_at`
    // (a groove's estimate wobbles a little; one block would be arbitrary).
    {
        double sum = 0.0;
        int count = 0;
        for (std::size_t b = 0; b <= settled_block; ++b)
            if (r.block_end[b] + seconds(1.0) >= r.block_end[settled_block]) {
                sum += r.applied_db[b];
                ++count;
            }
        t.settled_db = count > 0 ? sum / count : r.applied_db[settled_block];
    }
    double last_outside = from_s;
    for (std::size_t b = 0; b <= settled_block; ++b) {
        const double at = static_cast<double>(r.block_end[b]) / kRate;
        if (at < from_s) continue;
        if (std::abs(r.applied_db[b] - t.settled_db) > 1.0) last_outside = at;
    }
    t.t1db = last_outside - from_s;
    const auto a = momentary_from_start(r.out), b = momentary_from_start(reference.out);
    for (std::size_t k = 0; k < std::min(a.size(), b.size()); ++k) {
        const double at = static_cast<double>((k + 1) * kBlock) / kRate;
        if (at < from_s || at >= to_s) continue;
        if (!std::isfinite(a[k]) || !std::isfinite(b[k]) || b[k] < -70.0) continue;
        const double e = std::abs(a[k] - b[k]);
        t.max_momentary_error = std::max(t.max_momentary_error, e);
        if (e > 6.0) t.seconds_over_6lu += static_cast<double>(kBlock) / kRate;
    }
    return t;
}

inline Stereo scaled(Stereo s, double gain) {
    for (auto& v : s.l) v = static_cast<float>(v * gain);
    for (auto& v : s.r) v = static_cast<float>(v * gain);
    return s;
}

// Loudness error, LU: output with AUTO vs the input, both over [from, end).
inline double loudness_error(const Stereo& in, const Render& r, std::size_t from) {
    return integrated_lufs(r.out, from) - integrated_lufs(in, from);
}

} // namespace autogain_harness
