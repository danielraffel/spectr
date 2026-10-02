#include <spectr/experimental/shared_spectral_renderer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {
using Renderer = spectr::experimental::SharedSpectralMaskRenderer;
using Clock = std::chrono::steady_clock;

struct Result {
    int grid = 0;
    int hop = 0;
    unsigned long long gpu = 0;
    unsigned long long cpu = 0;
    unsigned long long cancelled = 0;
    unsigned long long lost = 0;
    double callback_us = 0.0;
    double elapsed_ms = 0.0;
    int latency = 0;
};

Result run(int grid, bool force_cpu) {
    const int hop = grid / 4;
    spectr::MaskRendererConfig config{.design_grid_size=grid,
        .analysis_hop=hop,.channels=2,.max_block=512,.sample_rate=48000,
        .initial_mix=.35f,.mix_ramp_samples=64};
    Renderer renderer(force_cpu);
    if (!renderer.prepare(config)) throw 2;
    spectr::MaskRenderer::Layout layout;
    layout.active_bands = 2;
    layout.edge_policy = pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    layout.bands[0].gain_db = -6.f;
    layout.bands[1].gain_db = -12.f;
    if (!renderer.publish_layout(layout)) throw 3;

    const unsigned frames = 512;
    const unsigned total = unsigned(grid * 8);
    std::vector<float> input(frames * 2), output(frames * 2);
    const float* in[] = {input.data(), input.data() + frames};
    float* out[] = {output.data(), output.data() + frames};
    std::uint32_t state = 0x51ed2701u;
    double callback_us = 0.0;
    const auto start = Clock::now();
    for (unsigned pos = 0; pos < total; pos += frames) {
        for (float& value : input) {
            state = state * 1664525u + 1013904223u;
            value = float(state >> 8) / 16777216.f - .5f;
        }
        const auto callback_start = Clock::now();
        if (!renderer.process(in, out, int(std::min(frames, total - pos)))) throw 4;
        callback_us += std::chrono::duration<double, std::micro>(Clock::now() - callback_start).count();
        for (float value : output) if (!std::isfinite(value)) throw 5;
        // This pacing is intentionally outside process(): it lets the existing
        // experimental service worker retire work. It is not a deadline test.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const auto snapshot = renderer.snapshot();
    Result result{grid,hop,snapshot.gpu_delivered,snapshot.cpu_fallback,
                  snapshot.cancelled,snapshot.lost_records,callback_us,
                  std::chrono::duration<double,std::milli>(Clock::now() - start).count(),
                  renderer.latency_samples()};
    if (result.lost || (force_cpu ? (result.gpu != 0 || result.cpu == 0 || snapshot.state != Renderer::ProviderState::CpuOnly) : (result.gpu == 0 || snapshot.state != Renderer::ProviderState::SharedReady))) throw 6;
    if (!renderer.release()) throw 7;
    std::cout << "spectral_scaling grid=" << result.grid
              << " force_cpu=" << force_cpu
              << " hop=" << result.hop
              << " latency_samples=" << result.latency
              << " gpu=" << result.gpu
              << " cpu=" << result.cpu
              << " cancelled=" << result.cancelled
              << " callback_us=" << result.callback_us
              << " elapsed_ms=" << result.elapsed_ms << '\n';
    return result;
}
} // namespace

int main() {
    try {
        const Result balanced = run(8192, false);
        const Result maximum = run(16384, false);
        const Result balanced_cpu = run(8192, true);
        const Result maximum_cpu = run(16384, true);
        if (!(maximum.callback_us > 0.0 && balanced.callback_us > 0.0 &&
              maximum_cpu.callback_us > 0.0 && balanced_cpu.callback_us > 0.0)) return 8;
    } catch (int code) {
        std::cerr << "spectral_scaling_failure=" << code << '\n';
        return code;
    }
    return 0;
}
