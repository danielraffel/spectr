// The product-independent loudness-compensation unit
// (include/spectr/upstream/loudness_compensation.hpp). Links Pulp::signal and
// Catch2 only, so it moves upstream with the header: pure-function vectors,
// the estimator's determinism across chunkings, gating, start-up, the target
// slew, and an allocation count over the realtime calls.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/upstream/loudness_compensation.hpp"

#include <atomic>
#include <complex>
#include <cstdio>
#include <span>
#include <cmath>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

// ── Allocation counter (this executable only) ──────────────────────────────
namespace {
std::atomic<bool> g_counting{false};
std::atomic<long> g_allocations{0};
void* counted(std::size_t n) {
    if (g_counting.load(std::memory_order_relaxed))
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
} // namespace
void* operator new(std::size_t n) { return counted(n); }
void* operator new[](std::size_t n) { return counted(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace pulp_candidate::signal;
using Catch::Approx;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> noise(std::size_t n, unsigned seed, double amplitude = 0.1) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> w(0.0, amplitude);
    std::vector<float> x(n);
    for (auto& v : x) v = static_cast<float>(w(rng));
    return x;
}

std::vector<float> tone(std::size_t n, double hz, double amplitude = 0.1) {
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    return x;
}

// Push a mono signal (fed to both channels) in a repeating chunk pattern and
// record the absolute sample index after which each frame completed.
std::vector<std::size_t> feed(LongTermSpectrum& s, const std::vector<float>& x,
                              const std::vector<int>& chunks) {
    std::vector<std::size_t> frames;
    std::size_t pos = 0, c = 0;
    while (pos < x.size()) {
        const auto n = static_cast<int>(std::min<std::size_t>(
            static_cast<std::size_t>(chunks[c++ % chunks.size()]), x.size() - pos));
        const float* ch[] = {x.data() + pos, x.data() + pos};
        s.push(ch, 2, n, [&](int consumed) { frames.push_back(pos + static_cast<std::size_t>(consumed)); });
        pos += static_cast<std::size_t>(n);
    }
    return frames;
}

} // namespace

TEST_CASE("makeup_gain_db: pure vectors", "[loudness-compensation][pure]") {
    const MakeupLimits limits{24.0f, 24.0f};
    const std::vector<double> flat(8, 1.0), spectrum{0, 0, 0, 1, 0, 0, 0, 0};
    CHECK(makeup_gain_db(flat, flat, limits) == Approx(0.0f).margin(1e-6));
    // Everything at bin 3, boosted 6.02 dB there: cut 6.02 dB.
    std::vector<double> boost3 = flat;
    boost3[3] = 4.0;
    CHECK(makeup_gain_db(boost3, spectrum, limits) == Approx(-6.0206f).margin(1e-3));
    // The same boost where there is no material changes nothing.
    std::vector<double> boost6 = flat;
    boost6[6] = 4.0;
    CHECK(makeup_gain_db(boost6, spectrum, limits) == Approx(0.0f).margin(1e-6));
    // Half the material muted: +3.01 dB.
    const std::vector<double> two{1, 1, 0, 0}, half{1, 0, 1, 1};
    CHECK(makeup_gain_db(half, two, limits) == Approx(3.0103f).margin(1e-3));
    // Everything removed: the boost limit; nothing to measure: 0 dB.
    CHECK(makeup_gain_db(std::vector<double>(8, 0.0), flat, limits) == 24.0f);
    CHECK(makeup_gain_db(flat, std::vector<double>(8, 0.0), limits) == 0.0f);
    // Clamps.
    std::vector<double> huge = flat;
    for (auto& v : huge) v = 1.0e6;
    CHECK(makeup_gain_db(huge, flat, {12.0f, 6.0f}) == -12.0f);
    CHECK(makeup_gain_db(std::vector<double>(8, 1.0e-6), flat, {12.0f, 6.0f}) == 6.0f);
}

TEST_CASE("blend_makeup_gain_db: the two-leg model", "[loudness-compensation][pure]") {
    const MakeupLimits limits{24.0f, 24.0f};
    const std::vector<double> p{1, 1, 1, 1}, zero(4, 0.0);
    using C = std::complex<double>;
    // Coherent legs (an ordinary effect), 100 % wet: the single-leg answer.
    const std::vector<C> boost{{2, 0}, {1, 0}, {1, 0}, {1, 0}};
    const LegSpectra coherent{p, p, p, zero};
    const std::vector<double> boost_power{4, 1, 1, 1};
    CHECK(blend_makeup_gain_db(boost, coherent, 1.0, limits)
          == Approx(makeup_gain_db(boost_power, p, limits)).margin(1e-6));
    // Coherent at 50 %: |0.5 H + 0.5|^2 per bin.
    const double half = (2.25 + 3.0) / 4.0;
    CHECK(blend_makeup_gain_db(boost, coherent, 0.5, limits)
          == Approx(-10.0 * std::log10(half)).margin(1e-4));
    // A pure phase flip at 50 %: coherent legs cancel, so the boost limit...
    const std::vector<C> flip(4, C{-1, 0});
    CHECK(blend_makeup_gain_db(flip, coherent, 0.5, limits) == 24.0f);
    // ...uncorrelated legs (a hold while the input moves on) add as powers,
    // so the same flip changes nothing.
    const LegSpectra uncorrelated{p, p, zero, zero};
    CHECK(blend_makeup_gain_db(flip, uncorrelated, 0.5, limits) == Approx(0.0f).margin(1e-6));
    // Uncorrelated, 50 %, the wet leg muted: half the power, +3.01 dB.
    const std::vector<C> mute(4, C{0, 0});
    CHECK(blend_makeup_gain_db(mute, uncorrelated, 0.5, limits) == Approx(3.0103f).margin(1e-3));
    // Nothing to measure.
    const LegSpectra silent{zero, zero, zero, zero};
    CHECK(blend_makeup_gain_db(boost, silent, 1.0, limits) == 0.0f);
}

TEST_CASE("MinimumPhaseResponse keeps the magnitude and has minimum phase",
          "[loudness-compensation][pure]") {
    constexpr int n = 1024;
    MinimumPhaseResponse mp;
    mp.prepare(n);
    REQUIRE(mp.prepared());
    std::vector<std::complex<double>> out(n / 2 + 1);
    // Flat: zero phase.
    REQUIRE(mp.compute(std::vector<double>(n / 2 + 1, 1.0), 1e-6, out));
    double worst_phase = 0.0;
    for (const auto& h : out) worst_phase = std::max(worst_phase, std::abs(std::arg(h)));
    CHECK(worst_phase < 1e-5);
    // A one-pole low-pass's magnitude comes back as that one-pole's response
    // (a one-pole is minimum phase), phase and all.
    const double a = 0.9;
    std::vector<double> mag(n / 2 + 1);
    for (int k = 0; k <= n / 2; ++k) {
        const auto z = std::polar(1.0, -2.0 * kPi * k / n);
        mag[static_cast<std::size_t>(k)] = std::abs((1.0 - a) / (1.0 - a * z));
    }
    REQUIRE(mp.compute(mag, 1e-6, out));
    double worst = 0.0;
    for (int k = 0; k <= n / 2; ++k) {
        const auto z = std::polar(1.0, -2.0 * kPi * k / n);
        const auto want = (1.0 - a) / (1.0 - a * z);
        worst = std::max(worst, std::abs(out[static_cast<std::size_t>(k)] - want));
    }
    INFO("worst deviation from the one-pole response " << worst);
    CHECK(worst < 1e-3);
}

TEST_CASE("LongTermSpectrum measures how the legs combine", "[loudness-compensation][state]") {
    LoudnessCompensationConfig config;
    config.track_dry_leg = true;
    const auto x = noise(static_cast<std::size_t>(kRate * 6.0), 21u);
    const auto y = noise(x.size(), 22u);
    const auto sum = [](std::span<const double> v) {
        double t = 0;
        for (const double e : v) t += e;
        return t;
    };
    // The wet leg IS the dry leg: the cross-spectrum is the power spectrum.
    LongTermSpectrum same;
    same.prepare(kRate, 1, config);
    const float* xc[] = {x.data()};
    same.push(xc, xc, 1, static_cast<int>(x.size()), [](int) {});
    const auto s = same.legs();
    CHECK(sum(s.wd_re) == Approx(sum(s.ww)).epsilon(1e-6));
    CHECK(std::abs(sum(s.wd_im)) < 1e-6 * sum(s.ww));
    // Independent legs: the cross-spectrum is a small fraction of the power.
    LongTermSpectrum apart;
    apart.prepare(kRate, 1, config);
    const float* yc[] = {y.data()};
    apart.push(xc, yc, 1, static_cast<int>(x.size()), [](int) {});
    const auto t = apart.legs();
    const double coherence = std::abs(sum(t.wd_re)) / std::sqrt(sum(t.ww) * sum(t.dd));
    std::printf("[loudness-compensation] independent legs: |sum Pwd| / sqrt(sum Pww sum Pdd) = %.4f\n",
                coherence);
    CHECK(coherence < 0.05);
}

TEST_CASE("band_makeup_gain_db over a SpectrumCdf", "[loudness-compensation][pure]") {
    // Bins of 100 Hz; all the energy in bins 2..3 (150..350 Hz).
    std::vector<double> power(10, 0.0);
    power[2] = power[3] = 1.0;
    SpectrumCdf cdf;
    cdf.prepare(power.size());
    cdf.assign(power, 100.0);
    CHECK(cdf.weight(0.0, 1.0e9) == Approx(1.0));
    CHECK(cdf.weight(150.0, 250.0) == Approx(0.5));
    CHECK(cdf.weight(200.0, 250.0) == Approx(0.25));   // linear inside a bin
    const auto weight = [&](double lo, double hi) { return cdf.weight(lo, hi); };
    const MakeupLimits limits{24.0f, 24.0f};
    const BandPowerGain cut_material[] = {{1.0, 150.0, 1.0}, {150.0, 350.0, 0.25}, {350.0, 1e9, 1.0}};
    CHECK(band_makeup_gain_db(cut_material, weight, limits) == Approx(6.0206f).margin(1e-3));
    const BandPowerGain boost_elsewhere[] = {{1.0, 150.0, 16.0}, {150.0, 350.0, 1.0}, {350.0, 1e9, 16.0}};
    CHECK(band_makeup_gain_db(boost_elsewhere, weight, limits) == Approx(0.0f).margin(1e-6));
}

TEST_CASE("k_weighting_power follows BS.1770", "[loudness-compensation][pure]") {
    const auto k = pulp::signal::k_weighting_coefficients(kRate);
    REQUIRE(k.valid);
    const auto db = [&](double hz) { return 10.0 * std::log10(k_weighting_power(hz, k, kRate)); };
    // +0.69 dB at 1 kHz (what the -0.691 in the LUFS formula cancels), the
    // ~+4 dB shelf by 10 kHz, and the high-pass well down by 20 Hz.
    CHECK(db(997.0) == Approx(0.69).margin(0.05));
    CHECK(db(10000.0) == Approx(4.0).margin(0.3));
    CHECK(db(20.0) < -10.0);
}

TEST_CASE("LongTermSpectrum does not depend on how the stream is chopped",
          "[loudness-compensation][determinism]") {
    LoudnessCompensationConfig config;
    auto x = noise(static_cast<std::size_t>(kRate * 2.0), 3u);
    const auto t = tone(static_cast<std::size_t>(kRate * 2.0), 3000.0);
    x.insert(x.end(), t.begin(), t.end());
    LongTermSpectrum reference;
    reference.prepare(kRate, 2, config);
    const auto reference_frames = feed(reference, x, {512});
    REQUIRE(reference_frames.size() > 40);
    for (const auto& chunks : std::vector<std::vector<int>>{
             {1}, {37}, {64}, {4096}, {2048}, {17, 256, 3, 64, 101}, {10000}}) {
        LongTermSpectrum s;
        s.prepare(kRate, 2, config);
        const auto frames = feed(s, x, chunks);
        CHECK(frames == reference_frames);
        const auto a = reference.spectrum(), b = s.spectrum();
        REQUIRE(a.size() == b.size());
        std::size_t differ = 0;
        for (std::size_t i = 0; i < a.size(); ++i) differ += a[i] != b[i];
        CHECK(differ == 0);
        CHECK(s.weight(2000.0, 4000.0) == reference.weight(2000.0, 4000.0));
    }
    // Control: a different signal gives a different estimate.
    LongTermSpectrum other;
    other.prepare(kRate, 2, config);
    (void)feed(other, noise(x.size(), 4u), {512});
    REQUIRE(other.weight(2000.0, 4000.0) != reference.weight(2000.0, 4000.0));
}

TEST_CASE("LongTermSpectrum starts at the prior, converges, holds through silence, resets",
          "[loudness-compensation][state]") {
    LoudnessCompensationConfig config;
    const int bins = LongTermSpectrum::bins_for(kRate, config);
    // A prior with all its energy below 100 Hz.
    std::vector<double> prior(static_cast<std::size_t>(bins), 0.0);
    for (int k = 1; k < 8; ++k) prior[static_cast<std::size_t>(k)] = 1.0;
    LongTermSpectrum s;
    s.prepare(kRate, 1, config, prior);
    CHECK(s.weight(0.0, 100.0) == Approx(1.0));
    // A 3 kHz tone for 12 s (4 time constants): the estimate moves to it.
    const auto x = tone(static_cast<std::size_t>(kRate * 12.0), 3000.0);
    const float* ch[] = {x.data()};
    s.push(ch, 1, static_cast<int>(x.size()));
    const double at_tone = s.weight(2900.0, 3100.0);
    std::printf("[loudness-compensation] after 12 s of 3 kHz: %.3f of the energy within "
                "100 Hz of it, %.4f still below 100 Hz\n", at_tone, s.weight(0.0, 100.0));
    CHECK(at_tone > 0.95);
    // Silence: once the analysis window has left the tone (one frame
    // length), every frame is gated and the estimate is held bit for bit.
    const std::vector<float> silence(static_cast<std::size_t>(kRate * 2.0), 0.0f);
    const float* sc[] = {silence.data()};
    s.push(sc, 1, s.fft_size());
    std::vector<double> before(s.spectrum().begin(), s.spectrum().end());
    const auto gated_before = s.gated_frames();
    s.push(sc, 1, static_cast<int>(silence.size()));
    CHECK(s.gated_frames() > gated_before + 40);
    CHECK(std::equal(before.begin(), before.end(), s.spectrum().begin()));
    // Near-silence below the gate (-80 dBFS noise) is held too.
    const auto hiss = noise(static_cast<std::size_t>(kRate * 2.0), 9u, 1.0e-4);
    const float* hc[] = {hiss.data()};
    s.push(hc, 1, static_cast<int>(hiss.size()));
    CHECK(std::equal(before.begin(), before.end(), s.spectrum().begin()));
    // Reset: back to the prior.
    s.reset();
    CHECK(s.weight(0.0, 100.0) == Approx(1.0));
    CHECK(s.observed_frames() == 0);
}

TEST_CASE("MakeupTarget: an edit jumps, the material slews", "[loudness-compensation][state]") {
    MakeupTarget t;
    CHECK(t.retarget(-6.0f));
    CHECK(t.value_db() == -6.0f);
    // 6 dB/s over a 42.7 ms hop: at most 0.256 dB per step.
    CHECK(t.follow(0.0f, 2048.0 / kRate, 6.0));
    CHECK(t.value_db() == Approx(-6.0f + 0.256f).margin(1e-3));
    CHECK(t.retarget(3.0f));
    CHECK(t.value_db() == 3.0f);
    CHECK_FALSE(t.follow(3.0f, 0.1, 6.0));
    // No limit when the slew is off.
    CHECK(t.follow(-10.0f, 0.001, 0.0));
    CHECK(t.value_db() == -10.0f);
}

TEST_CASE("The realtime calls do not allocate", "[loudness-compensation][rt-safety]") {
    LoudnessCompensationConfig config;
    config.track_dry_leg = true;
    LongTermSpectrum s;
    s.prepare(kRate, 2, config);
    MinimumPhaseResponse mp;
    mp.prepare(s.fft_size());
    std::vector<std::complex<double>> phased(static_cast<std::size_t>(s.bins()));
    std::vector<double> magnitude(static_cast<std::size_t>(s.bins()), 0.5);
    MakeupTarget target;
    const auto x = noise(static_cast<std::size_t>(kRate * 1.0), 5u);
    const float* ch[] = {x.data(), x.data()};
    std::vector<BandPowerGain> bands{{1.0, 500.0, 4.0}, {500.0, 1e9, 1.0}};
    std::vector<double> response(static_cast<std::size_t>(s.bins()), 2.0);
    g_allocations = 0;
    g_counting = true;
    for (std::size_t pos = 0; pos + 256 <= x.size(); pos += 256) {
        const float* c[] = {ch[0] + pos, ch[1] + pos};
        s.push(c, c, 2, 256, [&](int) {
            (void)target.follow(band_makeup_gain_db(
                bands, [&](double lo, double hi) { return s.weight(lo, hi); },
                {config.max_cut_db, config.max_boost_db}), 0.04, 6.0);
        });
    }
    (void)makeup_gain_db(response, s.spectrum(), {24.0f, 24.0f});
    REQUIRE(mp.compute(magnitude, 1e-6, phased));
    (void)blend_makeup_gain_db(phased, s.legs(), 0.5, {24.0f, 24.0f});
    s.reset();
    g_counting = false;
    INFO("allocations on the realtime path: " << g_allocations.load());
    CHECK(g_allocations.load() == 0);
    // Control: the counter sees an allocation.
    g_counting = true;
    auto* probe = new std::vector<int>(16);
    g_counting = false;
    delete probe;
    REQUIRE(g_allocations.load() > 0);
}
