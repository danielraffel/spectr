#include <catch2/catch_test_macros.hpp>
#include <pulp/audio/analysis/audio_assertions.hpp>
#include <pulp/format/headless.hpp>
#include <spectr/spectr.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

TEST_CASE("Spectr actual processor selects shared output with matching fallback and PDC",
          "[shared-product][installed-sdk]") {
    using namespace std::chrono;
    constexpr unsigned block = 512;
    pulp::format::HeadlessHost gpu(spectr::create_spectr), cpu(spectr::create_spectr);
    auto* g = static_cast<spectr::Spectr*>(gpu.processor());
    auto* c = static_cast<spectr::Spectr*>(cpu.processor());
    REQUIRE(c->set_shared_product_force_cpu(true));
    REQUIRE(g->set_render_mode(spectr::MaskRenderMode::linear_phase));
    REQUIRE(c->set_render_mode(spectr::MaskRenderMode::linear_phase));
    const int expected_latency = spectr::kSpectralLatency + (spectr::kSpectralAnalysisHop / 2) * 5;
    CHECK(g->latency_samples() == expected_latency);
    pulp::audio::Buffer<float> input(2, block), actual(2, block), reference(2, block);
    const float* ptrs[]{input.channel(0).data(), input.channel(1).data()};
    pulp::audio::BufferView<const float> in(ptrs, 2, block);
    auto out = actual.view(), ref = reference.view();
    // A second prepare/release cycle exercises product startup reset and teardown.
    // This is ordinary-thread paced processing, not an audio-workgroup test.
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        gpu.prepare(48000, block);
        cpu.prepare(48000, block);
        REQUIRE(g->shared_product_snapshot().shared_renderer);
        REQUIRE(c->shared_product_snapshot().shared_renderer);
        CHECK_FALSE(g->set_shared_product_force_cpu(true));
        CHECK(g->latency_samples() == expected_latency);
        CHECK(c->latency_samples() == expected_latency);
        const auto start = steady_clock::now();
        for (unsigned b = 0; b < 256; ++b) {
            std::this_thread::sleep_until(start + nanoseconds(std::uint64_t(b) * block * 1000000000 / 48000));
            for (unsigned i = 0; i < block; ++i) {
                const auto n = b * block + i;
                input.channel(0)[i] = 0.2f * std::sin(6.283185307179586 * 997 * n / 48000);
                input.channel(1)[i] = 0.15f * std::sin(6.283185307179586 * 431 * n / 48000);
            }
            pulp::state::ParameterEventQueue events;
            REQUIRE(events.push({spectr::kMix, 0, b >= 96 ? 40.0f : 100.0f, 0}));
            REQUIRE(events.push({spectr::kOutputTrim, 0, b >= 160 ? -6.0f : 0.0f, 0}));
            REQUIRE(events.push({spectr::band_gain_param_id(0), 0, b >= 64 ? -3.0f : 0.0f, 0}));
            gpu.process(out, in, events);
            cpu.process(ref, in, events);
            const auto match = pulp::test::audio::assert_null_near(actual, reference, -90.0);
            INFO("cycle=" << cycle << " block=" << b << " " << match.message);
            REQUIRE(match.passed);
        }
        CHECK(pulp::test::audio::assert_not_silent(
            pulp::test::audio::analyze(actual, 48000)).passed);
        // The service thread drains selections; this sleep is outside measured audio.
        std::this_thread::sleep_for(milliseconds(20));
        const auto gs = g->shared_product_snapshot(), cs = c->shared_product_snapshot();
        std::printf("cycle=%u gpu_selected=%llu cpu_selected=%llu forced_gpu=%llu forced_cpu=%llu latency=%d\n",
                    cycle, (unsigned long long)gs.gpu_selected, (unsigned long long)gs.cpu_selected,
                    (unsigned long long)cs.gpu_selected, (unsigned long long)cs.cpu_selected, expected_latency);
        CHECK(gs.gpu_selected > 0);
        CHECK(cs.gpu_selected == 0);
        CHECK(cs.cpu_selected > 0);
        CHECK(gs.lost_records == 0);
        CHECK(cs.lost_records == 0);
        gpu.release();
        cpu.release();
        CHECK_FALSE(g->shared_product_snapshot().shared_renderer);
        CHECK_FALSE(c->shared_product_snapshot().shared_renderer);
    }
    REQUIRE(g->set_render_mode(spectr::MaskRenderMode::zero_latency));
    gpu.prepare(48000, block);
    CHECK(g->latency_samples() == 0);
    CHECK_FALSE(g->shared_product_snapshot().shared_renderer);
    gpu.release();
}
