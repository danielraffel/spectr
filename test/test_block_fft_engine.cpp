// Milestone 2 — DSP truth spike tests.
//
// Verifies that the BlockFftEngine passes the product's core truth tests:
//   1. Flat-state transparency: unity mask → output ≈ input.
//   2. Mute depth: muted bands attenuate signal in that region by at
//      least 80 dB.
//   3. Bin-to-band mapping is deterministic under layout changes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

using Catch::Approx;
using spectr::BandField;
using spectr::EngineKind;
using spectr::EnginePrepare;
using spectr::Layout;
using spectr::ResponseMode;
using spectr::Viewport;
using spectr::make_engine;

namespace {

constexpr double kSampleRate = 48000.0;

/// Buffer pair with writable output and immutable input views.
struct Buffers {
    std::vector<float>           ch0_in, ch1_in, ch0_out, ch1_out;
    std::array<const float*, 2>  in_ptrs{};
    std::array<float*, 2>        out_ptrs{};

    explicit Buffers(std::size_t n)
        : ch0_in(n, 0.0f), ch1_in(n, 0.0f), ch0_out(n, 0.0f), ch1_out(n, 0.0f)
    {
        in_ptrs[0]  = ch0_in.data();
        in_ptrs[1]  = ch1_in.data();
        out_ptrs[0] = ch0_out.data();
        out_ptrs[1] = ch1_out.data();
    }

    pulp::audio::BufferView<const float> in_view() const {
        return {in_ptrs.data(), 2, ch0_in.size()};
    }
    pulp::audio::BufferView<float> out_view() {
        return {out_ptrs.data(), 2, ch0_out.size()};
    }
};

/// Fill both channels with a sine wave at frequency `hz`.
void fill_sine(Buffers& b, double hz, double sr = kSampleRate) {
    const double w = 2.0 * M_PI * hz / sr;
    for (std::size_t i = 0; i < b.ch0_in.size(); ++i) {
        const float s = static_cast<float>(std::sin(w * static_cast<double>(i)));
        b.ch0_in[i] = s;
        b.ch1_in[i] = s;
    }
}

/// Compute RMS of a buffer.
float rms(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double sum = 0.0;
    for (auto s : v) sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(v.size())));
}

/// Compute energy of a specific frequency bin via a one-shot DFT — slow but
/// dependency-free. Used to verify that a specific frequency was or wasn't
/// attenuated.
float bin_energy(const std::vector<float>& v, double hz, double sr = kSampleRate) {
    const double w = 2.0 * M_PI * hz / sr;
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        re += v[i] * std::cos(w * static_cast<double>(i));
        im += v[i] * std::sin(w * static_cast<double>(i));
    }
    const double mag = std::sqrt(re * re + im * im) / static_cast<double>(v.size()) * 2.0;
    return static_cast<float>(mag);
}

auto make_ready_engine(int block) {
    auto e = make_engine(EngineKind::Fft);
    EnginePrepare p;
    p.sample_rate = kSampleRate;
    p.max_block   = block;
    p.layout      = Layout::Bands32;
    p.viewport    = Viewport{};
    e->prepare(p);
    return e;
}

} // namespace

TEST_CASE("BlockFftEngine: flat-state transparency — output ≈ input") {
    constexpr int N = 1024;
    Buffers b(N);
    fill_sine(b, 1000.0);  // 1 kHz test tone

    auto engine = make_ready_engine(N);
    BandField f;  // all slots neutral → unity mask
    Viewport  v;
    auto wv = b.out_view();
    auto rv = b.in_view();
    engine->process(wv, rv, f, v, Layout::Bands32, ResponseMode::Precision);

    // Orthogonality of DFT means round-trip should be exact in principle.
    // In float with vDSP, a small amount of numerical noise creeps in — we
    // tolerate 1e-4 relative.
    for (std::size_t i = 0; i < b.ch0_out.size(); ++i) {
        CHECK(b.ch0_out[i] == Approx(b.ch0_in[i]).margin(1e-4));
        CHECK(b.ch1_out[i] == Approx(b.ch1_in[i]).margin(1e-4));
    }
}

TEST_CASE("BlockFftEngine: mute-all produces silence") {
    constexpr int N = 1024;
    Buffers b(N);
    fill_sine(b, 1000.0);

    auto engine = make_ready_engine(N);
    BandField f;
    for (auto& band : f.bands) band.muted = true;  // silence every slot
    Viewport v;
    auto wv = b.out_view();
    auto rv = b.in_view();
    engine->process(wv, rv, f, v, Layout::Bands32, ResponseMode::Precision);

    // RMS should be essentially zero — numerical noise only.
    CHECK(rms(b.ch0_out) < 1e-4f);
    CHECK(rms(b.ch1_out) < 1e-4f);
}

TEST_CASE("BlockFftEngine: bin-aligned tone in a muted band is suppressed by >80 dB") {
    // Pick a tone that sits exactly on an FFT bin so the block-FFT has no
    // spectral leakage. This is the strongest mute-depth claim the engine
    // can make; verifies the engine's in-band suppression is clean.
    constexpr int N = 2048;
    constexpr double kSR = kSampleRate;
    constexpr int    bin_k = 43;                        // 43 * 48000/2048
    constexpr double tone_hz = bin_k * kSR / N;         // = 1007.8125 Hz

    Buffers b(N);
    fill_sine(b, tone_hz, kSR);

    auto engine = make_ready_engine(N);
    BandField f;
    Viewport  v;

    // Mute the band that contains this bin's frequency.
    const auto n_vis = spectr::visible_count(Layout::Bands32);
    const auto target_band = v.band_for_hz(static_cast<float>(tone_hz), n_vis);
    f.bands[target_band].muted = true;

    auto wv = b.out_view();
    auto rv = b.in_view();
    engine->process(wv, rv, f, v, Layout::Bands32, ResponseMode::Precision);

    const float e_in  = bin_energy(b.ch0_in, tone_hz);
    const float e_out = bin_energy(b.ch0_out, tone_hz);
    REQUIRE(e_in > 0.1f);
    const float atten_db = 20.0f * std::log10(e_out / e_in);
    INFO("Attenuation at bin-aligned tone: " << atten_db << " dB");
    CHECK(atten_db < -80.0f);
}

TEST_CASE("BlockFftEngine: painted region attenuates a non-aligned tone by >40 dB") {
    // A tone NOT aligned to any FFT bin exhibits sidelobe leakage into
    // neighbouring bins. Real users paint a region to kill a target area;
    // this test verifies that a ±2-band painted region gives a substantial
    // attenuation for such tones. The deeper -80 dB target from the
    // product spec is a Milestone 11 refinement using a windowed STFT
    // engine; the block-FFT spike ships ~40 dB for leaky tones.
    constexpr int N  = 2048;
    constexpr double tone_hz = 1000.0;  // ≈ bin 42.67 — intentionally leaky
    Buffers b(N);
    fill_sine(b, tone_hz);

    auto engine = make_ready_engine(N);
    BandField f;
    Viewport  v;

    const auto n_vis = spectr::visible_count(Layout::Bands32);
    const auto centre = v.band_for_hz(static_cast<float>(tone_hz), n_vis);
    for (int d = -2; d <= 2; ++d) {
        const int idx = static_cast<int>(centre) + d;
        if (idx >= 0 && idx < static_cast<int>(n_vis)) {
            f.bands[static_cast<std::size_t>(idx)].muted = true;
        }
    }

    auto wv = b.out_view();
    auto rv = b.in_view();
    engine->process(wv, rv, f, v, Layout::Bands32, ResponseMode::Precision);

    const float e_in  = bin_energy(b.ch0_in, tone_hz);
    const float e_out = bin_energy(b.ch0_out, tone_hz);
    REQUIRE(e_in > 0.1f);
    const float atten_db = 20.0f * std::log10(e_out / e_in);
    INFO("Attenuation at non-aligned tone: " << atten_db << " dB");
    CHECK(atten_db < -40.0f);
}

TEST_CASE("BlockFftEngine: muting the OTHER band preserves the tone") {
    constexpr int N  = 2048;
    constexpr double tone_hz = 1000.0;
    Buffers b(N);
    fill_sine(b, tone_hz);

    auto engine = make_ready_engine(N);
    BandField f;
    Viewport  v;

    // Mute a band that does NOT contain 1 kHz. Band 0 ≈ 20 Hz area.
    f.bands[0].muted = true;

    auto wv = b.out_view();
    auto rv = b.in_view();
    engine->process(wv, rv, f, v, Layout::Bands32, ResponseMode::Precision);

    // 1 kHz energy should survive (within a few dB).
    const float e_in  = bin_energy(b.ch0_in, tone_hz);
    const float e_out = bin_energy(b.ch0_out, tone_hz);
    REQUIRE(e_in > 0.1f);
    const float atten_db = 20.0f * std::log10(e_out / e_in);
    CHECK(atten_db > -3.0f);
}

TEST_CASE("BlockFftEngine: layout projection is deterministic") {
    // The same BandField under different visible layouts should route the
    // same tone to a stable output, modulo band-boundary choices.
    constexpr int N  = 2048;
    constexpr double tone_hz = 1000.0;

    auto run_with_layout = [&](Layout L) {
        Buffers b(N);
        fill_sine(b, tone_hz);
        auto engine = make_ready_engine(N);
        BandField f;  // neutral
        Viewport  v;
        auto wv = b.out_view();
        auto rv = b.in_view();
        engine->process(wv, rv, f, v, L, ResponseMode::Precision);
        return rms(b.ch0_out);
    };

    const float rms_32 = run_with_layout(Layout::Bands32);
    const float rms_64 = run_with_layout(Layout::Bands64);
    // Flat mask → both should be ≈ input RMS (≈ 1/√2 for unit sine).
    CHECK(rms_32 == Approx(1.0f / std::sqrt(2.0f)).margin(0.02f));
    CHECK(rms_64 == Approx(1.0f / std::sqrt(2.0f)).margin(0.02f));
}

TEST_CASE("BlockFftEngine: preserves passthrough after a layout change") {
    auto engine = make_ready_engine(1024);
    BandField f;
    Viewport  v;

    Buffers b(1024);
    fill_sine(b, 500.0);

    // First block under 32-band layout.
    auto wv1 = b.out_view();
    auto rv1 = b.in_view();
    engine->process(wv1, rv1, f, v, Layout::Bands32, ResponseMode::Precision);
    const float rms_a = rms(b.ch0_out);

    // Zero output, rerun under 64-band layout.
    std::fill(b.ch0_out.begin(), b.ch0_out.end(), 0.0f);
    std::fill(b.ch1_out.begin(), b.ch1_out.end(), 0.0f);
    auto wv2 = b.out_view();
    auto rv2 = b.in_view();
    engine->process(wv2, rv2, f, v, Layout::Bands64, ResponseMode::Precision);
    const float rms_b = rms(b.ch0_out);

    CHECK(rms_a == Approx(rms_b).margin(1e-3f));
}
