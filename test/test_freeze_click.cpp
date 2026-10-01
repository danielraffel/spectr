// Freeze's engage and release on realistic material: is there a click?
//
// A steady tone is the kindest case for a crossfade -- the product's own
// kink rows in test_freeze.cpp use one. A user heard a click tapping Freeze
// on real material, so this file presses Freeze at many random moments in
// three license-safe synthetic programmes (a drum loop, a noisy chord, a
// speech-like voice), renders each press and the same segment unpressed, and
// scores every engage and release window with a discontinuity detector.
//
// THE DETECTOR. A click is energy the signal's own recent past does not
// predict, concentrated in a sample or two. Each onset fits a linear
// predictor (order 32) to the unpressed render's live audio just before the
// press; the same whitening filter is applied to the pressed and unpressed
// renders over the window, and every residual sample is scored against the
// residual's own RMS in the few milliseconds around it (excluding the
// sample's own millisecond). A predicted transient -- a drum hit the live
// input carries in both renders -- scores the same in both; an isolated
// discontinuity stands tens of dB above its neighbourhood.
//
// The unpressed render at the same onset is the control: its score over the
// same window is what the programme does by itself. A planted step in the
// control proves the detector sees a real discontinuity at a level people
// hear, and sets the audibility threshold's sanity check.

#include <catch2/catch_test_macros.hpp>

#include <pulp/format/headless.hpp>
#include <pulp/signal/realtime_pitch_time_processor.hpp>
#include <pulp/state/parameter_event_queue.hpp>

#include "spectr/freeze_source.hpp"
#include "spectr/spectr.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using spectr::FreezeSource;
using spectr::MaskRenderMode;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::size_t samples(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * kRate));
}

struct Stereo {
    std::vector<float> l, r;
    std::size_t size() const { return l.size(); }
    void resize(std::size_t n) { l.assign(n, 0.0f); r.assign(n, 0.0f); }
};

// ── Material ───────────────────────────────────────────────────────────────
// Deterministic synthesis only: no third-party audio, so no licence question.

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad bandpass(double hz, double q) {
        const double w = 2.0 * kPi * hz / kRate, alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = alpha / a0; f.b1 = 0.0; f.b2 = -alpha / a0;
        f.a1 = -2.0 * std::cos(w) / a0; f.a2 = (1.0 - alpha) / a0;
        return f;
    }
    void retune(double hz, double q) {
        const auto n = bandpass(hz, q);
        b0 = n.b0; b1 = n.b1; b2 = n.b2; a1 = n.a1; a2 = n.a2;
    }
    double operator()(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

void normalise(Stereo& s, double peak) {
    double m = 1e-9;
    for (std::size_t n = 0; n < s.size(); ++n)
        m = std::max({m, std::abs(double(s.l[n])), std::abs(double(s.r[n]))});
    const double g = peak / m;
    for (std::size_t n = 0; n < s.size(); ++n) { s.l[n] *= float(g); s.r[n] *= float(g); }
}

// A -60 dBFS noise floor under everything, as any recording has.
void add_room(Stereo& s, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, 0.001);
    for (std::size_t n = 0; n < s.size(); ++n) { s.l[n] += float(g(rng)); s.r[n] += float(g(rng)); }
}

// 120 BPM: kick on 1 and 3, snare on 2 and 4, closed hats on eighths with an
// open hat on the last one of each bar, and a sub bass line.
Stereo drum_loop(double seconds) {
    Stereo s; s.resize(samples(seconds));
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    const double beat = 0.5;
    const auto hit = [&](double at, auto&& voice, double length) {
        const auto start = samples(at);
        for (std::size_t i = 0; i < samples(length) && start + i < s.size(); ++i) {
            const double t = double(i) / kRate;
            const auto [l, r] = voice(t);
            s.l[start + i] += float(l);
            s.r[start + i] += float(r);
        }
    };
    for (double bar = 0.0; bar < seconds; bar += 4 * beat) {
        for (int b = 0; b < 4; ++b) {
            const double at = bar + b * beat;
            if (b % 2 == 0) {
                double phase = 0.0;
                hit(at, [&](double t) {
                    const double f = 48.0 + 110.0 * std::exp(-t / 0.035);
                    phase += 2.0 * kPi * f / kRate;
                    const double v = 0.9 * std::sin(phase) * std::exp(-t / 0.28)
                                   + (t < 0.003 ? 0.3 * u(rng) * (1.0 - t / 0.003) : 0.0);
                    return std::pair{v, v};
                }, 0.9);
            } else {
                Biquad body = Biquad::bandpass(1800.0, 0.7);
                hit(at, [&](double t) {
                    const double v = 0.55 * body(u(rng)) * std::exp(-t / 0.13)
                                   + 0.3 * std::sin(2.0 * kPi * 185.0 * t) * std::exp(-t / 0.07);
                    return std::pair{v * 0.95, v};
                }, 0.6);
            }
        }
        for (int e = 0; e < 8; ++e) {
            const double at = bar + e * beat / 2.0 + (e % 2 ? 0.012 : 0.0); // a little swing
            const double decay = e == 7 ? 0.18 : 0.025;
            double prev = 0.0;
            hit(at, [&](double t) {
                const double x = u(rng);
                const double v = 0.22 * (x - prev) * std::exp(-t / decay);
                prev = x;
                return std::pair{v * 0.7, v};
            }, decay * 6);
        }
        // Sub bass, one note per beat.
        static constexpr double notes[] = {55.0, 55.0, 65.41, 49.0};
        for (int b = 0; b < 4; ++b) {
            const double f = notes[b];
            hit(bar + b * beat, [&](double t) {
                const double env = std::min(1.0, t / 0.01) * std::exp(-t / 0.4);
                const double v = 0.35 * env * std::sin(2.0 * kPi * f * t);
                return std::pair{v, v};
            }, beat);
        }
    }
    normalise(s, 0.7);
    add_room(s, 12);
    return s;
}

// A detuned four-voice band-limited saw chord over filtered noise, swelling.
Stereo noisy_chord(double seconds) {
    Stereo s; s.resize(samples(seconds));
    static constexpr double chord[] = {220.0, 261.63, 329.63, 440.0};
    std::mt19937 rng(21);
    std::normal_distribution<double> g(0.0, 1.0);
    double lp_l = 0.0, lp_r = 0.0;
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / kRate;
        double l = 0.0, r = 0.0;
        for (int v = 0; v < 4; ++v) {
            const double vib = 1.0 + 0.003 * std::sin(2.0 * kPi * (5.1 + v * 0.3) * t);
            for (int side = 0; side < 2; ++side) {
                const double f = chord[v] * vib * (side ? 1.0035 : 0.9965);
                double saw = 0.0;
                for (int h = 1; h * f < 16000.0; ++h)
                    saw += std::sin(2.0 * kPi * h * f * t + v + side) / h;
                (side ? r : l) += 0.12 * saw;
            }
        }
        lp_l += 0.15 * (g(rng) - lp_l);
        lp_r += 0.15 * (g(rng) - lp_r);
        const double swell = 0.65 + 0.35 * std::sin(2.0 * kPi * 0.37 * t);
        s.l[n] = float(swell * (l + 0.35 * lp_l));
        s.r[n] = float(swell * (r + 0.35 * lp_r));
    }
    normalise(s, 0.6);
    add_room(s, 22);
    return s;
}

// A voice-like source: a glottal pulse train with a moving pitch through three
// formant resonators that step between vowels, syllable envelopes, and
// fricative noise in the gaps.
Stereo speech_like(double seconds) {
    Stereo s; s.resize(samples(seconds));
    static constexpr double vowels[][3] = {
        {730, 1090, 2440}, {270, 2290, 3010}, {300, 870, 2240},
        {530, 1840, 2480}, {570, 840, 2410}, {660, 1720, 2410}};
    std::mt19937 rng(31);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::uniform_int_distribution<int> pick(0, 5);
    std::array<Biquad, 3> formant{Biquad::bandpass(730, 8), Biquad::bandpass(1090, 10),
                                  Biquad::bandpass(2440, 12)};
    std::array<double, 3> f_now{730, 1090, 2440}, f_target = f_now;
    double glottal_phase = 0.0, glottal = 0.0, fric_prev = 0.0;
    double syllable_start = 0.0, syllable_length = 0.2;
    bool voiced = true;
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / kRate;
        if (t >= syllable_start + syllable_length) {
            syllable_start = t;
            voiced = (pick(rng) != 0);
            syllable_length = voiced ? 0.12 + 0.04 * pick(rng) : 0.06 + 0.01 * pick(rng);
            const auto& v = vowels[pick(rng)];
            for (int k = 0; k < 3; ++k) f_target[k] = v[k];
        }
        for (int k = 0; k < 3; ++k) {
            f_now[k] += 0.004 * (f_target[k] - f_now[k]);
            if (n % 32 == 0) formant[k].retune(f_now[k], 6.0 + 3.0 * k);
        }
        const double f0 = 125.0 + 30.0 * std::sin(2.0 * kPi * 0.6 * t)
                        + 12.0 * std::sin(2.0 * kPi * 3.1 * t);
        glottal_phase += f0 / kRate;
        double pulse = 0.0;
        if (glottal_phase >= 1.0) { glottal_phase -= 1.0; pulse = 1.0; }
        glottal = 0.96 * glottal + pulse;               // a soft glottal shape
        const double into = glottal;
        const double local = t - syllable_start;
        const double env = std::min(1.0, local / 0.02)
                         * std::min(1.0, (syllable_length - local) / 0.03);
        double v = 0.0;
        if (voiced)
            v = env * (formant[0](into) * 1.0 + formant[1](into) * 0.6 + formant[2](into) * 0.35);
        else {
            const double x = u(rng);
            v = env * 0.25 * (x - fric_prev);
            fric_prev = x;
            (void)formant[0](0.0); (void)formant[1](0.0); (void)formant[2](0.0);
        }
        s.l[n] = float(v);
        s.r[n] = float(v);
    }
    normalise(s, 0.6);
    add_room(s, 32);
    return s;
}

// ── The detector ───────────────────────────────────────────────────────────

constexpr int kOrder = 32;

/// Prediction-error filter [1, a1..ap] fitted to x[from, to) (Hann windowed
/// autocorrelation, Levinson-Durbin, a tiny ridge so silence is well posed).
std::vector<double> fit_whitener(const std::vector<float>& x, std::size_t from, std::size_t to) {
    const std::size_t n = to - from;
    std::vector<double> w(n);
    for (std::size_t i = 0; i < n; ++i)
        w[i] = x[from + i] * (0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1)));
    std::vector<double> r(kOrder + 1, 0.0);
    for (int k = 0; k <= kOrder; ++k)
        for (std::size_t i = std::size_t(k); i < n; ++i) r[std::size_t(k)] += w[i] * w[i - std::size_t(k)];
    r[0] = r[0] * (1.0 + 1e-6) + 1e-12;
    std::vector<double> a(kOrder + 1, 0.0), tmp(kOrder + 1);
    a[0] = 1.0;
    double err = r[0];
    for (int i = 1; i <= kOrder; ++i) {
        double acc = r[std::size_t(i)];
        for (int j = 1; j < i; ++j) acc += a[std::size_t(j)] * r[std::size_t(i - j)];
        const double k = -acc / err;
        tmp = a;
        for (int j = 1; j < i; ++j) a[std::size_t(j)] = tmp[std::size_t(j)] + k * tmp[std::size_t(i - j)];
        a[std::size_t(i)] = k;
        err *= (1.0 - k * k);
        if (err <= 0.0) break;
    }
    return a;
}

std::vector<double> residual(const std::vector<float>& x, const std::vector<double>& a,
                             std::size_t from, std::size_t to) {
    std::vector<double> e(to - from, 0.0);
    for (std::size_t n = from; n < to; ++n) {
        double acc = 0.0;
        for (int k = 0; k <= kOrder && n >= std::size_t(k); ++k)
            acc += a[std::size_t(k)] * x[n - std::size_t(k)];
        e[n - from] = acc;
    }
    return e;
}

/// Worst ratio, in dB, of a residual sample to the RMS of the residual in the
/// 4 ms either side of it, excluding the 0.5 ms nearest it. Scored over
/// `e[lead, e.size() - lead)`, so every scored sample has a full neighbourhood.
double spike_db(const std::vector<double>& e, std::size_t lead, std::size_t* where = nullptr) {
    const std::size_t outer = samples(0.004), inner = samples(0.0005);
    std::vector<double> prefix(e.size() + 1, 0.0);
    for (std::size_t i = 0; i < e.size(); ++i) prefix[i + 1] = prefix[i] + e[i] * e[i];
    const auto energy = [&](std::size_t a, std::size_t b) { return prefix[b] - prefix[a]; };
    double worst = -200.0;
    for (std::size_t n = std::max(lead, outer); n + outer < e.size() && n + lead < e.size(); ++n) {
        const double around = energy(n - outer, n - inner) + energy(n + inner + 1, n + outer + 1);
        const double rms = std::sqrt(around / double(2 * (outer - inner)) + 1e-24);
        const double db = 20.0 * std::log10(std::abs(e[n]) / rms + 1e-12);
        if (db > worst) { worst = db; if (where) *where = n; }
    }
    return worst;
}

/// 1 ms energies of the first difference (an HF emphasis) of x over [from, from + n).
std::vector<double> hf_envelope(const std::vector<float>& x, std::size_t from, std::size_t n) {
    const std::size_t w = samples(0.001);
    std::vector<double> e;
    for (std::size_t s = from; s + w <= from + n; s += w) {
        double acc = 0;
        for (std::size_t i = s; i < s + w; ++i) { const double d = double(x[i]) - x[i - 1]; acc += d * d; }
        e.push_back(std::log10(acc + 1e-12));
    }
    return e;
}
double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    double ma = 0, mb = 0;
    for (std::size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= double(n); mb /= double(n);
    double sab = 0, saa = 0, sbb = 0;
    for (std::size_t i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt(saa * sbb + 1e-30);
}

struct Score { double spike = -200.0; double overshoot = -200.0; double ghost = -1.0; };

/// The span of output one edge can touch, relative to where the edge reaches
/// the output: from `before` samples ahead of it to `after` past it.
struct Span { std::size_t before = 0; std::size_t after = 0; };

constexpr int kFrame = 512;
constexpr int kFrameHop = 128;

/// Power spectra of Hann frames of `x` starting at each of `starts`.
std::vector<std::vector<double>> frames_power(const std::vector<float>& x,
                                              const std::vector<std::size_t>& starts) {
    static const pulp::signal::Fft fft(kFrame);
    std::vector<float> buf(kFrame);
    std::vector<std::complex<float>> spec(kFrame);
    std::vector<std::vector<double>> out;
    for (auto s : starts) {
        for (int i = 0; i < kFrame; ++i)
            buf[std::size_t(i)] = x[s + std::size_t(i)]
                * float(0.5 - 0.5 * std::cos(2.0 * kPi * i / kFrame));
        fft.forward_real(buf.data(), spec.data());
        std::vector<double> p(kFrame / 2 + 1);
        for (int k = 0; k <= kFrame / 2; ++k) p[std::size_t(k)] = std::norm(std::complex<double>(spec[std::size_t(k)]));
        out.push_back(std::move(p));
    }
    return out;
}

/// SPECTRAL OVERSHOOT. Across a transition from live input into a held
/// sound, every short frame of the output should be explained by those two:
/// per bin, no more than a coherent sum of the live frame and the hold. The
/// hold is stationary, so its mean spectrum (measured where the output is
/// held) stands in for it, with headroom for a frame's own fluctuation.
/// Energy beyond that bound -- modulation sidebands, a broadband click --
/// is scored, per frame, against the frame's explained energy; the worst
/// frame is the edge's score, in dB.
double overshoot_db(const std::vector<float>& x, const std::vector<float>& live,
                    const std::vector<double>& hold, std::size_t from, std::size_t to) {
    std::vector<std::size_t> starts;
    for (std::size_t s = from; s + kFrame <= to; s += kFrameHop) starts.push_back(s);
    const auto px = frames_power(x, starts);
    const auto pl = frames_power(live, starts);
    double worst = -200.0;
    for (std::size_t f = 0; f < starts.size(); ++f) {
        double excess = 0.0, explained = 0.0;
        for (std::size_t k = 1; k < px[f].size(); ++k) {
            const double bound = 2.0 * (pl[f][k] + 6.0 * hold[k]);
            excess += std::max(0.0, px[f][k] - bound);
            explained += pl[f][k] + hold[k];
        }
        worst = std::max(worst, 10.0 * std::log10(excess / (explained + 1e-30) + 1e-12));
    }
    return worst;
}

/// Mean frame power spectrum of `x` over [from, to).
std::vector<double> mean_power(const std::vector<float>& x, std::size_t from, std::size_t to) {
    std::vector<std::size_t> starts;
    for (std::size_t s = from; s + kFrame <= to; s += kFrameHop) starts.push_back(s);
    const auto p = frames_power(x, starts);
    std::vector<double> m(kFrame / 2 + 1, 0.0);
    for (const auto& f : p) for (std::size_t k = 0; k < m.size(); ++k) m[k] += f[k] / double(p.size());
    return m;
}

/// Score `x` over [edge - span.before, edge + span.after): the waveform spike
/// (whitened by a predictor fitted to `live` over the 300 ms before
/// `fit_end`) and the spectral overshoot against `live` and the hold heard
/// in `held` over [held_from, held_from + 0.4 s).
Score score(const Stereo& x, const Stereo& live, const Stereo& held, std::size_t edge,
            Span span, std::size_t fit_end, std::size_t held_from, std::size_t ghost_lag = 0) {
    Score out;
    // GHOST: does the window after the edge replay the window before it? The
    // HF envelope correlation at the lag of the variant's analysis window.
    if (ghost_lag > 0 && edge >= ghost_lag + 1 && edge + ghost_lag <= x.size())
        out.ghost = std::max(correlation(hf_envelope(x.l, edge, ghost_lag), hf_envelope(x.l, edge - ghost_lag, ghost_lag)),
                             correlation(hf_envelope(x.r, edge, ghost_lag), hf_envelope(x.r, edge - ghost_lag, ghost_lag)));
    const std::size_t lead = samples(0.035);
    const std::size_t from = edge - span.before - lead;
    const std::size_t to = std::min(x.size(), edge + span.after + lead);
    for (int side = 0; side < 2; ++side) {
        const auto& v = side ? x.r : x.l;
        const auto& c = side ? live.r : live.l;
        const auto a = fit_whitener(c, fit_end - samples(0.3), fit_end);
        out.spike = std::max(out.spike, spike_db(residual(v, a, from, to), lead));
        const auto hold = mean_power(side ? held.r : held.l, held_from, held_from + samples(0.4));
        out.overshoot = std::max(out.overshoot,
            overshoot_db(v, c, hold, edge - span.before, edge + span.after));
    }
    return out;
}

// ── Renderers ──────────────────────────────────────────────────────────────

/// A segment of programme and when, inside it, Freeze goes on and off.
struct Take {
    const Stereo* programme = nullptr;
    std::size_t start = 0;   // programme sample the render starts at
    std::size_t length = 0;
    std::size_t press = 0;   // render-relative
    std::size_t release = 0; // render-relative
};

using Render = std::function<Stereo(const Take&, bool pressed)>;

/// The Spectr processor under a HeadlessHost; Freeze rides a sample-accurate
/// parameter event, as a host's automation would deliver a tap.
struct ProcessorRender {
    MaskRenderMode mode;
    float mix;
    int latency = 0;
    Stereo operator()(const Take& take, bool pressed) {
        pulp::format::HeadlessHost host{spectr::create_spectr};
        auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
        REQUIRE(plugin != nullptr);
        REQUIRE(plugin->set_render_mode(mode));
        constexpr int block = 256;
        host.prepare(kRate, block);
        host.state().set_value(spectr::kMix, mix);
        latency = plugin->latency_samples();
        Stereo out; out.resize(take.length);
        pulp::midi::MidiBuffer mi, mo;
        pulp::audio::Buffer<float> in(2, block), buf(2, block);
        for (std::size_t pos = 0; pos < take.length; pos += block) {
            const auto n = std::min<std::size_t>(block, take.length - pos);
            pulp::state::ParameterEventQueue events;
            pulp::format::ProcessContext ctx;
            if (pressed) {
                if (take.press >= pos && take.press < pos + n)
                    REQUIRE(events.push({spectr::kParamFreeze, std::int32_t(take.press - pos), 1.0f, 0}));
                if (take.release >= pos && take.release < pos + n)
                    REQUIRE(events.push({spectr::kParamFreeze, std::int32_t(take.release - pos), 0.0f, 0}));
            }
            pulp::audio::Buffer<float> inb(2, n), outb(2, n);
            for (std::size_t i = 0; i < n; ++i) {
                inb.channel(0)[i] = take.programme->l[take.start + pos + i];
                inb.channel(1)[i] = take.programme->r[take.start + pos + i];
            }
            const float* ip[] = {inb.channel(0).data(), inb.channel(1).data()};
            pulp::audio::BufferView<const float> iv(ip, 2, n);
            auto ov = outb.view();
            host.process(ov, iv, mi, mo, events, ctx);
            // What the adapter commits once the block is done.
            if (pressed) {
                if (take.press >= pos && take.press < pos + n) host.state().set_value(spectr::kParamFreeze, 1.0f);
                if (take.release >= pos && take.release < pos + n) host.state().set_value(spectr::kParamFreeze, 0.0f);
            }
            std::copy(outb.channel(0).begin(), outb.channel(0).end(), out.l.begin() + long(pos));
            std::copy(outb.channel(1).begin(), outb.channel(1).end(), out.r.begin() + long(pos));
        }
        return out;
    }
};

/// Split a take into blocks of at most `block` that also break at the press
/// and the release, calling `step(pos, n, frozen)`.
template <typename Step>
void walk(const Take& take, bool pressed, int block, Step&& step) {
    std::size_t pos = 0;
    while (pos < take.length) {
        std::size_t end = std::min<std::size_t>(pos + std::size_t(block), take.length);
        for (auto edge : {take.press, take.release})
            if (edge > pos && edge < end) end = edge;
        const bool frozen = pressed && pos >= take.press && pos < take.release;
        step(pos, end - pos, frozen);
        pos = end;
    }
}

/// bendr-pulp's freeze, the reference the product is compared with: the
/// RealtimePitchTimeProcessor configured as bendr-pulp configures it, at unity
/// pitch, frozen with set_frozen() before each block.
struct BendrReferenceRender {
    int latency = 0;
    Stereo operator()(const Take& take, bool pressed) {
        pulp::signal::RealtimePitchTimeProcessor pitch;
        pulp::signal::RealtimePitchTimeConfig config;
        config.quality = pulp::signal::PitchTimeQuality::quality;
        config.channels = 2;
        config.max_block = 256;
        config.formant_mode = pulp::signal::FormantMode::preserve;
        config.noise_morphing = true;
        REQUIRE(pitch.prepare(kRate, config) == pulp::signal::PitchTimePrepareStatus::prepared);
        pitch.set_pitch_semitones(0.0f);
        pitch.set_formant_semitones(0.0f);
        latency = pitch.latency_samples();
        Stereo out; out.resize(take.length);
        walk(take, pressed, 256, [&](std::size_t pos, std::size_t n, bool frozen) {
            pitch.set_frozen(frozen);
            const float* in[] = {take.programme->l.data() + take.start + pos,
                                 take.programme->r.data() + take.start + pos};
            float* o[] = {out.l.data() + pos, out.r.data() + pos};
            pitch.process(in, o, int(n));
        });
        return out;
    }
};

/// The freeze source alone (the product's wet source, before the mask).
struct SourceRender {
    int crossfade = 0;
    Stereo operator()(const Take& take, bool pressed) {
        FreezeSource source;
        REQUIRE(source.prepare(kRate, 2));
        if (crossfade > 0) source.set_crossfade_samples(crossfade);
        Stereo out; out.resize(take.length);
        walk(take, pressed, 256, [&](std::size_t pos, std::size_t n, bool frozen) {
            source.set_frozen(frozen);
            const float* in[] = {take.programme->l.data() + take.start + pos,
                                 take.programme->r.data() + take.start + pos};
            float* o[] = {out.l.data() + pos, out.r.data() + pos};
            source.process_block(in, o, 2, int(n));
        });
        return out;
    }
};

/// The bendr-pulp-shaped alternative for Spectr: one continuous analysis ->
/// FreezeHold -> resynthesis stream that always runs, frozen with
/// set_frozen() and heard as the wet signal, with no time-domain crossfade,
/// pre-roll or level match -- FreezeHold's own frame-domain fade is the only
/// transition. Same geometry as the product's source (8192 / 512). Its
/// output is the input delayed by one analysis window.
class ContinuousFreezeStream {
public:
    static constexpr int kN = FreezeSource::kFftSize;
    static constexpr int kHop = FreezeSource::kHop;
    void prepare() {
        pulp::signal::FreezeHold::Config config;
        config.fft_size = kN;
        config.channels = 2;
        config.analysis_hop = kHop;
        config.sample_rate = kRate;
        config.capture_seconds = FreezeSource::kDefaultHoldSeconds;
        hold_.prepare(config);
        fft_ = pulp::signal::Fft(kN);
        window_.resize(kN);
        for (int n = 0; n < kN; ++n) window_[std::size_t(n)] = float(0.5 - 0.5 * std::cos(2 * kPi * n / kN));
        double overlap = 0;
        for (int n = 0; n < kN; n += kHop) overlap += double(window_[std::size_t(n)]) * window_[std::size_t(n)];
        scale_ = float(1.0 / overlap);
        for (int ch = 0; ch < 2; ++ch) {
            ring_[ch].assign(kN, 0.0f); ola_[ch].assign(kN, 0.0f); spec_[ch].assign(kN, {});
        }
        time_.assign(kN, {}); scratch_.assign(kN, 0.0f);
    }
    int latency() const { return kN; }
    void set_frozen(bool f) { hold_.set_frozen(f); }
    void process(const float* const* in, float* const* out, int n) {
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                ring_[ch][pos_] = in[ch][i];
                out[ch][i] = ola_[ch][std::size_t(hop_pos_)];
            }
            pos_ = (pos_ + 1) % std::size_t(kN);
            if (++hop_pos_ == kHop) { hop_pos_ = 0; boundary(); }
        }
    }
private:
    void boundary() {
        for (int ch = 0; ch < 2; ++ch) {
            auto& o = ola_[ch];
            std::copy(o.begin() + kHop, o.end(), o.begin());
            std::fill(o.end() - kHop, o.end(), 0.0f);
            for (int k = 0; k < kN; ++k)
                scratch_[std::size_t(k)] = ring_[ch][(pos_ + std::size_t(k)) % std::size_t(kN)] * window_[std::size_t(k)];
            fft_.forward_real(scratch_.data(), spec_[ch].data());
        }
        std::complex<float>* frames[] = {spec_[0].data(), spec_[1].data()};
        hold_.process_group(frames, 2, kN / 2 + 1);
        for (int ch = 0; ch < 2; ++ch) {
            for (int k = 0; k <= kN / 2; ++k) {
                time_[std::size_t(k)] = spec_[ch][std::size_t(k)];
                if (k > 0 && k < kN / 2) time_[std::size_t(kN - k)] = std::conj(spec_[ch][std::size_t(k)]);
            }
            time_[0] = {spec_[ch][0].real(), 0.0f};
            time_[std::size_t(kN / 2)] = {spec_[ch][std::size_t(kN / 2)].real(), 0.0f};
            fft_.inverse(time_.data());
            for (int k = 0; k < kN; ++k)
                ola_[ch][std::size_t(k)] += time_[std::size_t(k)].real() * window_[std::size_t(k)] * scale_;
        }
    }
    pulp::signal::FreezeHold hold_;
    pulp::signal::Fft fft_{};
    std::vector<float> window_, scratch_;
    std::vector<std::complex<float>> time_;
    std::array<std::vector<float>, 2> ring_, ola_;
    std::array<std::vector<std::complex<float>>, 2> spec_;
    float scale_ = 1.0f;
    std::size_t pos_ = 0;
    int hop_pos_ = 0;
};

struct StreamRender {
    int latency = ContinuousFreezeStream::kN;
    Stereo operator()(const Take& take, bool pressed) {
        ContinuousFreezeStream stream;
        stream.prepare();
        Stereo out; out.resize(take.length);
        walk(take, pressed, 256, [&](std::size_t pos, std::size_t n, bool frozen) {
            stream.set_frozen(frozen);
            const float* in[] = {take.programme->l.data() + take.start + pos,
                                 take.programme->r.data() + take.start + pos};
            float* o[] = {out.l.data() + pos, out.r.data() + pos};
            stream.process(in, o, int(n));
        });
        return out;
    }
};

// ── Sweeps ─────────────────────────────────────────────────────────────────

struct Programmes {
    Stereo drums = drum_loop(9.0);
    Stereo chord = noisy_chord(9.0);
    Stereo voice = speech_like(9.0);
    const Stereo* at(int i) const { return i == 0 ? &drums : i == 1 ? &chord : &voice; }
    static const char* name(int i) { return i == 0 ? "drums" : i == 1 ? "chord" : "voice"; }
};

const Programmes& programmes() {
    static const Programmes p;
    return p;
}

struct Onset {
    int programme = 0;
    std::size_t press = 0; // programme sample
    Score engage_pressed, engage_control, engage_planted, engage_ideal, engage_steady;
    Score release_pressed, release_control, release_ideal;
};

/// A -30 dBFS waveform step at `at`: what a crossfade that jumps between two
/// unrelated signals leaves behind, at a level plainly audible over this
/// material on headphones. The detector's positive control.
Stereo plant_step(Stereo x, std::size_t at) {
    for (std::size_t n = at; n < x.size(); ++n) { x.l[n] += 0.0316f; x.r[n] += 0.0316f; }
    return x;
}

/// A click-free transition: a 48 ms equal-power crossfade, starting at `at`,
/// from `x` into `hold` -- the same variant's own held output, taken from
/// where it is steady. What an ideal engage into that hold would read.
Stereo ideal_transition(Stereo x, const Stereo& hold, std::size_t at, std::size_t hold_from) {
    const std::size_t fade = samples(0.048);
    for (std::size_t n = at; n < x.size() && hold_from + (n - at) < hold.size(); ++n) {
        const double p = std::min(1.0, double(n - at) / double(fade));
        const double gl = std::cos(p * kPi / 2), gh = std::sin(p * kPi / 2);
        x.l[n] = float(gl * x.l[n] + gh * hold.l[hold_from + (n - at)]);
        x.r[n] = float(gl * x.r[n] + gh * hold.r[hold_from + (n - at)]);
    }
    return x;
}

/// Press at `count` random moments (seeded), release `hold` seconds later.
/// Each edge is scored over `span` around where it reaches the output, which
/// is `latency` samples after the press or release.
std::vector<Onset> sweep(const Render& render, const std::function<int()>& latency,
                         int count, unsigned seed, Span span, std::size_t ghost_lag,
                         double hold = 0.7) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> where(1.3, 6.8);
    std::vector<Onset> out;
    for (int i = 0; i < count; ++i) {
        Onset o;
        o.programme = i % 3;
        const auto absolute = samples(where(rng));
        Take take;
        take.programme = programmes().at(o.programme);
        take.start = absolute - samples(1.2);
        take.press = samples(1.2);
        take.release = take.press + samples(hold);
        take.length = take.release + span.after + samples(0.1) + 20000;
        const auto pressed = render(take, true);
        const auto control = render(take, false);
        const auto l = std::size_t(latency());
        const auto engage = take.press + l, release = take.release + l;
        // Where the output is steadily held: well past the engage, before
        // the release.
        const std::size_t held_from = engage + span.after + samples(0.1);
        REQUIRE(held_from + samples(0.45) < release);
        const auto sc = [&](const Stereo& x, std::size_t edge) {
            return score(x, control, pressed, edge, span, engage, held_from, ghost_lag);
        };
        o.engage_pressed = sc(pressed, engage);
        o.engage_control = sc(control, engage);
        o.engage_planted = sc(plant_step(control, engage + span.after / 2), engage);
        o.engage_ideal = sc(ideal_transition(control, pressed, engage, held_from), engage);
        // The hold alone, scored as if it were an edge with nothing live
        // under it: its own frame-to-frame fluctuation against its mean.
        Stereo silent; silent.resize(pressed.size());
        o.engage_steady = score(pressed, pressed, pressed, held_from, span, held_from, held_from);
        o.engage_steady.overshoot = std::max(
            overshoot_db(pressed.l, silent.l, mean_power(pressed.l, held_from, held_from + samples(0.4)),
                         held_from - span.before, held_from + span.after),
            overshoot_db(pressed.r, silent.r, mean_power(pressed.r, held_from, held_from + samples(0.4)),
                         held_from - span.before, held_from + span.after));
        o.release_pressed = sc(pressed, release);
        o.release_control = sc(control, release);
        // An ideal release: the same hold, never released, faded out into
        // the live input by a plain equal-power fade from the release on.
        // Where the hold's partials meet the same partials still playing
        // live, even that fade reads above the hold's own fluctuation.
        {
            Take held_on = take;
            held_on.release = take.length + 1;
            const auto continued = render(held_on, true);
            Stereo ideal = continued;
            const std::size_t fade = samples(FreezeSource::kCrossfadeSeconds);
            // From where the pressed render's own fade begins: the freeze
            // source's first hop boundary after the release (its hops count
            // from the start of the stream), plus the latency.
            const auto hop = std::size_t(FreezeSource::kHop);
            const std::size_t begins = (take.release + hop - 1) / hop * hop + l - 1;
            for (std::size_t n = begins; n < ideal.size(); ++n) {
                const double q = std::min(1.0, double(n - begins) / double(fade));
                const double gh = std::cos(q * kPi / 2), gl = std::sin(q * kPi / 2);
                ideal.l[n] = float(gh * continued.l[n] + gl * control.l[n]);
                ideal.r[n] = float(gh * continued.r[n] + gl * control.r[n]);
            }
            o.release_ideal = sc(ideal, release);
        }
        o.press = absolute;
        out.push_back(o);
    }
    return out;
}

double quantile(std::vector<double> v, double q) {
    std::sort(v.begin(), v.end());
    const double pos = q * double(v.size() - 1);
    const auto i = std::size_t(pos);
    const double f = pos - double(i);
    return i + 1 < v.size() ? v[i] * (1 - f) + v[i + 1] * f : v[i];
}

struct Distribution {
    double max = 0, p95 = 0, median = 0;
    int above = 0;
    std::string text(double threshold) const {
        char b[160];
        std::snprintf(b, sizeof b, "max %6.2f  p95 %6.2f  median %6.2f  above %.1f dB: %d",
                      max, p95, median, threshold, above);
        return b;
    }
};

Distribution distribution(const std::vector<double>& v, double threshold) {
    Distribution d;
    d.max = *std::max_element(v.begin(), v.end());
    d.p95 = quantile(v, 0.95);
    d.median = quantile(v, 0.5);
    d.above = int(std::count_if(v.begin(), v.end(), [&](double x) { return x > threshold; }));
    return d;
}

std::vector<double> column(const std::vector<Onset>& onsets, Score Onset::*edge, double Score::*metric) {
    std::vector<double> v;
    for (const auto& o : onsets) v.push_back(o.*edge.*metric);
    return v;
}

const char* mode_label(MaskRenderMode mode) {
    return mode == MaskRenderMode::linear_phase ? "Mixing" : "Tracking";
}

int onset_count() {
    if (const char* env = std::getenv("SPECTR_FREEZE_CLICK_ONSETS")) return std::max(3, std::atoi(env));
    return 200;
}


// ── Report ─────────────────────────────────────────────────────────────────

void report(const char* label, const std::vector<Onset>& onsets) {
    std::printf("\n== %s (%zu onsets)\n", label, onsets.size());
    for (auto metric : {&Score::spike, &Score::overshoot}) {
        const bool spike = metric == &Score::spike;
        // Spike: the no-freeze control's worst + 3 dB. Overshoot is zero on
        // the control by construction, so its band is the ideal fade into
        // the same hold, and the hold's own frame-to-frame fluctuation.
        const auto a = column(onsets, spike ? &Onset::engage_control : &Onset::engage_ideal, metric);
        const auto b = column(onsets, spike ? &Onset::release_control : &Onset::engage_steady, metric);
        double threshold = std::max(*std::max_element(a.begin(), a.end()),
                                    *std::max_element(b.begin(), b.end())) + 3.0;
        std::printf("  [%s] threshold = %s + 3 dB = %.2f dB\n", spike ? "spike" : "overshoot",
                    spike ? "no-freeze control max" : "max(ideal fade, steady hold)", threshold);
        const std::pair<const char*, Score Onset::*> rows[] = {
            {"engage  control ", &Onset::engage_control},
            {"engage  planted ", &Onset::engage_planted},
            {"engage  ideal xf", &Onset::engage_ideal},
            {"steady  hold    ", &Onset::engage_steady},
            {"engage  PRESSED ", &Onset::engage_pressed},
            {"release control ", &Onset::release_control},
            {"release ideal xf", &Onset::release_ideal},
            {"release PRESSED ", &Onset::release_pressed}};
        for (const auto& [name, edge] : rows)
            std::printf("    %s %s\n", name,
                        distribution(column(onsets, edge, metric), threshold).text(threshold).c_str());
        for (const auto& o : onsets)
            if (o.engage_pressed.*metric > threshold || o.release_pressed.*metric > threshold)
                std::printf("      over: %s at %.4f s  engage %.2f (control %.2f)  release %.2f (control %.2f)\n",
                            Programmes::name(o.programme), double(o.press) / kRate,
                            o.engage_pressed.*metric, o.engage_control.*metric,
                            o.release_pressed.*metric, o.release_control.*metric);
    }
    {
        const auto c = column(onsets, &Onset::engage_control, &Score::ghost);
        const auto i = column(onsets, &Onset::engage_ideal, &Score::ghost);
        const double threshold = std::max(*std::max_element(c.begin(), c.end()),
                                          *std::max_element(i.begin(), i.end()));
        std::printf("  [ghost] HF-envelope correlation of the window after the edge with the one before; threshold = max(no-freeze control, ideal fade) = %.2f\n", threshold);
        const std::pair<const char*, Score Onset::*> rows[] = {
            {"engage  control ", &Onset::engage_control},
            {"engage  ideal xf", &Onset::engage_ideal},
            {"engage  PRESSED ", &Onset::engage_pressed},
            {"release control ", &Onset::release_control},
            {"release PRESSED ", &Onset::release_pressed}};
        for (const auto& [name, edge] : rows)
            std::printf("    %s %s\n", name,
                        distribution(column(onsets, edge, &Score::ghost), threshold).text(threshold).c_str());
        for (const auto& o : onsets)
            if (o.engage_pressed.ghost > threshold || o.release_pressed.ghost > threshold)
                std::printf("      over: %s at %.4f s  engage %.2f (control %.2f, ideal fade %.2f)  release %.2f (control %.2f)\n",
                            Programmes::name(o.programme), double(o.press) / kRate,
                            o.engage_pressed.ghost, o.engage_control.ghost, o.engage_ideal.ghost,
                            o.release_pressed.ghost, o.release_control.ghost);
    }
    std::fflush(stdout);
}

} // namespace

TEST_CASE("Freeze click report: every variant on realistic material",
          "[.][freeze-click-report]") {
    const int count = onset_count();
    const char* only = std::getenv("SPECTR_FREEZE_CLICK_ONLY");
    const auto want = [&](const char* name) { return !only || std::string(only) == name; };
    // Spectr's edge reaches the output at the next 512-sample hop and fades
    // for 48 ms; bendr-pulp's is a 6-frame spectral fade inside a 4096-point
    // STFT, so it reaches back one window before its latency.
    const Span spectr_span{samples(0.002), 512 + samples(0.048) + samples(0.012)};
    const Span bendr_span{4096 + samples(0.002), 512 * 7 + samples(0.012)};

    if (want("bendr")) {
        BendrReferenceRender bendr;
        report("bendr-pulp reference (RealtimePitchTimeProcessor, quality)",
               sweep(std::ref(bendr), [&] { return bendr.latency; }, count, 1, bendr_span, 4096));
    }
    if (want("stream")) {
        StreamRender stream;
        // The fade is 6 frames inside an 8192-point window: it reaches back
        // one window before the latency and on for the fade.
        const Span stream_span{8192 + samples(0.002), 512 * 7 + samples(0.012)};
        report("continuous stream (analysis -> FreezeHold -> OLA, 8192/512)",
               sweep(std::ref(stream), [&] { return stream.latency; }, count, 1, stream_span, 8192));
    }
    if (want("source")) {
        SourceRender source;
        report("Spectr FreezeSource alone",
               sweep(std::ref(source), [] { return 0; }, count, 1, spectr_span, FreezeSource::kFftSize));
    }
    if (want("processor")) {
        for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase})
            for (const float mix : {100.0f, 50.0f}) {
                ProcessorRender render{mode, mix};
                const std::string label = std::string("Spectr ")
                    + (mode == MaskRenderMode::linear_phase ? "Mixing" : "Tracking")
                    + " Mix " + std::to_string(int(mix)) + "%";
                report(label.c_str(), sweep(std::ref(render), [&] { return render.latency; },
                                            count, 1, spectr_span, FreezeSource::kFftSize));
            }
    }
}


// ── Acceptance ─────────────────────────────────────────────────────────────

namespace {

/// One press through the freeze source, rendered unpressed, as shipped, and
/// with a one-sample crossfade -- the hold alone from the engage on, whole
/// and as its tonal partials and its noise-like rest. It is the same hold in
/// every pressed render: the latch, the phases and their random walk do not
/// depend on the fade or on which part is played.
struct SourceTake {
    Stereo live, shipped, hold, tonal, noise;
    std::size_t engage = 0; // first sample the hold reaches
};

SourceTake source_take(const Stereo& programme, std::size_t absolute) {
    Take take;
    take.programme = &programme;
    take.start = absolute - samples(1.2);
    take.press = samples(1.2);
    take.release = take.press + samples(5.0);
    take.length = take.press + samples(0.4);
    SourceTake out;
    const auto run = [&](bool pressed, int crossfade, bool tonal = true, bool noise = true) {
        FreezeSource source;
        REQUIRE(source.prepare(kRate, 2));
        if (crossfade > 0) source.set_crossfade_samples(crossfade);
        source.set_hold_parts(tonal, noise);
        Stereo o; o.resize(take.length);
        walk(take, pressed, 64, [&](std::size_t pos, std::size_t n, bool frozen) {
            source.set_frozen(frozen);
            const float* in[] = {programme.l.data() + take.start + pos, programme.r.data() + take.start + pos};
            float* w[] = {o.l.data() + pos, o.r.data() + pos};
            source.process_block(in, w, 2, int(n));
        });
        return o;
    };
    out.live = run(false, 0);
    out.shipped = run(true, 0);
    out.hold = run(true, 1);
    out.tonal = run(true, 1, true, false);
    out.noise = run(true, 1, false, true);
    out.engage = take.press;
    while (out.engage < take.length && out.hold.l[out.engage] == out.live.l[out.engage]
           && out.hold.r[out.engage] == out.live.r[out.engage])
        ++out.engage;
    REQUIRE(out.engage < take.press + samples(0.1));
    return out;
}

/// Worst millisecond, across the engage fade, of what the shipped render
/// adds beyond the fade laws applied to the live input and to the hold's
/// tonal and noise-like parts, in dB re the fade's mean power.
double beyond_fade_db(const SourceTake& t, double (*g_live)(double), double (*g_tonal)(double),
                      double (*g_noise)(double)) {
    const std::size_t fade = samples(FreezeSource::kCrossfadeSeconds), step = samples(0.001);
    double worst = -300.0;
    for (int side = 0; side < 2; ++side) {
        const auto& L = side ? t.live.r : t.live.l;
        const auto& S = side ? t.shipped.r : t.shipped.l;
        const auto& T = side ? t.tonal.r : t.tonal.l;
        const auto& N = side ? t.noise.r : t.noise.l;
        double mean = 0.0;
        std::vector<double> u(fade);
        for (std::size_t i = 0; i < fade; ++i) {
            const double p = double(i + 1) / double(fade);
            const double ideal = g_live(p) * L[t.engage + i] + g_tonal(p) * T[t.engage + i]
                               + g_noise(p) * N[t.engage + i];
            mean += ideal * ideal;
            u[i] = S[t.engage + i] - ideal;
        }
        mean /= double(fade);
        for (std::size_t n = 0; n + step <= fade; n += step) {
            double e = 0.0;
            for (std::size_t i = n; i < n + step; ++i) e += u[i] * u[i];
            worst = std::max(worst, 10.0 * std::log10(e / double(step) / mean + 1e-30));
        }
    }
    return worst;
}

double cos_law(double p) { return std::cos(p * kPi / 2); }
double sin_law(double p) { return std::sin(p * kPi / 2); }
double linear_out(double p) { return 1.0 - p; }
double linear_in(double p) { return p; }
double cos_complement(double p) { return 1.0 - std::cos(p * kPi / 2); }

std::vector<std::size_t> random_moments(int count, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> where(1.3, 6.8);
    std::vector<std::size_t> out;
    for (int i = 0; i < count; ++i) out.push_back(samples(where(rng)));
    return out;
}

} // namespace

TEST_CASE("The freeze engage is its fade into the hold and nothing else",
          "[freeze][click]") {
    // A gain that renormalises the fade sample by sample -- following the
    // waveform's own power -- modulates it at audio rate: on a drum loop that
    // is a burst of distortion as loud as the signal for the length of the
    // fade, heard as a click. What the shipped engage plays beyond its fade
    // must be nothing: the live input by cos, the hold's tonal partials
    // (which continue the live ones in phase) by 1 - cos, its noise-like rest
    // by sin. Control: the same instrument against the WRONG fade law
    // (linear) must read the difference, so a clean reading is not a blind
    // one.
    double worst = -300.0, wrong_law = 300.0;
    const auto moments = random_moments(60, 1);
    for (std::size_t i = 0; i < moments.size(); ++i) {
        const auto t = source_take(*programmes().at(int(i % 3)), moments[i]);
        worst = std::max(worst, beyond_fade_db(t, cos_law, cos_complement, sin_law));
        wrong_law = std::min(wrong_law, beyond_fade_db(t, linear_out, linear_in, linear_in));
    }
    INFO("beyond the fade: worst " << worst << " dB; the linear-law control: least "
         << wrong_law << " dB");
    CHECK(worst < -90.0);
    CHECK(wrong_law > -40.0);
}

TEST_CASE("The hold does not replay the window it was taken from", "[freeze][click][ghost]") {
    // The latched frame's phases carry WHEN things happened inside it; a hold
    // that advances them as they are replays that timing one analysis window
    // later -- a hit just before the press comes back as a tick just after
    // it. The HF envelope of the hold's first window, against the input's
    // window before the latch, must look no more alike than the live input's
    // own consecutive windows do. Control: an actual replay reads 1.
    const std::size_t N = FreezeSource::kFftSize;
    std::vector<double> held, live_band;
    double replay = 0.0;
    const auto moments = random_moments(60, 1);
    for (std::size_t i = 0; i < moments.size(); ++i) {
        const auto t = source_take(*programmes().at(int(i % 3)), moments[i]);
        const auto& live = t.live.l;
        const auto before = hf_envelope(live, t.engage - N, N);
        held.push_back(correlation(hf_envelope(t.hold.l, t.engage, N), before));
        live_band.push_back(correlation(hf_envelope(live, t.engage, N), before));
        std::vector<float> replayed(live.begin(), live.end());
        std::copy(live.begin() + long(t.engage - N), live.begin() + long(t.engage),
                  replayed.begin() + long(t.engage));
        replay = std::max(replay, correlation(hf_envelope(replayed, t.engage, N), before));
    }
    const auto h = distribution(held, 0.5), l = distribution(live_band, 0.5);
    INFO("hold vs the latched window: " << h.text(0.5) << "\nlive vs its previous window: "
         << l.text(0.5) << "\nplanted replay: " << replay);
    CHECK(h.max <= l.max);
    CHECK(h.p95 <= l.p95);
    CHECK(replay > 0.99);
}

TEST_CASE("Freeze taps on a drum loop, chords and a voice add no click in either mode",
          "[freeze][click][realistic]") {
    // Freeze pressed at random moments on realistic material, through the
    // product, in both Latency modes, and released 0.7 s later. Every edge is
    // scored twice (see the file comment): a waveform SPIKE against the
    // no-freeze render at the same moment, and spectral OVERSHOOT -- energy
    // neither the live input nor the hold explains -- against an ideal
    // equal-power fade into the same hold and against the hold's own
    // frame-to-frame fluctuation. Thresholds are those references + 3 dB;
    // incidence above them must be zero, on every tap.
    const Span span{samples(0.002), 512 + samples(0.048) + samples(0.012)};
    struct Case { MaskRenderMode mode; float mix; int onsets; };
    for (const Case c : {Case{MaskRenderMode::zero_latency, 100.0f, 200},
                         Case{MaskRenderMode::linear_phase, 100.0f, 200},
                         Case{MaskRenderMode::zero_latency, 50.0f, 50},
                         Case{MaskRenderMode::linear_phase, 50.0f, 50}}) {
        ProcessorRender render{c.mode, c.mix};
        const auto onsets = sweep(std::ref(render), [&] { return render.latency; }, c.onsets, 1,
                                  span, FreezeSource::kFftSize);
        const auto worst = [&](Score Onset::*edge, double Score::*metric) {
            const auto v = column(onsets, edge, metric);
            return *std::max_element(v.begin(), v.end());
        };
        const auto p95 = [&](Score Onset::*edge, double Score::*metric) {
            return quantile(column(onsets, edge, metric), 0.95);
        };
        const double spike_limit = std::max(worst(&Onset::engage_control, &Score::spike),
                                            worst(&Onset::release_control, &Score::spike)) + 3.0;
        INFO(mode_label(c.mode) << " Mix " << c.mix << "%: spike engage " << worst(&Onset::engage_pressed, &Score::spike)
             << " release " << worst(&Onset::release_pressed, &Score::spike) << " limit " << spike_limit
             << "; p95 engage " << p95(&Onset::engage_pressed, &Score::spike)
             << " vs control " << p95(&Onset::engage_control, &Score::spike));
        CHECK(worst(&Onset::engage_pressed, &Score::spike) < spike_limit);
        CHECK(worst(&Onset::release_pressed, &Score::spike) < spike_limit);
        CHECK(p95(&Onset::engage_pressed, &Score::spike) <= p95(&Onset::engage_control, &Score::spike) + 1.0);
        CHECK(p95(&Onset::release_pressed, &Score::spike) <= p95(&Onset::release_control, &Score::spike) + 1.0);
        if (c.mix < 100.0f) continue; // the held reference is the wet leg alone at 100%
        // The hold's own frame-to-frame fluctuation sets the band -- but a
        // hold steady enough to fluctuate by less than kInaudibleOvershoot
        // would otherwise tighten it below what anyone hears (the engage
        // itself unchanged): energy that far under the frame's explained
        // energy is no click.
        constexpr double kInaudibleOvershoot = -45.0;
        const double overshoot_limit = std::max({worst(&Onset::engage_ideal, &Score::overshoot),
                                                 worst(&Onset::engage_steady, &Score::overshoot),
                                                 kInaudibleOvershoot}) + 3.0;
        const auto above = [&](Score Onset::*edge) {
            const auto v = column(onsets, edge, &Score::overshoot);
            return std::count_if(v.begin(), v.end(), [&](double x) { return x > overshoot_limit; });
        };
        INFO("overshoot limit " << overshoot_limit << " dB; engage worst "
             << worst(&Onset::engage_pressed, &Score::overshoot) << ", release worst "
             << worst(&Onset::release_pressed, &Score::overshoot) << "; the -30 dBFS planted step: "
             << above(&Onset::engage_planted) << " of " << onsets.size() << " seen");
        CHECK(above(&Onset::engage_pressed) == 0);
        // A release reads against the limit, or against an ideal equal-power
        // release into the same live input, whichever is higher.
        const auto release_over = std::count_if(onsets.begin(), onsets.end(), [&](const Onset& o) {
            return o.release_pressed.overshoot > std::max(overshoot_limit, o.release_ideal.overshoot + 3.0);
        });
        INFO("releases over both: " << release_over);
        CHECK(release_over == 0);
        // The instrument sees a planted step: this is what "zero" is not.
        CHECK(above(&Onset::engage_planted) > long(onsets.size()) / 2);
    }
}

TEST_CASE("A release after a long hold on a steady tone stays level", "[freeze][click][release]") {
    // The hold's quarter-cycle turn keeps it orthogonal to a live tone for as
    // long as it plays, so the release's plain equal-power fade is flat
    // without a gain riding it -- also after holds far longer than a fade.
    for (const double hz : {110.0, 440.0, 1234.5, 3000.0}) {
        for (const double hold_s : {0.7, 3.0, 10.0}) {
            const std::size_t press = samples(1.0), release = press + samples(hold_s);
            const std::size_t total = release + samples(0.5);
            FreezeSource source;
            REQUIRE(source.prepare(kRate, 1));
            std::vector<float> in(total), out(total);
            for (std::size_t n = 0; n < total; ++n)
                in[n] = float(0.3 * std::sin(2 * kPi * hz * double(n) / kRate));
            for (std::size_t n = 0; n < total; n += 256) {
                source.set_frozen(n >= press && n < release);
                const float* i[] = {in.data() + n};
                float* o[] = {out.data() + n};
                source.process_block(i, o, 1, int(std::min<std::size_t>(256, total - n)));
            }
            const auto rms = [&](std::size_t from, std::size_t len) {
                double e = 0;
                for (std::size_t n = from; n < from + len; ++n) e += double(out[n]) * out[n];
                return 10 * std::log10(e / double(len) + 1e-20);
            };
            const double before = rms(release - samples(0.1), samples(0.08));
            const double after = rms(release + samples(0.2), samples(0.08));
            double dip = 0, bump = -100;
            for (std::size_t n = release; n < release + samples(0.12); n += samples(0.005)) {
                dip = std::min(dip, rms(n, samples(0.005)) - std::min(before, after));
                bump = std::max(bump, rms(n, samples(0.005)) - std::max(before, after));
            }
            INFO(hz << " Hz held " << hold_s << " s: release dip " << dip << " dB, bump " << bump
                 << " dB (held " << before << ", live " << after << ")");
            CHECK(dip > -1.0);
            CHECK(bump < 1.0);
            CHECK(std::abs(before - after) < 1.0);
        }
    }
}

TEST_CASE("Freeze engage latency: when the hold replaces the live sound", "[.][freeze-engage-latency]") {
    // 440 Hz until the press, 660 Hz from the press on (a 5 ms raised-cosine
    // join). The hold is 440, the live input 660. In 2.5 ms steps: when the
    // 440 hold first reaches 10% and 90% of its settled level, and when the
    // live 660 falls below 10% of its level, in ms after the press, both in
    // wall time and after the variant's reported latency.
    const std::size_t press = samples(1.0);
    Stereo tone2; tone2.resize(samples(2.5));
    for (std::size_t n = 0; n < tone2.size(); ++n) {
        const double t = double(n) / kRate;
        const double c = std::clamp((t - 1.0) / 0.005, 0.0, 1.0);
        const double w = 0.5 - 0.5 * std::cos(kPi * c);
        const auto v = float(0.3 * ((1 - w) * std::sin(2 * kPi * 440 * t) + w * std::sin(2 * kPi * 660 * t)));
        tone2.l[n] = tone2.r[n] = v;
    }
    Take take;
    take.programme = &tone2;
    take.start = 0;
    take.press = press;
    take.release = tone2.size();
    take.length = tone2.size();
    const auto measure = [&](const char* label, const Stereo& out, int latency) {
        const auto amp = [&](std::size_t at, double hz) {
            double c = 0, s = 0;
            const std::size_t len = samples(0.005);
            for (std::size_t n = at; n < at + len; ++n) {
                const double ph = 2 * kPi * hz * double(n) / kRate;
                c += out.l[n] * std::cos(ph); s += out.l[n] * std::sin(ph);
            }
            return 2.0 * std::hypot(c, s) / double(len);
        };
        const double held = amp(press + samples(1.0), 440.0);
        const double live = 0.3;
        double first = -1, full = -1, gone = -1;
        for (std::size_t n = press; n + samples(0.005) < out.size() && n < press + samples(0.6); n += samples(0.0025)) {
            const double ms = double(n - press) * 1000.0 / kRate;
            const bool after_latency = n >= press + std::size_t(latency);
            if (after_latency && first < 0 && amp(n, 440.0) > 0.1 * held) first = ms;
            if (after_latency && full < 0 && amp(n, 440.0) > 0.9 * held) full = ms;
            if (gone < 0 && n > press + std::size_t(latency) && amp(n, 660.0) < 0.1 * live) gone = ms;
        }
        const double lat = latency * 1000.0 / kRate;
        std::printf("%-44s latency %6.1f ms | hold 10%% at %6.1f, 90%% at %6.1f, live gone at %6.1f ms after the press"
                    " (%6.1f / %6.1f / %6.1f after the latency)\n",
                    label, lat, first, full, gone, first - lat, full - lat, gone - lat);
    };
    {
        SourceRender r; measure("Spectr (shipped source, any mode)", r(take, true), 0);
    }
    for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
        ProcessorRender r{mode, 100.0f};
        const auto out = r(take, true);
        measure(mode == MaskRenderMode::linear_phase ? "Spectr Mixing (processor)" : "Spectr Tracking (processor)", out, r.latency);
    }
    { BendrReferenceRender r; const auto out = r(take, true); measure("bendr-pulp reference", out, r.latency); }
    { StreamRender r; const auto out = r(take, true); measure("continuous stream variant (8192/512)", out, r.latency); }
}
