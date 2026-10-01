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

struct Render {
    Stereo out;
    int latency = 0;
    double max_block_ms = 0.0; // costliest process() call
    double max_edge_ms = 0.0;  // costliest call that delivered a note-on
};

Render render(const Stereo& in, std::vector<Edge> edges, double hold_seconds,
              bool keys = true, int block = 256,
              MaskRenderMode mode = MaskRenderMode::zero_latency) {
    pulp::format::HeadlessHost host{spectr::create_spectr};
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->set_render_mode(mode));
    plugin->set_freeze_hold_seconds(hold_seconds);
    plugin->set_freeze_keys_enabled(keys);
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

struct HoldCase { const char* name; double seconds; };
const HoldCase kHolds[] = {{"spectral", kSpectralHold}, {"loop", kLoopHold}};

double ratio(int semitones) { return std::exp2(semitones / 12.0); }

} // namespace

TEST_CASE("Freeze Keys: the root note plays the hold at its pitch, other keys transpose",
          "[freeze-keys]") {
    const auto input = sine(220.0, 3.0);
    for (const auto& hold : kHolds) {
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

TEST_CASE("Freeze Keys: a chord plays every one of its pitches", "[freeze-keys]") {
    const auto input = sine(220.0, 3.0);
    for (const auto& hold : kHolds) {
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
            CAPTURE(m.name, hold.name);
            const auto root = play(m.audio, hold.seconds, {60});
            const auto fifth = play(m.audio, hold.seconds, {67});
            const auto rp = peaks(root.out, window_start(root), kWindow, 1, 1500.0);
            REQUIRE(rp.size() == 1);
            // The same partial, a fifth up: the fifth render's peak nearest it.
            const double expected = rp[0].first * ratio(7);
            const auto fp = peaks(fifth.out, window_start(fifth), kWindow, 12, 3000.0);
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

TEST_CASE("Freeze Keys: report the cost of an 8-note chord's note-on", "[freeze-keys][.report]") {
    const auto input = pad(3.0);
    for (const auto& hold : kHolds) {
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
