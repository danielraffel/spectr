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
#include <cstdlib>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <utility>
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

// How a Rig's blocks are paced against Spectr's workers (mask design, param
// sync). A real-time host paces its callbacks, so the workers finish between
// them; a back-to-back harness does not, and on a loaded machine the adoption
// of a staged layout would then trail by a load-dependent number of blocks.
enum class Pacing {
    // Real-time blocks; after each one, wait for both worker backlogs to read
    // zero -- the pacing a real-time host gives them. Deterministic.
    paced,
    // Offline blocks back to back: the processor itself waits for its workers,
    // so this must equal `paced` sample for sample.
    offline,
    // Real-time blocks back to back with no wait: what a bounce was before the
    // processor heard it was offline. Load-dependent; a control only.
    unpaced,
};

// Event-driven on the exported counters (no fixed delay); a worker that never
// drains fails the test rather than measuring a stale render.
void await_workers() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (spectr_mask_design_backlog_v1() != 0 || spectr_param_sync_backlog_v1() != 0) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

struct Rig {
    Pacing pacing = Pacing::paced;
    pulp::format::HeadlessHost host{create_tracking};
    spectr::Spectr* plugin = nullptr;
    std::uint64_t n = 0;
    explicit Rig(Pacing p = Pacing::paced) : pacing(p) {
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
        pulp::midi::MidiBuffer midi_in, midi_out;
        pulp::format::ProcessContext ctx;
        if (pacing == Pacing::offline) {
            ctx.process_mode = pulp::format::ProcessMode::Offline;
            ctx.render_speed_hint = pulp::format::RenderSpeedHint::FasterThanRealtime;
        } else {
            ctx.process_mode = pulp::format::ProcessMode::Realtime;
            ctx.render_speed_hint = pulp::format::RenderSpeedHint::Realtime;
        }
        const auto t0 = std::chrono::steady_clock::now();
        host.process(ov, iv, midi_in, midi_out, events, ctx);
        const auto t1 = std::chrono::steady_clock::now();
        // Outside the timed region: the cost is the callback's own.
        if (pacing == Pacing::paced) await_workers();
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

// ── Hold for Length: every trigger is its own freeze, of its own Length ─────
//
// "Hold for Length = keep Freeze enabled for the length of the modulated
// Length": each rising edge of the Freeze target's gate freezes fresh audio
// and holds it for the Length in effect at that trigger -- the Length
// target's step included -- then releases; the next trigger takes the Length
// in effect then. These runs log every hold (trigger, the Length it took,
// when it ended, whether the freeze source latched fresh audio for it, the
// loop it played) from the processor's own counters.
namespace {

struct HoldSetup {
    double bpm = 120.0;
    LfoShape freeze_shape = LfoShape::Square;
    float freeze_rate = 4.0f;    // beats per LFO 1 cycle
    float freeze_depth = 0.1f;   // the gate's duty
    int length_preset = spectr::kDefaultLengthPreset;
    bool hold = true;
    float length_depth_lfo1 = -1.0f;  // < 0: LFO 1 does not drive Length
    bool lfo2 = false;                // LFO 2 drives Length
    LfoShape lfo2_shape = LfoShape::Saw;
    float lfo2_rate = 7.0f;
    float lfo2_depth = 0.5f;
    double seconds = 30.0;
    double freeze_from = 0.0;  // the Freeze route comes on here (warm-up)
};

struct HoldLog {
    std::uint64_t start = 0, end = 0;  // [start, end) samples
    double seconds = 0.0;              // the Length the trigger took
    int index = -1;                    // its LENGTH-list index (label)
    int labels_seen = 0;               // distinct labels shown while held
    std::uint32_t source_latches = 0;  // fresh freezes the source made for it
    std::int64_t loop = 0;             // the loop it played
    bool loop_resized = false;         // the loop changed mid-hold
    int modulated_seen_differs = 0;    // blocks the Length target sat elsewhere
    int source_length_drift = 0;       // blocks the source was asked another length
};

struct HoldRunLog {
    std::vector<HoldLog> holds;  // complete holds only
    std::vector<std::pair<std::uint64_t, std::uint64_t>> frozen;  // effective spans
    std::vector<int> label_between;  // label shown on each block between holds
};

std::size_t hold_slack() { return kBlock; }

HoldRunLog run_hold_log(const HoldSetup& setup) {
    pulp::format::HeadlessHost host{create_tracking};
    host.prepare(kRate, kBlock);
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    auto& s = host.state();
    s.set_value(spectr::kParamLfoEnabled, 1.0f);
    s.set_value(spectr::lfo_route_enabled_param_id(0, 0), 0.0f);
    s.set_value(spectr::lfo_route_enabled_param_id(1, 0), 0.0f);
    s.set_value(spectr::kParamLfoShape, float(setup.freeze_shape));
    s.set_value(spectr::kParamLfoRate, setup.freeze_rate);
    s.set_value(spectr::lfo_route_amount_param_id(0, kFreezeT), setup.freeze_depth);
    s.set_value(spectr::lfo_route_enabled_param_id(0, kFreezeT),
                setup.freeze_from > 0.0 ? 0.0f : 1.0f);
    s.set_value(spectr::kParamFreezeLength, float(setup.length_preset));
    s.set_value(spectr::kParamFreezeHoldForLength, setup.hold ? 1.0f : 0.0f);
    if (setup.length_depth_lfo1 >= 0.0f) {
        s.set_value(spectr::lfo_route_enabled_param_id(0, kLengthT), 1.0f);
        s.set_value(spectr::lfo_route_amount_param_id(0, kLengthT), setup.length_depth_lfo1);
    }
    if (setup.lfo2) {
        s.set_value(spectr::kParamLfo2Enabled, 1.0f);
        s.set_value(spectr::kParamLfo2Shape, float(setup.lfo2_shape));
        s.set_value(spectr::kParamLfo2Rate, setup.lfo2_rate);
        s.set_value(spectr::lfo_route_enabled_param_id(1, kLengthT), 1.0f);
        s.set_value(spectr::lfo_route_amount_param_id(1, kLengthT), setup.lfo2_depth);
    }
    HoldRunLog log;
    pulp::audio::Buffer<float> in(2, kBlock), o(2, kBlock);
    double phase = 0.0;
    bool was = false, open = false;
    HoldLog current;
    std::uint32_t latches = plugin->freeze_hold_latch_count();
    std::uint32_t source_latches = plugin->freeze_source().latch_count();
    std::int64_t loop_first = -1;
    std::vector<int> labels;
    const auto close = [&](std::uint64_t at) {
        current.end = at;
        current.labels_seen = int(labels.size());
        log.holds.push_back(current);
        open = false;
    };
    std::uint64_t n = 0;
    while (n < std::uint64_t(setup.seconds * kRate)) {
        if (setup.freeze_from > 0.0 && n == std::uint64_t(setup.freeze_from * kRate) / kBlock * kBlock)
            s.set_value(spectr::lfo_route_enabled_param_id(0, kFreezeT), 1.0f);
        for (std::size_t i = 0; i < kBlock; ++i) {
            phase += 2.0 * kPi * 440.0 / kRate;
            in.channel(0)[i] = in.channel(1)[i] = 0.3f * float(std::sin(phase));
        }
        const float* ip[] = {in.channel(0).data(), in.channel(1).data()};
        pulp::audio::BufferView<const float> iv(ip, 2, kBlock);
        auto ov = o.view();
        pulp::format::ProcessContext ctx;
        ctx.tempo_bpm = setup.bpm;
        ctx.time_sig_numerator = 4;
        ctx.time_sig_denominator = 4;
        ctx.position_beats = double(n) / kRate * setup.bpm / 60.0;
        ctx.is_playing = true;
        host.process(ov, iv, ctx);
        const bool now = plugin->freeze_effective();
        if (now && !was) log.frozen.push_back({n, 0});
        if (!now && was) log.frozen.back().second = n;
        was = now;
        // A latch: the hold before it (if any) ends here, back to back.
        const auto count = plugin->freeze_hold_latch_count();
        if (count != latches) {
            REQUIRE(count == latches + 1);  // at most one per block here
            latches = count;
            if (open) close(n);
            current = HoldLog{};
            current.start = n;
            current.seconds = plugin->freeze_hold_latched_seconds();
            current.index = plugin->freeze_engaged_length_index();
            open = true;
            loop_first = -1;
            labels.clear();
        } else if (open && !now) {
            close(n);
        }
        const auto source = plugin->freeze_source().latch_count();
        if (open) {
            current.source_latches += source - source_latches;
            const int label = plugin->freeze_shown_length_index();
            if (std::find(labels.begin(), labels.end(), label) == labels.end())
                labels.push_back(label);
            if (plugin->freeze_modulated_length_index() != current.index)
                ++current.modulated_seen_differs;
            // Whenever the source latches for this hold -- a hop, or a whole
            // release fade, after the trigger -- it takes the hold's length.
            if (now && plugin->freeze_source().hold_seconds() != current.seconds)
                ++current.source_length_drift;
            // The loop this hold's latch made, once it plays alone.
            if (current.source_latches > 0
                && plugin->freeze_source().phase() == spectr::FreezeSource::Phase::held) {
                const auto loop = plugin->freeze_source().loop_length();
                if (loop_first < 0) loop_first = loop;
                else if (loop != loop_first) current.loop_resized = true;
                current.loop = loop;
            }
        } else {
            log.label_between.push_back(plugin->freeze_shown_length_index());
        }
        source_latches = source;
        n += kBlock;
    }
    return log;
}

std::uint64_t samples_of(double seconds) {
    return std::uint64_t(std::llround(seconds * kRate));
}

// Each complete hold lasted its own Length (to the slice), was a fresh freeze
// of the source, showed its own Length the whole time, and was never resized.
void check_holds_each_their_own(const HoldRunLog& log, std::size_t at_least) {
    REQUIRE(log.holds.size() >= at_least);
    for (std::size_t i = 0; i < log.holds.size(); ++i) {
        const auto& h = log.holds[i];
        INFO("hold " << i << " from " << double(h.start) / kRate << " s, Length "
             << h.seconds << " s (index " << h.index << "), ended "
             << double(h.end) / kRate << " s, source latches " << h.source_latches);
        CHECK(h.end - h.start >= samples_of(h.seconds));
        CHECK(h.end - h.start < samples_of(h.seconds) + hold_slack());
        CHECK(h.source_latches == 1);
        CHECK(h.labels_seen == 1);
        CHECK_FALSE(h.loop_resized);
        CHECK(h.source_length_drift == 0);
    }
}

}  // namespace

TEST_CASE("Hold for Length releases every hold even when the next trigger lands on its end",
          "[modulation][freeze-target][hold-for-length]") {
    // The defaults: 1 bar Length, LFO at 4 beats -- every hold ends on the
    // slice the next cycle's gate rises. Each must still be its own freeze.
    HoldSetup setup;
    setup.seconds = 20.0;
    const auto log = run_hold_log(setup);
    check_holds_each_their_own(log, 8);
    for (const auto& h : log.holds) CHECK(h.seconds == Approx(2.0));
    // Back to back: the freeze never visibly drops, yet every hold re-latched.
    CHECK(log.holds.size() >= 8);
    CHECK(log.frozen.size() == 1);
}

TEST_CASE("Hold for Length: one LFO on Freeze and Length walks its lengths, every hold released",
          "[modulation][freeze-target][hold-for-length][length-target]") {
    // The reported setup: LFO 1 Sine at 4 beats, Freeze Depth 18 %, Length
    // Depth 69 %, LENGTH 1 bar, Hold for Length on, 120 BPM.
    HoldSetup setup;
    setup.freeze_shape = LfoShape::Sine;
    setup.freeze_rate = 4.0f;
    setup.freeze_depth = 0.18f;
    setup.length_depth_lfo1 = 0.69f;
    setup.seconds = 150.0;
    const auto log = run_hold_log(setup);
    check_holds_each_their_own(log, 9);
    // The trigger phase is the same every cycle (the gate's threshold), so
    // the n-th hold reads the wave n/8 of a cycle past it: an independent
    // oracle for each hold's Length.
    const double cycle_seconds = 4.0 * 60.0 / setup.bpm;
    std::vector<int> distinct;
    for (std::size_t i = 0; i < log.holds.size(); ++i) {
        const auto& h = log.holds[i];
        const double trigger_phase = double(h.start) / kRate / cycle_seconds;
        const double read = trigger_phase + 0.125 * double(i % 8);
        const float coordinate = 0.69f * float(std::sin(2.0 * kPi * read));
        const int expected = spectr::modulated_length_index(16, coordinate, 20);
        INFO("hold " << i << " read at phase " << read << " coordinate " << coordinate);
        CHECK(h.index == expected);
        CHECK(h.seconds == Approx(spectr::length_in_bars(
            spectr::kLengthPresets[std::size_t(expected)]) * cycle_seconds));
        if (i < 8 && std::find(distinct.begin(), distinct.end(), h.index) == distinct.end())
            distinct.push_back(h.index);
    }
    // Eight holds follow the wave's shape: several different lengths.
    CHECK(distinct.size() >= 3);
    // Released between holds, the label tracks the Length target again.
    std::vector<int> between = log.label_between;
    std::sort(between.begin(), between.end());
    CHECK(std::unique(between.begin(), between.end()) - between.begin() >= 2);
}

TEST_CASE("Hold for Length: a Length on the other LFO is read at each trigger",
          "[modulation][freeze-target][hold-for-length][length-target]") {
    // LFO 1 (square, 4 beats) triggers; LFO 2 (saw, 7 beats) steps Length
    // around 5/6 bar by up to 4 steps: 1/2 bar .. 2 bars. Freeze comes on
    // after a warm-up long enough to hold a 2-bar loop.
    HoldSetup setup;
    setup.length_preset = 13;
    setup.lfo2 = true;
    setup.lfo2_shape = LfoShape::Saw;
    setup.lfo2_rate = 7.0f;
    setup.lfo2_depth = 0.5f;
    setup.freeze_from = 5.0;
    setup.seconds = 60.0;
    const auto log = run_hold_log(setup);
    check_holds_each_their_own(log, 10);
    const double bar = 4.0 * 60.0 / setup.bpm;
    std::vector<int> distinct;
    for (std::size_t i = 0; i < log.holds.size(); ++i) {
        const auto& h = log.holds[i];
        // LFO 2's phase at the trigger, straight from the transport.
        const double beats = double(h.start) / kRate * setup.bpm / 60.0;
        const double p = beats / 7.0 - std::floor(beats / 7.0);
        const int expected = spectr::modulated_length_index(
            13, float(2.0 * p - 1.0) * 0.5f, 20);
        INFO("hold " << i << " at " << double(h.start) / kRate << " s, LFO 2 phase " << p);
        CHECK(h.index == expected);
        // The loop it played is exactly its Length (enough history behind it).
        CHECK(h.loop == std::int64_t(samples_of(h.seconds)));
        CHECK(h.seconds == Approx(spectr::length_in_bars(
            spectr::kLengthPresets[std::size_t(expected)]) * bar));
        // The Length target kept moving during the hold without resizing it.
        if (std::find(distinct.begin(), distinct.end(), h.index) == distinct.end())
            distinct.push_back(h.index);
    }
    CHECK(distinct.size() >= 4);
    // Lengths changed between triggers, including from long to short.
    bool long_then_short = false;
    for (std::size_t i = 0; i + 1 < log.holds.size(); ++i)
        long_then_short = long_then_short
            || (log.holds[i].index >= 16 && log.holds[i + 1].index <= 10);
    CHECK(long_then_short);
    int moved = 0;
    for (const auto& h : log.holds) moved += h.modulated_seen_differs > 0 ? 1 : 0;
    CHECK(moved > 0);  // control: the target did move while holds played
}

TEST_CASE("Hold for Length off: Freeze follows the gate with modulated Length",
          "[modulation][freeze-target][hold-for-length]") {
    HoldSetup setup;
    setup.freeze_shape = LfoShape::Sine;
    setup.freeze_depth = 0.18f;
    setup.length_depth_lfo1 = 0.69f;
    setup.hold = false;
    setup.seconds = 20.0;
    const auto log = run_hold_log(setup);
    CHECK(log.holds.empty());  // no hold-mode latch at all
    REQUIRE(log.frozen.size() >= 8);
    for (std::size_t i = 0; i + 1 < log.frozen.size(); ++i) {
        const double held = double(log.frozen[i].second - log.frozen[i].first) / kRate;
        // The gate's own 18 % of a 2 s cycle, to the block.
        CHECK(held == Approx(0.18 * 2.0).margin(2.0 * double(kBlock) / kRate));
    }
}

TEST_CASE("Hold for Length is off in a new instance and in a session saved without it",
          "[modulation][freeze-target][hold-for-length][state]") {
    const auto wired = [] {
        auto store = std::make_unique<pulp::state::StateStore>();
        auto plugin = std::make_unique<spectr::Spectr>();
        plugin->set_state_store(store.get());
        plugin->define_parameters(*store);
        return std::make_pair(std::move(store), std::move(plugin));
    };
    auto [store, plugin] = wired();
    CHECK(store->get_value(spectr::kParamFreezeHoldForLength) == 0.0f);
    CHECK_FALSE(plugin->freeze_hold_for_length());
    // An older session: a store that never had the lane.
    pulp::state::StateStore old;
    {
        pulp::state::ParamInfo info;
        info.id = spectr::kParamFreezeLength;
        info.name = "Freeze Length";
        info.range = {0.0f, 20.0f, 16.0f, 1.0f};
        old.add_parameter(info);
        old.set_value(spectr::kParamFreezeLength, 17.0f);
    }
    const auto blob = old.serialize();
    auto [loaded_store, loaded] = wired();
    REQUIRE(loaded_store->deserialize(std::span<const std::uint8_t>(blob)));
    CHECK(loaded_store->get_value(spectr::kParamFreezeLength) == 17.0f);  // control
    CHECK(loaded_store->get_value(spectr::kParamFreezeHoldForLength) == 0.0f);
    CHECK_FALSE(loaded->freeze_hold_for_length());
    // Control: a session saved with it on restores it on.
    store->set_value(spectr::kParamFreezeHoldForLength, 1.0f);
    const auto with = store->serialize();
    auto [on_store, on] = wired();
    REQUIRE(on_store->deserialize(std::span<const std::uint8_t>(with)));
    CHECK(on->freeze_hold_for_length());
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
    Rig rig(std::getenv("SPECTR_TEST_UNPACED") ? Pacing::unpaced : Pacing::paced);
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
    std::printf("[bands-target] switch-off largest 1 ms step %.4f dB\n", step);
    INFO("largest 1 ms step across the switch-off " << step << " dB");
    // Measured 0.27 dB paced: the route's own 60 ms slew walks the count down
    // through the list, each step its own fade. (Rendered back to back it read
    // 1.5-2.9 dB: the workers fell behind, and a param sync's base mask could
    // replace the fading one for a block.) A straight switch home is 10-11 dB
    // (the plant in the test above).
    CHECK(step < 2.5);
}

TEST_CASE("an offline render of the Bands destination fades exactly as a paced one",
          "[modulation][bands-target][offline]") {
    // Bands swept by a sine LFO and then switched off, rendered twice: paced
    // like a real-time host, and offline back to back. The processor waits
    // for its own workers on an offline block, so the bounce takes every
    // band-count fade step a paced render takes, at the same block.
    const auto render = [](Pacing pacing) {
        Rig rig(pacing);
        draw_comb(rig);
        rig.set(spectr::kParamBandCount, 48.0f);
        rig.set(spectr::kParamLfoRate, 2.0f);
        rig.set(spectr::lfo_route_enabled_param_id(0, kBandsT), 1.0f);
        rig.set(spectr::lfo_route_amount_param_id(0, kBandsT), 1.0f);
        std::vector<float> out;
        for (int b = 0; b < int(3.0 * kRate / kBlock); ++b) {
            if (b == int(2.0 * kRate / kBlock))
                rig.set(spectr::lfo_route_enabled_param_id(0, kBandsT), 0.0f);
            rig.block([](double) { return 2000.0; }, &out);
        }
        return out;
    };
    const auto max_diff = [](const std::vector<float>& a, const std::vector<float>& b) {
        REQUIRE(a.size() == b.size());
        double worst = 0.0;
        std::size_t first = a.size(), last = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double d = std::fabs(double(a[i]) - double(b[i]));
            if (d > 0.0) { first = std::min(first, i); last = i; }
            worst = std::max(worst, d);
        }
        if (first < a.size())
            std::printf("[bands-target]   differs over samples %zu..%zu (blocks %zu..%zu)\n",
                        first, last, first / kBlock, last / kBlock);
        return worst;
    };
    const auto paced = render(Pacing::paced);
    const auto offline = render(Pacing::offline);
    // Control, reported only: back to back with no wait adopts wherever the
    // worker lands, which on a loaded machine is not where a paced host would.
    const auto unpaced = render(Pacing::unpaced);
    const double offline_diff = max_diff(paced, offline);
    const double unpaced_diff = max_diff(paced, unpaced);
    std::printf("[bands-target] offline vs paced max |diff| %.3g; unpaced (control) %.3g\n",
                offline_diff, unpaced_diff);
    INFO("offline vs paced " << offline_diff << ", unpaced control " << unpaced_diff);
    CHECK(offline_diff <= 1e-6);
}

namespace {
// Restores the in-process sync-worker stall on every exit, so a failing CHECK
// cannot leave the next test case running against a starved worker.
struct SyncStall {
    explicit SyncStall(int ms) { spectr::detail::g_param_sync_test_stall_ms.store(ms); }
    ~SyncStall() {
        spectr::detail::g_param_sync_test_stall_ms.store(0);
        await_workers();
    }
};
}  // namespace

TEST_CASE("an offline flag left set never holds a block past the wait budget",
          "[offline][offline-budget]") {
    // A host that set the offline flag for a bounce and never wrote it back
    // makes every later block an offline one. With the sync worker starved
    // (2 s), such a block may wait for it no longer than the budget, then
    // render; the processor counts the give-up.
    Rig rig(Pacing::unpaced);
    rig.plugin->set_host_offline_render(true);
    const SyncStall stall(2000);
    rig.set(spectr::kParamBandCount, 48.0f);
    (void)rig.block([](double) { return 1000.0; });  // spawns the sync at its end
    const double waited_us = rig.block([](double) { return 1000.0; });
    const double budget_us =
        std::chrono::duration<double, std::micro>(spectr::kOfflineBlockWaitBudget).count();
    std::printf("[offline-budget] block with a starved worker took %.1f ms (budget %.0f ms)\n",
                waited_us / 1000.0, budget_us / 1000.0);
    INFO("block took " << waited_us / 1000.0 << " ms, budget " << budget_us / 1000.0 << " ms");
    CHECK(waited_us < budget_us + 250'000.0);
    // And the budget itself stays a few hundred ms, whatever it is set to: a
    // realtime callback under a stuck flag pays it.
    CHECK(budget_us <= 500'000.0);
    CHECK(rig.plugin->offline_wait_budget_exhausted_count() >= 1);
}

TEST_CASE("prepare clears a host offline flag", "[offline][offline-budget]") {
    Rig rig(Pacing::unpaced);
    rig.plugin->set_host_offline_render(true);
    REQUIRE(rig.plugin->host_offline_render());
    rig.host.prepare(kRate, kBlock);
    CHECK_FALSE(rig.plugin->host_offline_render());
}

namespace {
// Runs the param-sync worker to completion at the instant it is spawned: the
// worst interleaving the scheduler could produce.
void run_sync_worker_now() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (spectr_param_sync_backlog_v1() != 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
}
struct SpawnHook {
    SpawnHook() { spectr::detail::g_param_sync_spawned_hook.store(&run_sync_worker_now); }
    ~SpawnHook() { spectr::detail::g_param_sync_spawned_hook.store(nullptr); }
};
}  // namespace

TEST_CASE("a param sync loses to the mask the audio path claimed, however fast it runs",
          "[modulation][bands-target][sync-order]") {
    // While an LFO drives Bands the audio path owns the live mask and claims
    // it every block. A host write drifts the surface and spawns a param sync
    // in that same block; its base mask must be superseded by the audio
    // path's request, even when the worker runs to completion the instant it
    // is spawned -- before the block's handoff, had the spawn come first.
    Rig rig(Pacing::paced);
    draw_comb(rig);
    rig.set(spectr::kParamBandCount, 48.0f);
    rig.set(spectr::kParamLfoRate, 2.0f);
    rig.set(spectr::lfo_route_enabled_param_id(0, kBandsT), 1.0f);
    rig.set(spectr::lfo_route_amount_param_id(0, kBandsT), 1.0f);
    for (int b = 0; b < 40; ++b) rig.block([](double) { return 2000.0; });
    const SpawnHook hook;
    const auto before = rig.plugin->param_sync_superseded_count();
    constexpr int kWrites = 6;
    for (int w = 0; w < kWrites; ++w) {
        rig.set(spectr::band_gain_param_id(5), w % 2 ? 12.0f : -6.0f);
        for (int b = 0; b < 8; ++b) rig.block([](double) { return 2000.0; });
    }
    const auto superseded = rig.plugin->param_sync_superseded_count() - before;
    std::printf("[sync-order] %llu of %d worker-first sync publishes superseded\n",
                static_cast<unsigned long long>(superseded), kWrites);
    // Every write's sync loses. With the spawn ahead of the block's handoff
    // (the order this replaced) the same run counts 0: each worker staged its
    // base mask over the request the audio path had not yet published.
    CHECK(superseded == static_cast<std::uint64_t>(kWrites));
}
