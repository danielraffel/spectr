// Glitches a listener reported, each reproduced through the whole processor.
//
//   1. Switching Latency (Mixing <-> Tracking) dropped out and clicked.
//   2. In Tracking, engaging or releasing FROZEN sometimes sounded staticky.
//   3. In Tracking, dragging bands, or letting a Freeze loop run, sounded
//      glitchy.
//
// THE DETECTORS. A dropout is a span the output's short-term level falls far
// below the level around it: scored as 5 ms RMS windows against the steady
// level before the event, and reported as milliseconds below -12 dB. A click
// is energy the signal's own recent past does not predict: an order-32
// linear predictor is fitted to the steady output before the event, the same
// whitening filter is run over the event window, and each residual sample is
// scored against the residual's RMS in the 4 ms around it (excluding its own
// half millisecond) -- the detector test_freeze_click.cpp scores Freeze's
// engage with. The same score over a steady window of the SAME render is the
// control: what the material does by itself. Each gate states its negative
// control, which restores the defect and must fail.

#include <catch2/catch_test_macros.hpp>

#include <pulp/format/headless.hpp>
#include <pulp/state/parameter_event_queue.hpp>

#include "spectr/freeze_length.hpp"
#include "spectr/level_controls.hpp"
#include "spectr/modulation.hpp"
#include "spectr/param_surface.hpp"
#include "spectr/upstream/processing_switch_crossfade.hpp"
#include "spectr/spectr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <ctime>
#include <chrono>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

using spectr::MaskRenderMode;
using spectr::Spectr;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Stereo {
    std::vector<float> l, r;
    std::size_t size() const { return l.size(); }
    void resize(std::size_t n) { l.assign(n, 0.0f); r.assign(n, 0.0f); }
};

std::size_t at(double seconds, double rate) {
    return static_cast<std::size_t>(std::llround(seconds * rate));
}

// ── Material ───────────────────────────────────────────────────────────────
// Deterministic synthesis only.

/// A sustained chord with a slow vibrato and a breath of noise: steady enough
/// that a predictor fitted to it whitens it, busy enough to be music.
Stereo chord(double seconds, double rate, unsigned seed = 3) {
    Stereo s; s.resize(at(seconds, rate));
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, 1.0);
    const double notes[] = {130.81, 196.0, 261.63, 329.63, 392.0, 523.25};
    std::vector<double> phase(std::size(notes), 0.0);
    double lp_l = 0.0, lp_r = 0.0;
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / rate;
        double l = 0.0, r = 0.0;
        for (std::size_t k = 0; k < std::size(notes); ++k) {
            const double f = notes[k] * (1.0 + 0.003 * std::sin(2.0 * kPi * (4.7 + 0.3 * double(k)) * t));
            phase[k] += 2.0 * kPi * f / rate;
            const double v = std::sin(phase[k]) / double(k + 1) * 0.9;
            l += v * (k % 2 ? 0.8 : 1.0);
            r += v * (k % 2 ? 1.0 : 0.8);
        }
        lp_l += 0.05 * (g(rng) - lp_l);
        lp_r += 0.05 * (g(rng) - lp_r);
        s.l[n] = float(0.18 * l + 0.01 * lp_l);
        s.r[n] = float(0.18 * r + 0.01 * lp_r);
    }
    return s;
}

/// Kick, snare and hats over a bass line at 120 BPM: transients a predictor
/// cannot whiten, so a gate on it has to beat what the drums score alone.
Stereo drums(double seconds, double rate, unsigned seed = 7) {
    Stereo s; s.resize(at(seconds, rate));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    const double beat = 0.5;
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / rate;
        const double in_beat = std::fmod(t, beat);
        const int beat_index = int(t / beat);
        double v = 0.0;
        if (beat_index % 2 == 0) {
            const double f = 50.0 + 100.0 * std::exp(-in_beat / 0.03);
            v += 0.7 * std::sin(2.0 * kPi * f * in_beat) * std::exp(-in_beat / 0.25);
        } else {
            v += 0.35 * u(rng) * std::exp(-in_beat / 0.09);
        }
        const double in_eighth = std::fmod(t, beat / 2.0);
        v += 0.08 * u(rng) * std::exp(-in_eighth / 0.015);
        v += 0.2 * std::sin(2.0 * kPi * 55.0 * t);
        s.l[n] = float(0.6 * v);
        s.r[n] = float(0.6 * v);
    }
    return s;
}

/// Plucked notes separated by true silence: a Freeze pressed in a gap arms
/// and latches on the next onset.
Stereo staccato(double seconds, double rate) {
    Stereo s; s.resize(at(seconds, rate));
    const double notes[] = {220.0, 277.18, 329.63, 440.0, 392.0, 293.66};
    const double every = 0.375, length = 0.22;
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = double(n) / rate;
        const int k = int(t / every);
        const double in = std::fmod(t, every);
        if (in >= length) continue;
        const double f = notes[std::size_t(k) % std::size(notes)];
        const double env = std::min(1.0, in / 0.004) * std::exp(-in / 0.09)
                         * std::min(1.0, (length - in) / 0.01);
        const double v = 0.5 * env * (std::sin(2.0 * kPi * f * in) + 0.3 * std::sin(4.0 * kPi * f * in));
        s.l[n] = float(v);
        s.r[n] = float(v);
    }
    return s;
}

// ── Detectors ──────────────────────────────────────────────────────────────

constexpr int kOrder = 32;

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

std::vector<double> whiten(const std::vector<float>& x, const std::vector<double>& a,
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

/// Worst residual sample against the residual RMS in the 4 ms around it.
double spike_db(const std::vector<double>& e, double rate, std::size_t* where = nullptr) {
    const std::size_t outer = at(0.004, rate), inner = at(0.0005, rate);
    std::vector<double> prefix(e.size() + 1, 0.0);
    for (std::size_t i = 0; i < e.size(); ++i) prefix[i + 1] = prefix[i] + e[i] * e[i];
    const auto energy = [&](std::size_t a, std::size_t b) { return prefix[b] - prefix[a]; };
    double worst = -200.0;
    for (std::size_t n = outer; n + outer + 1 < e.size(); ++n) {
        const double around = energy(n - outer, n - inner) + energy(n + inner + 1, n + outer + 1);
        const double rms = std::sqrt(around / double(2 * (outer - inner)) + 1e-24);
        const double db = 20.0 * std::log10(std::abs(e[n]) / rms + 1e-12);
        if (db > worst) { worst = db; if (where) *where = n; }
    }
    return worst;
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t n) {
    double acc = 0.0;
    for (std::size_t i = from; i < from + n && i < x.size(); ++i) acc += double(x[i]) * x[i];
    return std::sqrt(acc / double(std::max<std::size_t>(1, n)));
}

struct EventScore {
    double spike_db = -200.0;     ///< worst whitened spike over the event window
    double control_db = -200.0;   ///< the same score over a steady window of this render
    double min_level_db = 0.0;    ///< lowest 5 ms level against the steady level
    double gap_ms = 0.0;          ///< time below -12 dB of the steady level
};

/// Score `x` over [from, to) against the steady stretch [steady_from, from).
/// The spike detector needs 4 ms of neighbourhood either side of a sample, so
/// its window opens 10 ms before `from`: an event ON the first sample (a cut)
/// is scored, not skipped. The fit stops where that window opens.
EventScore score_event(const std::vector<float>& x, double rate, std::size_t steady_from,
                       std::size_t from, std::size_t to, std::size_t control_from) {
    EventScore s;
    const std::size_t lead = at(0.010, rate);
    const auto a = fit_whitener(x, steady_from, from - lead);
    s.spike_db = spike_db(whiten(x, a, from - lead, to), rate);
    s.control_db = spike_db(whiten(x, a, control_from, control_from + (to - from) + lead), rate);
    const double steady = rms(x, steady_from, from - steady_from);
    const std::size_t w = at(0.005, rate);
    double worst = 0.0;
    std::size_t below = 0;
    for (std::size_t p = from; p + w <= to; p += w) {
        const double db = 20.0 * std::log10(rms(x, p, w) / (steady + 1e-12) + 1e-12);
        worst = std::min(worst, db);
        if (db < -12.0) ++below;
    }
    s.min_level_db = worst;
    s.gap_ms = 5.0 * double(below);
    return s;
}

// ── Processor harness ──────────────────────────────────────────────────────

struct Run {
    double rate = 48000.0;
    int block = 256;
    MaskRenderMode mode = MaskRenderMode::zero_latency;
    float auto_gain = 0.0f;
    double tempo = 120.0;
    /// Vary each callback's length uniformly in [1, block] from this seed (0:
    /// fixed), as a host that splits its buffer at automation and loop
    /// points does.
    unsigned random_blocks = 0;
    /// Mark this stream sample's block as a host reset (0: never).
    std::size_t reset_at = 0;
    /// Draw a shape before the stream: a deep cut, a boost and a mute, the
    /// kind of field a user plays a Freeze through.
    bool shaped = false;
    /// Called before each block with the block's first sample; may edit the
    /// processor or queue sample-accurate parameter events.
    std::function<void(Spectr&, pulp::format::HeadlessHost&, std::size_t pos, int n,
                       pulp::state::ParameterEventQueue&)> before;
};

double thread_cpu_us() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return double(ts.tv_sec) * 1e6 + double(ts.tv_nsec) * 1e-3;
}

/// Thread CPU time of each host callback, in microseconds, by block start.
struct BlockCosts {
    std::vector<std::size_t> start;
    std::vector<double> us;
    /// Worst callback in [from, to) of the stream.
    double worst(std::size_t from, std::size_t to) const {
        double w = 0.0;
        for (std::size_t i = 0; i < start.size(); ++i)
            if (start[i] >= from && start[i] < to) w = std::max(w, us[i]);
        return w;
    }
};

Stereo render(const Stereo& input, const Run& run, Spectr** out_plugin = nullptr,
              std::unique_ptr<pulp::format::HeadlessHost>* keep = nullptr,
              BlockCosts* costs = nullptr) {
    auto host = std::make_unique<pulp::format::HeadlessHost>(spectr::create_spectr);
    auto* plugin = dynamic_cast<Spectr*>(host->processor());
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->set_render_mode(run.mode));
    host->prepare(run.rate, run.block);
    host->state().set_value(spectr::kParamAutoGain, run.auto_gain);
    if (run.shaped) {
        auto field = plugin->processing_state_snapshot().field;
        for (std::size_t b = 0; b < field.bands.size(); ++b) {
            if (b % 8 == 2) field.bands[b].gain_db = -24.0f;
            if (b % 8 == 5) field.bands[b].gain_db = 9.0f;
            if (b % 11 == 7) field.bands[b].muted = true;
        }
        plugin->replace_field(field);
    }
    Stereo out; out.resize(input.size());
    pulp::midi::MidiBuffer mi, mo;
    std::mt19937 block_rng(run.random_blocks);
    std::uniform_int_distribution<int> block_len(1, run.block);
    std::size_t n = 0;
    for (std::size_t pos = 0; pos < input.size(); pos += n) {
        n = std::min<std::size_t>(
            run.random_blocks ? std::size_t(block_len(block_rng)) : std::size_t(run.block),
            input.size() - pos);
        pulp::state::ParameterEventQueue events;
        pulp::format::ProcessContext ctx;
        ctx.sample_rate = run.rate;
        ctx.tempo_bpm = run.tempo;
        ctx.is_playing = true;
        ctx.reset_requested = run.reset_at != 0 && run.reset_at >= pos && run.reset_at < pos + n;
        if (run.before) run.before(*plugin, *host, pos, int(n), events);
        pulp::audio::Buffer<float> inb(2, n), outb(2, n);
        std::copy(input.l.begin() + long(pos), input.l.begin() + long(pos + n), inb.channel(0).begin());
        std::copy(input.r.begin() + long(pos), input.r.begin() + long(pos + n), inb.channel(1).begin());
        const float* ip[] = {inb.channel(0).data(), inb.channel(1).data()};
        pulp::audio::BufferView<const float> iv(ip, 2, n);
        auto ov = outb.view();
        const double t0 = thread_cpu_us();
        host->process(ov, iv, mi, mo, events, ctx);
        if (costs) { costs->start.push_back(pos); costs->us.push_back(thread_cpu_us() - t0); }
        // What an adapter commits once the block is done.
        for (const auto& e : events.events()) host->state().set_value(e.param_id, e.value);
        std::copy(outb.channel(0).begin(), outb.channel(0).end(), out.l.begin() + long(pos));
        std::copy(outb.channel(1).begin(), outb.channel(1).end(), out.r.begin() + long(pos));
    }
    if (out_plugin) *out_plugin = plugin;
    if (keep) *keep = std::move(host);
    return out;
}

/// A UI-thread action at a wall-clock offset into a real-time render.
struct UiAction {
    double at_seconds;
    std::function<void(Spectr&, pulp::format::HeadlessHost&)> act;
};

/// Render as a host does: the processor on its own thread, paced so each
/// block is delivered no earlier than its real-time deadline, while the UI
/// actions run on THIS thread at their wall-clock times. What an offline
/// render cannot reproduce -- when the design and sync workers finish
/// relative to the audio, and a UI edit landing mid-block -- happens here as
/// it does in a DAW. Not bit-reproducible by construction; read it as a
/// distribution.
Stereo render_realtime(const Stereo& input, const Run& run, const std::vector<UiAction>& ui,
                       BlockCosts* costs = nullptr) {
    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto* plugin = dynamic_cast<Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->set_render_mode(run.mode));
    host.prepare(run.rate, run.block);
    host.state().set_value(spectr::kParamAutoGain, run.auto_gain);
    if (run.shaped) {
        auto field = plugin->processing_state_snapshot().field;
        for (std::size_t b = 0; b < field.bands.size(); ++b) {
            if (b % 8 == 2) field.bands[b].gain_db = -24.0f;
            if (b % 8 == 5) field.bands[b].gain_db = 9.0f;
            if (b % 11 == 7) field.bands[b].muted = true;
        }
        plugin->replace_field(field);
    }
    Stereo out; out.resize(input.size());
    std::atomic<bool> started{false};
    const auto t0 = std::chrono::steady_clock::now();
    std::thread audio([&] {
        pulp::midi::MidiBuffer mi, mo;
        started.store(true);
        for (std::size_t pos = 0; pos < input.size(); pos += std::size_t(run.block)) {
            const auto n = std::min<std::size_t>(std::size_t(run.block), input.size() - pos);
            const auto due = t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(double(pos) / run.rate));
            std::this_thread::sleep_until(due);
            pulp::state::ParameterEventQueue events;
            pulp::format::ProcessContext ctx;
            ctx.sample_rate = run.rate;
            ctx.tempo_bpm = run.tempo;
            ctx.is_playing = true;
            pulp::audio::Buffer<float> inb(2, n), outb(2, n);
            std::copy(input.l.begin() + long(pos), input.l.begin() + long(pos + n), inb.channel(0).begin());
            std::copy(input.r.begin() + long(pos), input.r.begin() + long(pos + n), inb.channel(1).begin());
            const float* ip[] = {inb.channel(0).data(), inb.channel(1).data()};
            pulp::audio::BufferView<const float> iv(ip, 2, n);
            auto ov = outb.view();
            const double c0 = thread_cpu_us();
            host.process(ov, iv, mi, mo, events, ctx);
            if (costs) { costs->start.push_back(pos); costs->us.push_back(thread_cpu_us() - c0); }
            std::copy(outb.channel(0).begin(), outb.channel(0).end(), out.l.begin() + long(pos));
            std::copy(outb.channel(1).begin(), outb.channel(1).end(), out.r.begin() + long(pos));
        }
    });
    for (const auto& a : ui) {
        std::this_thread::sleep_until(t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(a.at_seconds)));
        a.act(*plugin, host);
    }
    audio.join();
    return out;
}

const char* mode_name(MaskRenderMode m) {
    return m == MaskRenderMode::linear_phase ? "Mixing" : "Tracking";
}

} // namespace

// ── 1. Latency switch ───────────────────────────────────────────────────────

TEST_CASE("The switch plan warms the incoming renderer and then fades at equal power",
          "[render-mode][render-switch]") {
    namespace sw = pulp_candidate::signal;
    const auto plan = sw::plan_processing_switch(10240, 0, 48000.0, 0.03);
    CHECK(plan.warm_samples == 10240);
    CHECK(plan.fade_samples == 1440);
    const auto fir = sw::plan_processing_switch(64, 8192, 96000.0, 0.03);
    CHECK(fir.warm_samples == 64 + 8192);
    CHECK(fir.fade_samples == 2880);
    // The schedule as a pure function: silent incoming while warming, equal
    // power through the fade, all incoming after it.
    for (std::int64_t p = 0; p <= plan.total_samples() + 10; p += 7) {
        float o = 0.0f, i = 0.0f;
        sw::processing_switch_gains_at(plan, p, o, i);
        if (p < plan.warm_samples) { CHECK(o == 1.0f); CHECK(i == 0.0f); }
        CHECK(std::abs(double(o) * o + double(i) * i - 1.0) < 1e-5);
        if (p >= plan.total_samples()) { CHECK(o == 0.0f); CHECK(i == 1.0f); }
    }
    // The state machine applies exactly that schedule.
    sw::ProcessingSwitchCrossfade xf;
    xf.begin({4, 4});
    float o[12], in[12];
    for (int k = 0; k < 12; ++k) { o[k] = 1.0f; in[k] = -1.0f; }
    float* op[] = {o};
    const float* ipp[] = {in};
    xf.mix(op, ipp, 1, 12);
    CHECK(xf.finished());
    for (int k = 0; k < 4; ++k) CHECK(o[k] == 1.0f);
    for (int k = 8; k < 12; ++k) CHECK(o[k] == -1.0f);
}

namespace {

struct SwitchCase {
    MaskRenderMode from;
    double rate;
    int block;
};

struct SwitchResult {
    EventScore score;
    std::size_t switch_at = 0;
};

SwitchResult measure_switch(const SwitchCase& c, const Stereo& material) {
    const MaskRenderMode to = c.from == MaskRenderMode::linear_phase
        ? MaskRenderMode::zero_latency : MaskRenderMode::linear_phase;
    const std::size_t switch_at = at(1.6, c.rate);
    Run run;
    run.rate = c.rate;
    run.block = c.block;
    run.mode = c.from;
    bool switched = false;
    std::size_t switched_at = 0;
    run.before = [&](Spectr& p, pulp::format::HeadlessHost&, std::size_t pos, int,
                     pulp::state::ParameterEventQueue&) {
        if (!switched && pos >= switch_at) {
            REQUIRE(p.set_render_mode(to));
            switched = true;
            switched_at = pos;
        }
    };
    const auto out = render(material, run);
    REQUIRE(switched);
    SwitchResult r;
    r.switch_at = switched_at;
    // The event: from the switch to well past the slowest warm (Mixing's
    // latency) plus the fade. The steady stretch and the control window come
    // from the same render, before the switch.
    const std::size_t steady_from = switched_at - at(0.5, c.rate);
    const std::size_t to_ = switched_at + at(0.45, c.rate);
    const std::size_t control_from = at(0.6, c.rate);
    r.score = score_event(out.l, c.rate, steady_from, switched_at, to_, control_from);
    return r;
}

bool hard_switch_planted() {
    return std::getenv("SPECTR_PLANT_HARD_RENDER_SWITCH") != nullptr;
}

} // namespace

TEST_CASE("A Latency switch neither drops out nor clicks in either direction",
          "[render-mode][render-switch][audio]") {
    // Mixing and Tracking differ by thousands of samples of delay, so the
    // material moves in time across a switch -- that is what changing the
    // latency means, and the host re-aligns it. What a switch must not do is
    // fall silent while the new renderer fills its delay line (213 ms into
    // Mixing at 48 kHz) or cut between two waveforms. The new renderer is
    // warmed on the live input while the old one is heard, then crossfaded in
    // at equal power. Negative control: SPECTR_PLANT_HARD_RENDER_SWITCH
    // restores the cut; it must fail.
    std::printf("\nLatency switch: dropout and click, chord material\n"
                "  %-24s %6s %5s  %9s %9s %9s %8s\n",
                "direction", "rate", "block", "spike dB", "control", "min lvl", "gap ms");
    bool all_pass = true;
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        const auto material = chord(2.4, rate);
        for (const int block : {64, 256, 1024}) {
            for (const auto from : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
                const auto r = measure_switch({from, rate, block}, material);
                const auto& s = r.score;
                const bool pass = s.gap_ms == 0.0 && s.min_level_db > -6.0
                    && s.spike_db <= s.control_db + 6.0;
                all_pass = all_pass && pass;
                std::printf("  %-8s -> %-12s %6.0f %5d  %9.1f %9.1f %9.1f %8.1f %s\n",
                            mode_name(from),
                            mode_name(from == MaskRenderMode::linear_phase
                                          ? MaskRenderMode::zero_latency
                                          : MaskRenderMode::linear_phase),
                            rate, block, s.spike_db, s.control_db, s.min_level_db,
                            s.gap_ms, pass ? "" : "FAIL");
            }
        }
    }
    if (hard_switch_planted()) {
        // The planted cut must be caught -- by the gap, the click, or both.
        REQUIRE_FALSE(all_pass);
        // Reported as a failure so the registered negative control (WILL_FAIL)
        // passes only when the plant was seen.
        FAIL("planted hard switch detected, as it must be");
    }
    REQUIRE(all_pass);
}

TEST_CASE("A Latency switch keeps a playing hold and lands in the new mode",
          "[render-mode][render-switch][freeze]") {
    // Freeze's source is run once per block during the fade, however many
    // renderers listen: a held sound keeps sounding through the switch.
    const double rate = 48000.0;
    const auto material = chord(3.0, rate);
    Run run;
    run.rate = rate;
    run.block = 256;
    run.mode = MaskRenderMode::zero_latency;
    bool switched = false;
    std::size_t switched_at = 0;
    Spectr* plugin = nullptr;
    std::unique_ptr<pulp::format::HeadlessHost> keep;
    run.before = [&](Spectr& p, pulp::format::HeadlessHost& h, std::size_t pos, int,
                     pulp::state::ParameterEventQueue&) {
        if (pos == 0) {
            h.state().set_value(spectr::kParamFreeze, 0.0f);
        }
        if (pos >= at(0.8, rate) && pos < at(0.8, rate) + 256)
            h.state().set_value(spectr::kParamFreeze, 1.0f);
        if (!switched && pos >= at(1.8, rate)) {
            REQUIRE(p.set_render_mode(MaskRenderMode::linear_phase));
            switched = true;
            switched_at = pos;
        }
    };
    const auto out = render(material, run, &plugin, &keep);
    REQUIRE(switched);
    REQUIRE(plugin->render_mode() == MaskRenderMode::linear_phase);
    REQUIRE_FALSE(plugin->render_switch_in_flight());
    // The old renderer is freed off the audio thread as soon as the fade
    // ends, not left alive (a GPU renderer keeps a service thread) until the
    // next control call.
    for (int i = 0; i < 200 && !plugin->render_switch_settled(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(plugin->render_switch_settled());
    const auto s = score_event(out.l, rate, switched_at - at(0.5, rate), switched_at,
                               switched_at + at(0.45, rate), at(1.0, rate));
    std::printf("\nLatency switch under a playing hold: spike %.1f dB (control %.1f), "
                "min level %.1f dB, gap %.1f ms\n",
                s.spike_db, s.control_db, s.min_level_db, s.gap_ms);
    CHECK(s.gap_ms == 0.0);
    CHECK(s.spike_db <= s.control_db + 6.0);
}

TEST_CASE("A host reset during a Latency switch completes it instead of warming again",
          "[render-mode][render-switch]") {
    // A host that answers the latency change by resetting the plugin (a
    // transport re-sync) resets the OLD renderer too. Warming again from
    // there would hold the old Mixing renderer -- silent for its own 213 ms
    // latency after a reset -- in the output. The switch completes at the
    // reset instead: the new renderer starts as a reset starts any renderer.
    const double rate = 48000.0;
    const auto material = chord(3.0, rate);
    Run run;
    run.rate = rate;
    run.block = 256;
    run.mode = MaskRenderMode::linear_phase;
    const std::size_t switch_at = at(1.6, rate);
    run.reset_at = switch_at + 2 * 256 + 5;
    bool switched = false;
    std::size_t switched_at = 0;
    Spectr* plugin = nullptr;
    std::unique_ptr<pulp::format::HeadlessHost> keep;
    run.before = [&](Spectr& p, pulp::format::HeadlessHost&, std::size_t pos, int,
                     pulp::state::ParameterEventQueue&) {
        if (!switched && pos >= switch_at) {
            REQUIRE(p.set_render_mode(MaskRenderMode::zero_latency));
            switched = true;
            switched_at = pos;
        }
    };
    const auto out = render(material, run, &plugin, &keep);
    REQUIRE(switched);
    const auto s = score_event(out.l, rate, switched_at - at(0.5, rate), switched_at,
                               switched_at + at(0.4, rate), at(0.6, rate));
    std::printf("\nLatency switch Mixing -> Tracking with a host reset 2 blocks in: "
                "min level %.1f dB, gap %.1f ms\n", s.min_level_db, s.gap_ms);
    // Tracking's own reset latency is one 64-sample render block: under a
    // 5 ms window. Warming again would leave ~210 ms of silence.
    CHECK(s.gap_ms <= 5.0);
    REQUIRE_FALSE(plugin->render_switch_in_flight());
}

TEST_CASE("A Latency switch before the stream starts takes effect at once",
          "[render-mode][render-switch]") {
    // A host restoring a session's mode after it prepared the plug-in but
    // before the first render: nothing has been heard, so there is nothing to
    // fade from, and the stream must start in the restored mode -- an impulse
    // at sample 0 out at the new mode's latency, at full level.
    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto* plugin = dynamic_cast<Spectr*>(host.processor());
    REQUIRE(plugin->set_render_mode(MaskRenderMode::zero_latency));
    host.prepare(48000.0, 512);
    host.state().set_value(spectr::kParamAutoGain, 0.0f);
    REQUIRE(plugin->set_render_mode(MaskRenderMode::linear_phase));
    const int latency = plugin->latency_samples();
    pulp::midi::MidiBuffer mi, mo;
    std::vector<float> out_l;
    for (int b = 0; b < 30; ++b) {
        pulp::audio::Buffer<float> inb(2, 512), outb(2, 512);
        if (b == 0) { inb.channel(0)[0] = 0.5f; inb.channel(1)[0] = 0.5f; }
        const float* ip[] = {inb.channel(0).data(), inb.channel(1).data()};
        pulp::audio::BufferView<const float> iv(ip, 2, 512);
        auto ov = outb.view();
        pulp::state::ParameterEventQueue events;
        pulp::format::ProcessContext ctx;
        ctx.sample_rate = 48000.0;
        host.process(ov, iv, mi, mo, events, ctx);
        out_l.insert(out_l.end(), outb.channel(0).begin(), outb.channel(0).end());
    }
    INFO("latency " << latency << ", out at latency " << out_l[std::size_t(latency)]);
    CHECK(std::abs(out_l[std::size_t(latency)] - 0.5f) < 0.01f);
}

// ── 2/3. Reports: Freeze engage/release, band drags, loop seams (Tracking) ─

TEST_CASE("Glitch report: Freeze engage and release in Tracking",
          "[.][glitch-report]") {
    std::printf("\nFreeze engage/release, Tracking (spike dB over control)\n"
                "  %-7s %6s %5s %-8s %9s %9s %9s %9s\n",
                "mat", "rate", "block", "length", "eng", "rel", "eng-ctl", "gap");
    for (const bool product : {false, true})
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const int block : {64, 256, 1024}) {
            for (const int length_index : {spectr::kDefaultLengthPreset, 0, 8}) {
                for (int material_index = 0; material_index < 2; ++material_index) {
                    const auto material = material_index == 0 ? chord(4.0, rate) : drums(4.0, rate);
                    std::mt19937 rng(unsigned(rate) + unsigned(block) * 7 + unsigned(length_index));
                    std::uniform_int_distribution<int> jitter(0, block - 1);
                    const std::size_t engage = at(1.5, rate) + std::size_t(jitter(rng));
                    const std::size_t release = at(2.7, rate) + std::size_t(jitter(rng));
                    Run run;
                    run.rate = rate;
                    run.block = block;
                    run.mode = MaskRenderMode::zero_latency;
                    // `product`: a drawn shape with AUTO on, as an instance is used.
                    run.shaped = product;
                    run.auto_gain = product ? 1.0f : 0.0f;
                    run.before = [&](Spectr&, pulp::format::HeadlessHost& h, std::size_t pos, int n,
                                     pulp::state::ParameterEventQueue& ev) {
                        if (pos == 0) h.state().set_value(spectr::kParamFreezeLength, float(length_index));
                        if (engage >= pos && engage < pos + std::size_t(n))
                            (void)ev.push({spectr::kParamFreeze, std::int32_t(engage - pos), 1.0f, 0});
                        if (release >= pos && release < pos + std::size_t(n))
                            (void)ev.push({spectr::kParamFreeze, std::int32_t(release - pos), 0.0f, 0});
                    };
                    const auto out = render(material, run);
                    const auto e = score_event(out.l, rate, engage - at(0.4, rate), engage,
                                               engage + at(0.12, rate), at(0.6, rate));
                    const auto r = score_event(out.l, rate, release - at(0.4, rate), release,
                                               release + at(0.12, rate), at(0.6, rate));
                    std::printf("  %-7s %6.0f %5d %-8d %9.1f %9.1f %9.1f %9.1f\n",
                                (std::string(material_index == 0 ? "chord" : "drums") + (product ? "*" : "")).c_str(),
                                rate, block,
                                length_index, e.spike_db - e.control_db,
                                r.spike_db - r.control_db, e.control_db, e.gap_ms + r.gap_ms);
                }
            }
        }
    }
}

TEST_CASE("Glitch report: band drags in Tracking and Mixing", "[.][glitch-report]") {
    std::printf("\nBand drag at 60 Hz control rate (spike dB over control)\n"
                "  %-8s %6s %5s %9s %9s\n", "mode", "rate", "block", "spike", "control");
    for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
        for (const double rate : {48000.0, 96000.0}) {
            for (const int block : {64, 256, 1024}) {
                const auto material = chord(3.5, rate);
                const std::size_t start = at(1.5, rate), stop = at(2.7, rate);
                const std::size_t every = at(1.0 / 60.0, rate);
                std::size_t next = start;
                Run run;
                run.rate = rate;
                run.block = block;
                run.mode = mode;
                run.before = [&](Spectr& p, pulp::format::HeadlessHost&, std::size_t pos, int n,
                                 pulp::state::ParameterEventQueue&) {
                    while (next >= pos && next < pos + std::size_t(n) && next < stop) {
                        auto field = p.processing_state_snapshot().field;
                        const double t = double(next - start) / double(stop - start);
                        const auto bands = field.bands.size();
                        for (std::size_t b = 0; b < bands; ++b) {
                            // A drag across a group of bands: a moving dip.
                            const double centre = 0.2 + 0.5 * t;
                            const double x = double(b) / double(bands);
                            field.bands[b].gain_db = float(-24.0 * std::exp(-std::pow((x - centre) / 0.06, 2.0)));
                        }
                        p.replace_field(field);
                        next += every;
                    }
                };
                const auto out = render(material, run);
                const auto s = score_event(out.l, rate, start - at(0.5, rate), start,
                                           stop, at(0.3, rate));
                std::printf("  %-8s %6.0f %5d %9.1f %9.1f\n", mode_name(mode), rate, block,
                            s.spike_db - s.control_db, s.control_db);
            }
        }
    }
}

TEST_CASE("Glitch report: callback cost at Freeze engage and release and at a band drag",
          "[.][glitch-report]") {
    // An offline render cannot hear a dropped buffer: a callback that runs
    // past its budget renders perfectly here and drops out in a host. So
    // the cost of the callbacks around each transition, against the steady
    // callbacks before it, is the measurement that can see an xrun.
    std::printf("\nCallback thread-CPU at transitions, Tracking (us; budget = block/rate)\n"
                "  %-12s %6s %5s %8s %9s %9s %9s %9s\n",
                "event", "rate", "block", "budget", "steady", "engage", "release", "ratio");
    for (const bool auto_on : {false, true}) {
        for (const double rate : {48000.0, 96000.0}) {
            for (const int block : {64, 256, 1024}) {
                for (const int length_index : {spectr::kDefaultLengthPreset, 0}) {
                    const auto material = chord(4.0, rate);
                    const std::size_t engage = at(1.5, rate) + 17, release = at(2.7, rate) + 33;
                    Run run;
                    run.rate = rate;
                    run.block = block;
                    run.mode = MaskRenderMode::zero_latency;
                    run.auto_gain = auto_on ? 1.0f : 0.0f;
                    run.before = [&](Spectr&, pulp::format::HeadlessHost& h, std::size_t pos, int n,
                                     pulp::state::ParameterEventQueue& ev) {
                        if (pos == 0) h.state().set_value(spectr::kParamFreezeLength, float(length_index));
                        if (engage >= pos && engage < pos + std::size_t(n))
                            (void)ev.push({spectr::kParamFreeze, std::int32_t(engage - pos), 1.0f, 0});
                        if (release >= pos && release < pos + std::size_t(n))
                            (void)ev.push({spectr::kParamFreeze, std::int32_t(release - pos), 0.0f, 0});
                    };
                    BlockCosts costs;
                    (void)render(material, run, nullptr, nullptr, &costs);
                    const double steady = costs.worst(at(0.8, rate), at(1.4, rate));
                    const double eng = costs.worst(engage - std::size_t(block), engage + at(0.25, rate));
                    const double rel = costs.worst(release - std::size_t(block), release + at(0.25, rate));
                    std::printf("  %-12s %6.0f %5d %8.0f %9.0f %9.0f %9.0f %9.1f\n",
                                (std::string(auto_on ? "auto " : "") + "len" + std::to_string(length_index)).c_str(),
                                rate, block, 1e6 * block / rate, steady, eng, rel,
                                std::max(eng, rel) / std::max(1.0, steady));
                }
            }
        }
    }
}

TEST_CASE("Glitch report: a Freeze loop running in Tracking", "[.][glitch-report]") {
    // A loop held for several passes. Two questions: does any seam click
    // (the worst whitened spike over the whole hold, against the same span
    // of an unpressed render of the same material), and is each pass the
    // same as the last (the residual of a pass against the one before it,
    // relative to the pass) -- a hold that drifts, re-levels or re-masks
    // pass to pass is the "inconsistent" a listener hears.
    std::printf("\nFreeze loop, Tracking, 120 BPM 4/4\n"
                "  %-6s %-5s %6s %5s %6s %9s %9s %12s\n",
                "mat", "auto", "mask", "block", "length", "spike", "control", "pass resid");
    for (const int material_index : {0, 1}) {
        for (const bool auto_on : {false, true}) {
            for (const bool shaped : {false, true}) {
                for (const int block : {64, 512}) {
                    const double rate = 48000.0;
                    for (const int length_index : {3, 6, 9, spectr::kDefaultLengthPreset}) {
                        const double seconds = 10.0;
                        const auto material = material_index == 0 ? chord(seconds, rate) : drums(seconds, rate);
                        const std::size_t engage = at(2.2, rate) + 11;
                        const auto run_for = [&](bool pressed) {
                            Run run;
                            run.rate = rate;
                            run.block = block;
                            run.mode = MaskRenderMode::zero_latency;
                            run.auto_gain = auto_on ? 1.0f : 0.0f;
                            run.shaped = shaped;
                            run.before = [&](Spectr&, pulp::format::HeadlessHost& h, std::size_t pos, int n,
                                             pulp::state::ParameterEventQueue& ev) {
                                if (pos == 0) h.state().set_value(spectr::kParamFreezeLength, float(length_index));
                                if (pressed && engage >= pos && engage < pos + std::size_t(n))
                                    (void)ev.push({spectr::kParamFreeze, std::int32_t(engage - pos), 1.0f, 0});
                            };
                            return render(material, run);
                        };
                        const auto held = run_for(true);
                        const auto live = run_for(false);
                        const auto seconds_of = [&](int index) {
                            return spectr::length_seconds(spectr::kLengthPresets[std::size_t(index)], 120.0, 4, 4);
                        };
                        const std::size_t period = at(seconds_of(length_index), rate);
                        const std::size_t from = engage + at(0.3, rate), to = held.size() - at(0.05, rate);
                        const auto a = fit_whitener(live.l, from - at(0.5, rate), from);
                        const double spike = spike_db(whiten(held.l, a, from, to), rate);
                        const auto a2 = fit_whitener(held.l, from - at(0.1, rate), from);
                        const double control = spike_db(whiten(live.l, a, from, to), rate);
                        (void)a2;
                        // Pass-to-pass residual over the last two full passes.
                        double worst_resid = -200.0;
                        if (to > from + 2 * period) {
                            for (std::size_t p = from + period; p + period <= to; p += period) {
                                double e = 0.0, d = 0.0;
                                for (std::size_t i = p; i < p + period; ++i) {
                                    const double x = held.l[i], y = held.l[i - period];
                                    e += x * x; d += (x - y) * (x - y);
                                }
                                worst_resid = std::max(worst_resid, 10.0 * std::log10(d / (e + 1e-20) + 1e-20));
                            }
                        }
                        std::printf("  %-6s %-5s %6s %5d %6.3f %9.1f %9.1f %12.1f\n",
                                    material_index == 0 ? "chord" : "drums", auto_on ? "on" : "off",
                                    shaped ? "shaped" : "flat", block, seconds_of(length_index), spike, control, worst_resid);
                    }
                }
            }
        }
    }
}

TEST_CASE("Glitch report: callback cost during a band drag", "[.][glitch-report]") {
    std::printf("\nCallback thread-CPU during a 60 Hz band drag (us)\n"
                "  %-8s %-5s %6s %5s %8s %9s %9s %7s\n",
                "mode", "auto", "rate", "block", "budget", "steady", "drag", "ratio");
    for (const auto mode : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
        for (const bool auto_on : {false, true}) {
            for (const double rate : {48000.0, 96000.0}) {
                for (const int block : {64, 256}) {
                    const auto material = chord(3.5, rate);
                    const std::size_t start = at(1.5, rate), stop = at(2.7, rate);
                    const std::size_t every = at(1.0 / 60.0, rate);
                    std::size_t next = start;
                    Run run;
                    run.rate = rate;
                    run.block = block;
                    run.mode = mode;
                    run.shaped = true;
                    run.auto_gain = auto_on ? 1.0f : 0.0f;
                    run.before = [&](Spectr& p, pulp::format::HeadlessHost&, std::size_t pos, int n,
                                     pulp::state::ParameterEventQueue&) {
                        while (next >= pos && next < pos + std::size_t(n) && next < stop) {
                            auto field = p.processing_state_snapshot().field;
                            const double t = double(next - start) / double(stop - start);
                            field.bands[10].gain_db = float(-24.0 + 30.0 * t);
                            p.replace_field(field);
                            next += every;
                        }
                    };
                    BlockCosts costs;
                    (void)render(material, run, nullptr, nullptr, &costs);
                    const double steady = costs.worst(at(0.8, rate), at(1.4, rate));
                    const double drag = costs.worst(start, stop);
                    std::printf("  %-8s %-5s %6.0f %5d %8.0f %9.0f %9.0f %7.1f\n", mode_name(mode),
                                auto_on ? "on" : "off", rate, block, 1e6 * block / rate, steady, drag,
                                drag / std::max(1.0, steady));
                }
            }
        }
    }
}

TEST_CASE("Glitch report: Freeze engage over gaps and at other tempos and Mix",
          "[.][glitch-report]") {
    std::printf("\nFreeze engage/release, Tracking, product shape + AUTO (spike dB over control)\n"
                "  %-9s %6s %4s %6s %9s %9s %9s %9s\n",
                "mat", "tempo", "mix", "length", "eng", "rel", "ctl", "loop res");
    for (const int material_index : {0, 1, 2}) {
        for (const double tempo : {90.0, 120.0, 140.0}) {
            for (const float mix : {100.0f, 50.0f}) {
                for (const int length_index : {spectr::kDefaultLengthPreset, 17, 6}) {
                    const double rate = 48000.0;
                    const auto material = material_index == 0 ? chord(9.0, rate)
                        : material_index == 1 ? drums(9.0, rate) : staccato(9.0, rate);
                    std::mt19937 rng(unsigned(tempo) * 3 + unsigned(length_index));
                    std::uniform_int_distribution<int> jitter(0, 4095);
                    const std::size_t engage = at(2.0, rate) + std::size_t(jitter(rng));
                    const std::size_t release = at(7.5, rate) + std::size_t(jitter(rng));
                    Run run;
                    run.rate = rate;
                    run.block = 256;
                    run.mode = MaskRenderMode::zero_latency;
                    run.shaped = true;
                    run.auto_gain = 1.0f;
                    run.tempo = tempo;
                    run.before = [&](Spectr&, pulp::format::HeadlessHost& h, std::size_t pos, int n,
                                     pulp::state::ParameterEventQueue& ev) {
                        if (pos == 0) {
                            h.state().set_value(spectr::kParamFreezeLength, float(length_index));
                            h.state().set_value(spectr::kMix, mix);
                        }
                        if (engage >= pos && engage < pos + std::size_t(n))
                            (void)ev.push({spectr::kParamFreeze, std::int32_t(engage - pos), 1.0f, 0});
                        if (release >= pos && release < pos + std::size_t(n))
                            (void)ev.push({spectr::kParamFreeze, std::int32_t(release - pos), 0.0f, 0});
                    };
                    const auto out = render(material, run);
                    const auto e = score_event(out.l, rate, engage - at(0.4, rate), engage,
                                               engage + at(0.25, rate), at(0.6, rate));
                    const auto r = score_event(out.l, rate, release - at(0.4, rate), release,
                                               release + at(0.25, rate), at(0.6, rate));
                    const double secs = spectr::length_seconds(
                        spectr::kLengthPresets[std::size_t(length_index)], tempo, 4, 4);
                    const std::size_t period = at(secs, rate);
                    double resid = -200.0;
                    if (engage + at(0.6, rate) + 2 * period < release) {
                        const std::size_t p = release - period;
                        double en = 0.0, d = 0.0;
                        for (std::size_t i = p; i < release; ++i) {
                            const double x = out.l[i], y = out.l[i - period];
                            en += x * x; d += (x - y) * (x - y);
                        }
                        resid = 10.0 * std::log10(d / (en + 1e-20) + 1e-20);
                    }
                    std::printf("  %-9s %6.0f %4.0f %6.2f %9.1f %9.1f %9.1f %9.1f\n",
                                material_index == 0 ? "chord" : material_index == 1 ? "drums" : "staccato",
                                tempo, mix, secs, e.spike_db - e.control_db, r.spike_db - r.control_db,
                                e.control_db, resid);
                }
            }
        }
    }
}

TEST_CASE("Glitch report: real-time UI edits in Tracking", "[.][glitch-report][realtime]") {
    // The UI on its own thread, the audio paced in real time: a FROZEN press
    // and release from the editor, a band drag at 60 Hz, a loop left running.
    // Each scored against the same window of a real-time render with no edit.
    const double rate = 48000.0;
    for (const int block : {128, 512}) {
        const auto material = chord(8.0, rate);
        Run run;
        run.rate = rate;
        run.block = block;
        run.mode = MaskRenderMode::zero_latency;
        run.shaped = true;
        run.auto_gain = 1.0f;
        std::vector<UiAction> freeze_ui{
            {2.03, [](Spectr& p, pulp::format::HeadlessHost&) { (void)p.set_freeze_from_editor(true); }},
            {5.51, [](Spectr& p, pulp::format::HeadlessHost&) { (void)p.set_freeze_from_editor(false); }}};
        std::vector<UiAction> drag_ui;
        for (int k = 0; k < 72; ++k) {
            drag_ui.push_back({2.0 + k / 60.0, [k](Spectr& p, pulp::format::HeadlessHost&) {
                auto field = p.processing_state_snapshot().field;
                field.bands[10].gain_db = float(-24.0 + 30.0 * k / 71.0);
                field.bands[11].gain_db = float(6.0 - 30.0 * k / 71.0);
                p.replace_field(field);
            }});
        }
        BlockCosts c_none, c_freeze, c_drag;
        const auto none = render_realtime(material, run, {}, &c_none);
        const auto frozen = render_realtime(material, run, freeze_ui, &c_freeze);
        const auto dragged = render_realtime(material, run, drag_ui, &c_drag);
        const auto score = [&](const Stereo& x, double a, double b) {
            const std::size_t from = at(a, rate), to = at(b, rate);
            const auto w = fit_whitener(none.l, from - at(0.5, rate), from - at(0.01, rate));
            const double s1 = spike_db(whiten(x.l, w, from - at(0.01, rate), to), rate);
            const double s0 = spike_db(whiten(none.l, w, from - at(0.01, rate), to), rate);
            return std::pair{s1, s0};
        };
        const auto fe = score(frozen, 2.0, 2.4);
        const auto fl = score(frozen, 2.6, 5.4);
        const auto fr = score(frozen, 5.5, 5.9);
        const auto dd = score(dragged, 2.0, 3.3);
        std::printf("\nReal-time, Tracking, block %d (spike dB; untouched render's same window)\n"
                    "  engage %.1f (%.1f)  loop %.1f (%.1f)  release %.1f (%.1f)  drag %.1f (%.1f)\n"
                    "  worst callback us: none %.0f  freeze %.0f  drag %.0f  (budget %.0f)\n",
                    block, fe.first, fe.second, fl.first, fl.second, fr.first, fr.second,
                    dd.first, dd.second, c_none.worst(0, ~std::size_t(0)),
                    c_freeze.worst(0, ~std::size_t(0)), c_drag.worst(0, ~std::size_t(0)),
                    1e6 * block / rate);
    }
}

TEST_CASE("Glitch report: host-split callbacks in Tracking", "[.][glitch-report]") {
    std::printf("\nRandom callback lengths, Tracking, product shape + AUTO (spike dB; same window unpressed)\n"
                "  %-7s %5s %6s %9s %9s %9s %9s\n", "mat", "max", "length", "engage", "loop", "release", "drag");
    const double rate = 48000.0;
    for (const int material_index : {0, 1, 2}) {
        for (const int max_block : {256, 1024}) {
            for (const int length_index : {spectr::kDefaultLengthPreset, 6, 0}) {
                const auto material = material_index == 0 ? chord(8.0, rate)
                    : material_index == 1 ? drums(8.0, rate) : staccato(8.0, rate);
                const std::size_t engage = at(2.0, rate) + 101, release = at(5.5, rate) + 37;
                const std::size_t ds = at(6.0, rate), de = at(7.2, rate), every = at(1.0 / 60.0, rate);
                std::size_t next = ds;
                const auto run_for = [&](bool act) {
                    Run run;
                    run.rate = rate;
                    run.block = max_block;
                    run.random_blocks = 1234u + unsigned(length_index);
                    run.mode = MaskRenderMode::zero_latency;
                    run.shaped = true;
                    run.auto_gain = 1.0f;
                    next = ds;
                    run.before = [&](Spectr& p, pulp::format::HeadlessHost& h, std::size_t pos, int n,
                                     pulp::state::ParameterEventQueue& ev) {
                        if (pos == 0) h.state().set_value(spectr::kParamFreezeLength, float(length_index));
                        if (!act) return;
                        if (engage >= pos && engage < pos + std::size_t(n))
                            (void)ev.push({spectr::kParamFreeze, std::int32_t(engage - pos), 1.0f, 0});
                        if (release >= pos && release < pos + std::size_t(n))
                            (void)ev.push({spectr::kParamFreeze, std::int32_t(release - pos), 0.0f, 0});
                        while (next >= pos && next < pos + std::size_t(n) && next < de) {
                            auto field = p.processing_state_snapshot().field;
                            const double t = double(next - ds) / double(de - ds);
                            field.bands[10].gain_db = float(-24.0 + 30.0 * t);
                            p.replace_field(field);
                            next += every;
                        }
                    };
                    return render(material, run);
                };
                const auto acted = run_for(true);
                const auto plain = run_for(false);
                const auto score = [&](std::size_t from, std::size_t to) {
                    const auto w = fit_whitener(plain.l, from - at(0.5, rate), from - at(0.01, rate));
                    return spike_db(whiten(acted.l, w, from - at(0.01, rate), to), rate)
                         - spike_db(whiten(plain.l, w, from - at(0.01, rate), to), rate);
                };
                std::printf("  %-7s %5d %6d %9.1f %9.1f %9.1f %9.1f\n",
                            material_index == 0 ? "chord" : material_index == 1 ? "drums" : "staccato",
                            max_block, length_index,
                            score(engage, engage + at(0.25, rate)),
                            score(engage + at(0.3, rate), release),
                            score(release, release + at(0.25, rate)),
                            score(ds, de));
            }
        }
    }
}

TEST_CASE("Glitch report: LFO-driven destinations in Tracking against Mixing",
          "[.][glitch-report]") {
    // Clicks per second in a 4 s run with an LFO driving a destination: the
    // count of whitened-residual samples more than 12 dB above the same
    // run's own pre-LFO control, per mode. Mixing realises the same drawn
    // magnitude with no impulse swaps, so it is the standard Tracking is held to.
    struct Config { const char* name; std::size_t target; int shape; float beats; bool hold; };
    const Config configs[] = {
        {"freeze sq", std::size_t(spectr::ModulationTarget::Freeze), 2, 1.0f, false},
        {"freeze hold", std::size_t(spectr::ModulationTarget::Freeze), 2, 1.0f, true},
        {"bank sine", std::size_t(spectr::ModulationTarget::WholeBank), 0, 2.0f, false},
        {"bank square", std::size_t(spectr::ModulationTarget::WholeBank), 2, 1.0f, false},
        {"preset sine", std::size_t(spectr::ModulationTarget::Preset), 0, 2.0f, false},
        {"intensity", std::size_t(spectr::ModulationTarget::Intensity), 0, 0.5f, false},
    };
    std::printf("\nLFO destinations, product shape + AUTO, 48 kHz / 256 (clicks/s > control+12 dB; worst spike - control)\n"
                "  %-12s %-6s %10s %10s\n", "config", "mat", "Tracking", "Mixing");
    const double rate = 48000.0;
    for (const auto& c : configs) {
        for (const int material_index : {0, 1}) {
            double per_second[2] = {0, 0}, worst[2] = {0, 0}, moved[2] = {0, 0};
            for (int m = 0; m < 2; ++m) {
                const auto material = material_index == 0 ? chord(7.0, rate) : drums(7.0, rate);
                Run run;
                run.rate = rate;
                run.block = 256;
                run.mode = m == 0 ? MaskRenderMode::zero_latency : MaskRenderMode::linear_phase;
                run.shaped = true;
                run.auto_gain = 1.0f;
                const std::size_t lfo_on = at(2.5, rate);
                run.before = [&](Spectr&, pulp::format::HeadlessHost& h, std::size_t pos, int n,
                                 pulp::state::ParameterEventQueue&) {
                    if (pos <= lfo_on && lfo_on < pos + std::size_t(n)) {
                        h.state().set_value(spectr::kParamLfoShape, float(c.shape));
                        h.state().set_value(spectr::kParamLfoRate, c.beats);
                        h.state().set_value(spectr::kParamLfoDepth, 0.8f);
                        h.state().set_value(spectr::lfo_route_enabled_param_id(0, c.target), 1.0f);
                        h.state().set_value(spectr::lfo_route_amount_param_id(0, c.target), 0.8f);
                        if (c.hold) h.state().set_value(spectr::kParamFreezeHoldForLength, 1.0f);
                        h.state().set_value(spectr::kParamLfoEnabled, 1.0f);
                    }
                };
                const auto out = render(material, run);
                // Control: the LFO moved the audio at all.
                {
                    Run plain_run = run;
                    plain_run.before = nullptr;
                    const auto plain = render(material, plain_run);
                    double d = 0.0, en = 0.0;
                    for (std::size_t i = at(3.0, rate); i < at(6.5, rate); ++i) {
                        d += (double(out.l[i]) - plain.l[i]) * (double(out.l[i]) - plain.l[i]);
                        en += double(plain.l[i]) * plain.l[i];
                    }
                    moved[m] = 10.0 * std::log10(d / (en + 1e-20) + 1e-20);
                }
                const std::size_t lat = std::size_t(m == 0 ? 64 : 10240);
                const std::size_t from = lfo_on + lat + at(0.05, rate), to = from + at(4.0, rate);
                const std::size_t cfrom = at(0.6, rate) + lat;
                const auto w = fit_whitener(out.l, cfrom, cfrom + at(0.5, rate));
                const double control = spike_db(whiten(out.l, w, cfrom, cfrom + at(1.5, rate)), rate);
                const auto e = whiten(out.l, w, from, to);
                // Count spike samples above control+12 dB, merging within 5 ms.
                const std::size_t outer = at(0.004, rate), inner = at(0.0005, rate);
                std::vector<double> prefix(e.size() + 1, 0.0);
                for (std::size_t i = 0; i < e.size(); ++i) prefix[i + 1] = prefix[i] + e[i] * e[i];
                std::size_t clicks = 0, last = 0;
                double peak = -200.0;
                for (std::size_t n = outer; n + outer + 1 < e.size(); ++n) {
                    const double around = (prefix[n - inner] - prefix[n - outer])
                                        + (prefix[n + outer + 1] - prefix[n + inner + 1]);
                    const double r = std::sqrt(around / double(2 * (outer - inner)) + 1e-24);
                    const double db = 20.0 * std::log10(std::abs(e[n]) / r + 1e-12);
                    peak = std::max(peak, db);
                    if (db > control + 12.0 && (clicks == 0 || n - last > at(0.005, rate))) { ++clicks; last = n; }
                }
                per_second[m] = double(clicks) / 4.0;
                worst[m] = peak - control;
            }
            std::printf("  %-12s %-6s %5.2f %4.1f %5.2f %4.1f   (LFO moved the audio by %.1f / %.1f dB)\n",
                        c.name, material_index == 0 ? "chord" : "drums",
                        per_second[0], worst[0], per_second[1], worst[1], moved[0], moved[1]);
        }
    }
}

// ── Traced captures: audio + Perfetto on one timeline ──────────────────────
//
// Each flow renders through the processor with a trace session running and
// writes <flow>.wav and <flow>.pftrace side by side. Every host block is a
// "process" slice (category dsp) carrying `stream_pos` and `frames`, so a
// glitch found at sample N of the WAV is the slice whose [stream_pos,
// stream_pos + frames) holds N; Pulp's tools/audio/glitch_trace.py does that
// join and reports per-block time against the block deadline. Needs an SDK
// built with tracing; against a shipping SDK the session does not start and
// the case says so instead of writing empty traces.

#include <pulp/runtime/trace_session.hpp>
#include <filesystem>
#include <fstream>

namespace {

void write_wav(const std::string& path, const Stereo& x, double rate) {
    std::ofstream f(path, std::ios::binary);
    const auto put32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const std::uint32_t frames = std::uint32_t(x.size()), bytes = frames * 2 * 4;
    f.write("RIFF", 4); put32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); put32(16); put16(3); put16(2); put32(std::uint32_t(rate));
    put32(std::uint32_t(rate) * 8); put16(8); put16(32);
    f.write("data", 4); put32(bytes);
    for (std::size_t i = 0; i < x.size(); ++i) {
        f.write(reinterpret_cast<const char*>(&x.l[i]), 4);
        f.write(reinterpret_cast<const char*>(&x.r[i]), 4);
    }
}

} // namespace

TEST_CASE("Glitch trace: render each reported flow with Perfetto running", "[.][glitch-trace]") {
    const char* dir_env = std::getenv("SPECTR_GLITCH_TRACE_DIR");
    const std::string dir = dir_env ? dir_env : "/tmp/spectr-glitch-traces";
    std::filesystem::create_directories(dir);
    const double rate = 48000.0;
    const int block = 256;
    const auto capture = [&](const std::string& name, Run run, const Stereo& material) {
        run.rate = rate;
        run.block = block;
        const std::string trace_path = dir + "/" + name + ".pftrace";
        if (!pulp::runtime::Tracing::start({"dsp", "state"}, trace_path, 256u * 1024u)) {
            WARN("tracing is not compiled into this SDK; nothing captured for " << name);
            return;
        }
        const auto out = render(material, run);
        const auto stopped = pulp::runtime::Tracing::stop();
        write_wav(dir + "/" + name + ".wav", out, rate);
        std::printf("captured %s: %s (%llu bytes) + %s.wav\n", name.c_str(), stopped.path.c_str(),
                    (unsigned long long)stopped.trace_bytes, name.c_str());
    };
    const auto material = chord(6.0, rate);
    // Mixing <-> Tracking. SPECTR_PLANT_HARD_RENDER_SWITCH gives the cut.
    for (const auto from : {MaskRenderMode::zero_latency, MaskRenderMode::linear_phase}) {
        Run run;
        run.mode = from;
        run.shaped = true;
        run.auto_gain = 1.0f;
        bool switched = false;
        run.before = [switched, from](Spectr& p, pulp::format::HeadlessHost&, std::size_t pos, int,
                                      pulp::state::ParameterEventQueue&) mutable {
            if (!switched && pos >= std::size_t(2.0 * 48000.0)) {
                (void)p.set_render_mode(from == MaskRenderMode::zero_latency
                    ? MaskRenderMode::linear_phase : MaskRenderMode::zero_latency);
                switched = true;
            }
        };
        capture(from == MaskRenderMode::zero_latency ? "switch-tracking-to-mixing"
                                                     : "switch-mixing-to-tracking", run, material);
    }
    // FROZEN engage and release, Tracking, 1 bar and 1/4 bar.
    for (const int length_index : {spectr::kDefaultLengthPreset, 6}) {
        Run run;
        run.mode = MaskRenderMode::zero_latency;
        run.shaped = true;
        run.auto_gain = 1.0f;
        const std::size_t engage = at(2.0, rate) + 77, release = at(4.5, rate) + 13;
        run.before = [=](Spectr&, pulp::format::HeadlessHost& h, std::size_t pos, int n,
                         pulp::state::ParameterEventQueue& ev) {
            if (pos == 0) h.state().set_value(spectr::kParamFreezeLength, float(length_index));
            if (engage >= pos && engage < pos + std::size_t(n))
                (void)ev.push({spectr::kParamFreeze, std::int32_t(engage - pos), 1.0f, 0});
            if (release >= pos && release < pos + std::size_t(n))
                (void)ev.push({spectr::kParamFreeze, std::int32_t(release - pos), 0.0f, 0});
        };
        capture(length_index == spectr::kDefaultLengthPreset ? "freeze-tracking-1bar"
                                                             : "freeze-tracking-quarter", run, material);
    }
    // A 60 Hz band drag, Tracking.
    {
        Run run;
        run.mode = MaskRenderMode::zero_latency;
        run.shaped = true;
        run.auto_gain = 1.0f;
        std::size_t next = at(2.0, rate);
        run.before = [next, rate](Spectr& p, pulp::format::HeadlessHost&, std::size_t pos, int n,
                                  pulp::state::ParameterEventQueue&) mutable {
            const std::size_t start = at(2.0, rate), stop = at(3.5, rate);
            while (next >= pos && next < pos + std::size_t(n) && next < stop) {
                auto field = p.processing_state_snapshot().field;
                field.bands[10].gain_db = float(-24.0 + 30.0 * double(next - start) / double(stop - start));
                p.replace_field(field);
                next += at(1.0 / 60.0, rate);
            }
        };
        capture("drag-tracking", run, material);
    }
}
