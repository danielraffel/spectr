#include <spectr/experimental/shared_spectral_renderer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

// This is deliberately a test-only workload experiment.  It keeps several
// complete spectral renderers prepared at once and runs every renderer for
// each input block.  It does not change the product's renderer factory or
// imply that one GPU dispatch per audio block is a realtime-safe design.
namespace {
using Renderer = spectr::experimental::SharedSpectralMaskRenderer;
using Clock = std::chrono::steady_clock;

struct Result {
    unsigned layers = 0;
    bool force_cpu = false;
    unsigned long long gpu = 0;
    unsigned long long cpu = 0;
    unsigned long long cancelled = 0;
    unsigned long long lost = 0;
    double callback_us = 0.0;
    double elapsed_ms = 0.0;
};

Result run(unsigned layers, bool force_cpu) {
    constexpr unsigned grid = 8192;
    constexpr unsigned hop = grid / 4;
    constexpr unsigned frames = 512;
    constexpr unsigned total = grid * 4;
    spectr::MaskRendererConfig config{.design_grid_size=int(grid),
        .analysis_hop=int(hop), .channels=2, .max_block=int(frames),
        .sample_rate=48000, .initial_mix=.35f, .mix_ramp_samples=64};

    std::vector<std::unique_ptr<Renderer>> renderers;
    renderers.reserve(layers);
    for (unsigned layer = 0; layer < layers; ++layer) {
        auto renderer = std::make_unique<Renderer>(force_cpu);
        if (!renderer->prepare(config)) throw 2;
        spectr::MaskRenderer::Layout layout;
        layout.active_bands = 2;
        layout.edge_policy = pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
        // Slightly different masks ensure these are independent resident
        // workloads rather than repeated calls against one shared object.
        layout.bands[0].gain_db = -6.f - float(layer);
        layout.bands[1].gain_db = -12.f + float(layer);
        if (!renderer->publish_layout(layout)) throw 3;
        renderers.push_back(std::move(renderer));
    }

    std::vector<float> input(frames * 2);
    std::vector<std::vector<float>> output(layers,
                                            std::vector<float>(frames * 2));
    std::vector<const float*> in(2);
    in[0] = input.data();
    in[1] = input.data() + frames;
    std::vector<std::array<float*, 2>> out(layers);
    for (unsigned layer = 0; layer < layers; ++layer) {
        out[layer] = {output[layer].data(), output[layer].data() + frames};
    }

    std::uint32_t state = 0x915ed271u;
    double callback_us = 0.0;
    const auto start = Clock::now();
    for (unsigned pos = 0; pos < total; pos += frames) {
        for (float& value : input) {
            state = state * 1664525u + 1013904223u;
            value = float(state >> 8) / 16777216.f - .5f;
        }
        const auto callback_start = Clock::now();
        for (unsigned layer = 0; layer < layers; ++layer) {
            const float* const input_channels[] = {in[0], in[1]};
            float* const output_channels[] = {out[layer][0], out[layer][1]};
            if (!renderers[layer]->process(input_channels, output_channels,
                                           int(std::min(frames, total - pos)))) {
                throw 4;
            }
        }
        callback_us += std::chrono::duration<double, std::micro>(
            Clock::now() - callback_start).count();
        for (const auto& layer_output : output) {
            for (float value : layer_output) if (!std::isfinite(value)) throw 5;
        }
        // Pacing is intentionally outside process(). It gives each renderer's
        // existing service worker time to retire work; this is not a deadline
        // or scheduling benchmark.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    Result result{.layers=layers, .force_cpu=force_cpu,
                  .callback_us=callback_us,
                  .elapsed_ms=std::chrono::duration<double, std::milli>(
                      Clock::now() - start).count()};
    for (const auto& renderer : renderers) {
        const auto snapshot = renderer->snapshot();
        result.gpu += snapshot.gpu_delivered;
        result.cpu += snapshot.cpu_fallback;
        result.cancelled += snapshot.cancelled;
        result.lost += snapshot.lost_records;
        if (snapshot.lost_records ||
            (force_cpu ? (snapshot.gpu_delivered != 0 ||
                          snapshot.cpu_fallback == 0 ||
                          snapshot.state != Renderer::ProviderState::CpuOnly)
                       : (snapshot.gpu_delivered == 0 ||
                          snapshot.state != Renderer::ProviderState::SharedReady))) {
            throw 6;
        }
    }
    for (auto& renderer : renderers) if (!renderer->release()) throw 7;

    std::cout << "spectral_layers layers=" << result.layers
              << " force_cpu=" << result.force_cpu
              << " gpu=" << result.gpu
              << " cpu=" << result.cpu
              << " cancelled=" << result.cancelled
              << " callback_us=" << result.callback_us
              << " callback_us_per_layer=" << result.callback_us / double(result.layers)
              << " elapsed_ms=" << result.elapsed_ms << '\n';
    return result;
}
} // namespace

int main() {
    try {
        // One layer is the baseline. The 2/4 layer cases test whether keeping
        // several resident workloads alive changes delivery or only adds work.
        const Result one_gpu = run(1, false);
        const Result two_gpu = run(2, false);
        const Result four_gpu = run(4, false);
        const Result one_cpu = run(1, true);
        const Result two_cpu = run(2, true);
        const Result four_cpu = run(4, true);
        // The fixture is diagnostic, so avoid asserting a strict ratio that
        // could be perturbed by host load. Four resident layers must still do
        // materially more callback work than the one-layer control.
        if (!(one_gpu.callback_us > 0.0 && four_gpu.callback_us > one_gpu.callback_us &&
              one_cpu.callback_us > 0.0 && four_cpu.callback_us > one_cpu.callback_us)) return 8;
    } catch (int code) {
        std::cerr << "spectral_layers_failure=" << code << '\n';
        return code;
    }
    return 0;
}
