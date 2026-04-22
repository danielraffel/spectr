#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "spectr/engine.hpp"

#include <pulp/audio/buffer.hpp>
#include <array>
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

// Local helpers that construct a test BufferView without depending on a
// particular Pulp audio::Buffer shape. We own the backing storage.
struct TestBuffers {
    std::vector<float>        ch0, ch1;
    std::array<float*, 2>     write_ptrs{};
    std::array<const float*, 2> read_ptrs{};

    explicit TestBuffers(std::size_t n) : ch0(n), ch1(n) {
        write_ptrs[0] = ch0.data();
        write_ptrs[1] = ch1.data();
        read_ptrs[0]  = ch0.data();
        read_ptrs[1]  = ch1.data();
    }

    pulp::audio::BufferView<float> write_view() {
        return {write_ptrs.data(), 2, ch0.size()};
    }
    pulp::audio::BufferView<const float> read_view() const {
        return {read_ptrs.data(), 2, ch0.size()};
    }
};

} // namespace

TEST_CASE("make_engine returns a non-null engine for every kind") {
    for (auto k : {EngineKind::Iir, EngineKind::Fft, EngineKind::Hybrid}) {
        auto e = make_engine(k);
        REQUIRE(e != nullptr);
    }
}

TEST_CASE("Fft engine round-trips audio under a flat mask (float tolerance)") {
    constexpr std::size_t N = 64;
    TestBuffers in(N), out(N);
    for (std::size_t i = 0; i < N; ++i) {
        in.ch0[i] = 0.25f * static_cast<float>(i);
        in.ch1[i] = -0.10f * static_cast<float>(i);
    }

    auto engine = make_engine(EngineKind::Fft);
    EnginePrepare p;
    p.sample_rate = 48000.0;
    p.max_block   = N;
    p.layout      = Layout::Bands32;
    engine->prepare(p);

    BandField f;
    Viewport  v;
    auto wv = out.write_view();
    auto rv = in.read_view();
    engine->process(wv, rv, f, v, Layout::Bands32, ResponseMode::Precision);

    // DFT round-trip is exact in principle; in float with vDSP we expect a
    // few parts in 10^-4 of relative error on mid-range values.
    for (std::size_t i = 0; i < N; ++i) {
        CHECK(out.ch0[i] == Approx(in.ch0[i]).margin(2e-3));
        CHECK(out.ch1[i] == Approx(in.ch1[i]).margin(2e-3));
    }
}

TEST_CASE("Iir / Hybrid stub engines still pass audio through bit-exact") {
    constexpr std::size_t N = 64;
    for (auto k : {EngineKind::Iir, EngineKind::Hybrid}) {
        TestBuffers in(N), out(N);
        for (std::size_t i = 0; i < N; ++i) {
            in.ch0[i] = 0.25f * static_cast<float>(i);
            in.ch1[i] = -0.10f * static_cast<float>(i);
        }

        auto engine = make_engine(k);
        EnginePrepare p;
        p.sample_rate = 48000.0;
        p.max_block   = N;
        p.layout      = Layout::Bands32;
        engine->prepare(p);
        BandField f;
        Viewport  v;
        auto wv = out.write_view();
        auto rv = in.read_view();
        engine->process(wv, rv, f, v, Layout::Bands32, ResponseMode::Precision);
        for (std::size_t i = 0; i < N; ++i) {
            CHECK(out.ch0[i] == Approx(in.ch0[i]));
            CHECK(out.ch1[i] == Approx(in.ch1[i]));
        }
    }
}

TEST_CASE("Fft engine reports zero latency (block-synchronous)") {
    auto engine = make_engine(EngineKind::Fft);
    CHECK(engine->latency_samples() == 0);
}

TEST_CASE("EngineKind round-trips through the factory") {
    // Smoke test that factory calls for all three kinds don't crash when
    // prepare/process/release are called in the normal order.
    for (auto k : {EngineKind::Iir, EngineKind::Fft, EngineKind::Hybrid}) {
        auto e = make_engine(k);
        EnginePrepare p;
        p.sample_rate = 44100.0;
        p.max_block   = 128;
        p.layout      = Layout::Bands48;
        e->prepare(p);
        TestBuffers in(128), out(128);
        auto wv = out.write_view();
        auto rv = in.read_view();
        BandField f;
        Viewport  v;
        e->process(wv, rv, f, v, Layout::Bands48, ResponseMode::Live);
        e->release();
    }
}
