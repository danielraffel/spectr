// The Freeze and Length modulation targets: the gate's duty, fresh captures
// on every re-engage, the cost and continuity of rhythmic freezing, and the
// Length steps a freeze takes at its engage. See docs/modulation.md.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <pulp/format/headless.hpp>
#include "spectr/freeze_length.hpp"
#include "spectr/modulation.hpp"
#include "spectr/param_surface.hpp"
#include "spectr/spectr.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using Catch::Approx;
using spectr::LfoShape;
using spectr::ModulationTarget;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;
constexpr std::size_t kBlock = 256;
constexpr std::size_t kFreezeT = static_cast<std::size_t>(ModulationTarget::Freeze);
constexpr std::size_t kLengthT = static_cast<std::size_t>(ModulationTarget::Length);

std::unique_ptr<pulp::format::Processor> create_tracking() {
    auto p = spectr::create_spectr();
    REQUIRE(static_cast<spectr::Spectr*>(p.get())->set_render_mode(
        spectr::MaskRenderMode::zero_latency));
    return p;
}

struct Rig {
    pulp::format::HeadlessHost host{create_tracking};
    spectr::Spectr* plugin = nullptr;
    std::uint64_t n = 0;
    Rig() {
        host.prepare(kRate, kBlock);
        plugin = dynamic_cast<spectr::Spectr*>(host.processor());
        REQUIRE(plugin != nullptr);
        auto& s = host.state();
        // Only the targets under test: LFO 1 on, Bank off.
        s.set_value(spectr::kParamLfoEnabled, 1.0f);
        s.set_value(spectr::lfo_route_enabled_param_id(0, 0), 0.0f);
    }
    void set(pulp::state::ParamID id, float v) { host.state().set_value(id, v); }
    // One block of `tone(t)`; returns channel 0 and the block's wall time.
    double block(const std::function<double(double)>& hz, std::vector<float>* out = nullptr) {
        pulp::audio::Buffer<float> in(2, kBlock), o(2, kBlock);
        static double phase = 0.0;
        if (n == 0) phase = 0.0;
        for (std::size_t i = 0; i < kBlock; ++i, ++n) {
            phase += 2.0 * kPi * hz(double(n) / kRate) / kRate;
            const float v = 0.3f * float(std::sin(phase));
            in.channel(0)[i] = v;
            in.channel(1)[i] = v;
        }
        const float* ip[] = {in.channel(0).data(), in.channel(1).data()};
        pulp::audio::BufferView<const float> iv(ip, 2, kBlock);
        auto ov = o.view();
        pulp::state::ParameterEventQueue events;
        const auto t0 = std::chrono::steady_clock::now();
        host.process(ov, iv, events);
        const auto t1 = std::chrono::steady_clock::now();
        if (out) out->insert(out->end(), o.channel(0).begin(), o.channel(0).end());
        return std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
};

double goertzel(const std::vector<float>& x, std::size_t from, std::size_t len, double hz) {
    const double w = 2.0 * kPi * hz / kRate, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const double s0 = x[from + i] + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

}  // namespace

TEST_CASE("the Freeze gate's duty is its Depth, for every shape",
          "[modulation][freeze-target]") {
    for (auto shape : {LfoShape::Sine, LfoShape::Triangle, LfoShape::Square, LfoShape::Saw}) {
        for (float duty : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f}) {
            constexpr int kSteps = 20000;
            int frozen = 0;
            for (int i = 0; i < kSteps; ++i)
                frozen += spectr::lfo_freeze_gate(shape, (i + 0.5) / kSteps, duty) ? 1 : 0;
            INFO("shape " << int(shape) << " duty " << duty);
            CHECK(double(frozen) / kSteps == Approx(duty).margin(0.002));
        }
    }
    // 50 % centres the threshold: a sine is frozen exactly while above zero.
    CHECK(spectr::lfo_freeze_gate(LfoShape::Sine, 0.25, 0.5f));
    CHECK_FALSE(spectr::lfo_freeze_gate(LfoShape::Sine, 0.75, 0.5f));
}

TEST_CASE("the Freeze target gates the freeze at its duty through the processor",
          "[modulation][freeze-target][rt]") {
    for (float duty : {0.25f, 0.5f, 0.75f}) {
        Rig rig;
        rig.set(spectr::kParamLfoShape, float(LfoShape::Saw));
        rig.set(spectr::kParamLfoRate, 1.0f);  // 0.5 s per cycle at 120 BPM
        rig.set(spectr::lfo_route_enabled_param_id(0, kFreezeT), 1.0f);
        rig.set(spectr::lfo_route_amount_param_id(0, kFreezeT), duty);
        int frozen = 0, blocks = 0;
        for (int b = 0; b < 1875; ++b) {  // 10 s, 20 cycles
            rig.block([](double) { return 997.0; });
            if (b < 20) continue;
            REQUIRE(rig.plugin->freeze_gate_driven());
            frozen += rig.plugin->freeze_effective() ? 1 : 0;
            ++blocks;
        }
        INFO("duty " << duty);
        // Block granularity: 256 samples of a 24000-sample cycle.
        CHECK(double(frozen) / blocks == Approx(duty).margin(0.015));
    }
}

TEST_CASE("each Freeze-target engage captures fresh audio",
          "[modulation][freeze-target][audio]") {
    // A square at 2 beats (1 s cycle), Depth 50 %: frozen over the first half
    // of each cycle. The input tone changes three quarters into each cycle,
    // during the live half, so each engage has a quarter second of the NEW
    // tone behind it. A 1/16-bar Length holds the spectrum.
    Rig rig;
    rig.set(spectr::kParamLfoShape, float(LfoShape::Square));
    rig.set(spectr::kParamLfoRate, 2.0f);
    rig.set(spectr::kParamFreezeLength, 1.0f);  // 1/16 bar
    rig.set(spectr::lfo_route_enabled_param_id(0, kFreezeT), 1.0f);
    rig.set(spectr::lfo_route_amount_param_id(0, kFreezeT), 0.5f);
    const double tones[] = {500.0, 810.0, 1310.0, 2120.0, 500.0, 810.0};
    const auto tone_at = [&](double t) {
        const int cycle = int(std::floor(t + 0.25));  // switches at x.75 s
        return tones[std::min(cycle, 5)];
    };
    std::vector<float> out;
    for (int b = 0; b < int(5.0 * kRate / kBlock); ++b) rig.block(tone_at, &out);
    // Inside each frozen half the held sound must be this cycle's tone, not
    // the tone the PREVIOUS engage captured -- which is what a hold that was
    // never re-captured would still be playing.
    int fresh = 0;
    for (int cycle = 1; cycle <= 4; ++cycle) {
        const std::size_t at = std::size_t((cycle + 0.30) * kRate);
        const std::size_t len = 4096;
        const double now = goertzel(out, at, len, tones[cycle]);
        const double before = goertzel(out, at, len, tones[cycle - 1]);
        std::printf("[freeze-target] cycle %d: held %.0f Hz power %.3g, previous %.0f Hz %.3g\n",
                    cycle, tones[cycle], now, tones[cycle - 1], before);
        if (now > 20.0 * before) ++fresh;
    }
    CHECK(fresh == 4);
}

TEST_CASE("the Freeze target over a changing tone holds, not follows",
          "[modulation][freeze-target][audio]") {
    // Control for the test above: while frozen the input moves on but the
    // output keeps the captured tone. 1500 Hz plays from x.75 s to x.20 s and
    // 600 Hz in between, so each engage (x.0 s) captures 1500 Hz and the input
    // turns to 600 Hz inside the frozen half: at x.43 s the output must still
    // be 1500 Hz.
    Rig rig;
    rig.set(spectr::kParamLfoShape, float(LfoShape::Square));
    rig.set(spectr::kParamLfoRate, 2.0f);
    rig.set(spectr::kParamFreezeLength, 1.0f);
    rig.set(spectr::lfo_route_enabled_param_id(0, kFreezeT), 1.0f);
    rig.set(spectr::lfo_route_amount_param_id(0, kFreezeT), 0.5f);
    const auto tone_at = [](double t) {
        const double f = std::fmod(t, 1.0);
        return f >= 0.20 && f < 0.75 ? 600.0 : 1500.0;
    };
    std::vector<float> out;
    for (int b = 0; b < int(3.0 * kRate / kBlock); ++b) rig.block(tone_at, &out);
    const std::size_t at = std::size_t(2.43 * kRate);
    CHECK(goertzel(out, at, 1024, 1500.0) > 20.0 * goertzel(out, at, 1024, 600.0));
    // And live again in the open half (x.6 s): the input's 600 Hz.
    const std::size_t live = std::size_t(2.62 * kRate);
    CHECK(goertzel(out, live, 1024, 600.0) > 20.0 * goertzel(out, live, 1024, 1500.0));
}

TEST_CASE("rhythmic Freeze-target re-engage stays inside the real-time cost and is click-free",
          "[modulation][freeze-target][rt][cost]") {
    // A 1/16-note square (0.25 beat: 125 ms cycle), Depth 50 %, over a steady
    // tone: 8 engages and 8 releases a second. A steady tone frozen and
    // released must stay continuous, and no callback may cost far more than
    // the same render with the target off.
    const auto run = [](bool gated, std::vector<float>* out) {
        std::vector<double> cheapest;
        for (int rep = 0; rep < 3; ++rep) {
            Rig rig;
            rig.set(spectr::kParamLfoShape, float(LfoShape::Square));
            rig.set(spectr::kParamLfoRate, 0.25f);
            rig.set(spectr::kParamFreezeLength, 1.0f);
            rig.set(spectr::lfo_route_enabled_param_id(0, kFreezeT), gated ? 1.0f : 0.0f);
            rig.set(spectr::lfo_route_amount_param_id(0, kFreezeT), 0.5f);
            // Something for the LFO to do when the target is off, so the
            // control runs the same modulated branch.
            rig.set(spectr::lfo_route_enabled_param_id(0, 0), gated ? 0.0f : 1.0f);
            rig.set(spectr::lfo_route_amount_param_id(0, 0), 0.0f);
            std::vector<double> us;
            std::vector<float> o;
            for (int b = 0; b < int(4.0 * kRate / kBlock); ++b)
                us.push_back(rig.block([](double) { return 1003.0; }, &o));
            if (rep == 0 && out) *out = o;
            if (cheapest.empty()) cheapest = us;
            else for (std::size_t i = 0; i < us.size(); ++i) cheapest[i] = std::min(cheapest[i], us[i]);
        }
        std::vector<double> tail(cheapest.begin() + 40, cheapest.end());
        std::sort(tail.begin(), tail.end());
        return std::pair{tail[tail.size() / 2], tail.back()};
    };
    std::vector<float> gated_out;
    const auto off = run(false, nullptr);
    const auto on = run(true, &gated_out);
    const double budget = 1e6 * kBlock / kRate;
    std::printf("[freeze-target] per-callback us (median / max of cheapest-of-3), budget %.0f: "
                "target off %.1f / %.1f, 1/16 square %.1f / %.1f\n",
                budget, off.first, off.second, on.first, on.second);
    CHECK(on.second < 0.5 * budget);
    CHECK(on.first <= 2.0 * off.first + 20.0);
    // Continuity: 1 ms peak envelope of a steady tone, after start-up.
    constexpr std::size_t ms = 48;
    float largest = 0.0f;
    float prev = -200.0f;
    for (std::size_t at = std::size_t(0.5 * kRate); at + ms <= gated_out.size(); at += ms) {
        float peak = 1e-9f;
        for (std::size_t i = at; i < at + ms; ++i) peak = std::max(peak, std::abs(gated_out[i]));
        const float db = 20.0f * std::log10(peak);
        if (prev > -60.0f) largest = std::max(largest, std::abs(db - prev));
        prev = db;
    }
    std::printf("[freeze-target] 1/16 square over a steady tone: largest 1 ms envelope step %.2f dB\n",
                largest);
    CHECK(largest < 3.0f);
}

TEST_CASE("Length steps the next freeze's loop by the LFO value at its engage",
          "[modulation][length-target]") {
    using spectr::modulated_length_index;
    // Pure: round(coordinate x 8) steps, clamped to the list.
    CHECK(modulated_length_index(16, 0.0f, 20) == 16);
    CHECK(modulated_length_index(16, 0.25f, 20) == 18);
    CHECK(modulated_length_index(16, -0.25f, 20) == 14);
    CHECK(modulated_length_index(16, 1.0f, 20) == 19);   // clamped at 8 bars
    CHECK(modulated_length_index(2, -1.0f, 20) == 0);    // clamped at 1/32
    CHECK(modulated_length_index(10, 0.06f, 20) == 10);  // 0.48 step rounds to 0
    CHECK(modulated_length_index(10, 0.07f, 20) == 11);

    // Through the processor: base 1 bar (index 16), Depth 25 % (+/-2 steps),
    // a square LFO at 16 beats (8 s): +1 for 4 s, then -1. Each engage comes
    // after more than 8 s of input, so the loop has the history to be as long
    // as the length asks (a loop never reaches back further than it heard).
    const auto engage_at = [](double seconds, int* index) {
        Rig rig;
        rig.set(spectr::kParamLfoShape, float(LfoShape::Square));
        rig.set(spectr::kParamLfoRate, 16.0f);
        rig.set(spectr::lfo_route_enabled_param_id(0, kLengthT), 1.0f);
        rig.set(spectr::lfo_route_amount_param_id(0, kLengthT), 0.25f);
        const int press = int(seconds * kRate / kBlock);
        for (int b = 0; b < press + int(0.6 * kRate / kBlock); ++b) {
            if (b == press) {
                *index = rig.plugin->freeze_modulated_length_index();
                rig.set(spectr::kParamFreeze, 1.0f);
            }
            rig.block([](double) { return 440.0; });
        }
        return rig.plugin->freeze_source().loop_length();
    };
    int up = -1, down = -1;
    const auto up_len = engage_at(9.0, &up);
    const auto down_len = engage_at(13.0, &down);
    CHECK(up == 18);    // 4 bars
    CHECK(down == 14);  // 7/8 bar
    // 120 BPM 4/4: a bar is 2 s.
    CHECK(up_len == std::int64_t(4 * 2.0 * kRate));
    CHECK(down_len == std::int64_t(0.875 * 2.0 * kRate));
}

TEST_CASE("a playing loop is never resized by the Length target",
          "[modulation][length-target]") {
    Rig rig;
    rig.set(spectr::kParamLfoShape, float(LfoShape::Square));
    rig.set(spectr::kParamLfoRate, 4.0f);  // 2 s: +1 for 1 s, then -1
    rig.set(spectr::lfo_route_enabled_param_id(0, kLengthT), 1.0f);
    rig.set(spectr::lfo_route_amount_param_id(0, kLengthT), 0.25f);
    std::int64_t first = 0;
    bool saw_other_index = false;
    for (int b = 0; b < int(16.0 * kRate / kBlock); ++b) {
        // Engaged at 10.2 s (the LFO at +1, 10 s of history behind it).
        if (b == int(10.2 * kRate / kBlock)) rig.set(spectr::kParamFreeze, 1.0f);
        rig.block([](double) { return 440.0; });
        const auto len = rig.plugin->freeze_source().loop_length();
        if (first == 0 && len > 0) first = len;
        if (first > 0) REQUIRE(len == first);
        if (rig.plugin->freeze_modulated_length_index() == 14) saw_other_index = true;
    }
    CHECK(first == std::int64_t(4 * 2.0 * kRate));
    // Control: the LFO did swing to a different length while the loop played.
    CHECK(saw_other_index);
}

TEST_CASE("a press while the Freeze target drives the freeze holds until the gate's next change",
          "[modulation][freeze-target][automation]") {
    Rig rig;
    rig.set(spectr::kParamLfoShape, float(LfoShape::Square));
    rig.set(spectr::kParamLfoRate, 4.0f);  // 2 s cycle: frozen for the first second
    rig.set(spectr::lfo_route_enabled_param_id(0, kFreezeT), 1.0f);
    rig.set(spectr::lfo_route_amount_param_id(0, kFreezeT), 0.5f);
    const auto run_to = [&](double seconds) {
        while (rig.n < std::uint64_t(seconds * kRate)) rig.block([](double) { return 440.0; });
    };
    run_to(0.5);
    CHECK(rig.plugin->freeze_effective());       // the gate froze it
    rig.set(spectr::kParamFreeze, 1.0f);           // a press that agrees: nothing changes
    run_to(0.6);
    rig.set(spectr::kParamFreeze, 0.0f);           // the user unfreezes mid-gate
    run_to(0.7);
    CHECK_FALSE(rig.plugin->freeze_effective());  // the press holds...
    run_to(1.5);                                   // ...across the gate's fall (1.0 s)
    CHECK_FALSE(rig.plugin->freeze_effective());
    run_to(2.2);                                   // the gate rises again at 2.0 s
    CHECK(rig.plugin->freeze_effective());        // and modulation carries on
}

TEST_CASE("the Freeze and Length targets are off in an old session",
          "[modulation][freeze-target][length-target]") {
    pulp::state::StateStore store;
    spectr::Spectr plugin;
    plugin.set_state_store(&store);
    plugin.define_parameters(store);
    for (std::size_t lfo = 0; lfo < 2; ++lfo) {
        CHECK(store.get_value(spectr::lfo_route_enabled_param_id(lfo, kFreezeT)) == 0.0f);
        CHECK(store.get_value(spectr::lfo_route_enabled_param_id(lfo, kLengthT)) == 0.0f);
    }
}

// ── Hold for Length ─────────────────────────────────────────────────────────
//
// With Freeze "Hold for Length" on, each rising edge of the Freeze target's
// gate latches the freeze for exactly the effective Length, ignoring the
// LFO's off-phase, then releases; the next rising edge latches again. Off, the
// freeze follows the gate as before.
namespace {

struct HoldRun {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> holds;  // [start, end) samples
};

// A square LFO with Depth 10 % (frozen 10 % of the cycle) drives Freeze; the
// processor runs at @p bpm in 4/4 and the frozen spans are recorded.
HoldRun run_hold(double bpm, float rate_beats, bool hold, double seconds,
                 int length_preset) {
    pulp::format::HeadlessHost host{create_tracking};
    host.prepare(kRate, kBlock);
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    auto& s = host.state();
    s.set_value(spectr::kParamLfoEnabled, 1.0f);
    s.set_value(spectr::lfo_route_enabled_param_id(0, 0), 0.0f);
    s.set_value(spectr::kParamLfoShape, float(LfoShape::Square));
    s.set_value(spectr::kParamLfoRate, rate_beats);
    s.set_value(spectr::lfo_route_enabled_param_id(0, kFreezeT), 1.0f);
    s.set_value(spectr::lfo_route_amount_param_id(0, kFreezeT), 0.1f);
    s.set_value(spectr::kParamFreezeLength, float(length_preset));
    s.set_value(spectr::kParamFreezeHoldForLength, hold ? 1.0f : 0.0f);
    HoldRun run;
    pulp::audio::Buffer<float> in(2, kBlock), o(2, kBlock);
    double phase = 0.0;
    bool was = false;
    std::uint64_t n = 0;
    while (n < std::uint64_t(seconds * kRate)) {
        for (std::size_t i = 0; i < kBlock; ++i) {
            phase += 2.0 * kPi * 440.0 / kRate;
            in.channel(0)[i] = in.channel(1)[i] = 0.3f * float(std::sin(phase));
        }
        const float* ip[] = {in.channel(0).data(), in.channel(1).data()};
        pulp::audio::BufferView<const float> iv(ip, 2, kBlock);
        auto ov = o.view();
        pulp::format::ProcessContext ctx;
        ctx.tempo_bpm = bpm;
        ctx.time_sig_numerator = 4;
        ctx.time_sig_denominator = 4;
        ctx.position_beats = double(n) / kRate * bpm / 60.0;
        ctx.is_playing = true;
        host.process(ov, iv, ctx);
        const bool now = plugin->freeze_effective();
        if (now && !was) run.holds.push_back({n, 0});
        if (!now && was) run.holds.back().second = n;
        was = now;
        n += kBlock;
    }
    if (was) run.holds.back().second = n;
    return run;
}

}  // namespace

TEST_CASE("Hold for Length latches each Freeze-target engage for exactly the Length",
          "[modulation][freeze-target][hold-for-length]") {
    // 1 bar (LENGTH index 16) and a fraction of a bar (index 8) at three tempos; the LFO
    // cycles every 8 beats (2 bars) and rises once a cycle, its own frozen
    // window only 10 % long. Each hold lasts the Length, to the slice.
    struct Case { double bpm; int preset; };
    for (const Case c : {Case{120.0, 16}, Case{90.0, 16}, Case{150.0, 8}}) {
        const double bars = spectr::length_in_bars(
            spectr::kLengthPresets[static_cast<std::size_t>(c.preset)]);
        INFO("bpm " << c.bpm << " length " << bars << " bar");
        const double bar_seconds = 4.0 * 60.0 / c.bpm;
        const auto expected = std::uint64_t(std::llround(bars * bar_seconds * kRate));
        const auto run = run_hold(c.bpm, 8.0f, true, 6.0 * 2.0 * bar_seconds, c.preset);
        // A completed hold per LFO cycle: the next edge after each release
        // latches again.
        REQUIRE(run.holds.size() >= 4);
        for (std::size_t i = 0; i + 1 < run.holds.size(); ++i) {
            const auto [start, end] = run.holds[i];
            INFO("hold " << i << " at " << start);
            CHECK(end - start >= expected);
            CHECK(end - start < expected + kBlock);
            // Re-latched one LFO cycle (2 bars) later, on the next rising edge.
            CHECK(double(run.holds[i + 1].first - start) / kRate
                  == Approx(2.0 * bar_seconds).margin(double(kBlock) / kRate));
        }
    }
}

TEST_CASE("Hold for Length off leaves the gate following the LFO",
          "[modulation][freeze-target][hold-for-length]") {
    // Control: the same LFO without the hold freezes for its own 10 % window
    // (0.2 bar of a 2-bar cycle), far shorter than the 1-bar Length.
    const double bar_seconds = 2.0;
    const auto run = run_hold(120.0, 8.0f, false, 6.0 * 2.0 * bar_seconds, 16);
    REQUIRE(run.holds.size() >= 4);
    for (std::size_t i = 0; i + 1 < run.holds.size(); ++i) {
        const double held = double(run.holds[i].second - run.holds[i].first) / kRate;
        CHECK(held == Approx(0.1 * 2.0 * bar_seconds).margin(double(kBlock) / kRate));
    }
}

// ── Bands and Preset destinations ───────────────────────────────────────────
namespace {

constexpr std::size_t kBandsT = static_cast<std::size_t>(ModulationTarget::Bands);
constexpr std::size_t kPresetT = static_cast<std::size_t>(ModulationTarget::Preset);

// A comb the bank can be heard through: alternate bands at +12 / -12 dB.
void draw_comb(Rig& rig) {
    for (std::size_t band = 0; band < spectr::kMaxBands; ++band)
        rig.set(spectr::band_gain_param_id(band), band % 2 ? -12.0f : 12.0f);
}

// Largest 1 ms RMS-envelope step, dB, past the first second (settling).
double largest_step_db(const std::vector<float>& out) {
    const std::size_t win = std::size_t(kRate / 1000.0);
    double prev = 0.0, worst = 0.0;
    bool have = false;
    for (std::size_t at = std::size_t(kRate); at + win <= out.size(); at += win) {
        double e = 0.0;
        for (std::size_t i = 0; i < win; ++i) e += double(out[at + i]) * out[at + i];
        const double db = 10.0 * std::log10(e / double(win) + 1e-12);
        if (have) worst = std::max(worst, std::fabs(db - prev));
        prev = db;
        have = true;
    }
    return worst;
}

}  // namespace

TEST_CASE("the Bands destination steps the band count click-free inside the cost gate",
          "[modulation][bands-target]") {
    // A sine LFO at 2 beats (1 s) on Bands at Depth 100 % sweeps the whole
    // list (32..64) around a 48-band base, over a comb heard through a steady
    // 2 kHz tone. Plant: the host's own band-count lane switching straight
    // through the same counts at the same moments.
    const auto run = [](bool target, std::vector<int>* counts, std::vector<double>* costs,
                        float depth = 1.0f) {
        Rig rig;
        draw_comb(rig);
        rig.set(spectr::kParamBandCount, 48.0f);
        rig.set(spectr::kParamLfoRate, 2.0f);
        if (target) {
            rig.set(spectr::lfo_route_enabled_param_id(0, kBandsT), 1.0f);
            rig.set(spectr::lfo_route_amount_param_id(0, kBandsT), depth);
        }
        std::vector<float> out;
        for (int b = 0; b < int(4.0 * kRate / kBlock); ++b) {
            if (!target) {
                // The count the target would play, written as host automation.
                const double phase = double(rig.n) / kRate;  // 1 cycle a second
                const float wave = float(std::sin(2.0 * kPi * phase));
                const int count = spectr::modulated_band_count(48, wave);
                rig.set(spectr::kParamBandCount, float(count));
            }
            const double us = rig.block([](double) { return 2000.0; }, &out);
            if (costs) costs->push_back(us);
            if (counts) counts->push_back(rig.plugin->modulated_band_count_shown());
        }
        return out;
    };
    std::vector<int> counts;
    std::vector<double> costs;
    const auto with_target = run(true, &counts, &costs);
    // Cost: each block's cheapest of three identical renders, so a shared
    // build host's preemption is not read as the target's cost.
    for (int repeat = 0; repeat < 2; ++repeat) {
        std::vector<double> again;
        (void)run(true, nullptr, &again);
        for (std::size_t i = 0; i < std::min(again.size(), costs.size()); ++i)
            costs[i] = std::min(costs[i], again[i]);
    }
    const auto host_lane = run(false, nullptr, nullptr);
    // It reached both ends of the list and the base between them.
    CHECK(std::find(counts.begin(), counts.end(), 32) != counts.end());
    CHECK(std::find(counts.begin(), counts.end(), 64) != counts.end());
    CHECK(std::find(counts.begin(), counts.end(), 48) != counts.end());
    const auto splatter_db = [](const std::vector<float>& out) {
        // Energy far above the 2 kHz tone, relative to it, in the worst 5 ms
        // window: a discontinuity sprays it, a crossfade only moves the
        // tone's level.
        double worst = -200.0;
        const std::size_t win = std::size_t(0.005 * kRate);
        for (std::size_t at = std::size_t(kRate); at + win <= out.size(); at += win / 2) {
            const double tone = goertzel(out, at, win, 2000.0) + 1e-20;
            const double hf = goertzel(out, at, win, 9000.0) + goertzel(out, at, win, 13000.0);
            worst = std::max(worst, 10.0 * std::log10(hf / tone + 1e-20));
        }
        return worst;
    };
    // Control: the same render with the route at Depth 0 changes nothing,
    // and is the floor both measures sit on (the comb's own edges).
    const auto steady = run(true, nullptr, nullptr, 0.0f);
    const double target_step = largest_step_db(with_target);
    const double host_step = largest_step_db(host_lane);
    const double target_splatter = splatter_db(with_target);
    const double host_splatter = splatter_db(host_lane);
    const double steady_step = largest_step_db(steady);
    const double steady_splatter = splatter_db(steady);
    INFO("steady control: step " << steady_step << " dB, splatter " << steady_splatter << " dB");
    CHECK(steady_step < 1.0);
    INFO("largest 1 ms step: Bands target " << target_step << " dB, host band-count lane "
         << host_step << " dB; splatter " << target_splatter << " / " << host_splatter << " dB");
    // The target crossfades each count change through flat
    // (kBandsFadeSeconds, 80 ms, each way), so it is gentle where a straight switch
    // -- the host's band-count lane, the plant here -- is not: measured
    // 0.55-0.70 dB / -49..-52 dB against 10-11 dB / -15..-16 dB (cost,
    // cheapest of three: median 155-171 us, max 338-368 us of 5 333).
    CHECK(target_step < 1.5);
    CHECK(target_splatter < -40.0);
    CHECK(host_step > target_step);          // the plant shows the defect...
    CHECK(host_splatter > target_splatter);  // ...that the crossfade removes
    // The real-time cost gate: 256 samples at 48 kHz is 5 333 us.
    std::sort(costs.begin(), costs.end());
    const double median = costs[costs.size() / 2];
    INFO("per-callback cost median " << median << " us, p99 "
         << costs[costs.size() * 99 / 100] << " us, max " << costs.back() << " us");
    CHECK(median < 5333.0 / 4.0);
    CHECK(costs[costs.size() * 99 / 100] < 5333.0);
    // (The maximum is reported, not gated: a shared build host preempts.)
    std::printf("[bands-target] steady control step %.2f dB splatter %.1f dB\n",
                steady_step, steady_splatter);
    std::printf("[bands-target] largest 1 ms step %.2f dB (host lane %.2f dB); splatter %.1f dB"
                " (host lane %.1f dB); cost median %.0f us p99 %.0f us max %.0f us\n",
                target_step, host_step, target_splatter, host_splatter, median,
                costs[costs.size() * 99 / 100], costs.back());
}

TEST_CASE("the Preset destination morphs toward neighbouring presets and back",
          "[modulation][preset-target]") {
    Rig rig;
    // Neighbourhood: the current preset flat, one above at +12 dB everywhere,
    // one below at -12 dB everywhere.
    spectr::PresetModulationNeighbours n{};
    n.below = 1;
    n.above = 1;
    for (auto& g : n.gains[spectr::kPresetModulationSteps + 1]) g = 12.0f;
    for (auto& g : n.gains[spectr::kPresetModulationSteps - 1]) g = -12.0f;
    std::array<std::string, spectr::kPresetNeighbourCount> names{};
    names[spectr::kPresetModulationSteps - 1] = "BELOW";
    names[spectr::kPresetModulationSteps] = "CURRENT";
    names[spectr::kPresetModulationSteps + 1] = "ABOVE";
    REQUIRE(rig.plugin->set_preset_modulation("factory:flat", names, n));
    rig.set(spectr::kParamLfoShape, float(LfoShape::Square));
    rig.set(spectr::kParamLfoRate, 4.0f);  // 2 s: +1 for 1 s, then -1
    rig.set(spectr::lfo_route_enabled_param_id(0, kPresetT), 1.0f);
    rig.set(spectr::lfo_route_amount_param_id(0, kPresetT), 0.25f);  // 1 preset each way
    std::vector<float> out;
    std::vector<int> steps;
    for (int b = 0; b < int(4.0 * kRate / kBlock); ++b) {
        rig.block([](double) { return 1000.0; }, &out);
        steps.push_back(rig.plugin->preset_modulation_step_shown());
    }
    // +1 in the first second of each cycle, -1 in the second.
    CHECK(steps[std::size_t(0.8 * kRate / kBlock)] == 1);
    CHECK(steps[std::size_t(1.8 * kRate / kBlock)] == -1);
    CHECK(rig.plugin->preset_modulation_name(1) == "ABOVE");
    CHECK(rig.plugin->preset_modulation_name(-1) == "BELOW");
    // The level follows: ~+12 dB in the ABOVE half, ~-12 dB in the BELOW half.
    const auto level = [&](double from) {
        double e = 0.0;
        const std::size_t at = std::size_t(from * kRate), len = std::size_t(0.2 * kRate);
        for (std::size_t i = 0; i < len; ++i) e += double(out[at + i]) * out[at + i];
        return 10.0 * std::log10(e / double(len));
    };
    const double up = level(2.6), down = level(3.6);
    INFO("ABOVE half " << up << " dB, BELOW half " << down << " dB");
    CHECK(up - down == Approx(24.0).margin(3.0));
    // Persisted with the session.
    const auto blob = rig.plugin->serialize_plugin_state();
    Rig reopened;
    REQUIRE(reopened.plugin->deserialize_plugin_state(blob));
    CHECK(reopened.plugin->preset_modulation_centre_id() == "factory:flat");
    CHECK(reopened.plugin->preset_modulation_name(1) == "ABOVE");
}

TEST_CASE("switching the Bands route off fades back to the user's band count",
          "[modulation][bands-target]") {
    // A square at +1 holds 64 bands over a 32-band base; the route is then
    // switched off mid-render. The way home crossfades through flat too: the
    // route's 60 ms slew walks the count home while the route still plays, so
    // each step takes the fade (audio_bands_modulated_ also covers a route that
    // stops without its slew). A regression guard rather than a fail-before
    // test: it also passes with that flag removed.
    Rig rig;
    draw_comb(rig);
    rig.set(spectr::kParamLfoShape, float(LfoShape::Square));
    rig.set(spectr::kParamLfoRate, 16.0f);  // 8 s: +1 for 4 s
    rig.set(spectr::lfo_route_enabled_param_id(0, kBandsT), 1.0f);
    rig.set(spectr::lfo_route_amount_param_id(0, kBandsT), 1.0f);
    std::vector<float> out;
    bool saw_64 = false;
    for (int b = 0; b < int(3.0 * kRate / kBlock); ++b) {
        if (b == int(2.0 * kRate / kBlock))
            rig.set(spectr::lfo_route_enabled_param_id(0, kBandsT), 0.0f);
        rig.block([](double) { return 2000.0; }, &out);
        saw_64 = saw_64 || rig.plugin->modulated_band_count_shown() == 64;
    }
    CHECK(saw_64);
    CHECK(rig.plugin->modulated_band_count_shown() == 0);
    const double step = largest_step_db(out);
    INFO("largest 1 ms step across the switch-off " << step << " dB");
    // Measured 1.8 dB: the route's own 60 ms slew walks the count down
    // through the list, each step its own fade. A straight switch home is
    // 10-11 dB (the plant in the test above).
    CHECK(step < 2.5);
}
