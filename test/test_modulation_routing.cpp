// Per-LFO multi-destination routing: the combination rules, the viewport
// destinations, and the smoothness every routing lane owes a host that
// automates it. See docs/modulation.md for the contract these tests pin.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <pulp/format/headless.hpp>
#include "spectr/modulation.hpp"
#include "spectr/param_surface.hpp"
#include "spectr/spectr.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

using Catch::Approx;
using spectr::ModulationTarget;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::unique_ptr<pulp::format::Processor> create_mixing() {
    auto p = spectr::create_spectr();
    REQUIRE(static_cast<spectr::Spectr*>(p.get())->set_render_mode(
        spectr::MaskRenderMode::linear_phase));
    return p;
}
std::unique_ptr<pulp::format::Processor> create_tracking() {
    auto p = spectr::create_spectr();
    REQUIRE(static_cast<spectr::Spectr*>(p.get())->set_render_mode(
        spectr::MaskRenderMode::zero_latency));
    return p;
}

std::size_t idx(ModulationTarget t) { return static_cast<std::size_t>(t); }

spectr::LfoRoutes routes_of(std::initializer_list<ModulationTarget> on,
                            float amount = 1.0f) {
    spectr::LfoRoutes routes{};
    for (auto t : on) routes[idx(t)] = {true, amount};
    return routes;
}

struct Bank {
    spectr::SnapshotBank bank;
    spectr::BandField canonical;
    Bank() {
        canonical.reset();
        spectr::BandField a, b;
        a.reset();
        b.reset();
        for (std::size_t i = 0; i < spectr::kMaxBands; ++i) {
            canonical.bands[i].gain_db = -6.0f;
            a.bands[i].gain_db = -12.0f;
            b.bands[i].gain_db = 12.0f;
        }
        bank.capture_into(spectr::SnapshotBank::Slot::A, a, {}, spectr::Layout::Bands32);
        bank.capture_into(spectr::SnapshotBank::Slot::B, b, {}, spectr::Layout::Bands32);
    }
};

spectr::ModulationSettings two_lfos(spectr::LfoRoutes r1, float d1,
                                    spectr::LfoRoutes r2 = {}, float d2 = 0.0f) {
    spectr::ModulationSettings s;
    s.enabled = d1 > 0.0f;
    s.depth = d1;
    s.lfo2_enabled = d2 > 0.0f;
    s.lfo2_depth = d2;
    s.routes = {r1, r2};
    return s;
}

}  // namespace

// ── Combination math ─────────────────────────────────────────────────────

TEST_CASE("stacked Bank and Morph both reach the field", "[modulation][routing]") {
    // Before routing, Morph REPLACED the field and a Bank selected alongside it
    // was silently discarded. Now Morph reshapes and Bank offsets the result.
    Bank b;
    const float host_morph = 0.5f;  // canonical sits mid-morph: 0 dB in A..B
    for (auto& band : b.canonical.bands) band.gain_db = 0.0f;
    const auto s = two_lfos(routes_of({ModulationTarget::WholeBank,
                                       ModulationTarget::Morph}), 0.5f);
    const auto out = spectr::compose_internal_modulation(
        b.canonical, b.bank, host_morph, s, 1.0f, 0.0f);
    // Morph: t 0.5 -> 0.75 moves the field by morph(.75) - morph(.5) = +6 dB.
    // Bank: +1 x 0.5 x 12 = +6 dB on top.
    CHECK(out.field.bands[0].gain_db == Approx(12.0f));
    // Each alone, as controls.
    const auto bank_only = spectr::compose_internal_modulation(
        b.canonical, b.bank, host_morph,
        two_lfos(routes_of({ModulationTarget::WholeBank}), 0.5f), 1.0f, 0.0f);
    const auto morph_only = spectr::compose_internal_modulation(
        b.canonical, b.bank, host_morph,
        two_lfos(routes_of({ModulationTarget::Morph}), 0.5f), 1.0f, 0.0f);
    CHECK(bank_only.field.bands[0].gain_db == Approx(6.0f));
    CHECK(morph_only.field.bands[0].gain_db == Approx(6.0f));
}

TEST_CASE("two LFOs on one destination sum, independent of order", "[modulation][routing]") {
    Bank b;
    const auto bank = routes_of({ModulationTarget::WholeBank});
    const auto ab = spectr::compose_internal_modulation(
        b.canonical, b.bank, 0.0f, two_lfos(bank, 1.0f, bank, 1.0f), 1.0f, 1.0f);
    // Summed then clamped ONCE: -6 + 24 = +18 dB, inside the band range. A
    // per-LFO clamp-then-apply would agree here; the order test below is the
    // one that separates them.
    CHECK(ab.field.bands[0].gain_db == Approx(18.0f));
    // Opposite waves cancel exactly -- an LFO-by-LFO clamp at the floor would not.
    spectr::BandField low = b.canonical;
    for (auto& band : low.bands) band.gain_db = spectr::kBandGainMinDb + 2.0f;
    const auto cancel = spectr::compose_internal_modulation(
        low, b.bank, 0.0f, two_lfos(bank, 1.0f, bank, 1.0f), -1.0f, 1.0f);
    CHECK(cancel.field.bands[0].gain_db == Approx(spectr::kBandGainMinDb + 2.0f));
    // Swapping which LFO carries which wave changes nothing.
    const auto swapped = spectr::compose_internal_modulation(
        low, b.bank, 0.0f, two_lfos(bank, 1.0f, bank, 1.0f), 1.0f, -1.0f);
    CHECK(swapped.field.bands[0].gain_db == Approx(cancel.field.bands[0].gain_db));
}

TEST_CASE("every stack stays inside the band range and keeps mutes", "[modulation][routing]") {
    Bank b;
    b.canonical.bands[3].muted = true;
    const auto all = routes_of({ModulationTarget::WholeBank, ModulationTarget::SnapshotA,
                                ModulationTarget::SnapshotB, ModulationTarget::Morph,
                                ModulationTarget::ViewportPosition,
                                ModulationTarget::ViewportZoom});
    for (int step = 0; step <= 64; ++step) {
        const float w1 = std::sin(2.0f * static_cast<float>(kPi) * step / 64.0f);
        const float w2 = std::cos(2.0f * static_cast<float>(kPi) * step / 64.0f);
        for (float host_morph : {0.0f, 0.5f, 1.0f}) {
            const auto out = spectr::compose_internal_modulation(
                b.canonical, b.bank, host_morph, two_lfos(all, 1.0f, all, 1.0f), w1, w2);
            for (std::size_t i = 0; i < spectr::kMaxBands; ++i) {
                REQUIRE(out.field.bands[i].gain_db >= spectr::kBandGainMinDb);
                REQUIRE(out.field.bands[i].gain_db <= spectr::kBandGainMaxDb);
            }
            REQUIRE(out.field.bands[3].muted);
            REQUIRE(out.field.bands[3].gain_db == Approx(-6.0f));
            const auto v = spectr::apply_viewport_modulation(spectr::Viewport{100.0f, 1000.0f},
                                                             out.coords);
            REQUIRE(v.valid());
            REQUIRE(v.min_hz >= 20.0f * 0.999f);
            REQUIRE(v.max_hz <= 20000.0f * 1.001f);
            REQUIRE(std::log10(v.max_hz / v.min_hz) >= std::log10(2.0f) - 1e-4f);
        }
    }
}

TEST_CASE("a target's Depth alone scales its modulation", "[modulation][routing]") {
    // No LFO-level depth: an LFO that is on contributes wave x target Depth.
    Bank b;
    for (float depth : {0.0f, 0.25f, 0.5f, 1.0f}) {
        INFO("depth " << depth);
        const auto out = spectr::compose_internal_modulation(
            b.canonical, b.bank, 0.0f,
            two_lfos(routes_of({ModulationTarget::WholeBank}, depth), 1.0f), 1.0f, 0.0f);
        CHECK(out.field.bands[0].gain_db == Approx(-6.0f + depth * 12.0f));
        const auto v = spectr::apply_viewport_modulation(
            spectr::Viewport{100.0f, 1000.0f},
            spectr::modulation_coordinates(
                two_lfos(routes_of({ModulationTarget::ViewportPosition}, depth), 1.0f),
                1.0f, 0.0f));
        CHECK(std::log10(v.min_hz) == Approx(2.0f + depth).margin(1e-4));
        CHECK(std::log10(v.max_hz) == Approx(3.0f + depth).margin(1e-4));
    }
    // Two targets of one LFO at different depths move independently.
    auto mixed = routes_of({ModulationTarget::WholeBank, ModulationTarget::ViewportPosition});
    mixed[idx(ModulationTarget::WholeBank)].amount = 0.25f;
    mixed[idx(ModulationTarget::ViewportPosition)].amount = 1.0f;
    const auto both = spectr::compose_internal_modulation(
        b.canonical, b.bank, 0.0f, two_lfos(mixed, 1.0f), 1.0f, 0.0f);
    CHECK(both.field.bands[0].gain_db == Approx(-6.0f + 3.0f));
    CHECK(both.coords[ModulationTarget::ViewportPosition] == Approx(1.0f));
    // A disabled route ignores its Depth.
    auto r = routes_of({});
    r[idx(ModulationTarget::WholeBank)] = {false, 1.0f};
    const auto off = spectr::compose_internal_modulation(
        b.canonical, b.bank, 0.0f, two_lfos(r, 1.0f), 1.0f, 0.0f);
    CHECK(off.field.bands[0].gain_db == Approx(-6.0f));
}

TEST_CASE("viewport position slides and zoom scales about the centre", "[modulation][routing][viewport]") {
    const spectr::Viewport base{100.0f, 1000.0f};  // centre 10^2.5, one decade
    const auto at = [&](ModulationTarget t, float depth, float wave) {
        return spectr::apply_viewport_modulation(
            base, spectr::modulation_coordinates(two_lfos(routes_of({t}), depth), wave, 0.0f));
    };
    // Position: +/- depth decades, width kept.
    auto v = at(ModulationTarget::ViewportPosition, 0.5f, 1.0f);
    CHECK(std::log10(v.min_hz) == Approx(2.5f).margin(1e-4));
    CHECK(std::log10(v.max_hz) == Approx(3.5f).margin(1e-4));
    v = at(ModulationTarget::ViewportPosition, 0.5f, -1.0f);
    CHECK(std::log10(v.min_hz) == Approx(1.5f).margin(1e-4));
    // ...held at the 20 Hz edge with its width, never squeezed.
    v = at(ModulationTarget::ViewportPosition, 1.0f, -1.0f);
    CHECK(v.min_hz == Approx(20.0f).epsilon(1e-3));
    CHECK(std::log10(v.max_hz / v.min_hz) == Approx(1.0f).margin(1e-4));
    // Zoom: width x 2^(depth x wave) in log-frequency, centre fixed.
    v = at(ModulationTarget::ViewportZoom, 1.0f, 1.0f);
    CHECK(std::log10(v.min_hz) == Approx(1.5f).margin(1e-4));
    CHECK(std::log10(v.max_hz) == Approx(3.5f).margin(1e-4));
    v = at(ModulationTarget::ViewportZoom, 1.0f, -1.0f);
    CHECK(std::log10(v.min_hz) == Approx(2.25f).margin(1e-4));
    CHECK(std::log10(v.max_hz) == Approx(2.75f).margin(1e-4));
    // Identity at zero.
    v = at(ModulationTarget::ViewportZoom, 1.0f, 0.0f);
    CHECK(v.min_hz == base.min_hz);
    CHECK(v.max_hz == base.max_hz);
}

// ── Through the processor ────────────────────────────────────────────────

namespace {

struct Render {
    std::vector<float> band0_db;       // audible band 0 gain per block
    std::vector<float> center_log;     // audible viewport centre per block
    std::vector<float> out;            // channel 0
    std::vector<double> block_us;      // process() wall time per block
};

using Schedule = std::function<void(std::size_t block, pulp::state::ParameterEventQueue&)>;

// `paced` delivers blocks at the real-time rate, as a host's audio callback
// does. The zero-latency renderer redesigns on a worker thread; an offline
// loop running many times faster than real time on a loaded machine starves
// that worker in a way no real-time host does, and measures the test rig.
Render render(std::size_t blocks, const Schedule& schedule, bool tracking = false,
              float tone_hz = 997.0f,
              const std::function<float(std::size_t band)>& gain_of = {},
              bool paced = false, bool auto_gain = false) {
    constexpr std::size_t block_size = 512;
    constexpr double sr = 48000.0;
    pulp::format::HeadlessHost host(tracking ? create_tracking : create_mixing);
    host.prepare(sr, block_size);
    auto* plugin = dynamic_cast<spectr::Spectr*>(host.processor());
    REQUIRE(plugin != nullptr);
    pulp::audio::Buffer<float> in(2, block_size), out(2, block_size);
    const float* input_channels[] = {in.channel(0).data(), in.channel(1).data()};
    pulp::audio::BufferView<const float> input(input_channels, 2, block_size);
    auto output = out.view();
    Render r;
    std::uint64_t n = 0, last_sequence = 0;
    float last_band = -12.0f, last_center = 0.0f;
    const auto origin = std::chrono::steady_clock::now();
    for (std::size_t block = 0; block < blocks; ++block) {
        if (paced)
            std::this_thread::sleep_until(origin + std::chrono::microseconds(
                static_cast<long long>(1e6 * block * block_size / sr)));
        for (std::size_t i = 0; i < block_size; ++i, ++n) {
            const float v = 0.25f * static_cast<float>(std::sin(2.0 * kPi * tone_hz * n / sr));
            in.channel(0)[i] = v;
            in.channel(1)[i] = v;
        }
        pulp::state::ParameterEventQueue events;
        for (std::size_t band = 0; band < 32; ++band)
            REQUIRE(events.push({spectr::band_gain_param_id(band), 0,
                                 gain_of ? gain_of(band) : -12.0f, 0}));
        // These cases measure the mask and the routing, so Auto Gain (on for
        // a new instance) is off unless a case asks for it: its make-up
        // would otherwise lift a deliberately attenuated control render.
        REQUIRE(events.push({spectr::kParamAutoGain, 0, auto_gain ? 1.0f : 0.0f, 0}));
        schedule(block, events);
        const auto t0 = std::chrono::steady_clock::now();
        host.process(output, input, events);
        const auto t1 = std::chrono::steady_clock::now();
        r.block_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        const auto& snap = plugin->read_modulated_field();
        if (snap.sequence != last_sequence) {
            last_sequence = snap.sequence;
            last_band = snap.active ? snap.field.bands[0].gain_db : -12.0f;
            const auto& v = snap.active ? snap.viewport : snap.base_viewport;
            last_center = 0.5f * (std::log10(v.min_hz) + std::log10(v.max_hz));
        }
        r.band0_db.push_back(last_band);
        r.center_log.push_back(last_center);
        r.out.insert(r.out.end(), out.channel(0).begin(), out.channel(0).end());
    }
    return r;
}

// LFO 1 on/off, shape and rate. No depth: each target's Depth is its own
// lane (see route()), and the LFO-level Depth lane is a legacy command.
void lfo1(pulp::state::ParameterEventQueue& e, spectr::LfoShape shape, float rate,
          bool enabled = true) {
    REQUIRE(e.push({spectr::kParamLfoEnabled, 0, enabled ? 1.0f : 0.0f, 0}));
    REQUIRE(e.push({spectr::kParamLfoShape, 0, static_cast<float>(shape), 0}));
    REQUIRE(e.push({spectr::kParamLfoRate, 0, rate, 0}));
}

void route(pulp::state::ParameterEventQueue& e, std::size_t lfo, ModulationTarget t,
           bool on, float amount = 1.0f) {
    REQUIRE(e.push({spectr::lfo_route_enabled_param_id(lfo, idx(t)), 0, on ? 1.0f : 0.0f, 0}));
    REQUIRE(e.push({spectr::lfo_route_amount_param_id(lfo, idx(t)), 0, amount, 0}));
}

float largest_step(const std::vector<float>& v, std::size_t from, std::size_t to) {
    float largest = 0.0f;
    for (std::size_t i = from; i + 1 < v.size() && i < to; ++i)
        largest = std::max(largest, std::abs(v[i + 1] - v[i]));
    return largest;
}

double rms_db(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double acc = 0.0;
    for (std::size_t i = from; i < to; ++i) acc += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(acc / static_cast<double>(to - from) + 1e-20);
}

}  // namespace

TEST_CASE("the legacy LFO Depth lane sets every enabled target's Depth",
          "[modulation][routing][automation]") {
    // Old automation of 4003 still does something sensible: a move sets the
    // Depth of each target LFO 1 drives, at the event's block.
    const auto r = render(60, [](std::size_t b, pulp::state::ParameterEventQueue& e) {
        REQUIRE(e.push({spectr::kParamViewportCenter, 0, 2.5f, 0}));
        REQUIRE(e.push({spectr::kParamViewportWidth, 0, 1.0f, 0}));
        lfo1(e, spectr::LfoShape::Square, 16.0f);
        // The target lanes hold 20 % throughout; from block 30 the host plays
        // a Depth-lane point at 90 % (held, as an automation lane is).
        route(e, 0, ModulationTarget::WholeBank, true, 0.2f);
        route(e, 0, ModulationTarget::ViewportPosition, true, 0.2f);
        if (b >= 30) REQUIRE(e.push({spectr::kParamLfoDepth, 0, 0.9f, 0}));
    });
    // Square at phase 0 holds +1: band 0 at -12 + 12 x depth.
    CHECK(r.band0_db[25] == Approx(-12.0f + 12.0f * 0.2f).margin(1e-3));
    CHECK(r.band0_db.back() == Approx(-12.0f + 12.0f * 0.9f).margin(1e-3));
    INFO("band 0 at block 25 " << r.band0_db[25] << ", last " << r.band0_db.back());
    CHECK(r.center_log[25] == Approx(2.5f + 0.2f).margin(0.01f));
    CHECK(r.center_log.back() == Approx(2.5f + 0.9f).margin(0.01f));
}

// A tone sits outside the base window (so in an attenuated edge band) and
// inside one known band of the window the LFO moves it to. Only that band is
// open; the tone is heard only if the window moved by the expected amount.
TEST_CASE("viewport destinations move the filter bank by the expected amount",
          "[modulation][routing][viewport][audio]") {
    // Base window 100 Hz..1 kHz: centre 2.5, width 1 decade.
    const auto base = [](pulp::state::ParameterEventQueue& e) {
        REQUIRE(e.push({spectr::kParamViewportCenter, 0, 2.5f, 0}));
        REQUIRE(e.push({spectr::kParamViewportWidth, 0, 1.0f, 0}));
    };
    struct Case {
        const char* name; ModulationTarget target; float amount;
        float tone_log; std::size_t open_band; float min_log, max_log;
    };
    // Square LFO at phase 0 holds +1 for the first half of its 16-beat cycle.
    const Case cases[] = {
        // position, full: window 1k..10k; 10^3.45 = 2818 Hz -> t .45 -> band 14
        {"position 100%", ModulationTarget::ViewportPosition, 1.0f, 3.45f, 14, 3.0f, 4.0f},
        // position, half amount: 316..3162 Hz; tone 10^3.2 -> t .7 -> band 22
        {"position 50%", ModulationTarget::ViewportPosition, 0.5f, 3.2f, 22, 2.5f, 3.5f},
        // zoom: width 2 decades about 2.5 -> 1.5..3.5; tone 10^3.2 -> t .85 -> band 27
        {"zoom 100%", ModulationTarget::ViewportZoom, 1.0f, 3.2f, 27, 1.5f, 3.5f},
    };
    for (const auto& c : cases) {
        INFO(c.name);
        const auto gains = [&](std::size_t band) { return band == c.open_band ? 0.0f : -30.0f; };
        const auto run = [&](bool on) {
            return render(150, [&](std::size_t, pulp::state::ParameterEventQueue& e) {
                base(e);
                lfo1(e, spectr::LfoShape::Square, 16.0f, on);
                route(e, 0, ModulationTarget::WholeBank, false);
                route(e, 0, c.target, true, c.amount);
            }, false, std::pow(10.0f, c.tone_log), gains);
        };
        const auto modulated = run(true);
        const auto still = run(false);
        // The published audible window is exactly the expected one.
        REQUIRE(modulated.center_log.size() == 150);
        CHECK(modulated.center_log.back() == Approx(0.5f * (c.min_log + c.max_log)).margin(1e-3));
        const std::size_t from = 100 * 512, to = 150 * 512;
        const double heard = rms_db(modulated.out, from, to);
        const double unmoved = rms_db(still.out, from, to);
        const double input = 20.0 * std::log10(0.25 / std::sqrt(2.0));
        std::printf("[viewport-route] %s: tone %.0f Hz, input %.1f dB, unmodulated %.1f dB, "
                    "modulated %.1f dB\n", c.name, std::pow(10.0, c.tone_log), input,
                    unmoved, heard);
        CHECK(heard > input - 3.0);     // the open band reached the tone
        CHECK(unmoved < input - 20.0);  // control: without the LFO it is shut
    }
}

namespace {

// Band 0's audible gain per block, -12 dB bank, sine LFO 1 at 1 beat (0.5 s
// cycle): crest at block 12, trough at block 35. `edit` writes this block's
// routing lanes.
std::vector<float> routed_band_gain(std::size_t blocks,
                                    const std::function<void(std::size_t, pulp::state::ParameterEventQueue&)>& edit) {
    return render(blocks, [&](std::size_t b, pulp::state::ParameterEventQueue& e) {
        lfo1(e, spectr::LfoShape::Sine, 1.0f);
        edit(b, e);
    }).band0_db;
}

}  // namespace

// The acceptance bar for host automation of the routing lanes: no block-to-
// block move larger than 15% of the LFO's own 24 dB swing (the free-running
// sine moves at most ~1.6 dB a block at this rate). Prints every reading so a
// run under SPECTR_MODULATION_PLANT=route-step gives the fail-before numbers.
TEST_CASE("routing lanes automate smoothly: toggles fade, amounts ramp",
          "[modulation][routing][automation][rt]") {
    constexpr float kSwing = 24.0f, kGate = 0.15f * kSwing;
    constexpr std::size_t kCrest = 12, kTrough = 35, kBlocks = 80;
    const auto report = [](const char* what, float step) {
        std::printf("[route-smoothness] %-34s max step %.2f dB/block (gate %.2f)\n",
                    what, step, kGate);
    };
    SECTION("control: free-running") {
        const auto g = routed_band_gain(kBlocks, [](std::size_t, auto& e) {
            route(e, 0, ModulationTarget::WholeBank, true);
        });
        const auto [lo, hi] = std::minmax_element(g.begin() + 4, g.end());
        REQUIRE(*hi - *lo > 0.9f * kSwing);
        report("free-running sine", largest_step(g, 4, kBlocks));
        CHECK(largest_step(g, 4, kBlocks) < kGate);
    }
    SECTION("Bank on at the crest") {
        const auto g = routed_band_gain(kBlocks, [](std::size_t b, auto& e) {
            route(e, 0, ModulationTarget::WholeBank, b >= kCrest);
        });
        REQUIRE(*std::max_element(g.begin() + kCrest, g.end()) > -1.0f);
        const float s = largest_step(g, kCrest - 2, kCrest + 12);
        report("Bank toggled on at crest", s);
        CHECK(s < kGate);
    }
    SECTION("Bank off at the trough") {
        const auto g = routed_band_gain(kBlocks, [](std::size_t b, auto& e) {
            route(e, 0, ModulationTarget::WholeBank, b < kTrough);
        });
        REQUIRE(g[kTrough - 1] < -20.0f);
        CHECK(g.back() == Approx(-12.0f));
        const float s = largest_step(g, kTrough - 2, kTrough + 12);
        report("Bank toggled off at trough", s);
        CHECK(s < kGate);
    }
    SECTION("amount jump 10% -> 100% at the crest") {
        const auto g = routed_band_gain(kBlocks, [](std::size_t b, auto& e) {
            route(e, 0, ModulationTarget::WholeBank, true, b < kCrest ? 0.1f : 1.0f);
        });
        const float s = largest_step(g, kCrest - 2, kCrest + 12);
        report("Bank amount jump at crest", s);
        CHECK(s < kGate);
    }
    SECTION("amount ramp 0 -> 100% over 2 s, toggled mid-render") {
        // 2 s = 188 blocks. Toggle off at 120, back on at 150.
        const auto g = routed_band_gain(260, [](std::size_t b, auto& e) {
            const float amount = std::min(1.0f, static_cast<float>(b) / 188.0f);
            route(e, 0, ModulationTarget::WholeBank, !(b >= 120 && b < 150), amount);
        });
        const auto [lo, hi] = std::minmax_element(g.begin() + 200, g.end());
        REQUIRE(*hi - *lo > 0.9f * kSwing);  // reached full amount
        const float s = largest_step(g, 2, 259);
        report("amount ramp + toggles", s);
        CHECK(s < kGate);
    }
}

TEST_CASE("viewport routes automate smoothly", "[modulation][routing][viewport][automation][rt]") {
    // Viewport centre in decades per block. Full-depth position swings +/-1
    // decade; gate at 15% of that 2-decade swing, the same proportion.
    constexpr float kGate = 0.15f * 2.0f;
    constexpr std::size_t kCrest = 12;
    const auto sweep = [&](const std::function<bool(std::size_t)>& on) {
        return render(80, [&](std::size_t b, pulp::state::ParameterEventQueue& e) {
            REQUIRE(e.push({spectr::kParamViewportCenter, 0, 2.8f, 0}));
            REQUIRE(e.push({spectr::kParamViewportWidth, 0, 1.0f, 0}));
            lfo1(e, spectr::LfoShape::Sine, 1.0f);
            route(e, 0, ModulationTarget::WholeBank, false);
            route(e, 0, ModulationTarget::ViewportPosition, on(b));
        }).center_log;
    };
    // Before the first publication (nothing routed yet) the audible window is
    // the base one.
    const auto settled = [](std::vector<float> c) {
        for (auto& v : c) if (v <= 0.0f) v = 2.8f;
        return c;
    };
    const auto free = settled(sweep([](std::size_t) { return true; }));
    const auto [lo, hi] = std::minmax_element(free.begin() + 4, free.end());
    REQUIRE(*hi - *lo > 1.8f);
    const float free_step = largest_step(free, 4, 80);
    const auto toggled = settled(sweep([](std::size_t b) { return b >= kCrest; }));
    const float toggle_step = largest_step(toggled, kCrest - 2, kCrest + 12);
    std::printf("[route-smoothness] viewport free-running              max step %.3f dec/block (gate %.2f)\n",
                free_step, kGate);
    std::printf("[route-smoothness] viewport position on at crest      max step %.3f dec/block (gate %.2f)\n",
                toggle_step, kGate);
    CHECK(free_step < kGate);
    CHECK(toggle_step < kGate);
}

// The swept filter bank, heard. A tone under a smooth contour (a raised-cosine
// bump across the bank) while the viewport position LFO sweeps it at full
// depth: the envelope moves as the bump passes the tone, but continuously. The
// score is the largest jump between consecutive 1 ms peak envelopes, as in the
// LFO-enable audio test; and the same render with the LFO on Bank is the
// yardstick.
TEST_CASE("a swept viewport is click-free in both renderers",
          "[modulation][routing][viewport][audio][rt]") {
    const auto bump = [](std::size_t band) {
        const float x = (static_cast<float>(band) + 0.5f) / 32.0f;
        return -24.0f + 24.0f * 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(kPi) * x));
    };
    for (const bool tracking : {true, false}) {
        INFO((tracking ? "zero-latency renderer" : "linear-phase renderer"));
        const auto run = [&](ModulationTarget t) {
            return render(200, [&](std::size_t, pulp::state::ParameterEventQueue& e) {
                REQUIRE(e.push({spectr::kParamViewportCenter, 0, 2.8f, 0}));
                REQUIRE(e.push({spectr::kParamViewportWidth, 0, 1.0f, 0}));
                lfo1(e, spectr::LfoShape::Sine, 2.0f);
                route(e, 0, ModulationTarget::WholeBank, t == ModulationTarget::WholeBank);
                route(e, 0, t, true, t == ModulationTarget::WholeBank ? 1.0f : 0.5f);
            }, tracking, 630.0f, bump, /*paced=*/true);
        };
        const auto score = [](const std::vector<float>& x) {
            constexpr std::size_t ms = 48;
            std::vector<float> env;
            for (std::size_t at = 20 * 512; at + ms <= x.size(); at += ms) {
                float peak = 1e-9f;
                for (std::size_t i = at; i < at + ms; ++i) peak = std::max(peak, std::abs(x[i]));
                env.push_back(20.0f * std::log10(peak));
            }
            float largest = 0.0f, lo = 0.0f, hi = -200.0f;
            for (std::size_t i = 0; i + 1 < env.size(); ++i) {
                if (env[i] > -50.0f && env[i + 1] > -50.0f)
                    largest = std::max(largest, std::abs(env[i + 1] - env[i]));
                lo = std::min(lo, env[i]);
                hi = std::max(hi, env[i]);
            }
            return std::pair{largest, hi - lo};
        };
        const auto [vp_jump, vp_swing] = score(run(ModulationTarget::ViewportPosition).out);
        const auto [bank_jump, bank_swing] = score(run(ModulationTarget::WholeBank).out);
        std::printf("[viewport-click] %s: viewport sweep swing %.1f dB, largest 1 ms jump %.2f dB; "
                    "Bank LFO swing %.1f dB, jump %.2f dB\n",
                    tracking ? "Tracking" : "Mixing", vp_swing, vp_jump, bank_swing, bank_jump);
        REQUIRE(vp_swing > 10.0f);  // control: the sweep is audible
        CHECK(vp_jump < 3.0f);
    }
}

// Audio-thread cost of a viewport destination against the Bank destination it
// is compared with in the editor: both restage the mask every block, so the
// per-callback cost must be the same order. Tracking, the default renderer.
TEST_CASE("a viewport destination costs no more per callback than Bank",
          "[modulation][routing][viewport][rt][cost]") {
    // Each block's cost is its CHEAPEST over three renders of the identical
    // stimulus (as the AU freeze probe does): what is left is the plug-in's
    // own work, with a scheduler preemption on a shared machine removed.
    const auto run = [](ModulationTarget t) {
        std::vector<double> cheapest;
        for (int rep = 0; rep < 3; ++rep) {
            auto r = render(300, [&](std::size_t, pulp::state::ParameterEventQueue& e) {
                REQUIRE(e.push({spectr::kParamViewportCenter, 0, 2.8f, 0}));
                REQUIRE(e.push({spectr::kParamViewportWidth, 0, 1.0f, 0}));
                lfo1(e, spectr::LfoShape::Sine, 1.0f);
                route(e, 0, ModulationTarget::WholeBank, t == ModulationTarget::WholeBank);
                route(e, 0, t, true, 0.5f);
            }, true);
            if (cheapest.empty()) cheapest.assign(r.block_us.begin() + 20, r.block_us.end());
            else
                for (std::size_t i = 0; i < cheapest.size(); ++i)
                    cheapest[i] = std::min(cheapest[i], r.block_us[i + 20]);
        }
        std::sort(cheapest.begin(), cheapest.end());
        return std::pair{cheapest[cheapest.size() / 2], cheapest[cheapest.size() * 99 / 100]};
    };
    const auto bank = run(ModulationTarget::WholeBank);
    const auto position = run(ModulationTarget::ViewportPosition);
    const auto zoom = run(ModulationTarget::ViewportZoom);
    const double budget = 1e6 * 512 / 48000.0;
    std::printf("[route-cost] per-callback us (median / p99), budget %.0f us: Bank %.1f / %.1f, "
                "position %.1f / %.1f, zoom %.1f / %.1f\n", budget, bank.first, bank.second,
                position.first, position.second, zoom.first, zoom.second);
    CHECK(position.first <= 1.5 * bank.first + 5.0);
    CHECK(zoom.first <= 1.5 * bank.first + 5.0);
    // The tail against Bank's own tail, not against the budget: on a loaded
    // machine every callback's p99 is set by the scheduler, and a gate that
    // reads that as the plug-in's cost fails for the wrong reason.
    CHECK(position.second <= 2.0 * bank.second + 100.0);
    CHECK(zoom.second <= 2.0 * bank.second + 100.0);
}
