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
