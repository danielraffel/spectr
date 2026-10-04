#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <pulp/audio/analysis/audio_assertions.hpp>
#include <pulp/audio/analysis/latency_evidence.hpp>
#include <pulp/format/headless.hpp>
#include <spectr/spectr.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <thread>

TEST_CASE("Spectr actual processor selects shared output with matching fallback and PDC",
          "[shared-product][installed-sdk]") {
    using namespace std::chrono;
    constexpr unsigned block = 512;
    pulp::format::HeadlessHost gpu(spectr::create_spectr), cpu(spectr::create_spectr);
    auto* g = static_cast<spectr::Spectr*>(gpu.processor());
    auto* c = static_cast<spectr::Spectr*>(cpu.processor());
    REQUIRE(c->set_shared_product_force_cpu(true));
    // GPU processing is opt-in; both twins render Mixing through the shared
    // renderer, one forced onto its CPU fallback.
    REQUIRE(g->set_gpu_processing(true));
    REQUIRE(c->set_gpu_processing(true));
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
            REQUIRE(events.push({spectr::kMix, 0, b > 96 ? 40.0f : 100.0f, 0}));
            REQUIRE(events.push({spectr::kOutputTrim, 0, b > 160 ? -6.0f : 0.0f, 0}));
            REQUIRE(events.push({spectr::band_gain_param_id(0), 0, b > 64 ? -3.0f : 0.0f, 0}));
            if (b == 96) REQUIRE(events.push({spectr::kMix, 17, 40.0f, 0}));
            if (b == 160) REQUIRE(events.push({spectr::kOutputTrim, 29, -6.0f, 0}));
            if (b == 64) REQUIRE(events.push({spectr::band_gain_param_id(0), 31, -3.0f, 0}));
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
        const auto live_gpu = g->gpu_audio_status();
        std::printf("cycle=%u gpu_selected=%llu cpu_selected=%llu forced_gpu=%llu forced_cpu=%llu latency=%d\n",
                    cycle, (unsigned long long)gs.gpu_selected, (unsigned long long)gs.cpu_selected,
                    (unsigned long long)cs.gpu_selected, (unsigned long long)cs.cpu_selected, expected_latency);
        REQUIRE(live_gpu.availability == spectr::GpuAudioStatus::Availability::Available);
        REQUIRE(live_gpu.delivery.has_value());
        CHECK(live_gpu.delivery->provider_state == 1);
        CHECK(live_gpu.delivery->gpu_selected > 0);
        CHECK(live_gpu.delivery->lost_terminal_records == 0);
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
    // The near-zero-latency renderer has a fixed small FIR latency. It is
    // intentionally not the literal zero used by the render-mode name.
    CHECK(g->latency_samples() == spectr::kZeroLatencyRenderBlock);
    CHECK_FALSE(g->shared_product_snapshot().shared_renderer);
    gpu.release();
}

TEST_CASE("Spectr actual output delay equals its pinned shared PDC",
          "[shared-product][installed-sdk][latency]") {
    using namespace pulp::test::audio;
    constexpr unsigned block = 512;
    constexpr int expected = spectr::kSpectralLatency + (spectr::kSpectralAnalysisHop / 2) * 5;
    const unsigned frames = ((expected + 1024 + block - 1) / block) * block;
    for (bool force_cpu : {false, true}) {
        pulp::format::HeadlessHost host(spectr::create_spectr);
        auto* processor = static_cast<spectr::Spectr*>(host.processor());
        REQUIRE(processor->set_shared_product_force_cpu(force_cpu));
        REQUIRE(processor->set_gpu_processing(true));
        REQUIRE(processor->set_render_mode(spectr::MaskRenderMode::linear_phase));
        host.state().set_value(spectr::kMix, 0.0f);
        host.state().set_value(spectr::kOutputTrim, 0.0f);
        host.prepare(48000, block);
        const int reported = processor->latency_samples();
        pulp::audio::Buffer<float> input(2, block), output(2, block);
        pulp::audio::Buffer<float> stimulus(2, frames), rendered(2, frames), delayed(2, frames);
        stimulus.clear(); rendered.clear(); delayed.clear();
        constexpr unsigned marker[2]{13, 29};
        constexpr float level[2]{0.5f, -0.3f};
        for (unsigned ch = 0; ch < 2; ++ch) {
            stimulus.channel(ch)[marker[ch]] = level[ch];
            delayed.channel(ch)[marker[ch] + expected] = level[ch];
        }
        const float* ptrs[]{input.channel(0).data(), input.channel(1).data()};
        pulp::audio::BufferView<const float> in(ptrs, 2, block);
        auto out = output.view();
        for (unsigned offset = 0; offset < frames; offset += block) {
            for (unsigned ch = 0; ch < 2; ++ch)
                std::copy_n(stimulus.channel(ch).data() + offset, block, input.channel(ch).data());
            host.process(out, in);
            CHECK(processor->latency_samples() == reported);
            for (unsigned ch = 0; ch < 2; ++ch)
                std::copy_n(output.channel(ch).data(), block, rendered.channel(ch).data() + offset);
        }
        const auto null = assert_null_near(rendered, delayed, -100.0);
        INFO(null.message);
        CHECK(null.passed);
        for (unsigned ch = 0; ch < 2; ++ch) {
            pulp::audio::Buffer<float> mono_in(1, frames), mono_out(1, frames);
            std::copy_n(stimulus.channel(ch).data(), frames, mono_in.channel(0).data());
            std::copy_n(rendered.channel(ch).data(), frames, mono_out.channel(0).data());
            auto evidence = measure_marker_offset(mono_in, mono_out, reported,
                {.input_marker_frame = marker[ch], .onset_threshold = 0.1});
            apply_expected_samples(evidence, expected);
            INFO(latency_evidence_summary(evidence));
            std::printf("force_cpu=%u channel=%u latency=%s\n", unsigned(force_cpu), ch,
                        latency_evidence_to_json(evidence).c_str());
            CHECK(evidence.contract_outcome == LatencyContractOutcome::satisfied);
            // A wrong report must fail the same measurement, never be accepted
            // because both implementations returned the same wrong number.
            const auto wrong = measure_marker_offset(mono_in, mono_out, reported + 1,
                {.input_marker_frame = marker[ch], .onset_threshold = 0.1});
            CHECK(wrong.contract_outcome == LatencyContractOutcome::violated);
        }
        host.release();
    }
}

// Freeze in Mixing on the shared GPU renderer. The held source must reach the
// output through the GPU path (live delivery while frozen), match the forced
// CPU reference of the same renderer, survive a switch to Tracking and back,
// and audibly differ from an unfrozen instance once the input changes -- the
// instrument's positive control, without which a dropped source would pass.
TEST_CASE("Spectr Freeze reaches the output through the shared GPU renderer",
          "[shared-product][installed-sdk][freeze]") {
    using namespace std::chrono;
    constexpr unsigned block = 512;
    pulp::format::HeadlessHost gpu(spectr::create_spectr), cpu(spectr::create_spectr),
        live(spectr::create_spectr);
    auto* g = static_cast<spectr::Spectr*>(gpu.processor());
    auto* c = static_cast<spectr::Spectr*>(cpu.processor());
    auto* l = static_cast<spectr::Spectr*>(live.processor());
    REQUIRE(c->set_shared_product_force_cpu(true));
    for (auto* p : {g, c, l}) {
        REQUIRE(p->set_gpu_processing(true));
        REQUIRE(p->set_render_mode(spectr::MaskRenderMode::linear_phase));
    }
    gpu.prepare(48000, block);
    cpu.prepare(48000, block);
    live.prepare(48000, block);
    REQUIRE(g->freeze_source_wired());
    REQUIRE(c->freeze_source_wired());
    pulp::audio::Buffer<float> input(2, block), actual(2, block), reference(2, block),
        unfrozen(2, block);
    const float* ptrs[]{input.channel(0).data(), input.channel(1).data()};
    pulp::audio::BufferView<const float> in(ptrs, 2, block);
    auto out = actual.view(), ref = reference.view(), unf = unfrozen.view();
    constexpr unsigned freeze_block = 96, switch_block = 160, tracking_block = 200,
                       mixing_block = 224, total_blocks = 320;
    double held_vs_live = 0, held_power = 0;
    unsigned compared = 0;
    const auto start = steady_clock::now();
    for (unsigned b = 0; b < total_blocks; ++b) {
        std::this_thread::sleep_until(start + nanoseconds(std::uint64_t(b) * block * 1000000000 / 48000));
        for (unsigned i = 0; i < block; ++i) {
            const auto n = b * block + i;
            const double hz = b < switch_block ? 997.0 : 3100.0;
            input.channel(0)[i] = 0.2f * std::sin(6.283185307179586 * hz * n / 48000);
            input.channel(1)[i] = 0.15f * std::sin(6.283185307179586 * (hz * 0.43) * n / 48000);
        }
        pulp::state::ParameterEventQueue events, cpu_events, live_events;
        if (b == freeze_block) {
            // The Freeze switch, as the editor or a host sets it, between blocks.
            gpu.state().set_value(spectr::kParamFreeze, 1.0f);
            cpu.state().set_value(spectr::kParamFreeze, 1.0f);
        }
        if (b == tracking_block) {
            // A Latency switch while frozen hands the SAME hold to the new
            // renderer. Both twins take the same excursion so their histories
            // stay identical.
            REQUIRE(g->set_render_mode(spectr::MaskRenderMode::zero_latency));
            REQUIRE(c->set_render_mode(spectr::MaskRenderMode::zero_latency));
            REQUIRE(g->freeze_source_wired());
        }
        if (b == mixing_block) {
            REQUIRE(g->set_render_mode(spectr::MaskRenderMode::linear_phase));
            REQUIRE(c->set_render_mode(spectr::MaskRenderMode::linear_phase));
            REQUIRE(g->freeze_source_wired());
        }
        gpu.process(out, in, events);
        cpu.process(ref, in, cpu_events);
        live.process(unf, in, live_events);
        // Through the whole run -- frozen, in Tracking and back in Mixing --
        // the GPU instance matches its forced CPU twin.
        {
            const auto match = pulp::test::audio::assert_null_near(actual, reference, -90.0);
            INFO("block=" << b << " " << match.message);
            REQUIRE(match.passed);
        }
        if (b >= switch_block + 24 && b < tracking_block) {
            for (unsigned ch = 0; ch < 2; ++ch)
                for (unsigned i = 0; i < block; ++i) {
                    const double d = double(reference.channel(ch)[i]) - unfrozen.channel(ch)[i];
                    held_vs_live += d * d;
                    held_power += double(reference.channel(ch)[i]) * reference.channel(ch)[i];
                    ++compared;
                }
        }
    }
    std::this_thread::sleep_for(milliseconds(20));
    const auto live_gpu = g->gpu_audio_status();
    const double held_rms = std::sqrt(held_power / std::max(1u, compared));
    const double diff_rms = std::sqrt(held_vs_live / std::max(1u, compared));
    std::printf("freeze gpu: held_rms=%.4f held_vs_unfrozen_rms=%.4f gpu_selected=%llu cpu_fallback=%llu lost=%llu\n",
                held_rms, diff_rms,
                live_gpu.delivery ? (unsigned long long)live_gpu.delivery->gpu_selected : 0ull,
                live_gpu.delivery ? (unsigned long long)live_gpu.delivery->cpu_fallback : 0ull,
                live_gpu.delivery ? (unsigned long long)live_gpu.delivery->lost_terminal_records : 0ull);
    REQUIRE(live_gpu.availability == spectr::GpuAudioStatus::Availability::Available);
    REQUIRE(live_gpu.delivery.has_value());
    CHECK(live_gpu.delivery->provider_state == 1);
    CHECK(live_gpu.delivery->gpu_selected > 0);
    CHECK(live_gpu.delivery->lost_terminal_records == 0);
    CHECK(held_rms > 0.01);
    CHECK(diff_rms > 0.5 * held_rms);
    gpu.release();
    cpu.release();
    live.release();
}

// The delay a host is told must be the delay it gets, on every path the
// product can take, at more than one rate and block size: Mixing with the GPU
// selected, Mixing forced onto its CPU fallback, and Tracking -- dry (Mix 0)
// and wet (Mix 100). The figure the editor shows (render_mode_latency_samples)
// must be the same number. SPECTR_LATENCY_PLANT_OFFSET adds an offset to the
// reported figure before it is checked: the negative control, which must fail.
TEST_CASE("Spectr reported latency equals measured delay on every path",
          "[shared-product][installed-sdk][latency-matrix]") {
    using namespace pulp::test::audio;
    using namespace std::chrono;
    const char* plant_env = std::getenv("SPECTR_LATENCY_PLANT_OFFSET");
    const int plant = plant_env ? std::atoi(plant_env) : 0;
    enum class Path { gpu, forced_cpu, mixing_cpu, tracking };
    std::printf("latency-matrix: sr block path mix reported ui measured_ch0 measured_ch1 gpu_selected cpu_fallback\n");
    for (const double sr : {44100.0, 48000.0})
        for (const unsigned block : {512u, 128u})
            for (const Path path : {Path::gpu, Path::forced_cpu, Path::mixing_cpu, Path::tracking})
                for (const float mix : {0.0f, 100.0f}) {
                    pulp::format::HeadlessHost host(spectr::create_spectr);
                    auto* p = static_cast<spectr::Spectr*>(host.processor());
                    const auto mode = path == Path::tracking ? spectr::MaskRenderMode::zero_latency
                                                             : spectr::MaskRenderMode::linear_phase;
                    REQUIRE(p->set_shared_product_force_cpu(path == Path::forced_cpu));
                    REQUIRE(p->set_gpu_processing(path == Path::gpu || path == Path::forced_cpu));
                    REQUIRE(p->set_render_mode(mode));
                    host.state().set_value(spectr::kMix, mix);
                    host.state().set_value(spectr::kOutputTrim, 0.0f);
                    // Auto Gain matches the wet level to the input's running
                    // level; an isolated impulse has none, so it is off here.
                    host.state().set_value(spectr::kParamAutoGain, 0.0f);
                    host.prepare(sr, block);
                    const int reported = p->latency_samples();
                    const int ui = p->render_mode_latency_samples(mode);
                    const unsigned frames = ((reported + 4096 + block - 1) / block) * block;
                    pulp::audio::Buffer<float> input(2, block), output(2, block);
                    pulp::audio::Buffer<float> stimulus(2, frames), rendered(2, frames);
                    stimulus.clear(); rendered.clear();
                    constexpr unsigned marker[2]{13, 29};
                    constexpr float level[2]{0.5f, -0.3f};
                    const bool wet = mix > 0.0f;
                    if (!wet) {
                        for (unsigned ch = 0; ch < 2; ++ch) stimulus.channel(ch)[marker[ch]] = level[ch];
                    } else {
                        // The wet path is measured with a noise burst and the
                        // lag of the input/output cross-correlation peak: the
                        // spectral path does not carry an isolated impulse at
                        // full height (it is spread over the analysis frame).
                        std::uint32_t rng = 12345u;
                        for (unsigned i = 0; i < 4096; ++i)
                            for (unsigned ch = 0; ch < 2; ++ch) {
                                rng = rng * 1664525u + 1013904223u;
                                stimulus.channel(ch)[1000 + i] = 0.25f * (float(rng >> 8) / 16777216.0f - 0.5f);
                            }
                    }
                    const float* ptrs[]{input.channel(0).data(), input.channel(1).data()};
                    pulp::audio::BufferView<const float> in(ptrs, 2, block);
                    auto out = output.view();
                    const auto start = steady_clock::now();
                    for (unsigned offset = 0, b = 0; offset < frames; offset += block, ++b) {
                        // Real-time pacing on the GPU path, so the shared
                        // renderer's worker delivers GPU output.
                        if (path == Path::gpu)
                            std::this_thread::sleep_until(start + nanoseconds(
                                std::uint64_t(b) * block * 1000000000ull / std::uint64_t(sr)));
                        for (unsigned ch = 0; ch < 2; ++ch)
                            std::copy_n(stimulus.channel(ch).data() + offset, block, input.channel(ch).data());
                        host.process(out, in);
                        CHECK(p->latency_samples() == reported);
                        for (unsigned ch = 0; ch < 2; ++ch)
                            std::copy_n(output.channel(ch).data(), block, rendered.channel(ch).data() + offset);
                    }
                    std::this_thread::sleep_for(milliseconds(20));
                    const auto status = p->gpu_audio_status();
                    const unsigned long long gpu = status.delivery ? status.delivery->gpu_selected : 0;
                    const unsigned long long cpu = status.delivery ? status.delivery->cpu_fallback : 0;
                    long long measured[2]{-1, -1};
                    bool satisfied = true;
                    if (wet) {
                        for (unsigned ch = 0; ch < 2; ++ch) {
                            const float* x = stimulus.channel(ch).data();
                            const float* y = rendered.channel(ch).data();
                            double best = -1.0; long long best_lag = -1;
                            const long long lo = std::max(0LL, (long long)reported - 2048);
                            const long long hi = (long long)reported + 2048;
                            for (long long lag = lo; lag <= hi; ++lag) {
                                double acc = 0.0;
                                for (unsigned i = 1000; i < 1000 + 4096 && i + lag < frames; ++i)
                                    acc += double(x[i]) * double(y[i + lag]);
                                if (acc > best) { best = acc; best_lag = lag; }
                            }
                            measured[ch] = best_lag;
                            satisfied = satisfied && best_lag == reported + plant;
                        }
                    }
                    for (unsigned ch = 0; ch < 2 && !wet; ++ch) {
                        pulp::audio::Buffer<float> mono_in(1, frames), mono_out(1, frames);
                        std::copy_n(stimulus.channel(ch).data(), frames, mono_in.channel(0).data());
                        std::copy_n(rendered.channel(ch).data(), frames, mono_out.channel(0).data());
                        auto evidence = measure_marker_offset(mono_in, mono_out, reported + plant,
                            {.input_marker_frame = marker[ch], .onset_threshold = 0.1});
                        if (evidence.measured_samples) measured[ch] = *evidence.measured_samples;
                        INFO(latency_evidence_summary(evidence));
                        satisfied = satisfied && evidence.contract_outcome == LatencyContractOutcome::satisfied;
                    }
                    const char* name = path == Path::gpu ? "mixing-gpu"
                        : path == Path::forced_cpu ? "mixing-gpu-forced-fallback"
                        : path == Path::mixing_cpu ? "mixing-cpu" : "tracking";
                    for (unsigned ch = 0; ch < 2 && wet; ++ch) {
                        std::size_t at = 0; float peak = 0.0f;
                        for (std::size_t i = 0; i < frames; ++i)
                            if (std::abs(rendered.channel(ch)[i]) > peak) { peak = std::abs(rendered.channel(ch)[i]); at = i; }
                        std::printf("latency-matrix-wet: ch%u output peak %.4f\n", ch, peak); (void)at;
                    }
                    std::printf("latency-matrix: %.0f %u %s %.0f %d %d %lld %lld %llu %llu\n",
                                sr, block, name, mix, reported + plant, ui, measured[0], measured[1], gpu, cpu);
                    INFO("sr=" << sr << " block=" << block << " path=" << name << " mix=" << mix);
                    CHECK(satisfied);
                    CHECK(ui == reported + plant);
                    if (path == Path::gpu) CHECK(gpu > 0);
                    if (path == Path::forced_cpu) CHECK((gpu == 0 && cpu > 0));
                    if (path == Path::mixing_cpu || path == Path::tracking)
                        CHECK(status.availability != spectr::GpuAudioStatus::Availability::Available);
                    host.release();
                }
}

// GPU processing is Mixing's renderer choice. Switching it must behave like a
// Tracking/Mixing switch: the host is told exactly once, the reported latency
// is already the new one when it is told, the audio stays bounded and keeps
// playing, and the delay the audio then has is the delay reported. In
// Tracking it records the choice and moves nothing. It round-trips with the
// session, and a session saved without it opens on the CPU.
TEST_CASE("GPU processing switches Mixing's renderer and latency like a mode switch",
          "[shared-product][installed-sdk][gpu-processing]") {
    using namespace pulp::test::audio;
    constexpr unsigned block = 256;
    pulp::format::HeadlessHost host(spectr::create_spectr);
    auto* p = static_cast<spectr::Spectr*>(host.processor());
    REQUIRE_FALSE(p->gpu_processing());   // off by default
    REQUIRE(p->set_render_mode(spectr::MaskRenderMode::linear_phase));
    host.prepare(48000, block);
    (void)p->consume_latency_changed_flag();
    const int cpu_latency = p->latency_samples();
    CHECK(cpu_latency == p->render_mode_latency_samples(spectr::MaskRenderMode::linear_phase, false));
    CHECK(p->gpu_audio_status().availability == spectr::GpuAudioStatus::Availability::NonSharedRenderer);

    pulp::audio::Buffer<float> input(2, block), output(2, block);
    const float* ptrs[]{input.channel(0).data(), input.channel(1).data()};
    pulp::audio::BufferView<const float> in(ptrs, 2, block);
    auto out = output.view();
    std::uint64_t n = 0;
    double worst = 0.0, tail = 0.0;
    const auto run = [&](unsigned blocks, bool record_tail) {
        int flags = 0, reported_at_flag = -1;
        for (unsigned b = 0; b < blocks; ++b) {
            for (unsigned i = 0; i < block; ++i, ++n) {
                const float v = 0.4f * float(std::sin(6.283185307179586 * 440.0 * double(n) / 48000.0));
                input.channel(0)[i] = v; input.channel(1)[i] = v;
            }
            host.process(out, in);
            for (unsigned ch = 0; ch < 2; ++ch)
                for (unsigned i = 0; i < block; ++i) {
                    const double s = std::abs(double(output.channel(ch)[i]));
                    worst = std::max(worst, s);
                    if (record_tail && b + 16 >= blocks) tail = std::max(tail, s);
                }
            if (p->latency_change_pending()) {
                if (reported_at_flag < 0) reported_at_flag = p->latency_samples();
                if (p->consume_latency_changed_flag()) ++flags;
            }
        }
        return std::pair{flags, reported_at_flag};
    };
    (void)run(120, false);
    REQUIRE(p->set_gpu_processing(true));
    const auto on = run(160, true);
    const int gpu_latency = p->latency_samples();
    INFO("cpu " << cpu_latency << " gpu " << gpu_latency << " flags " << on.first
         << " reported at flag " << on.second << " worst " << worst << " tail " << tail);
    CHECK(gpu_latency == p->render_mode_latency_samples(spectr::MaskRenderMode::linear_phase, true));
    CHECK(gpu_latency > cpu_latency);
    CHECK(on.first == 1);
    CHECK(on.second == gpu_latency);
    CHECK(worst <= 1.0);
    CHECK(tail > 0.01);
    CHECK(p->gpu_audio_status().availability == spectr::GpuAudioStatus::Availability::Available);

    // Off again: back to the CPU renderer and its latency, told once.
    tail = 0.0;
    REQUIRE(p->set_gpu_processing(false));
    const auto off = run(160, true);
    CHECK(off.first == 1);
    CHECK(off.second == cpu_latency);
    CHECK(p->latency_samples() == cpu_latency);
    CHECK(tail > 0.01);

    // In Tracking the choice is recorded and nothing moves.
    REQUIRE(p->set_render_mode(spectr::MaskRenderMode::zero_latency));
    (void)run(4, false);
    (void)p->consume_latency_changed_flag();
    const int tracking = p->latency_samples();
    REQUIRE(p->set_gpu_processing(true));
    const auto tracking_run = run(8, false);
    CHECK(tracking_run.first == 0);
    CHECK(p->latency_samples() == tracking);
    CHECK(p->gpu_processing());

    // The session carries it: back into Mixing from a restore builds the GPU
    // renderer at its latency.
    REQUIRE(p->set_render_mode(spectr::MaskRenderMode::linear_phase));
    const auto saved = host.save_state();
    host.release();
    pulp::format::HeadlessHost restored(spectr::create_spectr);
    auto* r = static_cast<spectr::Spectr*>(restored.processor());
    REQUIRE(restored.load_state(saved));
    CHECK(r->gpu_processing());
    restored.prepare(48000, block);
    CHECK(r->latency_samples() == gpu_latency);
    CHECK(r->gpu_audio_status().availability == spectr::GpuAudioStatus::Availability::Available);
    restored.release();

    // A session without the field (written before it existed) opens on the
    // CPU: the plugin-state JSON is the processor's own serialization, so
    // drop the field from it and restore that.
    {
        const auto plugin_state = r->serialize_plugin_state();
        std::string text(plugin_state.begin(), plugin_state.end());
        const auto at = text.find("\"gpu_processing\"");
        INFO(text.substr(at == std::string::npos ? 0 : (at > 40 ? at - 40 : 0), 120));
        REQUIRE(at != std::string::npos);
        const auto value_end = text.find_first_of(",}", at);
        REQUIRE(text.substr(at, value_end - at).find("true") != std::string::npos);
        // Drop the member and the comma that separated it from the previous one.
        const auto from = text.rfind(',', at);
        text.erase(from, value_end - from);
        pulp::format::HeadlessHost old(spectr::create_spectr);
        auto* o = static_cast<spectr::Spectr*>(old.processor());
        REQUIRE(o->set_gpu_processing(true));
        REQUIRE(o->deserialize_plugin_state(std::vector<std::uint8_t>(text.begin(), text.end())));
        CHECK_FALSE(o->gpu_processing());
    }
}

// Report-only (hidden tag): the host-thread CPU each path costs per block and
// what the whole process uses meanwhile (which includes the shared renderer's
// GPU service worker), paced in real time, across band counts, rates and block
// sizes. Run with: Spectr-shared-product-acceptance "[cpu-cost]"
TEST_CASE("CPU cost of Mixing on the CPU, Mixing on the GPU and Tracking",
          "[.][cpu-cost]") {
    using namespace std::chrono;
    const auto thread_ns = [] {
        timespec t{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
        return std::int64_t(t.tv_sec) * 1000000000LL + t.tv_nsec;
    };
    const auto process_ns = [] {
        timespec t{}; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
        return std::int64_t(t.tv_sec) * 1000000000LL + t.tv_nsec;
    };
    enum class Path { mixing_cpu, mixing_gpu, tracking };
    std::printf("cpu-cost: path bands sr block | host_us_mean host_us_p99 host_%%core | process_%%cores | gpu_selected cpu_fallback\n");
    for (const Path path : {Path::mixing_cpu, Path::mixing_gpu, Path::tracking})
        for (const float bands : {32.0f, 64.0f})
            for (const double sr : {44100.0, 48000.0})
                for (const unsigned block : {128u, 512u}) {
                    pulp::format::HeadlessHost host(spectr::create_spectr);
                    auto* p = static_cast<spectr::Spectr*>(host.processor());
                    REQUIRE(p->set_gpu_processing(path == Path::mixing_gpu));
                    REQUIRE(p->set_render_mode(path == Path::tracking
                        ? spectr::MaskRenderMode::zero_latency : spectr::MaskRenderMode::linear_phase));
                    host.state().set_value(spectr::kParamBandCount, bands);
                    host.prepare(sr, block);
                    pulp::audio::Buffer<float> input(2, block), output(2, block);
                    const float* ptrs[]{input.channel(0).data(), input.channel(1).data()};
                    pulp::audio::BufferView<const float> in(ptrs, 2, block);
                    auto out = output.view();
                    const unsigned warm = unsigned(1.0 * sr / block), total = unsigned(4.0 * sr / block);
                    std::vector<double> host_us;
                    std::int64_t proc0 = 0;
                    steady_clock::time_point wall0{};
                    std::uint64_t n = 0;
                    const auto start = steady_clock::now();
                    for (unsigned b = 0; b < total; ++b) {
                        std::this_thread::sleep_until(start + nanoseconds(
                            std::uint64_t(b) * block * 1000000000ull / std::uint64_t(sr)));
                        for (unsigned i = 0; i < block; ++i, ++n) {
                            const float v = 0.3f * float(std::sin(6.283185307179586 * 997.0 * double(n) / sr))
                                + 0.05f * float(std::sin(6.283185307179586 * 5011.0 * double(n) / sr));
                            input.channel(0)[i] = v; input.channel(1)[i] = -v;
                        }
                        if (b == warm) { proc0 = process_ns(); wall0 = steady_clock::now(); }
                        const auto t0 = thread_ns();
                        host.process(out, in);
                        const auto t1 = thread_ns();
                        if (b >= warm) host_us.push_back(double(t1 - t0) / 1000.0);
                    }
                    const double wall_s = duration<double>(steady_clock::now() - wall0).count();
                    const double proc_s = double(process_ns() - proc0) / 1e9;
                    std::sort(host_us.begin(), host_us.end());
                    double mean = 0; for (double v : host_us) mean += v; mean /= double(host_us.size());
                    const double p99 = host_us[std::size_t(0.99 * double(host_us.size() - 1))];
                    const double block_us = 1e6 * block / sr;
                    const auto status = p->gpu_audio_status();
                    const char* name = path == Path::mixing_cpu ? "mixing-cpu"
                        : path == Path::mixing_gpu ? "mixing-gpu" : "tracking";
                    std::printf("cpu-cost: %s %.0f %.0f %u | %.1f %.1f %.2f%% | %.1f%% | %llu %llu\n",
                                name, double(bands), sr, block, mean, p99, 100.0 * mean / block_us,
                                100.0 * proc_s / wall_s,
                                status.delivery ? (unsigned long long)status.delivery->gpu_selected : 0ull,
                                status.delivery ? (unsigned long long)status.delivery->cpu_fallback : 0ull);
                    host.release();
                }
}

// A bounce is rendered as fast as the host can go, which outruns the GPU
// worker: without help most quanta would fall back to the CPU stand-in. On a
// block the host marks offline the shared renderer waits (bounded) for its
// GPU output instead; a realtime block never waits. Control: the same
// unpaced render marked realtime does fall back, so the zero below is the
// wait's doing and not an instrument that cannot see fallbacks.
TEST_CASE("An offline bounce waits for GPU output instead of falling back",
          "[shared-product][installed-sdk][offline-gpu]") {
    using namespace std::chrono;
    constexpr unsigned block = 512;
    const auto bounce = [&](bool offline, bool force_cpu, std::vector<float>* rendered) {
        pulp::format::HeadlessHost host(spectr::create_spectr);
        auto* p = static_cast<spectr::Spectr*>(host.processor());
        REQUIRE(p->set_shared_product_force_cpu(force_cpu));
        REQUIRE(p->set_gpu_processing(true));
        REQUIRE(p->set_render_mode(spectr::MaskRenderMode::linear_phase));
        host.prepare(48000, block);
        pulp::audio::Buffer<float> input(2, block), output(2, block);
        const float* ptrs[]{input.channel(0).data(), input.channel(1).data()};
        pulp::audio::BufferView<const float> in(ptrs, 2, block);
        auto out = output.view();
        std::uint64_t n = 0;
        const auto start = steady_clock::now();
        for (unsigned b = 0; b < 375; ++b) {   // 4 s at 48 kHz, unpaced
            for (unsigned i = 0; i < block; ++i, ++n) {
                const float v = 0.3f * float(std::sin(6.283185307179586 * 997.0 * double(n) / 48000.0));
                input.channel(0)[i] = v; input.channel(1)[i] = -v;
            }
            pulp::format::ProcessContext ctx;
            ctx.process_mode = offline ? pulp::format::ProcessMode::Offline
                                       : pulp::format::ProcessMode::Realtime;
            host.process(out, in, ctx);
            if (rendered) rendered->insert(rendered->end(), output.channel(0).begin(), output.channel(0).end());
        }
        const double seconds = duration<double>(steady_clock::now() - start).count();
        std::this_thread::sleep_for(milliseconds(30));
        const auto s = p->gpu_audio_status();
        REQUIRE(s.delivery.has_value());
        std::printf("bounce offline=%d force_cpu=%d: gpu_selected=%llu cpu_fallback=%llu in %.2f s\n",
                    int(offline), int(force_cpu), (unsigned long long)s.delivery->gpu_selected,
                    (unsigned long long)s.delivery->cpu_fallback, seconds);
        host.release();
        return std::tuple{s.delivery->gpu_selected, s.delivery->cpu_fallback, seconds};
    };
    std::vector<float> gpu_out, cpu_out;
    const auto [og, oc, os] = bounce(true, false, &gpu_out);
    CHECK(og > 0);
    CHECK(oc == 0);
    const auto [rg, rc, rs] = bounce(false, false, nullptr);
    CHECK(rc > 0);          // control: unpaced realtime outruns the worker
    (void)rg; (void)rs; (void)os;
    (void)bounce(true, true, &cpu_out);
    REQUIRE(gpu_out.size() == cpu_out.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < gpu_out.size(); ++i)
        worst = std::max(worst, double(std::abs(gpu_out[i] - cpu_out[i])));
    INFO("worst |offline gpu - forced cpu| = " << worst);
    CHECK(worst < 1e-5);
}

// The GPU path primes its stream start the way the CPU reference does, so the
// first samples after prepare reach the output at full level there too, as
// delivered by the GPU (paced) and by its forced CPU fallback.
TEST_CASE("GPU Mixing passes the first samples of a stream at full level",
          "[shared-product][installed-sdk][stream-start]") {
    using namespace std::chrono;
    constexpr unsigned block = 512;
    for (const bool force_cpu : {false, true})
        for (const unsigned at : {0u, 13u, 1024u, 2048u}) {
            pulp::format::HeadlessHost host(spectr::create_spectr);
            auto* p = static_cast<spectr::Spectr*>(host.processor());
            REQUIRE(p->set_shared_product_force_cpu(force_cpu));
            REQUIRE(p->set_gpu_processing(true));
            REQUIRE(p->set_render_mode(spectr::MaskRenderMode::linear_phase));
            host.state().set_value(spectr::kMix, 100.0f);
            host.state().set_value(spectr::kParamAutoGain, 0.0f);
            host.prepare(48000, block);
            const unsigned lat = unsigned(p->latency_samples());
            const unsigned frames = ((at + lat + 2 * block) / block + 1) * block;
            std::vector<float> x(frames, 0.0f), y(frames, 0.0f);
            x[at] = 0.5f;
            pulp::audio::Buffer<float> input(2, block), output(2, block);
            const float* ptrs[]{input.channel(0).data(), input.channel(1).data()};
            pulp::audio::BufferView<const float> in(ptrs, 2, block);
            auto out = output.view();
            const auto start = steady_clock::now();
            for (unsigned o = 0, b = 0; o < frames; o += block, ++b) {
                std::this_thread::sleep_until(start + nanoseconds(std::uint64_t(b) * block * 1000000000ull / 48000));
                std::copy_n(x.data() + o, block, input.channel(0).data());
                std::copy_n(x.data() + o, block, input.channel(1).data());
                host.process(out, in);
                std::copy_n(output.channel(0).data(), block, y.data() + o);
            }
            const auto s = p->gpu_audio_status();
            INFO("force_cpu=" << force_cpu << " at=" << at << " -> " << y[at + lat]
                 << " gpu=" << (s.delivery ? s.delivery->gpu_selected : 0));
            CHECK(y[at + lat] == Catch::Approx(0.5f).margin(1e-3));
            host.release();
        }
}
