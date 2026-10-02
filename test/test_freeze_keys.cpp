// Freeze Keys: while Freeze holds a sound, MIDI notes play it chromatically;
// unfrozen, MIDI does nothing.
//
// Every row renders the product -- the Spectr processor under a HeadlessHost,
// Freeze pressed with a sample-accurate parameter event and notes delivered
// in the host's MIDI buffer -- and reads the pitch off the output: Hann
// window, zero-padded, parabolic interpolation on log magnitude, which
// resolves well under a cent over a third of a second. Each row carries a
// control that must read differently (the root note against a transposed
// one, frozen against unfrozen), so a pass is a reading of the product.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/format/headless.hpp>
#include <pulp/signal/fft.hpp>
#include <pulp/state/parameter_event_queue.hpp>

#include "spectr/freeze_keys.hpp"
#include "spectr/freeze_source.hpp"
#include "spectr/spectr.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using spectr::FreezeKeys;
using spectr::FreezeSource;
using spectr::MaskRenderMode;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::size_t at(double seconds) { return static_cast<std::size_t>(std::llround(seconds * kRate)); }

struct Stereo {
    std::vector<float> l, r;
    std::size_t size() const { return l.size(); }
    void resize(std::size_t n) { l.assign(n, 0.0f); r.assign(n, 0.0f); }
};

// ── Material ──────────────────────────────────────────────────────────────

Stereo sine(double hz, double seconds) {
    Stereo s; s.resize(at(seconds));
    for (std::size_t n = 0; n < s.size(); ++n)
        s.l[n] = s.r[n] = float(0.3 * std::sin(2 * kPi * hz * double(n) / kRate));
    return s;
}

double saw(double phase, double f) {
    double v = 0.0;
    for (int h = 1; h * f < 12000.0; ++h) v += std::sin(h * phase) / h;
    return v;
}

void normalise(Stereo& s, double peak) {
    double m = 1e-9;
    for (std::size_t n = 0; n < s.size(); ++n)
        m = std::max({m, std::abs(double(s.l[n])), std::abs(double(s.r[n]))});
    for (std::size_t n = 0; n < s.size(); ++n) { s.l[n] *= float(peak / m); s.r[n] *= float(peak / m); }
}

/// A C major triad of band-limited saws, slightly wide.
Stereo saw_chord(double seconds) {
    Stereo s; s.resize(at(seconds));
    static constexpr double notes[] = {261.63, 329.63, 392.0};
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / kRate;
        double l = 0, r = 0;
        for (int v = 0; v < 3; ++v) {
            l += saw(2 * kPi * notes[v] * t + v, notes[v]);
            r += saw(2 * kPi * notes[v] * t + v + 0.4, notes[v]);
        }
        s.l[n] = float(l); s.r[n] = float(r);
    }
    normalise(s, 0.5);
    return s;
}

/// A pad: A minor, three detuned saws a note through a one-pole low-pass,
/// spread across the image, with a little room noise.
Stereo pad(double seconds) {
    Stereo s; s.resize(at(seconds));
    static constexpr double am[] = {220.0, 261.63, 329.63, 440.0};
    static constexpr double detune[] = {-0.004, 0.0, 0.0042};
    double lp_l = 0, lp_r = 0;
    const double a = 1.0 - std::exp(-2 * kPi * 2500.0 / kRate);
    std::mt19937 rng(5);
    std::normal_distribution<double> g(0.0, 0.002);
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / kRate;
        double l = 0, r = 0;
        for (int v = 0; v < 4; ++v)
            for (int d = 0; d < 3; ++d) {
                const double pan = (d - 1) * 0.35 + (v % 2 ? 0.1 : -0.1);
                const double x = saw(2 * kPi * am[v] * (1 + detune[d]) * t + v + 2 * d, am[v]);
                l += x * (0.5 - pan); r += x * (0.5 + pan);
            }
        lp_l += a * (l - lp_l); lp_r += a * (r - lp_r);
        s.l[n] = float(lp_l + g(rng)); s.r[n] = float(lp_r + g(rng));
    }
    normalise(s, 0.5);
    return s;
}

// ── Measures ──────────────────────────────────────────────────────────────

/// Spectral peaks of the left channel over [from, from + n), strongest
/// first: (hz, log magnitude). Peaks within 15 Hz of a stronger one are
/// skipped.
std::vector<std::pair<double, double>> peaks(const Stereo& s, std::size_t from, std::size_t n,
                                             int count, double max_hz = 5000.0) {
    constexpr int N = 262144;
    static const pulp::signal::Fft fft(N);
    std::vector<float> buf(N, 0.0f);
    std::vector<std::complex<float>> spec(N);
    for (std::size_t i = 0; i < n; ++i)
        buf[i] = float(s.l[from + i] * (0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n))));
    fft.forward_real(buf.data(), spec.data());
    std::vector<double> mag(N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k) mag[std::size_t(k)] = std::log(std::abs(spec[std::size_t(k)]) + 1e-20);
    const double bin = kRate / N;
    std::vector<std::pair<double, double>> found;
    for (std::size_t k = 2; k + 2 < mag.size() && double(k) * bin < max_hz; ++k)
        if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1]) {
            const double a = mag[k - 1], b = mag[k], c = mag[k + 1];
            const double d = 0.5 * (a - c) / (a - 2 * b + c);
            found.push_back({(double(k) + d) * bin, b});
        }
    std::sort(found.begin(), found.end(), [](auto& x, auto& y) { return x.second > y.second; });
    std::vector<std::pair<double, double>> out;
    for (const auto& p : found) {
        if (int(out.size()) == count) break;
        if (std::any_of(out.begin(), out.end(), [&](auto& o) { return std::abs(o.first - p.first) < 15.0; }))
            continue;
        out.push_back(p);
    }
    return out;
}

double cents(double reference, double measured) { return 1200.0 * std::log2(measured / reference); }

double rms(const Stereo& s, std::size_t from, std::size_t to) {
    double e = 0;
    for (std::size_t n = from; n < to; ++n) e += double(s.l[n]) * s.l[n] + double(s.r[n]) * s.r[n];
    return std::sqrt(e / double(2 * (to - from)));
}

double db(double x) { return 20.0 * std::log10(x + 1e-30); }

double max_diff(const Stereo& a, const Stereo& b, std::size_t from = 0) {
    double d = 0.0;
    for (std::size_t n = from; n < a.size(); ++n)
        d = std::max({d, double(std::abs(a.l[n] - b.l[n])), double(std::abs(a.r[n] - b.r[n]))});
    return d;
}

/// Two renders of the processor with identical input and edges differ by a
/// few float ulps from run to run (its mask design is adopted from a worker
/// thread), so "the same render" is this close -- -120 dBFS.
constexpr double kSameRender = 1e-6;

// ── The product, rendered ─────────────────────────────────────────────────

struct Edge {
    enum Kind { freeze, unfreeze, on, off } kind;
    std::size_t at;
    int note = 0;
    int velocity = 127;
};

/// Freeze Keys' loop-restart setting for the renders that follow; the hold
/// cases below set it.
bool g_restart_loop = true;

struct Render {
    Stereo out;
    int latency = 0;
    double max_block_ms = 0.0; // costliest process() call
    double max_edge_ms = 0.0;  // costliest call that delivered a note-on
};

Render render(const Stereo& in, std::vector<Edge> edges, double hold_seconds,
              bool keys = true, int block = 256,
              MaskRenderMode mode = MaskRenderMode::zero_latency,
              std::vector<double>* block_ms = nullptr) {
    pulp::format::HeadlessHost host{spectr::create_spectr};
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->set_render_mode(mode));
    plugin->set_freeze_seconds_override(hold_seconds);
    plugin->set_freeze_keys_enabled(keys);
    plugin->set_freeze_keys_restart_loop(g_restart_loop);
    host.prepare(kRate, block);
    Render r;
    r.latency = plugin->latency_samples();
    r.out.resize(in.size());
    pulp::midi::MidiBuffer mi, mo;
    mi.reserve_events(256);
    for (std::size_t pos = 0; pos < in.size(); pos += std::size_t(block)) {
        const auto n = std::min<std::size_t>(std::size_t(block), in.size() - pos);
        pulp::state::ParameterEventQueue events;
        pulp::format::ProcessContext ctx;
        mi.clear();
        float freeze_after = -1.0f;
        bool note_on_here = false;
        for (const auto& e : edges) {
            if (e.at < pos || e.at >= pos + n) continue;
            const auto offset = std::int32_t(e.at - pos);
            switch (e.kind) {
            case Edge::freeze:
            case Edge::unfreeze: {
                const float v = e.kind == Edge::freeze ? 1.0f : 0.0f;
                REQUIRE(events.push({spectr::kParamFreeze, offset, v, 0}));
                freeze_after = v;
                break;
            }
            case Edge::on: {
                auto m = pulp::midi::MidiEvent::note_on(0, std::uint8_t(e.note), std::uint8_t(e.velocity));
                m.sample_offset = offset;
                mi.add(m);
                note_on_here = true;
                break;
            }
            case Edge::off: {
                auto m = pulp::midi::MidiEvent::note_off(0, std::uint8_t(e.note));
                m.sample_offset = offset;
                mi.add(m);
                break;
            }
            }
        }
        pulp::audio::Buffer<float> ib(2, n), ob(2, n);
        std::copy_n(in.l.begin() + long(pos), n, ib.channel(0).begin());
        std::copy_n(in.r.begin() + long(pos), n, ib.channel(1).begin());
        const float* ip[] = {ib.channel(0).data(), ib.channel(1).data()};
        pulp::audio::BufferView<const float> iv(ip, 2, n);
        auto ov = ob.view();
        const auto t0 = std::chrono::steady_clock::now();
        host.process(ov, iv, mi, mo, events, ctx);
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        r.max_block_ms = std::max(r.max_block_ms, ms);
        if (block_ms) block_ms->push_back(ms);
        if (note_on_here) r.max_edge_ms = std::max(r.max_edge_ms, ms);
        if (freeze_after >= 0.0f) host.state().set_value(spectr::kParamFreeze, freeze_after);
        std::copy(ob.channel(0).begin(), ob.channel(0).end(), r.out.l.begin() + long(pos));
        std::copy(ob.channel(1).begin(), ob.channel(1).end(), r.out.r.begin() + long(pos));
    }
    return r;
}

/// Freeze at 0.6 s, the given notes from 1.5 s, held to the end.
Render play(const Stereo& in, double hold, std::vector<int> notes, int velocity = 127) {
    std::vector<Edge> edges{{Edge::freeze, at(0.6)}};
    for (int note : notes) edges.push_back({Edge::on, at(1.5), note, velocity});
    return render(in, edges, hold);
}

/// The measurement window: well after the note's attack and the hold's
/// engage, the renderer's latency taken off.
std::size_t window_start(const Render& r) { return at(1.9) + std::size_t(r.latency); }
constexpr std::size_t kWindow = 16384;

constexpr double kSpectralHold = FreezeSource::kDefaultHoldSeconds; // a spectral hold
constexpr double kLoopHold = 0.5;                                    // a loop

/// Every hold kind, and a loop hold both ways: notes restarting it from its
/// top (the default) and joining it where it plays.
struct HoldCase { const char* name; double seconds; bool restart = true; };
const HoldCase kHolds[] = {{"spectral", kSpectralHold},
                           {"loop", kLoopHold, true},
                           {"loop-join", kLoopHold, false}};

double ratio(int semitones) { return std::exp2(semitones / 12.0); }

} // namespace

TEST_CASE("Freeze Keys: the root note plays the hold at its pitch, other keys transpose",
          "[freeze-keys]") {
    const auto input = sine(220.0, 3.0);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        for (int semitones : {0, 7, -5, 12, -12}) {
            CAPTURE(semitones);
            const auto r = play(input, hold.seconds, {60 + semitones});
            const auto p = peaks(r.out, window_start(r), kWindow, 1);
            REQUIRE(p.size() == 1);
            const double expected = 220.0 * ratio(semitones);
            CAPTURE(p[0].first, expected);
            CHECK(std::abs(cents(expected, p[0].first)) < 3.0);
        }
    }
}

namespace {

/// Amplitude of the component at `hz` in the left channel over
/// [from, from + n): a Hann-windowed single-bin transform, scaled so a sine
/// of amplitude A reads A.
double level_at(const Stereo& s, std::size_t from, std::size_t n, double hz) {
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n));
        const double ph = 2 * kPi * hz * double(i) / kRate;
        re += w * s.l[from + i] * std::cos(ph);
        im -= w * s.l[from + i] * std::sin(ph);
        wsum += w;
    }
    return 2.0 * std::hypot(re, im) / wsum;
}

bool finite(const Stereo& s) {
    for (std::size_t n = 0; n < s.size(); ++n)
        if (!std::isfinite(s.l[n]) || !std::isfinite(s.r[n])) return false;
    return true;
}

double peak(const Stereo& s, std::size_t from = 0) {
    double m = 0.0;
    for (std::size_t n = from; n < s.size(); ++n)
        m = std::max({m, double(std::abs(s.l[n])), double(std::abs(s.r[n]))});
    return m;
}

} // namespace

TEST_CASE("Freeze Keys: notes are named as Logic and PlunderTube name them, MIDI 60 = C3",
          "[freeze-keys]") {
    const auto name = [](int note) {
        char out[6];
        FreezeKeys::note_name(note, out);
        return std::string{out};
    };
    CHECK(FreezeKeys::kDefaultRootNote == 60);
    CHECK(name(FreezeKeys::kDefaultRootNote) == "C3");
    CHECK(name(0) == "C-2");
    CHECK(name(11) == "B-2");
    CHECK(name(12) == "C-1");
    CHECK(name(24) == "C0");
    CHECK(name(48) == "C2");
    CHECK(name(61) == "C#3");
    CHECK(name(69) == "A3");
    CHECK(name(127) == "G8");
    CHECK(FreezeKeys::kLowestNote == 0);
    CHECK(FreezeKeys::kHighestNote == 127);
}

// Every MIDI note 0..127 plays, from the default root (60): 60 semitones down
// (1/32x) to 67 up (47.9x). Two tones, so every note has an in-band reading:
// 220 Hz carries the upper notes (note 127: 10.55 kHz), 1 kHz the lower (note
// 0: 31.25 Hz). A reading counts where the expected pitch is in band
// (30 Hz .. 0.4 fs); out of band (below 30 Hz, or past Nyquist) the output
// must still be finite and bounded, and a tone carried past Nyquist must not
// fold back (the next case measures that).
TEST_CASE("Freeze Keys: every MIDI note 0..127 plays, at its pitch where it is in band",
          "[freeze-keys]") {
    static constexpr int kNotes[] = {0, 12, 24, 48, 60, 72, 96, 120, 127};
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        int in_band = 0;
        for (double tone : {220.0, 1000.0}) {
            const auto input = sine(tone, 3.0);
            for (int note : kNotes) {
                CAPTURE(tone, note);
                const auto r = play(input, hold.seconds, {note});
                REQUIRE(finite(r.out));
                const double expected = tone * ratio(note - 60);
                // A held key never makes more than the hold's level plus
                // varispeed's power (a 1/32x read of a sine is still a sine).
                CHECK(peak(r.out, at(1.0)) < 1.0);
                if (expected >= 30.0 && expected <= 0.4 * kRate) {
                    ++in_band;
                    const auto p = peaks(r.out, window_start(r), kWindow, 1, 0.45 * kRate);
                    REQUIRE(p.size() == 1);
                    CAPTURE(expected, p[0].first);
                    CHECK(std::abs(cents(expected, p[0].first)) < 3.0);
                    // ...and at the hold's level, within 2 dB.
                    const double level = level_at(r.out, window_start(r), kWindow, expected);
                    CAPTURE(level);
                    CHECK(std::abs(db(level / 0.3)) < 2.0);
                    // No DC: the mean is a small fraction of the tone.
                    double mean = 0.0;
                    for (std::size_t n = window_start(r); n < window_start(r) + kWindow; ++n)
                        mean += r.out.l[n];
                    mean /= double(kWindow);
                    CHECK(std::abs(mean) < 0.01);
                }
            }
        }
        // 220 Hz: notes 48..127 (note 24 is 27.5 Hz); 1 kHz: notes 0..96.
        CHECK(in_band == 6 + 7);
    }
}

// Upward transposition removes what it would push past Nyquist rather than
// folding it back. A tone near 0.7 fs / ratio lands near 0.7 fs, so
// unfiltered it folds to about 0.3 fs (14.4 kHz) at full level; the control,
// a tone near 0.3 fs / ratio, lands near 0.3 fs in band and must read there
// at the hold's level (the same measurement in the same place, so a
// near-silent reading is the filter, not a deaf instrument). Tones are whole
// cycles of the 0.5 s loop (even hertz), so a loop's seam adds no sidebands
// and the measurement sits on its line. The limit: the fold at least 60 dB
// below the control.
TEST_CASE("Freeze Keys: an upward transposition does not fold past Nyquist",
          "[freeze-keys]") {
    const auto loop_tone = [](double hz) { return 2.0 * std::round(hz / 2.0); };
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        for (int note : {67, 72, 84, 96, 108, 120, 127}) {
            CAPTURE(note);
            const double rt = ratio(note - 60);
            const double high = loop_tone(0.7 * kRate / rt), low = loop_tone(0.3 * kRate / rt);
            const auto above = play(sine(high, 3.0), hold.seconds, {note});
            const auto control = play(sine(low, 3.0), hold.seconds, {note});
            REQUIRE(finite(above.out));
            const double fold = kRate - high * rt;
            CAPTURE(high, low, fold);
            const double folded = level_at(above.out, window_start(above), kWindow, fold);
            const double in_band = level_at(control.out, window_start(control), kWindow, low * rt);
            const double total = rms(above.out, window_start(above), window_start(above) + kWindow);
            CAPTURE(db(folded), db(in_band), db(total));
            CHECK(std::abs(db(in_band / 0.3)) < 2.0);
            CHECK(db(folded) < db(in_band) - 60.0);
            // ...and nothing else of it is left anywhere in the spectrum.
            CHECK(db(total) < db(in_band) - 55.0);
            std::printf("fold %-9s note %3d ratio %6.2f: control %6.1f dBFS, fold %7.1f dBFS, "
                        "all of it %7.1f dBFS\n", hold.name, note, rt, db(in_band), db(folded), db(total));
        }
    }
}

// The real-time gate at both ends of the range: an 8-note chord at the top
// (notes 120..127, ratios 32x..48x: a band-limited loop voice passes over
// every loop sample it skips) and at the bottom (0..7). Each block's cost is
// the least of three renders (scheduling noise only ever adds); the chord's
// costliest block may cost at most 6x the costliest block of the same freeze
// without keys -- the AU host probe's gate.
TEST_CASE("Freeze Keys: an 8-note chord at the top and bottom of the range keeps real time",
          "[freeze-keys]") {
    const auto input = pad(3.0);
    constexpr int block = 128;
    const auto least = [&](const std::vector<Edge>& edges, double seconds) {
        std::vector<double> best;
        for (int run = 0; run < 3; ++run) {
            std::vector<double> ms;
            (void)render(input, edges, seconds, true, block, MaskRenderMode::zero_latency, &ms);
            if (best.empty()) best = ms;
            for (std::size_t b = 0; b < best.size() && b < ms.size(); ++b) best[b] = std::min(best[b], ms[b]);
        }
        return best;
    };
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        const auto plain = least({{Edge::freeze, at(0.6)}}, hold.seconds);
        const double plain_max = *std::max_element(plain.begin(), plain.end());
        for (int lowest : {120, 0}) {
            CAPTURE(lowest);
            std::vector<Edge> edges{{Edge::freeze, at(0.6)}};
            for (int k = 0; k < 8; ++k) edges.push_back({Edge::on, at(1.5), lowest + k});
            const auto chord = least(edges, hold.seconds);
            // From the chord's note-on to the end: its preparation and the
            // sustained voices.
            const auto first = at(1.5) / std::size_t(block);
            const double chord_max = *std::max_element(chord.begin() + long(first), chord.end());
            double sum = 0.0;
            for (std::size_t b = first; b < chord.size(); ++b) sum += chord[b];
            const double mean = sum / double(chord.size() - first);
            std::printf("cost %-9s chord %3d..%3d block %d: costliest %.3f ms, mean %.3f ms; "
                        "freeze alone costliest %.3f ms; budget %.3f ms\n",
                        hold.name, lowest, lowest + 7, block, chord_max, mean, plain_max,
                        1000.0 * block / kRate);
            CAPTURE(chord_max, mean, plain_max);
            CHECK(chord_max <= 6.0 * plain_max);
        }
    }
}

TEST_CASE("Freeze Keys: a chord plays every one of its pitches", "[freeze-keys]") {
    const auto input = sine(220.0, 3.0);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        const auto r = play(input, hold.seconds, {60, 64, 67});
        const auto p = peaks(r.out, window_start(r), kWindow, 3);
        REQUIRE(p.size() == 3);
        for (int semitones : {0, 4, 7}) {
            const double expected = 220.0 * ratio(semitones);
            CAPTURE(semitones, expected);
            const auto near = std::min_element(p.begin(), p.end(), [&](auto& a, auto& b) {
                return std::abs(a.first - expected) < std::abs(b.first - expected);
            });
            CAPTURE(near->first);
            CHECK(std::abs(cents(expected, near->first)) < 3.0);
            // ...and each is a real voice, within 6 dB of the strongest.
            CHECK(near->second > p[0].second - std::log(2.0));
        }
    }
}

TEST_CASE("Freeze Keys: richer material transposes by the interval", "[freeze-keys]") {
    struct Material { const char* name; Stereo audio; };
    const Material materials[] = {{"saw chord", saw_chord(3.0)}, {"pad", pad(3.0)}};
    for (const auto& m : materials) {
        for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
            CAPTURE(m.name, hold.name);
            const auto root = play(m.audio, hold.seconds, {60});
            const auto fifth = play(m.audio, hold.seconds, {67});
            const auto rp = peaks(root.out, window_start(root), kWindow, 1, 1500.0);
            REQUIRE(rp.size() == 1);
            // The same partial, a fifth up: the fifth render's peak nearest it.
            const double expected = rp[0].first * ratio(7);
            // A loop voice is a varispeed read: the fifth plays the same
            // stretch of the loop as the root's window in 1/ratio the time.
            // Read it over exactly that stretch, so both windows hold the
            // same loop audio, a wrap and its seam (a musical Length loops
            // exactly, its recurring seam unmatched) at the same place in
            // each; over any other stretch, the beating detuned pad reads a
            // few cents off either way. Both notes start at the same loop
            // position (the top, or wherever the hold plays at the shared
            // note-on). A spectral voice has no position: the same window.
            std::size_t fifth_start = window_start(fifth), fifth_window = kWindow;
            if (hold.seconds >= FreezeSource::kLoopMinSeconds) {
                const double into = double(window_start(root) - std::size_t(root.latency) - at(1.5));
                fifth_start = at(1.5) + std::size_t(fifth.latency)
                    + std::size_t(std::llround(into / ratio(7)));
                fifth_window = std::size_t(std::llround(double(kWindow) / ratio(7)));
            }
            const auto fp = peaks(fifth.out, fifth_start, fifth_window, 12, 3000.0);
            REQUIRE(!fp.empty());
            const auto near = std::min_element(fp.begin(), fp.end(), [&](auto& a, auto& b) {
                return std::abs(a.first - expected) < std::abs(b.first - expected);
            });
            CAPTURE(rp[0].first, expected, near->first);
            CHECK(std::abs(cents(expected, near->first)) < 3.0);
        }
    }
}

TEST_CASE("Freeze Keys: the root at full velocity plays at the hold's level; velocity scales it",
          "[freeze-keys]") {
    const auto input = pad(3.0);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        // Freeze alone: the hold's level over the same window.
        const auto frozen = render(input, {{Edge::freeze, at(0.6)}}, hold.seconds);
        const auto full = play(input, hold.seconds, {60}, 127);
        const auto soft = play(input, hold.seconds, {60}, 64);
        const auto from = window_start(full), to = from + kWindow;
        const double hold_db = db(rms(frozen.out, from, to));
        const double full_db = db(rms(full.out, from, to));
        const double soft_db = db(rms(soft.out, from, to));
        CAPTURE(hold_db, full_db, soft_db);
        CHECK(std::abs(full_db - hold_db) < 1.5);
        const double v = 64.0 / 127.0;
        CHECK(std::abs((soft_db - full_db) - db(v * v)) < 0.5);
    }
}

TEST_CASE("Freeze Keys: note-off releases to silence; no key held is silence", "[freeze-keys]") {
    const auto input = pad(3.5);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        const auto r = render(input, {{Edge::freeze, at(0.6)},
                                      {Edge::on, at(1.5), 60}, {Edge::on, at(1.5), 64},
                                      {Edge::off, at(2.2), 60}, {Edge::off, at(2.2), 64}},
                              hold.seconds);
        const auto lat = std::size_t(r.latency);
        const double playing = db(rms(r.out, at(1.8) + lat, at(2.15) + lat));
        // The release is kReleaseSeconds; the mask's own tail follows it.
        const double after = db(rms(r.out, at(2.6) + lat, at(3.4) + lat));
        CAPTURE(playing, after);
        CHECK(playing > -40.0);
        CHECK(after < -100.0);
    }
}

TEST_CASE("Freeze Keys: unfrozen, notes do nothing and the output is the live render",
          "[freeze-keys]") {
    const auto input = pad(5.0);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        const std::vector<Edge> frozen_part{{Edge::freeze, at(0.6)},
                                            {Edge::on, at(1.5), 60}, {Edge::on, at(1.5), 67},
                                            {Edge::unfreeze, at(2.6)}};
        auto with_late_notes = frozen_part;
        // Notes before any freeze and after the release: ignored.
        for (double t : {0.2, 3.2, 3.9})
            for (int note : {48, 60, 72}) with_late_notes.push_back({Edge::on, at(t), note});
        with_late_notes.push_back({Edge::off, at(4.3), 60});
        const auto a = render(input, with_late_notes, hold.seconds);
        const auto b = render(input, frozen_part, hold.seconds);
        // The ignored notes changed nothing anywhere.
        CHECK(max_diff(a.out, b.out) < kSameRender);

        // Once the release fades and the mask's tail passes, the frozen-and-
        // played render is the live (never frozen) render.
        const auto live = render(input, {}, hold.seconds);
        const auto lat = std::size_t(a.latency);
        double diff = 0.0, level = 0.0;
        for (std::size_t n = at(3.3) + lat; n < input.size(); ++n) {
            diff = std::max({diff, double(std::abs(a.out.l[n] - live.out.l[n])),
                             double(std::abs(a.out.r[n] - live.out.r[n]))});
            level = std::max(level, double(std::abs(live.out.l[n])));
        }
        CAPTURE(diff, level);
        CHECK(level > 0.1);
        CHECK(diff < 1e-5);
        // Control: while frozen and played, it is NOT the live render.
        double frozen_diff = 0.0;
        for (std::size_t n = at(1.8) + lat; n < at(2.4) + lat; ++n)
            frozen_diff = std::max(frozen_diff, double(std::abs(a.out.l[n] - live.out.l[n])));
        CHECK(frozen_diff > 0.05);
    }
}

TEST_CASE("Freeze Keys: no MIDI while frozen sounds exactly as Freeze did", "[freeze-keys]") {
    const auto input = pad(3.0);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        const std::vector<Edge> edges{{Edge::freeze, at(0.6)}, {Edge::unfreeze, at(2.2)}};
        const auto keys = render(input, edges, hold.seconds, true);
        const auto plain = render(input, edges, hold.seconds, false);
        CHECK(max_diff(keys.out, plain.out) < kSameRender);
    }
}

TEST_CASE("Freeze Keys: without MIDI the stage is the freeze source, bit for bit, at any block size",
          "[freeze-keys]") {
    // The source alone against the source wrapped by FreezeKeys (enabled, no
    // notes), fed the same uneven blocks: the wrapper must add nothing.
    const auto input = pad(2.5);
    for (double hold : {kSpectralHold, kLoopHold}) {
        CAPTURE(hold);
        FreezeSource bare, wrapped;
        REQUIRE(bare.prepare(kRate, 2));
        REQUIRE(wrapped.prepare(kRate, 2));
        FreezeKeys keys{wrapped};
        REQUIRE(keys.prepare(kRate, 2));
        keys.set_enabled(true);
        bare.set_hold_seconds(hold);
        wrapped.set_hold_seconds(hold);
        std::vector<float> a_l(input.size()), a_r(input.size()), b_l(input.size()), b_r(input.size());
        static constexpr int sizes[] = {1, 37, 128, 511, 64, 1024, 3};
        std::size_t pos = 0;
        for (int i = 0; pos < input.size(); ++i) {
            const auto n = std::min<std::size_t>(std::size_t(sizes[i % 7]), input.size() - pos);
            const bool frozen = pos >= at(0.6) && pos < at(1.9);
            bare.set_frozen(frozen);
            wrapped.set_frozen(frozen);
            keys.begin_block();
            const float* in[] = {input.l.data() + pos, input.r.data() + pos};
            float* oa[] = {a_l.data() + pos, a_r.data() + pos};
            float* ob[] = {b_l.data() + pos, b_r.data() + pos};
            bare.process_block(in, oa, 2, int(n));
            keys.process_block(in, ob, 2, int(n));
            pos += n;
        }
        CHECK(a_l == b_l);
        CHECK(a_r == b_r);
    }
}

TEST_CASE("Freeze Keys: a key pressed while the freeze arms sounds once the hold does",
          "[freeze-keys]") {
    const auto input = sine(220.0, 3.0);
    // Freeze and the note in the same sample: the hold is not yet latched.
    const auto r = render(input, {{Edge::freeze, at(0.6)}, {Edge::on, at(0.6), 67}}, kSpectralHold);
    const auto p = peaks(r.out, at(1.2) + std::size_t(r.latency), kWindow, 1);
    REQUIRE(p.size() == 1);
    CHECK(std::abs(cents(220.0 * ratio(7), p[0].first)) < 3.0);
}

TEST_CASE("Freeze Keys: a voice reads the loop correctly before its first pass has copied it",
          "[freeze-keys]") {
    // The loop is copied out of the recording as its own first pass plays,
    // so a key pressed during that pass reads samples the copy has not
    // reached yet. Every loop sample read halfway through the first pass
    // must equal the same sample read once the pass has wrapped and the
    // whole loop is in the copy. Control: the half not yet copied carries
    // the input's level, which a read of the unfilled copy would not.
    const auto input = pad(4.0);
    FreezeSource source;
    REQUIRE(source.prepare(kRate, 2));
    source.set_hold_seconds(kLoopHold);
    Stereo out; out.resize(input.size());
    std::vector<float> early[2], late[2];
    std::int64_t early_position = -1, previous = -1;
    bool wrapped = false;
    for (std::size_t pos = 0; pos < input.size(); pos += 128) {
        const auto n = std::min<std::size_t>(128, input.size() - pos);
        source.set_frozen(pos >= at(2.0));
        const float* i[] = {input.l.data() + pos, input.r.data() + pos};
        float* o[] = {out.l.data() + pos, out.r.data() + pos};
        source.process_block(i, o, 2, int(n));
        if (!source.hold_audible() || !source.looping()) continue;
        const std::int64_t length = source.loop_length();
        const std::int64_t position = source.loop_position();
        if (early_position < 0 && position >= length / 3) {
            early_position = position;
            for (int ch = 0; ch < 2; ++ch)
                for (std::int64_t k = 0; k < length; ++k) early[ch].push_back(source.loop_sample(ch, k));
        }
        if (early_position >= 0 && previous >= 0 && position < previous) wrapped = true;
        previous = position;
        if (wrapped && position >= length / 3) {
            for (int ch = 0; ch < 2; ++ch)
                for (std::int64_t k = 0; k < length; ++k) late[ch].push_back(source.loop_sample(ch, k));
            break;
        }
    }
    REQUIRE(early_position > 0);
    REQUIRE(wrapped);
    const auto length = static_cast<std::int64_t>(early[0].size());
    REQUIRE(early_position < length);
    for (int ch = 0; ch < 2; ++ch) {
        REQUIRE(late[ch].size() == early[ch].size());
        float worst = 0.0f;
        for (std::size_t k = 0; k < early[ch].size(); ++k)
            worst = std::max(worst, std::abs(early[ch][k] - late[ch][k]));
        CAPTURE(ch, worst);
        CHECK(worst == 0.0f);
        double ahead = 0.0;
        for (std::int64_t k = early_position; k < length; ++k)
            ahead += double(early[ch][std::size_t(k)]) * early[ch][std::size_t(k)];
        const double rms = std::sqrt(ahead / double(length - early_position));
        CAPTURE(rms);
        CHECK(rms > 0.05);
    }
}

namespace {

/// The freeze stage on its own (no mask): FreezeSource wrapped by FreezeKeys,
/// frozen at `freeze_at`, a loop hold, with notes at given samples. Returns
/// the stage output; `source` is left as it ended, for reading the loop.
struct StageNote { std::size_t at; int note; bool on; };
Stereo render_stage(FreezeSource& source, const Stereo& input, std::size_t freeze_at,
                    std::vector<StageNote> notes, bool restart, int block = 128) {
    REQUIRE(source.prepare(kRate, 2));
    source.set_hold_seconds(kLoopHold);
    FreezeKeys keys{source};
    REQUIRE(keys.prepare(kRate, 2));
    keys.set_enabled(true);
    keys.set_restart_loop(restart);
    Stereo out; out.resize(input.size());
    for (std::size_t pos = 0; pos < input.size(); pos += std::size_t(block)) {
        const auto n = std::min<std::size_t>(std::size_t(block), input.size() - pos);
        source.set_frozen(pos >= freeze_at);
        keys.begin_block();
        for (const auto& e : notes)
            if (e.at >= pos && e.at < pos + n) {
                if (e.on) REQUIRE(keys.note_on(int(e.at - pos), e.note, 127));
                else REQUIRE(keys.note_off(int(e.at - pos), e.note));
            }
        const float* i[] = {input.l.data() + pos, input.r.data() + pos};
        float* o[] = {out.l.data() + pos, out.r.data() + pos};
        keys.process_block(i, o, 2, int(n));
    }
    return out;
}

double correlation(const std::vector<float>& a, std::size_t from, const std::vector<float>& b,
                   std::size_t n) {
    double ab = 0, aa = 0, bb = 0;
    for (std::size_t k = 0; k < n; ++k) {
        ab += double(a[from + k]) * b[k];
        aa += double(a[from + k]) * a[from + k];
        bb += double(b[k]) * b[k];
    }
    return ab / std::sqrt(aa * bb + 1e-30);
}

} // namespace

TEST_CASE("Freeze Keys: with Restart loop on, a note plays the loop from its top",
          "[freeze-keys][restart]") {
    // A pad that keeps changing (a slow swell and a chord change), so the
    // loop's top is unlike the audio around wherever the frozen loop happens
    // to be playing: a note joining the running loop does not correlate with
    // it. Freeze at 1.4 s; the root at 2.31 s, released at 2.51 s, then again
    // at 2.68 s (each at a loop phase unrelated to the other). The same key
    // twice keeps both at the loop's own pitch, so each compares sample for
    // sample with the loop.
    auto input = pad(6.0);
    for (std::size_t n = 0; n < input.size(); ++n) {
        const double g = 0.55 + 0.45 * std::sin(2 * kPi * 1.3 * double(n) / kRate);
        input.l[n] *= float(g); input.r[n] *= float(g);
    }
    const std::size_t a = at(2.31), b = at(2.68);
    FreezeSource with_both, only_a;
    const auto both = render_stage(with_both, input, at(1.4),
                                   {{a, 60, true}, {at(2.51), 60, false}, {b, 60, true}}, true);
    const auto first = render_stage(only_a, input, at(1.4), {{a, 60, true}, {at(2.51), 60, false}}, true);
    FreezeSource held_source;
    const auto held = render_stage(held_source, input, at(1.4), {{a, 60, true}}, true);
    REQUIRE(with_both.looping());
    const auto length = std::size_t(with_both.loop_length());
    REQUIRE(length > std::size_t(at(0.25)));
    // The loop's top as its first pass plays it, then later passes.
    std::vector<float> top(length), pass(length);
    for (std::size_t k = 0; k < length; ++k) {
        top[k] = with_both.loop_sample(0, std::int64_t(k), false);
        pass[k] = with_both.loop_sample(0, std::int64_t(k), true);
    }
    // Each note over its first Hold length, its 10 ms attack ramp aside.
    const std::size_t attack = at(FreezeKeys::kAttackSeconds) + 1;
    const std::vector<float> top_after(top.begin() + long(attack), top.end());
    const double ra = correlation(held.l, a + attack, top_after, length - attack);
    // Note B: what B added to A.
    Stereo b_only; b_only.resize(input.size());
    for (std::size_t n = 0; n < input.size(); ++n) b_only.l[n] = both.l[n] - first.l[n];
    const double rb = correlation(b_only.l, b + attack, top_after, length - attack);
    CAPTURE(length, ra, rb);
    CHECK(ra > 0.99);
    CHECK(rb > 0.99);

    // Held for 2.5 loop lengths: after the attack, the voice IS the loop --
    // the plain top on the first pass, then each later pass with the seam
    // the source crossfades -- sample for sample.
    REQUIRE(a + length * 5 / 2 < input.size());
    float worst = 0.0f;
    for (std::size_t k = attack; k < length * 5 / 2; ++k) {
        const std::size_t i = k % length;
        const float expected = k < length ? top[i] : pass[i];
        worst = std::max(worst, std::abs(held.l[a + k] - expected));
    }
    CAPTURE(worst);
    CHECK(worst < 1e-6f);
    // ...so its wraps are the source's own seams: no sharper a step than
    // the plain hold makes over the same span.
    const auto step = [](const std::vector<float>& x, std::size_t from, std::size_t to) {
        double m = 0.0;
        for (std::size_t n = from; n + 2 < to; ++n)
            m = std::max(m, std::abs(double(x[n + 2]) - 2.0 * x[n + 1] + x[n]));
        return m;
    };
    FreezeSource plain_source;
    const auto plain = render_stage(plain_source, input, at(1.4), {}, true);
    const double voice_step = step(held.l, a + attack, a + length * 5 / 2);
    const double hold_step = step(plain.l, a, a + length * 5 / 2);
    CAPTURE(voice_step, hold_step);
    CHECK(voice_step < 1.5 * hold_step + 1e-4);

    // Control: joining the running loop instead (Restart off), the same
    // note does not start at the top.
    FreezeSource joined_source;
    const auto joined = render_stage(joined_source, input, at(1.4), {{a, 60, true}}, false);
    const double rj = correlation(joined.l, a + attack, top_after, length - attack);
    CAPTURE(rj);
    CHECK(rj < 0.9);
}

TEST_CASE("Freeze Keys: with Restart loop off, the root note continues the running loop",
          "[freeze-keys][restart]") {
    const auto input = pad(5.0);
    FreezeSource keyed, plain;
    const std::size_t a = at(2.31);
    const auto played = render_stage(keyed, input, at(1.4), {{a, 60, true}}, false);
    const auto held = render_stage(plain, input, at(1.4), {}, false);
    // The root joins the loop where it plays and the hold fades out under
    // it linearly: the same audio, sample for sample.
    CHECK(max_diff(played, held) < 1e-5);
}

TEST_CASE("Freeze Keys: the restart setting persists in the plugin state, absent is on",
          "[freeze-keys][restart]") {
    pulp::format::HeadlessHost host{spectr::create_spectr};
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    CHECK(plugin->freeze_keys_restart_loop());          // a new instance: on
    plugin->set_freeze_keys_restart_loop(false);
    const auto blob = plugin->serialize_plugin_state();
    std::string json(blob.begin(), blob.end());
    const std::string key = "\"freeze_keys_restart_loop\"";
    const auto key_at = json.find(key);
    REQUIRE(key_at != std::string::npos);
    const auto value_end = json.find_first_of(",}", key_at);
    REQUIRE(json.substr(key_at, value_end - key_at).find("false") != std::string::npos);

    pulp::format::HeadlessHost other{spectr::create_spectr};
    auto* restored = dynamic_cast<spectr::Spectr*>(other.processor());
    REQUIRE(restored != nullptr);
    REQUIRE(restored->deserialize_plugin_state(blob));
    CHECK_FALSE(restored->freeze_keys_restart_loop());

    // A session saved before the setting existed carries no member: on.
    std::string old = json;
    // The member and the comma before it.
    const auto comma = old.rfind(',', key_at);
    old.erase(comma, value_end - comma);
    REQUIRE(old.find("freeze_keys_restart_loop") == std::string::npos);
    REQUIRE(restored->deserialize_plugin_state(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(old.data()), old.size())));
    CHECK(restored->freeze_keys_restart_loop());
}

TEST_CASE("Freeze Keys: with Restart on, a note's loop period is the musical Length at host tempo",
          "[freeze-keys][restart][freeze-length]") {
    // The product, under a host transport: Length set in bars, Restart ON,
    // the root held for 2.5 loops. Its period, MEASURED from the render, is
    // the Length in seconds at the transport's tempo and meter to within
    // 1 ms; its first pass is the loop from its top. Noise in, so every
    // stretch of the loop is unlike every other: a voice that started
    // anywhere but the top, or ran any other period, does not correlate.
    using spectr::FreezeLength;
    using spectr::LengthFraction;
    struct Case { double bpm; int num, den; FreezeLength length; double seconds; };
    const Case cases[] = {
        {120.0, 4, 4, {2, LengthFraction::zero}, 4.0},  // 2 bars of 2 s
        {90.0, 4, 4, {1, LengthFraction::f1_8}, 3.0},   // 1 1/8 bars of 2 2/3 s
    };
    for (const auto& c : cases) {
        INFO(c.bpm << " BPM " << c.num << "/" << c.den << " " << spectr::length_label(c.length));
        pulp::format::HeadlessHost host{spectr::create_spectr};
        auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
        REQUIRE(plugin != nullptr);
        REQUIRE(plugin->set_render_mode(MaskRenderMode::zero_latency));
        plugin->set_freeze_keys_enabled(true);
        plugin->set_freeze_keys_restart_loop(true);
        constexpr int block = 256;
        host.prepare(kRate, block);
        REQUIRE(plugin->set_freeze_length_from_editor(c.length));
        const int latency = plugin->latency_samples();

        const double press = c.seconds + 1.5;
        const std::size_t note_on = at(press + 0.6);
        const std::size_t note_off = note_on + at(2.5 * c.seconds);
        const std::size_t total = note_off + at(0.3);
        std::mt19937 rng(31);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        Stereo in; in.resize(total);
        for (std::size_t n = 0; n < total; ++n) in.l[n] = in.r[n] = 0.25f * u(rng);

        Stereo out; out.resize(total);
        pulp::midi::MidiBuffer mi, mo;
        mi.reserve_events(16);
        bool waited = false;
        for (std::size_t pos = 0; pos < total; pos += std::size_t(block)) {
            const auto n = std::min<std::size_t>(std::size_t(block), total - pos);
            pulp::state::ParameterEventQueue events;
            pulp::format::ProcessContext ctx;
            ctx.tempo_bpm = c.bpm;
            ctx.time_sig_numerator = c.num;
            ctx.time_sig_denominator = c.den;
            mi.clear();
            const bool press_here = at(press) >= pos && at(press) < pos + n;
            if (press_here)
                REQUIRE(events.push({spectr::kParamFreeze, std::int32_t(at(press) - pos), 1.0f, 0}));
            if (note_on >= pos && note_on < pos + n) {
                auto m = pulp::midi::MidiEvent::note_on(0, 60, 127);
                m.sample_offset = std::int32_t(note_on - pos);
                mi.add(m);
            }
            if (note_off >= pos && note_off < pos + n) {
                auto m = pulp::midi::MidiEvent::note_off(0, 60);
                m.sample_offset = std::int32_t(note_off - pos);
                mi.add(m);
            }
            pulp::audio::Buffer<float> ib(2, n), ob(2, n);
            std::copy_n(in.l.begin() + long(pos), n, ib.channel(0).begin());
            std::copy_n(in.r.begin() + long(pos), n, ib.channel(1).begin());
            const float* ip[] = {ib.channel(0).data(), ib.channel(1).data()};
            pulp::audio::BufferView<const float> iv(ip, 2, n);
            auto ov = ob.view();
            host.process(ov, iv, mi, mo, events, ctx);
            if (press_here) host.state().set_value(spectr::kParamFreeze, 1.0f);
            std::copy(ob.channel(0).begin(), ob.channel(0).end(), out.l.begin() + long(pos));
            std::copy(ob.channel(1).begin(), ob.channel(1).end(), out.r.begin() + long(pos));
            // A loop longer than the rings prepare() set up grows on the
            // storage worker; an offline render outruns any thread, so let it
            // answer before the history the loop needs is recorded.
            if (!waited) {
                waited = true;
                const auto& source = plugin->freeze_source();
                for (int i = 0; i < 200 && !source.loop_storage_in_flight()
                                && source.loop_storage_longest()
                                       < static_cast<std::int64_t>(at(c.seconds)); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        const auto& source = plugin->freeze_source();
        CHECK(plugin->freeze_length_seconds() == Catch::Approx(c.seconds));
        REQUIRE(source.looping());
        const auto length = std::size_t(source.loop_length());
        CHECK(length == at(c.seconds));

        // THE PERIOD, measured: the lag, within 20 ms of the expected one, at
        // which the voice best matches itself one pass later, refined to a
        // fraction of a sample. Both windows are the voice alone (past its
        // attack, before its release).
        const std::size_t start = note_on + std::size_t(latency) + at(0.35);
        const std::size_t win = at(0.4);
        const long expected = long(at(c.seconds));
        const long span = long(at(0.02));
        std::vector<double> scores;
        double best = -2.0;
        long best_lag = expected;
        for (long lag = expected - span; lag <= expected + span; ++lag) {
            const std::vector<float> later(out.l.begin() + long(start) + lag,
                                           out.l.begin() + long(start) + lag + long(win));
            const double r = correlation(out.l, start, later, win);
            scores.push_back(r);
            if (r > best) { best = r; best_lag = lag; }
        }
        const auto k = std::size_t(best_lag - (expected - span));
        double frac = 0.0;
        if (k > 0 && k + 1 < scores.size()) {
            const double d = scores[k - 1] - 2.0 * best + scores[k + 1];
            if (d < 0.0) frac = 0.5 * (scores[k - 1] - scores[k + 1]) / d;
        }
        const double period = (double(best_lag) + frac) / kRate;
        CAPTURE(period, best);
        CHECK(best > 0.99);
        CHECK(std::abs(period - c.seconds) < 0.001);

        // THE FIRST PASS STARTS AT THE TOP: right after the note-on (through
        // the renderer's latency), the output is the loop's plain first pass
        // from sample 0. Searched a few ms either side of that alignment and
        // required there; control: the same span of the loop 10 ms on.
        const std::size_t attack = at(FreezeKeys::kAttackSeconds) + 1;
        const std::size_t first_win = at(0.5);
        std::vector<float> top(first_win), shifted(first_win);
        for (std::size_t i = 0; i < first_win; ++i) {
            top[i] = source.loop_sample(0, std::int64_t(attack + i), false);
            shifted[i] = source.loop_sample(0, std::int64_t(attack + at(0.01) + i), false);
        }
        double top_best = -2.0;
        long top_lag = 0;
        for (long lag = -long(at(0.005)); lag <= long(at(0.005)); ++lag) {
            const double r = correlation(out.l, std::size_t(long(note_on + attack) + latency + lag),
                                         top, first_win);
            if (r > top_best) { top_best = r; top_lag = lag; }
        }
        const double off = correlation(out.l, note_on + attack + std::size_t(latency), shifted,
                                       first_win);
        CAPTURE(top_best, top_lag, off, latency);
        CHECK(top_best > 0.99);
        CHECK(std::abs(top_lag) <= 2);
        CHECK(std::abs(off) < 0.2);
    }
}

TEST_CASE("Freeze Keys: a sounding loop voice keeps the source on the rings it reads",
          "[freeze-keys][freeze-length]") {
    // A long Length grows the loop rings on a worker; the source adopts the
    // new pair once no loop is held -- but a voice's release tail outlasts
    // the hold's own, and it reads those rings. Freeze, play the root, then
    // unfreeze with a bigger pair on offer: the source must keep its rings
    // while the voice still sounds and adopt the new pair once it has gone.
    // Control: with no voice it adopts at the first hop the hold is gone.
    const auto input = pad(4.0);
    for (const bool with_voice : {true, false}) {
        CAPTURE(with_voice);
        FreezeSource source;
        REQUIRE(source.prepare(kRate, 2));
        source.set_hold_seconds(kLoopHold);
        FreezeKeys keys{source};
        REQUIRE(keys.prepare(kRate, 2));
        keys.set_enabled(true);
        const auto before = source.loop_storage_longest();
        const std::size_t freeze_at = at(1.4), note_at = at(1.8), release_at = at(2.4);
        bool offered = false, adopted_while_sounding = false, voice_outlived_hold = false;
        std::size_t adopted_at = 0;
        Stereo out; out.resize(input.size());
        for (std::size_t pos = 0; pos < input.size(); pos += 128) {
            const auto n = std::min<std::size_t>(128, input.size() - pos);
            source.set_frozen(pos >= freeze_at && pos < release_at);
            if (!offered && pos >= release_at) {
                source.offer_loop_storage(source.allocate_loop_storage(8.0));
                offered = true;
            }
            keys.begin_block();
            if (with_voice && note_at >= pos && note_at < pos + n)
                REQUIRE(keys.note_on(int(note_at - pos), 60, 127));
            const bool sounding = keys.sounding_voices() > 0;
            const float* i[] = {input.l.data() + pos, input.r.data() + pos};
            float* o[] = {out.l.data() + pos, out.r.data() + pos};
            keys.process_block(i, o, 2, int(n));
            if (sounding && !source.hold_audible()) voice_outlived_hold = true;
            if (!adopted_at && source.loop_storage_longest() > before) {
                adopted_at = pos;
                if (sounding) adopted_while_sounding = true;
            }
            source.collect_retired_loop_storage();
        }
        CAPTURE(adopted_at);
        REQUIRE(adopted_at > 0);
        CHECK_FALSE(adopted_while_sounding);
        if (with_voice) {
            CHECK(voice_outlived_hold);   // the window this guards exists
            CHECK(adopted_at > release_at + at(FreezeKeys::kReleaseSeconds));
        } else {
            CHECK(adopted_at < release_at + at(FreezeSource::kCrossfadeSeconds) + 2048);
        }
    }
}

TEST_CASE("Freeze Keys: notes start, stop and switch the bus without a click", "[freeze-keys]") {
    // A sine's second difference is tiny (0.3 (2 pi f / fs)^2: ~1e-4 at
    // 220 Hz); a step anywhere -- a note that starts or stops at full level,
    // the hold cut off under the first note -- is a sample-sized jump many
    // times that. Every edge here is a note-on, a note-off, a chord, the
    // first key into the hold, and the release of Freeze under held notes.
    const auto input = sine(220.0, 4.0);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        CAPTURE(hold.name);
        const auto r = render(input, {{Edge::freeze, at(0.6)},
                                      {Edge::on, at(1.5), 60}, {Edge::off, at(1.9), 60},
                                      {Edge::on, at(2.0), 64}, {Edge::on, at(2.1), 67},
                                      {Edge::off, at(2.5), 64}, {Edge::on, at(2.6), 55},
                                      {Edge::unfreeze, at(3.0)}},
                              hold.seconds);
        double worst = 0.0;
        std::size_t where = 0;
        for (std::size_t n = at(1.0); n + 2 < r.out.size(); ++n)
            for (const auto* x : {&r.out.l, &r.out.r}) {
                const double d2 = std::abs(double((*x)[n + 2]) - 2.0 * (*x)[n + 1] + (*x)[n]);
                if (d2 > worst) { worst = d2; where = n; }
            }
        CAPTURE(worst, double(where) / kRate);
        CHECK(worst < 0.01);
    }
}

TEST_CASE("Freeze Keys: report the cost of an 8-note chord's note-on", "[freeze-keys][.report]") {
    const auto input = pad(3.0);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        for (int block : {128, 256}) {
            std::vector<Edge> edges{{Edge::freeze, at(0.6)}};
            for (int note : {48, 52, 55, 60, 64, 67, 71, 72}) edges.push_back({Edge::on, at(1.5), note});
            const auto chord = render(input, edges, hold.seconds, true, block);
            const auto plain = render(input, {{Edge::freeze, at(0.6)}}, hold.seconds, true, block);
            std::printf("%-8s block %4d: chord note-on call %.3f ms, costliest call %.3f ms; "
                        "freeze alone costliest %.3f ms; budget %.3f ms\n",
                        hold.name, block, chord.max_edge_ms, chord.max_block_ms, plain.max_block_ms,
                        1000.0 * block / kRate);
        }
    }
}

namespace {

void write_wav(const std::string& path, const Stereo& s, std::size_t trim) {
    FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    const auto frames = std::uint32_t(s.size() - trim), bytes = frames * 8;
    const auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(3); u16(2); u32(std::uint32_t(kRate)); u32(std::uint32_t(kRate) * 8); u16(8); u16(32);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (std::size_t n = trim; n < s.size(); ++n) { std::fwrite(&s.l[n], 4, 1, f); std::fwrite(&s.r[n], 4, 1, f); }
    std::fclose(f);
}

} // namespace

TEST_CASE("Freeze Keys: render a melody over a frozen pad to WAV", "[freeze-keys][.render]") {
    // FREEZE_KEYS_WAV_DIR (default /tmp/freeze-keys) must exist.
    const char* env = std::getenv("FREEZE_KEYS_WAV_DIR");
    const std::string dir = env ? env : "/tmp/freeze-keys";
    const auto input = pad(12.0);
    // Freeze at 1 s; a melody from 2 s, a chord, then Freeze released at
    // 10 s and the live pad again.
    std::vector<Edge> edges{{Edge::freeze, at(1.0)}};
    static constexpr int melody[] = {60, 62, 64, 67, 64, 62, 60, 55, 57, 60, 64, 72};
    double t = 2.0;
    for (int note : melody) {
        edges.push_back({Edge::on, at(t), note, 100});
        edges.push_back({Edge::off, at(t + 0.36), note});
        t += 0.42;
    }
    for (int note : {48, 55, 60, 64, 67}) edges.push_back({Edge::on, at(t + 0.2), note, 96});
    for (int note : {48, 55, 60, 64, 67}) edges.push_back({Edge::off, at(t + 2.2), note});
    edges.push_back({Edge::unfreeze, at(10.0)});
    write_wav(dir + "/pad-input.wav", input, 0);
    for (const auto& hold : kHolds) {
        g_restart_loop = hold.restart;
        const auto r = render(input, edges, hold.seconds);
        write_wav(dir + "/melody-over-frozen-pad-" + hold.name + ".wav", r.out, std::size_t(r.latency));
        // The same freeze without keys, for A/B.
        const auto plain = render(input, {{Edge::freeze, at(1.0)}, {Edge::unfreeze, at(10.0)}}, hold.seconds);
        write_wav(dir + "/frozen-pad-no-keys-" + hold.name + ".wav", plain.out, std::size_t(plain.latency));
    }
}

// A host that routes no MIDI to the effect sounds exactly like Freeze Keys
// not working. The product therefore logs what it receives: one line per
// note-on worth reporting (the first, and the first after Freeze changes),
// saying what the note did. This drives the product through the host's MIDI
// buffer and reads those lines back.
TEST_CASE("Freeze Keys: note-ons are counted and reported to the host log, with what they did",
          "[freeze-keys]") {
    const auto input = sine(220.0, 2.6);
    const auto run = [&](bool keys) {
        pulp::format::HeadlessHost host{spectr::create_spectr};
        auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
        REQUIRE(plugin != nullptr);
        plugin->set_freeze_seconds_override(kLoopHold);
        plugin->set_freeze_keys_enabled(keys);
        constexpr int block = 256;
        host.prepare(kRate, block);
        pulp::midi::MidiBuffer mi, mo;
        mi.reserve_events(16);
        // Unfrozen at 0.2 s; Freeze at 0.6 s; then 67 at 1.5 s and 72 at
        // 1.8 s (the same Freeze state: counted, not reported again).
        const std::pair<std::size_t, int> notes[] = {{at(0.2), 67}, {at(1.5), 67}, {at(1.8), 72}};
        for (std::size_t pos = 0; pos < input.size(); pos += block) {
            const auto n = std::min<std::size_t>(block, input.size() - pos);
            if (pos <= at(0.6) && at(0.6) < pos + n) host.state().set_value(spectr::kParamFreeze, 1.0f);
            mi.clear();
            for (const auto& [when, note] : notes)
                if (when >= pos && when < pos + n) {
                    auto m = pulp::midi::MidiEvent::note_on(0, std::uint8_t(note), 100);
                    m.sample_offset = std::int32_t(when - pos);
                    mi.add(m);
                }
            pulp::audio::Buffer<float> ib(2, n), ob(2, n);
            std::copy_n(input.l.begin() + long(pos), n, ib.channel(0).begin());
            std::copy_n(input.r.begin() + long(pos), n, ib.channel(1).begin());
            const float* ip[] = {ib.channel(0).data(), ib.channel(1).data()};
            pulp::audio::BufferView<const float> iv(ip, 2, n);
            auto ov = ob.view();
            pulp::state::ParameterEventQueue events;
            pulp::format::ProcessContext ctx;
            host.process(ov, iv, mi, mo, events, ctx);
            // The report of the 1.5 s note is printed by a worker: give it
            // time before the next one could be queued.
            if (plugin->freeze_keys_note_ons() == 1 && pos > at(1.0)) {
                for (int i = 0; i < 200 && plugin->freeze_keys_reports_logged() < 1; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        const std::uint64_t want = keys ? 2 : 0;
        for (int i = 0; i < 400 && plugin->freeze_keys_reports_logged() < want; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return std::tuple{plugin->freeze_keys_note_ons(), plugin->freeze_keys_reports_logged(),
                          plugin->freeze_keys_last_report()};
    };

    SECTION("a Freeze Keys build counts every note-on and reports the first per Freeze state") {
        auto [ons, logged, last] = run(true);
        CHECK(ons == 3);
        CHECK(logged == 2);
        CAPTURE(last);
        CHECK(last.find("[spectr-keys] note-on 67 (G3) vel 100") == 0);
        CHECK(last.find("frozen=1") != std::string::npos);
        CHECK(last.find("hold=loop") != std::string::npos);
        CHECK(last.find("mode=keys voices=1") != std::string::npos);
        CHECK(last.find("transpose=+7 st (root C3)") != std::string::npos);
        CHECK(last.find("note-ons=2") != std::string::npos);
        CHECK(last.find("-> playing") != std::string::npos);
    }
    SECTION("control: with Freeze Keys off nothing is read, counted or logged") {
        auto [ons, logged, last] = run(false);
        CHECK(ons == 0);
        CHECK(logged == 0);
        CHECK(last.empty());
    }
}

TEST_CASE("Freeze Keys: the report line says why a note did nothing", "[freeze-keys]") {
    spectr::FreezeKeysNoteReport r;
    r.note_ons = 1;
    r.note = 60;
    r.velocity = 90;
    r.sample_rate = 44100.0;
    const auto off = spectr::freeze_keys_report_line(r);
    CAPTURE(off);
    CHECK(off.find("note-on 60 (C3) vel 90 at +0: frozen=0 phase=live") != std::string::npos);
    CHECK(off.find("-> ignored: Freeze is off") != std::string::npos);
    r.frozen = true;
    r.phase = FreezeSource::Phase::arming;
    const auto arming = spectr::freeze_keys_report_line(r);
    CAPTURE(arming);
    CHECK(arming.find("-> waiting: the hold is not audible yet") != std::string::npos);
    r.phase = FreezeSource::Phase::held;
    r.keys_mode = true;
    r.voices = 2;
    r.note = 48;
    const auto playing = spectr::freeze_keys_report_line(r);
    CAPTURE(playing);
    CHECK(playing.find("note-on 48 (C2)") != std::string::npos);
    CHECK(playing.find("transpose=-12 st") != std::string::npos);
    CHECK(playing.find("-> playing") != std::string::npos);
}
