// Where the shared GPU renderer falls back to its CPU path in an ordinary
// session, and why. Report-only (no ctest): it prints every CPU-fallback
// terminal with its stream epoch, its position in that epoch and the fence
// reasons, for four phases of one paced session --
//   steady   real-time pacing, nothing else happening
//   reset    a transport jump (the renderer's realtime reset, which is what
//            Spectr does on a host seek or stop/start)
//   load     the same pacing with every core busy on other threads
//   burst    a host that delivers blocks late and then catches up
//   edits    a band being dragged: a new layout every block
//   offline  a bounce, delivered as fast as the host can render
// -- and whether a fallback block differs from the GPU block it replaces
// (it is rendered by the same-latency CPU path, so the answer is measured
// against the CPU-only twin of the same session).
#include <spectr/experimental/shared_spectral_renderer.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using Renderer = spectr::experimental::SharedSpectralMaskRenderer;
using Terminal = spectr::experimental::SharedSpectralBridge::Terminal;
struct Log {
    std::mutex mutex;
    std::vector<Terminal> terminals;
};
const char* reason(spectr::experimental::SharedSpectralBridge::FenceReason r) {
    using F = spectr::experimental::SharedSpectralBridge::FenceReason;
    switch (r) {
    case F::None: return "none"; case F::CpuProcess: return "cpu_process";
    case F::InputJournal: return "input_journal"; case F::ControlJournal: return "control_journal";
    case F::MissingControl: return "missing_control"; case F::ControlSequence: return "control_sequence";
    case F::ProviderPrepare: return "provider_prepare"; case F::ProviderRelease: return "provider_release";
    case F::ProviderResult: return "provider_result"; case F::ProviderSubmit: return "provider_submit";
    case F::InputSequence: return "input_sequence"; case F::ForcedCpu: return "forced_cpu";
    }
    return "?";
}
}

int main() {
    constexpr int sr = 48000, block = 512;
    spectr::MaskRendererConfig config{.design_grid_size = 8192, .analysis_hop = 2048,
        .channels = 2, .max_block = block, .sample_rate = sr, .initial_mix = 1.f,
        .mix_ramp_samples = 0};
    Log log;
    auto gpu = std::make_unique<Renderer>(false);
    auto cpu = std::make_unique<Renderer>(true);
    gpu->set_trace_observer({&log,
        [](void* context, std::uint64_t, const Terminal& t) noexcept {
            auto* l = static_cast<Log*>(context);
            std::lock_guard<std::mutex> lock(l->mutex);
            l->terminals.push_back(t);
        }, nullptr});
    if (!gpu->prepare(config) || !cpu->prepare(config)) return 2;
    spectr::MaskRenderer::Layout layout;
    layout.active_bands = 4; layout.transition_frames = 0;
    layout.edge_policy = pulp::signal::SpectralBandEdgePolicy::extend_edge_band;
    for (int b = 0; b < 4; ++b) layout.bands[b].gain_db = -3.0f * float(b);
    if (!gpu->publish_layout(layout) || !cpu->publish_layout(layout)) return 3;
    gpu->reset(); cpu->reset();

    struct Phase { const char* name; double seconds; };
    const Phase phases[] = {{"steady", 6.0}, {"reset", 4.0}, {"load", 6.0}, {"burst", 4.0},
                            {"edits", 4.0}, {"offline", 4.0}};
    std::vector<float> in0(block), in1(block), g0(block), g1(block), c0(block), c1(block);
    const float* in[] = {in0.data(), in1.data()};
    float* go[] = {g0.data(), g1.data()};
    float* co[] = {c0.data(), c1.data()};
    std::uint64_t n = 0, blocks = 0;
    double max_diff = 0.0;
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> per_phase;  // gpu, cpu
    std::atomic<bool> load_on{false}, stop{false};
    std::vector<std::thread> burners;
    const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < cores; ++i)
        burners.emplace_back([&] {
            volatile double x = 0;
            while (!stop.load()) {
                if (load_on.load()) for (int k = 0; k < 100000; ++k) x = x + std::sqrt(double(k));
                else std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    std::size_t terminal_mark = 0;
    for (const auto& phase : phases) {
        const std::string name = phase.name;
        if (name == "reset") { gpu->reset(); cpu->reset(); }
        load_on = name == "load";
        const auto count = std::uint64_t(phase.seconds * sr / block);
        auto start = std::chrono::steady_clock::now();
        for (std::uint64_t b = 0; b < count; ++b, ++blocks) {
            auto due = start + std::chrono::nanoseconds(std::int64_t(b) * block * 1000000000LL / sr);
            // A late host: every 2 s, stall 60 ms and then catch up.
            if (name == "burst" && b % (2 * sr / block) == 10)
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
            if (name != "offline") std::this_thread::sleep_until(due);
            if (name == "edits") {
                layout.transition_frames = 2;
                layout.bands[1].gain_db = -6.0f + 6.0f * float(std::sin(double(b) * 0.05));
                if (!gpu->set_layout_rt(layout) || !cpu->set_layout_rt(layout)) return 5;
            }
            for (int i = 0; i < block; ++i, ++n) {
                const float v = float(0.3 * std::sin(6.283185307179586 * 997.0 * double(n) / sr)
                                      + 0.1 * std::sin(6.283185307179586 * 211.0 * double(n) / sr));
                in0[i] = v; in1[i] = -v;
            }
            if (!gpu->process(in, go, block) || !cpu->process(in, co, block)) return 4;
            for (int i = 0; i < block; ++i)
                max_diff = std::max({max_diff, double(std::abs(g0[i] - c0[i])), double(std::abs(g1[i] - c1[i]))});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        load_on = false;
        std::lock_guard<std::mutex> lock(log.mutex);
        auto& counts = per_phase[name];
        std::uint64_t first_epoch_seq = 0;
        for (std::size_t i = terminal_mark; i < log.terminals.size(); ++i) {
            const auto& t = log.terminals[i];
            if (t.disposition == pulp::gpu_audio::GpuAudioTerminalDisposition::GpuDelivered) ++counts.first;
            else if (t.disposition == pulp::gpu_audio::GpuAudioTerminalDisposition::CpuFallback) {
                ++counts.second;
                std::printf("fallback phase=%s epoch=%llu sequence_in_epoch=%llu admitted=%d callback_fence=%s worker_fence=%s\n",
                            phase.name, (unsigned long long)(t.stream_epoch & 0xffffffffull),
                            (unsigned long long)t.block_sequence, int(t.ingress_admitted),
                            reason(t.callback_reason), reason(t.worker_reason));
            }
            (void)first_epoch_seq;
        }
        terminal_mark = log.terminals.size();
    }
    stop = true;
    for (auto& t : burners) t.join();
    const auto quantum_ms = 1000.0 * (config.analysis_hop / 2) / sr;
    for (const auto& phase : phases) {
        const auto& c = per_phase[phase.name];
        std::printf("phase=%-6s gpu_delivered=%llu cpu_fallback=%llu (quantum %.1f ms)\n", phase.name,
                    (unsigned long long)c.first, (unsigned long long)c.second, quantum_ms);
    }
    std::printf("max |gpu output - cpu-only output| over the session = %.3g (%.1f dBFS)\n",
                max_diff, 20.0 * std::log10(std::max(max_diff, 1e-12)));
    (void)gpu->release(); (void)cpu->release();
    return 0;
}
